/*
 * ri.cpp: the renderer behind the Ri interface (include/Ri.h).
 *
 * This is a stub. Every function the scenes call is here, does nothing
 * useful yet, and RiWorldEnd writes a black image of the right size and
 * name, so the whole starter kit builds and runs before you have written a
 * line. Replace the stubs with your REYES renderer, following the Suggested
 * Workflow: transformations and the camera first, then one primitive
 * (FirstPrimitive_sphere is the scene for it), then dicing, sampling, the
 * hider, the other primitives, shading, textures, displacement, opacity,
 * and depth output. The comment on each function says what the finished
 * version must do and where the handout describes it. Split the file up
 * as it grows (add the new files to CMakeLists.txt).
 *
 * The two functions at the end, writeImage and writeDepth, are complete:
 * they write the file RiDisplay named, in the format its extension says,
 * from a buffer you fill in. Nothing else here is.
 *
 * Shaders live in shaders.cpp. Everything the scene files include is in
 * include/, and must not change: Ri.h, shaders.h and scenes.h are the
 * interface; obj.h, shadowmap.h, sceneutil.h, anim.h and models.h are
 * helpers for the scene files that never call back into the renderer
 * except through that interface.
 */
#include "Ri.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <random>
#include <cstddef>

/* The one translation unit that compiles the stb implementations. */
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

/* Everything inside this unnamed namespace is private to this file, the
 * C++ way of marking each one static; only the Ri* functions below are
 * visible to the scenes. */
namespace {

/* What the scene has asked for so far. The finished renderer keeps much
 * more here: the current transformation, its stack, the color, opacity and
 * bound shaders, the projection, the sample buffer ... (handout, Section 2). */
struct Options {
    int xres = 640, yres = 480;
    int xsamples = 2, ysamples = 2;
    std::string display = "out.png";
    bool depthDisplay = false; /* RiDisplay(name, "zfile", "z")  */
    bool perspective = false; /* Default orthogonal projection */
    float fov = 90.0f;        /* Perspective view */
};

Options options;

struct TextureImage {
    int width = 0;
    int height = 0;
    std::vector<unsigned char> pixels;
};

TextureImage textures[8];

/* Save the current color and opacity */
RtColor currentColor = {1.0f, 1.0f, 1.0f};
RtColor currentOpacity = {1.0f, 1.0f, 1.0f};

struct Matrix4 {
    float m[4][4] = {};
};

Matrix4 identityMatrix()
{
    Matrix4 result;
    for (int i = 0; i < 4; ++i) {
        result.m[i][i] = 1.0f;
    }
    return result;
}

Matrix4 multiplyMatrix(const Matrix4 &a, const Matrix4 &b)
{
    Matrix4 result;
    for (int row = 0; row < 4; ++row) {
        for (int col = 0; col < 4; ++col) {
            for (int k = 0; k < 4; ++k) {
                result.m[row][col] += a.m[row][k] * b.m[k][col];
            }
        }
    }
    return result;
}

struct GridVertex {
    RtPoint Po = {};  /* Object-space position */
    RtPoint P = {};   /* Camera-space position */
    RtNormal Ng = {};
    RtNormal N = {};
    float u = 0.0f;
    float v = 0.0f;
    RtColor Ci = {};
    RtColor Oi = {};
};

struct MicroGrid {
    int nu;
    int nv;
    std::vector<GridVertex> vertices;

    MicroGrid(int uSteps, int vSteps)
        : nu(uSteps),
          nv(vSteps),
          vertices(static_cast<size_t>(uSteps + 1) * (vSteps + 1))
    {
    }

    GridVertex &at(int i, int j)
    {
        return vertices[static_cast<size_t>(j) * (nu + 1) + i];
    }
};

/* For affine transforms: input point has homogeneous coordinate w = 1. */
void transformPoint(const Matrix4 &matrix,
                    const RtPoint input, RtPoint output)
{
    /* Temporary storage also allows input and output to be the same array. */
    RtPoint result;
    for (int row = 0; row < 3; ++row) {
        result[row] =
            matrix.m[row][0] * input[0] +
            matrix.m[row][1] * input[1] +
            matrix.m[row][2] * input[2] +
            matrix.m[row][3];
    }
    for (int i = 0; i < 3; ++i) {
        output[i] = result[i];
    }
}

bool makeNormalMatrix(const Matrix4 &matrix, Matrix4 &result)
{
    /* Upper-left 3x3: translation does not affect normals. */
    const double a = matrix.m[0][0];
    const double b = matrix.m[0][1];
    const double c = matrix.m[0][2];
    const double d = matrix.m[1][0];
    const double e = matrix.m[1][1];
    const double f = matrix.m[1][2];
    const double g = matrix.m[2][0];
    const double h = matrix.m[2][1];
    const double i = matrix.m[2][2];

    const double determinant =
        a * (e * i - f * h)
      - b * (d * i - f * g)
      + c * (d * h - e * g);

    if (std::abs(determinant) < 1e-20) {
        return false;
    }

    /* Inverse transpose = cofactor matrix / determinant. */
    result = identityMatrix();

    result.m[0][0] = (e * i - f * h) / determinant;
    result.m[0][1] = (f * g - d * i) / determinant;
    result.m[0][2] = (d * h - e * g) / determinant;

    result.m[1][0] = (c * h - b * i) / determinant;
    result.m[1][1] = (a * i - c * g) / determinant;
    result.m[1][2] = (b * g - a * h) / determinant;

    result.m[2][0] = (b * f - c * e) / determinant;
    result.m[2][1] = (c * d - a * f) / determinant;
    result.m[2][2] = (a * e - b * d) / determinant;

    return true;
}

void transformNormal(const Matrix4 &normalMatrix,
                     const RtNormal input,
                     RtNormal output)
{
    RtNormal transformed = {};

    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 3; ++col) {
            transformed[row] += normalMatrix.m[row][col] * input[col];
        }
    }

    const float length = std::sqrt(
        transformed[0] * transformed[0] +
        transformed[1] * transformed[1] +
        transformed[2] * transformed[2]
    );

    for (int channel = 0; channel < 3; ++channel) {
        output[channel] =
            length > 0.0f ? transformed[channel] / length : 0.0f;
    }
}

Matrix4 currentTransform = identityMatrix();
std::vector<Matrix4> transformStack;

struct ShaderBinding {
    RtShaderFn function = STANDARD;

    /* Storage aligned for ordinary shader parameter structs. */
    std::vector<std::max_align_t> parameters;

    const void *data() const
    {
        return parameters.empty() ? nullptr : parameters.data();
    }
};

ShaderBinding currentSurface;
ShaderBinding currentDisplacement{nullptr, {}};

struct FrameState {
    Options options;
    Matrix4 transform;
    std::vector<Matrix4> transformStack;
    RtColor color;
    RtColor opacity;
    ShaderBinding surface;
    ShaderBinding displacement;
};

std::vector<FrameState> frameStack;

Matrix4 worldToCamera = identityMatrix();
std::vector<float> framebuffer;
std::vector<float> zbuffer;

struct SamplePosition {
    double x;
    double y;
};

std::vector<SamplePosition> samplePositions;
std::vector<float> sampleColors;

struct SurfaceHit {
    float z;
    RtColor Ci = {};
    RtColor Oi = {};
};

std::vector<std::vector<SurfaceHit>> sampleHits;

bool fullyOpaque(const SurfaceHit &hit)
{
    return hit.Oi[0] >= 1.0f &&
           hit.Oi[1] >= 1.0f &&
           hit.Oi[2] >= 1.0f;
}

void insertHit(size_t sample, const SurfaceHit &hit)
{
    /* Completely transparent surfaces contribute nothing. */
    if (hit.Oi[0] <= 0.0f &&
        hit.Oi[1] <= 0.0f &&
        hit.Oi[2] <= 0.0f) {
        return;
    }

    auto &hits = sampleHits[sample];

    const auto position = std::lower_bound(
        hits.begin(), hits.end(), hit.z,
        [](const SurfaceHit &existing, float depth) {
            return existing.z < depth;
        }
    );

    /* Hits are truncated after their first fully opaque surface. */
    if (position != hits.end() &&
        fullyOpaque(*position) &&
        position->z == hit.z) {
        return;
    }

    if (position == hits.end() &&
        !hits.empty() &&
        fullyOpaque(hits.back())) {
        return;
    }

    const size_t index =
        static_cast<size_t>(position - hits.begin());

    hits.insert(position, hit);

    if (fullyOpaque(hit)) {
        hits.resize(index + 1);
    }
}

/* Input is already in camera space.
   Output retains camera-space z. */
bool projectPoint(const RtPoint cameraPoint, RtPoint rasterPoint)
{
    if (cameraPoint[2] <= 0.01f) {
        return false;
    }

    float x = cameraPoint[0];
    float y = cameraPoint[1];

    if (options.perspective) {
        constexpr float pi = 3.14159265358979323846f;
        const float tanHalfFov =
            std::tan(options.fov * pi / 360.0f);

        x /= cameraPoint[2] * tanHalfFov;
        y /= cameraPoint[2] * tanHalfFov;
    }

    const float aspect =
        static_cast<float>(options.xres) / options.yres;

    const float halfWidth = aspect >= 1.0f ? aspect : 1.0f;
    const float halfHeight = aspect >= 1.0f ? 1.0f : 1.0f / aspect;

    rasterPoint[0] =
        (x / halfWidth + 1.0f) * 0.5f * options.xres;
    rasterPoint[1] =
        (1.0f - y / halfHeight) * 0.5f * options.yres;
    rasterPoint[2] = cameraPoint[2];

    return true;
}

int chooseGridResolution(const RtPoint boundMin,
                         const RtPoint boundMax)
{
    constexpr int maxResolution = 1024;

    double minX = INFINITY;
    double minY = INFINITY;
    double maxX = -INFINITY;
    double maxY = -INFINITY;

    /* Enumerate all eight corners of the object-space bounding box. */
    for (int corner = 0; corner < 8; ++corner) {
        RtPoint objectPoint = {
            (corner & 1) ? boundMax[0] : boundMin[0],
            (corner & 2) ? boundMax[1] : boundMin[1],
            (corner & 4) ? boundMax[2] : boundMin[2]
        };

        RtPoint cameraPoint;
        transformPoint(currentTransform, objectPoint, cameraPoint);

        /* A bound crossing the eye plane cannot be projected reliably. */
        if (cameraPoint[2] <= 0.01f) {
            return maxResolution;
        }

        RtPoint rasterPoint;
        if (!projectPoint(cameraPoint, rasterPoint)) {
            return maxResolution;
        }

        if (!std::isfinite(rasterPoint[0]) ||
            !std::isfinite(rasterPoint[1])) {
            return maxResolution;
        }

        minX = std::min(minX, static_cast<double>(rasterPoint[0]));
        minY = std::min(minY, static_cast<double>(rasterPoint[1]));
        maxX = std::max(maxX, static_cast<double>(rasterPoint[0]));
        maxY = std::max(maxY, static_cast<double>(rasterPoint[1]));
    }

    /* Do not clamp the projected bounds to the image. */
    const double width = maxX - minX;
    const double height = maxY - minY;
    const double diagonal = std::hypot(width, height);

    /* Clamp before converting to int. */
    const double resolution = std::clamp(
        std::ceil(2.0 * diagonal),
        2.0,
        static_cast<double>(maxResolution)
    );

    return static_cast<int>(resolution);
}

struct RasterVertex {
    double x, y, z;
    RtColor Ci = {};
    RtColor Oi = {};
};

RasterVertex makeRasterVertex(const RtPoint position,
                              const GridVertex &vertex)
{
    RasterVertex result{};
    result.x = position[0];
    result.y = position[1];
    result.z = position[2];

    for (int channel = 0; channel < 3; ++channel) {
        result.Ci[channel] = vertex.Ci[channel];
        result.Oi[channel] = vertex.Oi[channel];
    }

    return result;
}

double edgeFunction(const RasterVertex &a,
                    const RasterVertex &b,
                    double x, double y)
{
    return (b.x - a.x) * (y - a.y)
         - (b.y - a.y) * (x - a.x);
}

/* Raster y increases downward.
   Opposite directions of a shared edge have opposite ownership. */
bool isTopLeft(const RasterVertex &a, const RasterVertex &b)
{
    const double dy = b.y - a.y;
    const double dx = b.x - a.x;

    return dy < 0.0 || (dy == 0.0 && dx > 0.0);
}

bool edgeIncludes(double value, bool ownsEdge)
{
    return value > 0.0 || (value == 0.0 && ownsEdge);
}

void rasterizeTriangle(RasterVertex a,
                       RasterVertex b,
                       RasterVertex c)
{
    double area = edgeFunction(a, b, c.x, c.y);

    if (area == 0.0) {
        return;
    }

    /* Normalize winding; this does not cull backfaces. */
    if (area < 0.0) {
        std::swap(b, c);
        area = -area;
    }

    /* Clamp floating-point bounds before converting to integers. */
    const double minX = std::max(
        0.0, std::min({a.x, b.x, c.x}));
    const double maxX = std::min(
        static_cast<double>(options.xres),
        std::max({a.x, b.x, c.x}));
    const double minY = std::max(
        0.0, std::min({a.y, b.y, c.y}));
    const double maxY = std::min(
        static_cast<double>(options.yres),
        std::max({a.y, b.y, c.y}));

    if (minX >= maxX || minY >= maxY) {
        return;
    }

    const int xBegin = static_cast<int>(std::floor(minX));
    const int xEnd = static_cast<int>(std::ceil(maxX)) - 1;
    const int yBegin = static_cast<int>(std::floor(minY));
    const int yEnd = static_cast<int>(std::ceil(maxY)) - 1;

    const bool ownsBC = isTopLeft(b, c);
    const bool ownsCA = isTopLeft(c, a);
    const bool ownsAB = isTopLeft(a, b);

    const size_t samplesPerPixel = static_cast<size_t>(options.xsamples) * options.ysamples;

    for (int y = yBegin; y <= yEnd; ++y) {
        for (int x = xBegin; x <= xEnd; ++x) {
            const size_t pixel =
                static_cast<size_t>(y) * options.xres + x;

            for (size_t s = 0; s < samplesPerPixel; ++s) {
                const size_t index = pixel * samplesPerPixel + s;

                const double sampleX = samplePositions[index].x;
                const double sampleY = samplePositions[index].y;

                const double e0 =
                    edgeFunction(b, c, sampleX, sampleY);
                const double e1 =
                    edgeFunction(c, a, sampleX, sampleY);
                const double e2 =
                    edgeFunction(a, b, sampleX, sampleY);

                if (!edgeIncludes(e0, ownsBC) ||
                    !edgeIncludes(e1, ownsCA) ||
                    !edgeIncludes(e2, ownsAB)) {
                    continue;
                }

                const double b1 = e1 / area;
                const double b2 = e2 / area;

                const float depth = static_cast<float>(
                    a.z + b1 * (b.z - a.z)
                        + b2 * (c.z - a.z));

                SurfaceHit hit{};
                hit.z = depth;

                for (int channel = 0; channel < 3; ++channel) {
                    hit.Ci[channel] = static_cast<float>(
                        a.Ci[channel]
                        + b1 * (b.Ci[channel] - a.Ci[channel])
                        + b2 * (c.Ci[channel] - a.Ci[channel])
                    );

                    hit.Oi[channel] = static_cast<float>(
                        a.Oi[channel]
                        + b1 * (b.Oi[channel] - a.Oi[channel])
                        + b2 * (c.Oi[channel] - a.Oi[channel])
                    );
                }

                insertHit(index, hit);
            }
        }
    }
}

void rasterizeQuad(const GridVertex &a,
                   const GridVertex &b,
                   const GridVertex &c,
                   const GridVertex &d)
{
    /* Reject before perspective division. */
    if (a.P[2] <= 0.01f || b.P[2] <= 0.01f ||
        c.P[2] <= 0.01f || d.P[2] <= 0.01f) {
        return;
    }

    RtPoint pa, pb, pc, pd;

    if (!projectPoint(a.P, pa) ||
        !projectPoint(b.P, pb) ||
        !projectPoint(c.P, pc) ||
        !projectPoint(d.P, pd)) {
        return;
    }

    const RasterVertex ra = makeRasterVertex(pa, a);
    const RasterVertex rb = makeRasterVertex(pb, b);
    const RasterVertex rc = makeRasterVertex(pc, c);
    const RasterVertex rd = makeRasterVertex(pd, d);

    rasterizeTriangle(ra, rb, rc);
    rasterizeTriangle(ra, rc, rd);
}

void renderMicroGrid(MicroGrid &grid,
                     bool lowerPole = false,
                     bool upperPole = false,
                    bool polygonFacing = false)
{
    /* Build shader inputs from one grid vertex. */
    auto makeContext = [&](const GridVertex &vertex) {
        RtShaderContext context{};

        for (int channel = 0; channel < 3; ++channel) {
            context.P[channel] = vertex.P[channel];
            context.Po[channel] = vertex.Po[channel];
            context.N[channel] = vertex.N[channel];
            context.Cs[channel] = currentColor[channel];
            context.Os[channel] = currentOpacity[channel];
        }

        context.u = vertex.u;
        context.v = vertex.v;

        return context;
    };

    if (currentDisplacement.function) {
        /* Displace every vertex before recomputing any normal. */
        for (GridVertex &vertex : grid.vertices) {
            RtShaderContext context = makeContext(vertex);
            context.params = currentDisplacement.data();

            currentDisplacement.function(&context);

            for (int channel = 0; channel < 3; ++channel) {
                vertex.P[channel] = context.P[channel];
            }
        }

        /* Recompute normals from displaced camera-space positions. */
        for (int j = 0; j <= grid.nv; ++j) {
            for (int i = 0; i <= grid.nu; ++i) {
                GridVertex &vertex = grid.at(i, j);

                const bool pole =
                    (j == 0 && lowerPole) ||
                    (j == grid.nv && upperPole);

                if (pole) {
                    continue;
                }

                const GridVertex &uStart =
                    grid.at(i < grid.nu ? i : i - 1, j);
                const GridVertex &uEnd =
                    grid.at(i < grid.nu ? i + 1 : i, j);

                const GridVertex &vStart =
                    grid.at(i, j < grid.nv ? j : j - 1);
                const GridVertex &vEnd =
                    grid.at(i, j < grid.nv ? j + 1 : j);

                double du[3];
                double dv[3];

                for (int channel = 0; channel < 3; ++channel) {
                    du[channel] = grid.nu * (
                        static_cast<double>(uEnd.P[channel]) -
                        uStart.P[channel]
                    );

                    dv[channel] = grid.nv * (
                        static_cast<double>(vEnd.P[channel]) -
                        vStart.P[channel]
                    );
                }

                const double normal[3] = {
                    du[1] * dv[2] - du[2] * dv[1],
                    du[2] * dv[0] - du[0] * dv[2],
                    du[0] * dv[1] - du[1] * dv[0]
                };

                const double length = std::sqrt(
                    normal[0] * normal[0] +
                    normal[1] * normal[1] +
                    normal[2] * normal[2]
                );

                /* N was initialized to Ng, which is the fallback. */
                if (length > 0.0 && std::isfinite(length)) {
                    for (int channel = 0; channel < 3; ++channel) {
                        vertex.N[channel] =
                            static_cast<float>(normal[channel] / length);
                    }
                }
            }
        }
    }

    /* Orient normals and run the surface shader. */
    for (GridVertex &vertex : grid.vertices) {

        RtNormal facingNormal;
        for (int channel = 0; channel < 3; ++channel) {
            facingNormal[channel] = polygonFacing
                ? vertex.Ng[channel]
                : vertex.N[channel];
        }

        if (polygonFacing) {
            const float agreement =
                facingNormal[0] * vertex.N[0] +
                facingNormal[1] * vertex.N[1] +
                facingNormal[2] * vertex.N[2];

            if (agreement < 0.0f) {
                for (int channel = 0; channel < 3; ++channel) {
                    facingNormal[channel] = -facingNormal[channel];
                }
            }
        }

        const float normalDotView =
            facingNormal[0] * (-vertex.P[0]) +
            facingNormal[1] * (-vertex.P[1]) +
            facingNormal[2] * (-vertex.P[2]);

        if (normalDotView < 0.0f) {
            for (int channel = 0; channel < 3; ++channel) {
                vertex.N[channel] = -vertex.N[channel];
            }
        }

        RtShaderContext context = makeContext(vertex);
        context.params = currentSurface.data();

        currentSurface.function(&context);

        for (int channel = 0; channel < 3; ++channel) {
            vertex.Ci[channel] = context.Ci[channel];
            vertex.Oi[channel] = context.Oi[channel];
        }
    }

    /* Rasterize the shaded grid. */
    for (int j = 0; j < grid.nv; ++j) {
        for (int i = 0; i < grid.nu; ++i) {
            rasterizeQuad(
                grid.at(i,     j),
                grid.at(i + 1, j),
                grid.at(i + 1, j + 1),
                grid.at(i,     j + 1)
            );
        }
    }
}

void writeImage(const float *rgba);
void writeDepth(const float *depth);

} // namespace

/* ---------------------------------------------------------- graphics state */

/* RiBegin starts a rendering session and resets the graphics state to
 * RenderMan's defaults: color and opacity (1, 1, 1), surface shader
 * STANDARD, no displacement, orthographic projection, 640 x 480 pixels,
 * 2 x 2 samples. Scenes rely on these (FirstPrimitive never calls
 * RiOpacity). RiEnd ends the session (free textures and buffers here).
 * The name is always RI_NULL. */
RtVoid RiBegin(RtToken /*name*/) {
    options = Options{};
    currentTransform = identityMatrix(); // Reset to the identity matrix
    transformStack.clear(); // Reset the transformation stack

    for (int i = 0; i < 3; ++i) {
        currentColor[i] = 1.0f;
        currentOpacity[i] = 1.0f;
    }

    frameStack.clear();
    worldToCamera = identityMatrix();

    framebuffer.clear();
    zbuffer.clear();

    samplePositions.clear();
    sampleColors.clear();
    sampleHits.clear();

    currentSurface = ShaderBinding{};
    currentDisplacement = ShaderBinding{nullptr, {}};
}

RtVoid RiEnd(void)
{
    for (TextureImage &texture : textures) {
        texture = TextureImage{};
    }

    framebuffer.clear();
    zbuffer.clear();
    samplePositions.clear();
    sampleColors.clear();
    sampleHits.clear(); 
}

/* The image size in pixels. The screen window is [-a, a] x [-1, 1] for
 * a = xres / yres >= 1 and [-1, 1] x [-1/a, 1/a] for a < 1, so the field of
 * view spans the shorter side (Section 2). */
RtVoid RiFormat(RtInt xres, RtInt yres)
{
    options.xres = xres;
    options.yres = yres;
}

/* xsamples x ysamples jittered samples per pixel (Section 4). */
RtVoid RiPixelSamples(RtInt xsamples, RtInt ysamples)
{
    options.xsamples = std::max(1, xsamples);
    options.ysamples = std::max(1, ysamples);
}

/* The output file, written into renders/ by RiWorldEnd. RiDisplay(name, "",
 * "") names the picture; RiDisplay(name, "zfile", "z") asks for the depth
 * image instead (Section 4, "Depth output"). */
RtVoid RiDisplay(const char *name, RtToken type, RtToken mode)
{
    options.display = name;
    options.depthDisplay =
        (type && std::strcmp(type, "zfile") == 0) || (mode && std::strcmp(mode, "z") == 0);
}

/* RiFrameBegin/End bracket one frame of an animation: save the graphics
 * state at Begin and restore it at End, so that each frame starts afresh. */
RtVoid RiFrameBegin(RtInt /*frame*/)
{
    FrameState saved;
    saved.options = options;
    saved.transform = currentTransform;
    saved.transformStack = transformStack;

    for (int i = 0; i < 3; ++i) {
        saved.color[i] = currentColor[i];
        saved.opacity[i] = currentOpacity[i];
    }

    saved.surface = currentSurface;
    saved.displacement = currentDisplacement;

    frameStack.push_back(saved);
}

RtVoid RiFrameEnd(void)
{
    if (frameStack.empty()) {
        std::fprintf(stderr, "RiFrameEnd: no saved frame state\n");
        return;
    }

    const FrameState &saved = frameStack.back();

    options = saved.options;
    currentTransform = saved.transform;
    transformStack = saved.transformStack;

    for (int i = 0; i < 3; ++i) {
        currentColor[i] = saved.color[i];
        currentOpacity[i] = saved.opacity[i];
    }

    currentSurface = saved.surface;
    currentDisplacement = saved.displacement;

    frameStack.pop_back();
}

/* RiWorldBegin: the transformation built so far is the camera transform
 * (world to camera); from here on transformations place objects. Set up
 * the sample buffer. RiWorldEnd: all primitives have been drawn, so filter
 * the samples into pixels and write the image (Sections 2 and 4). */
RtVoid RiWorldBegin(void)
{
    worldToCamera = currentTransform;

    const size_t pixelCount =
        static_cast<size_t>(options.xres) * options.yres;

    const size_t samplesPerPixel =
        static_cast<size_t>(options.xsamples) * options.ysamples;

    const size_t sampleCount = pixelCount * samplesPerPixel;

     sampleHits.clear();              
    sampleHits.resize(sampleCount);  

    framebuffer.assign(pixelCount * 4, 0.0f);
    sampleColors.assign(sampleCount * 4, 0.0f);
    zbuffer.assign(sampleCount, INFINITY);
    samplePositions.resize(sampleCount);

    /* Fixed seed makes repeated renders reproducible. */
    std::mt19937 generator(285);
    std::uniform_real_distribution<double> random01(0.0, 1.0);

    for (int y = 0; y < options.yres; ++y) {
        for (int x = 0; x < options.xres; ++x) {
            const size_t pixel =
                static_cast<size_t>(y) * options.xres + x;

            for (int sy = 0; sy < options.ysamples; ++sy) {
                for (int sx = 0; sx < options.xsamples; ++sx) {
                    const size_t localSample =
                        static_cast<size_t>(sy) * options.xsamples + sx;

                    const size_t index =
                        pixel * samplesPerPixel + localSample;

                    /* One random point inside each subcell. */
                    samplePositions[index].x =
                        x + (sx + random01(generator)) / options.xsamples;

                    samplePositions[index].y =
                        y + (sy + random01(generator)) / options.ysamples;
                }
            }
        }
    }
}

/* TODO: your pixels. For now, a black, fully transparent image (or an
      * empty depth image, +infinity everywhere). */
RtVoid RiWorldEnd(void)
{
    const size_t pixelCount =
        static_cast<size_t>(options.xres) * options.yres;

    const size_t samplesPerPixel =
        static_cast<size_t>(options.xsamples) * options.ysamples;

    const float weight =
        1.0f / static_cast<float>(samplesPerPixel);

    framebuffer.assign(pixelCount * 4, 0.0f);

    /* Kept as per-sample depth for depth output. */
    zbuffer.assign(sampleHits.size(), INFINITY);

    for (size_t pixel = 0; pixel < pixelCount; ++pixel) {
        for (size_t s = 0; s < samplesPerPixel; ++s) {
            const size_t index = pixel * samplesPerPixel + s;

            double color[3] = {};
            double opacity[3] = {};

            for (const SurfaceHit &hit : sampleHits[index]) {
                for (int channel = 0; channel < 3; ++channel) {
                    const double remaining = 1.0 - opacity[channel];

                    color[channel] +=
                        remaining * hit.Ci[channel] * hit.Oi[channel];

                    opacity[channel] +=
                        remaining * hit.Oi[channel];
                }

                if (opacity[0] >= 1.0 &&
                    opacity[1] >= 1.0 &&
                    opacity[2] >= 1.0) {
                    break;
                }
            }

            const size_t offset = pixel * 4;

            for (int channel = 0; channel < 3; ++channel) {
                framebuffer[offset + channel] +=
                    static_cast<float>(color[channel] * weight);
            }

            const double alpha =
                (opacity[0] + opacity[1] + opacity[2]) / 3.0;

            framebuffer[offset + 3] +=
                static_cast<float>(alpha * weight);
        }
    }

    if (options.depthDisplay) {
        std::vector<float> depth(pixelCount, INFINITY);

        for (size_t pixel = 0; pixel < pixelCount; ++pixel) {
            for (size_t s = 0; s < samplesPerPixel; ++s) {
                const size_t index = pixel * samplesPerPixel + s;

                for (const SurfaceHit &hit : sampleHits[index]) {
                    const double meanOpacity =
                        (static_cast<double>(hit.Oi[0]) +
                        hit.Oi[1] +
                        hit.Oi[2]) / 3.0;

                    if (meanOpacity >= 0.5) {
                        zbuffer[index] = hit.z;
                        depth[pixel] = std::min(depth[pixel], hit.z);
                        break;
                    }
                }
            }
        }

        writeDepth(depth.data());
    } else {
        writeImage(framebuffer.data());
    }

    sampleHits.clear();
}

/* RiProjection(RI_PERSPECTIVE, "fov", &fov), fov in degrees over the
 * shorter side of the image, or RiProjection(RI_ORTHOGRAPHIC). It also
 * resets the current transformation to the identity: the camera transform
 * is built after it (Section 2). */
RtVoid RiProjection(RtToken name, RtToken token, RtPointer value)
{
    options.perspective =
        name && std::strcmp(name, RI_PERSPECTIVE) == 0;

    if (options.perspective) {
        options.fov = 90.0f;

        if (token && std::strcmp(token, "fov") == 0 && value) {
            options.fov = *static_cast<const RtFloat *>(value);
        }
    }

    /* Each time a projection is set, the current transformation is reset. */
    currentTransform = identityMatrix();
}

/* ----------------------------------------------------------- transformations */

/* The current transformation matrix (CTM) and its stack. Each call
 * post-multiplies the CTM, so the transformation called last is applied
 * to points first; RiTransformBegin pushes a copy and RiTransformEnd pops
 * it. Scenes nest these several deep. RiConcatTransform takes an RtMatrix
 * in RenderMan's row-vector form (p' = p M, translation in the bottom
 * row). Angles are degrees (Section 2). */
RtVoid RiTransformBegin(void)
{
    transformStack.push_back(currentTransform);
}

RtVoid RiTransformEnd(void)
{
    if (transformStack.empty()) {
        std::fprintf(stderr, "RiTransformEnd: empty transform stack\n");
        return;
    }

    currentTransform = transformStack.back();
    transformStack.pop_back();
}

RtVoid RiConcatTransform(RtMatrix transform)
{
    Matrix4 matrix;
    for (int row = 0; row < 4; ++row) {
        for (int col = 0; col < 4; ++col) {
            matrix.m[row][col] = transform[col][row];
        }
    }

    currentTransform = multiplyMatrix(currentTransform, matrix);
}

RtVoid RiTranslate(RtFloat dx, RtFloat dy, RtFloat dz)
{
    Matrix4 translation = identityMatrix();
    translation.m[0][3] = dx;
    translation.m[1][3] = dy;
    translation.m[2][3] = dz;

    currentTransform = multiplyMatrix(currentTransform, translation);
}

RtVoid RiRotate(RtFloat angle, RtFloat dx, RtFloat dy, RtFloat dz)
{
    const float length = std::sqrt(dx * dx + dy * dy + dz * dz);
    if (length == 0.0f) {
        return;
    }

    const float x = dx / length;
    const float y = dy / length;
    const float z = dz / length;

    constexpr float pi = 3.14159265358979323846f;
    const float radians = angle * pi / 180.0f;
    const float c = std::cos(radians);
    const float s = std::sin(radians);
    const float t = 1.0f - c;

    Matrix4 rotation = identityMatrix();

    rotation.m[0][0] = t * x * x + c;
    rotation.m[0][1] = t * x * y - s * z;
    rotation.m[0][2] = t * x * z + s * y;

    rotation.m[1][0] = t * x * y + s * z;
    rotation.m[1][1] = t * y * y + c;
    rotation.m[1][2] = t * y * z - s * x;

    rotation.m[2][0] = t * x * z - s * y;
    rotation.m[2][1] = t * y * z + s * x;
    rotation.m[2][2] = t * z * z + c;

    currentTransform = multiplyMatrix(currentTransform, rotation);
}

RtVoid RiScale(RtFloat sx, RtFloat sy, RtFloat sz)
{
    Matrix4 scale = identityMatrix();
    scale.m[0][0] = sx;
    scale.m[1][1] = sy;
    scale.m[2][2] = sz;

    currentTransform = multiplyMatrix(currentTransform, scale);
}
/* ---------------------------------------------------------------- primitives */

/* Each primitive goes through the REYES pipeline as soon as it is called:
 * bound it, choose a dice rate from the bound's size on screen, dice it
 * into a grid of micropolygons, shade the grid, and hand the micropolygons
 * to the hider (Section 3). The quadrics use the RenderMan parameterizations
 * (RiSpec section 5.4); thetamax, phimin and phimax are degrees.
 *
 * Shade the grid in two passes (Section 5): the displacement shader at
 * every vertex, then N recomputed from the displaced grid, then the surface
 * shader at every vertex. Each call fills one vertex's RtShaderContext,
 * runs the shader and keeps what it wrote. A shader sees only its own
 * vertex, so the order you visit them in does not matter; only the new N
 * needs the neighbors, which is why it waits for the whole first pass. */
RtVoid RiSphere(RtFloat radius, RtFloat zmin,
                RtFloat zmax, RtFloat thetamax)
{
    if (radius <= 0.0f || thetamax <= 0.0f) {
        return;
    }

    constexpr float pi = 3.14159265358979323846f;

    zmin = std::clamp(zmin, -radius, radius);
    zmax = std::clamp(zmax, -radius, radius);

    if (zmax <= zmin) {
        return;
    }

    const float phiMin = std::asin(zmin / radius);
    const float phiMax = std::asin(zmax / radius);
    const float thetaMax = thetamax * pi / 180.0f;

    const RtPoint boundMin = {-radius, -radius, zmin};
    const RtPoint boundMax = { radius,  radius, zmax};

    const int resolution =
        chooseGridResolution(boundMin, boundMax);

    int nu = resolution;
    const int nv = resolution;

    if (std::abs(thetamax - 360.0f) < 0.001f) {
        nu = std::max(nu, 4);
    }

    MicroGrid grid(nu, nv);

    Matrix4 normalMatrix;
    if (!makeNormalMatrix(currentTransform, normalMatrix)) {
        std::fprintf(stderr, "RiSphere: singular transformation\n");
        return;
    }

    for (int j = 0; j <= grid.nv; ++j) {
        const float v = static_cast<float>(j) / grid.nv;
        const float phi = phiMin + v * (phiMax - phiMin);

        for (int i = 0; i <= grid.nu; ++i) {
            const float u = static_cast<float>(i) / grid.nu;
            const float theta = u * thetaMax;

            GridVertex &vertex = grid.at(i, j);

            vertex.u = u;
            vertex.v = v;

            vertex.Po[0] =
                radius * std::cos(phi) * std::cos(theta);
            vertex.Po[1] =
                radius * std::cos(phi) * std::sin(theta);
            vertex.Po[2] = radius * std::sin(phi);

            /* Make collapsed pole rows exact. */
            if ((j == 0 && zmin == -radius) ||
                (j == grid.nv && zmax == radius)) {
                vertex.Po[0] = 0.0f;
                vertex.Po[1] = 0.0f;
                vertex.Po[2] = j == 0 ? -radius : radius;
            }

            transformPoint(currentTransform, vertex.Po, vertex.P);

            const RtNormal objectNormal = {
                vertex.Po[0] / radius,
                vertex.Po[1] / radius,
                vertex.Po[2] / radius
            };

            transformNormal(normalMatrix, objectNormal, vertex.Ng);

            for (int channel = 0; channel < 3; ++channel) {
                vertex.N[channel] = vertex.Ng[channel];
            }
        }
    }

    renderMicroGrid(
        grid,
        zmin == -radius,
        zmax == radius
    );

}

RtVoid RiCylinder(RtFloat radius,
                  RtFloat zmin,
                  RtFloat zmax,
                  RtFloat thetamax)
{
    if (radius <= 0.0f ||
        zmax <= zmin ||
        thetamax <= 0.0f) {
        return;
    }

    constexpr float pi = 3.14159265358979323846f;
    const float thetaMax = thetamax * pi / 180.0f;

    const RtPoint boundMin = {-radius, -radius, zmin};
    const RtPoint boundMax = { radius,  radius, zmax};

    const int resolution =
        chooseGridResolution(boundMin, boundMax);

    int nu = resolution;
    const int nv = resolution;

    if (std::abs(thetamax - 360.0f) < 0.001f) {
        nu = std::max(nu, 4);
    }

    Matrix4 normalMatrix;

    if (!makeNormalMatrix(currentTransform, normalMatrix)) {
        std::fprintf(stderr, "RiCylinder: singular transformation\n");
        return;
    }

    MicroGrid grid(nu, nv);

    for (int j = 0; j <= grid.nv; ++j) {
        const float v = static_cast<float>(j) / grid.nv;
        const float z = zmin + v * (zmax - zmin);

        for (int i = 0; i <= grid.nu; ++i) {
            const float u = static_cast<float>(i) / grid.nu;
            const float theta = u * thetaMax;

            const float cosine = std::cos(theta);
            const float sine = std::sin(theta);

            GridVertex &vertex = grid.at(i, j);

            vertex.u = u;
            vertex.v = v;

            vertex.Po[0] = radius * cosine;
            vertex.Po[1] = radius * sine;
            vertex.Po[2] = z;

            transformPoint(currentTransform, vertex.Po, vertex.P);

            /* Cylinder side normal points radially outward. */
            const RtNormal objectNormal = {
                cosine, sine, 0.0f
            };

            transformNormal(normalMatrix, objectNormal, vertex.Ng);

            for (int channel = 0; channel < 3; ++channel) {
                vertex.N[channel] = vertex.Ng[channel];
            }
        }
    }

    /* A cylinder side has no collapsed pole rows. */
    renderMicroGrid(grid);
}

RtVoid RiCone(RtFloat height,
              RtFloat radius,
              RtFloat thetamax)
{
    if (height <= 0.0f ||
        radius <= 0.0f ||
        thetamax <= 0.0f) {
        return;
    }

    constexpr float pi = 3.14159265358979323846f;
    const float thetaMax = thetamax * pi / 180.0f;

    const RtPoint boundMin = {-radius, -radius, 0.0f};
    const RtPoint boundMax = { radius,  radius, height};

    const int resolution =
        chooseGridResolution(boundMin, boundMax);

    int nu = resolution;
    const int nv = resolution;

    if (std::abs(thetamax - 360.0f) < 0.001f) {
        nu = std::max(nu, 4);
    }

    Matrix4 normalMatrix;

    if (!makeNormalMatrix(currentTransform, normalMatrix)) {
        std::fprintf(stderr, "RiCone: singular transformation\n");
        return;
    }

    MicroGrid grid(nu, nv);

    for (int j = 0; j <= grid.nv; ++j) {
        const float v = static_cast<float>(j) / grid.nv;

        /* Radius decreases from radius at the base to zero at the tip. */
        const float localRadius = radius * (1.0f - v);
        const float z = height * v;

        for (int i = 0; i <= grid.nu; ++i) {
            const float u = static_cast<float>(i) / grid.nu;
            const float theta = u * thetaMax;

            const float cosine = std::cos(theta);
            const float sine = std::sin(theta);

            GridVertex &vertex = grid.at(i, j);

            vertex.u = u;
            vertex.v = v;

            vertex.Po[0] = localRadius * cosine;
            vertex.Po[1] = localRadius * sine;
            vertex.Po[2] = z;

            transformPoint(currentTransform, vertex.Po, vertex.P);

            /* Outward side normal, including its upward component.
               At the tip, use the limiting normal for this theta. */
            const RtNormal objectNormal = {
                height * cosine,
                height * sine,
                radius
            };

            transformNormal(normalMatrix, objectNormal, vertex.Ng);

            for (int channel = 0; channel < 3; ++channel) {
                vertex.N[channel] = vertex.Ng[channel];
            }
        }
    }

    /* The final row collapses to the cone tip. */
    renderMicroGrid(grid, false, true);
}

RtVoid RiTorus(RtFloat majorradius,
               RtFloat minorradius,
               RtFloat phimin,
               RtFloat phimax,
               RtFloat thetamax)
{
    if (majorradius <= 0.0f ||
        minorradius <= 0.0f ||
        phimax <= phimin ||
        thetamax <= 0.0f) {
        return;
    }

    constexpr float pi = 3.14159265358979323846f;
    constexpr float degreesToRadians = pi / 180.0f;

    const float phiMin = phimin * degreesToRadians;
    const float phiMax = phimax * degreesToRadians;
    const float thetaMax = thetamax * degreesToRadians;

    const float extent = majorradius + minorradius;

    const RtPoint boundMin = {
        -extent, -extent, -minorradius
    };

    const RtPoint boundMax = {
        extent, extent, minorradius
    };

    const int resolution =
        chooseGridResolution(boundMin, boundMax);

    int nu = resolution;
    int nv = resolution;

    if (std::abs(thetamax - 360.0f) < 0.001f) {
        nu = std::max(nu, 4);
    }

    if (std::abs((phimax - phimin) - 360.0f) < 0.001f) {
        nv = std::max(nv, 4);
    }

    Matrix4 normalMatrix;

    if (!makeNormalMatrix(currentTransform, normalMatrix)) {
        std::fprintf(stderr, "RiTorus: singular transformation\n");
        return;
    }

    MicroGrid grid(nu, nv);

    for (int j = 0; j <= grid.nv; ++j) {
        const float v = static_cast<float>(j) / grid.nv;
        const float phi = phiMin + v * (phiMax - phiMin);

        const float cosPhi = std::cos(phi);
        const float sinPhi = std::sin(phi);

        /* Distance from the z axis to this point on the tube. */
        const float radial =
            majorradius + minorradius * cosPhi;

        for (int i = 0; i <= grid.nu; ++i) {
            const float u = static_cast<float>(i) / grid.nu;
            const float theta = u * thetaMax;

            const float cosTheta = std::cos(theta);
            const float sinTheta = std::sin(theta);

            GridVertex &vertex = grid.at(i, j);

            vertex.u = u;
            vertex.v = v;

            vertex.Po[0] = radial * cosTheta;
            vertex.Po[1] = radial * sinTheta;
            vertex.Po[2] = minorradius * sinPhi;

            transformPoint(currentTransform, vertex.Po, vertex.P);

            /* Normal points outward from the tube's centerline. */
            const RtNormal objectNormal = {
                cosPhi * cosTheta,
                cosPhi * sinTheta,
                sinPhi
            };

            transformNormal(normalMatrix, objectNormal, vertex.Ng);

            for (int channel = 0; channel < 3; ++channel) {
                vertex.N[channel] = vertex.Ng[channel];
            }
        }
    }

    renderMicroGrid(grid);
}

/* A mesh of polygons: polygon i has nverts[i] corners, whose indices into
 * P (and N and st, when they are not NULL) follow one another in verts.
 * Treat every polygon as its own primitive, bounded, diced, shaded and
 * hidden before the next: a quad is a bilinear patch, a triangle the same
 * patch with its last corner repeated, and a larger polygon a fan of
 * triangles from its first vertex (Section 3, "Polygon meshes"). */
RtVoid RiPointsPolygons(RtInt npolys,
                        const RtInt nverts[],
                        const RtInt verts[],
                        const RtPoint P[],
                        const RtNormal N[],
                        const RtFloat st[])
{
    if (npolys <= 0 || !nverts || !verts || !P) {
        return;
    }

    Matrix4 normalMatrix;

    if (!makeNormalMatrix(currentTransform, normalMatrix)) {
        std::fprintf(stderr,
                     "RiPointsPolygons: singular transformation\n");
        return;
    }

    /* Process one triangle or quad at a time. */
    auto renderPatch = [&](int p0, int p1, int p2, int p3,
                           bool triangle) {
        const int indices[4] = {p0, p1, p2, p3};

        RtPoint boundMin = {INFINITY, INFINITY, INFINITY};
        RtPoint boundMax = {-INFINITY, -INFINITY, -INFINITY};

        for (int corner = 0; corner < 4; ++corner) {
            for (int channel = 0; channel < 3; ++channel) {
                const float value = P[indices[corner]][channel];
                boundMin[channel] = std::min(boundMin[channel], value);
                boundMax[channel] = std::max(boundMax[channel], value);
            }
        }

        /* For a triangle, p3 repeats p2. */
        double edgeU[3];
        double edgeV[3];

        for (int channel = 0; channel < 3; ++channel) {
            edgeU[channel] =
                static_cast<double>(P[p1][channel]) - P[p0][channel];

            edgeV[channel] =
                static_cast<double>(P[p3][channel]) - P[p0][channel];
        }

        const double cross[3] = {
            edgeU[1] * edgeV[2] - edgeU[2] * edgeV[1],
            edgeU[2] * edgeV[0] - edgeU[0] * edgeV[2],
            edgeU[0] * edgeV[1] - edgeU[1] * edgeV[0]
        };

        const double length = std::sqrt(
            cross[0] * cross[0] +
            cross[1] * cross[1] +
            cross[2] * cross[2]
        );

        if (length == 0.0) {
            return;
        }

        RtNormal objectFaceNormal;

        for (int channel = 0; channel < 3; ++channel) {
            objectFaceNormal[channel] =
                static_cast<float>(cross[channel] / length);
        }

        RtNormal cameraFaceNormal;
        transformNormal(
            normalMatrix, objectFaceNormal, cameraFaceNormal
        );

        const int resolution =
            chooseGridResolution(boundMin, boundMax);

        MicroGrid grid(resolution, resolution);

        for (int j = 0; j <= grid.nv; ++j) {
            const float v = static_cast<float>(j) / grid.nv;

            for (int i = 0; i <= grid.nu; ++i) {
                const float u = static_cast<float>(i) / grid.nu;

                const float weights[4] = {
                    (1.0f - u) * (1.0f - v),
                    u * (1.0f - v),
                    u * v,
                    (1.0f - u) * v
                };

                GridVertex &vertex = grid.at(i, j);
                vertex.u = u;
                vertex.v = v;

                RtNormal objectNormal = {};

                for (int corner = 0; corner < 4; ++corner) {
                    const int index = indices[corner];
                    const float weight = weights[corner];

                    for (int channel = 0; channel < 3; ++channel) {
                        vertex.Po[channel] +=
                            weight * P[index][channel];

                        if (N) {
                            objectNormal[channel] +=
                                weight * N[index][channel];
                        }
                    }
                }

                /* Keep the triangle's collapsed row exactly coincident. */
                if (triangle && j == grid.nv) {
                    for (int channel = 0; channel < 3; ++channel) {
                        vertex.Po[channel] = P[p2][channel];
                    }
                }

                transformPoint(
                    currentTransform, vertex.Po, vertex.P
                );

                for (int channel = 0; channel < 3; ++channel) {
                    vertex.Ng[channel] = cameraFaceNormal[channel];
                    vertex.N[channel] = cameraFaceNormal[channel];
                }

                if (N) {
                    RtNormal transformed;
                    transformNormal(
                        normalMatrix, objectNormal, transformed
                    );

                    const float lengthSquared =
                        transformed[0] * transformed[0] +
                        transformed[1] * transformed[1] +
                        transformed[2] * transformed[2];

                    if (lengthSquared > 0.0f) {
                        for (int channel = 0; channel < 3; ++channel) {
                            vertex.N[channel] = transformed[channel];
                        }
                    }
                }

                if (st) {
                    vertex.u = 0.0f;
                    vertex.v = 0.0f;

                    for (int corner = 0; corner < 4; ++corner) {
                        const size_t index =
                            static_cast<size_t>(indices[corner]) * 2;

                        vertex.u += weights[corner] * st[index];
                        vertex.v += weights[corner] * st[index + 1];
                    }
                }
            }
        }

        renderMicroGrid(grid, false, triangle, true);
    };

    size_t offset = 0;

    for (int polygon = 0; polygon < npolys; ++polygon) {
        const int count = nverts[polygon];

        if (count < 0) {
            return;
        }

        if (count == 3) {
            renderPatch(
                verts[offset],
                verts[offset + 1],
                verts[offset + 2],
                verts[offset + 2],
                true
            );
        } else if (count == 4) {
            renderPatch(
                verts[offset],
                verts[offset + 1],
                verts[offset + 2],
                verts[offset + 3],
                false
            );
        } else if (count > 4) {
            for (int corner = 1; corner < count - 1; ++corner) {
                renderPatch(
                    verts[offset],
                    verts[offset + corner],
                    verts[offset + corner + 1],
                    verts[offset + corner + 1],
                    true
                );
            }
        }

        offset += static_cast<size_t>(count);
    }
}

/* ------------------------------------------------------------------ shading */

/* Attributes: they stay set until changed (RiTransformEnd does not restore
 * them). The color and opacity become Cs and Os in the shader context. */
// RtVoid RiColor(RtColor color) { (void)color; }

// RtVoid RiOpacity(RtColor opacity) { (void)opacity; }

RtVoid RiColor(RtColor color)
{
    for (int i = 0; i < 3; ++i) {
        currentColor[i] = color[i];
    }
}

RtVoid RiOpacity(RtColor opacity)
{
    for (int i = 0; i < 3; ++i) {
        currentOpacity[i] = opacity[i];
    }
}

/* Bind a shader to the primitives that follow. Copy nbytes of params now:
 * the scene's struct may be gone by the time the shader runs. fn NULL means
 * STANDARD for RiSurface and no displacement for RiDisplacement
 * (Section 5). */
// RtVoid RiSurface(RtShaderFn fn, const void *params, size_t nbytes) { (void)fn, (void)params, (void)nbytes; }
RtVoid RiSurface(RtShaderFn fn, const void *params, size_t nbytes)
{
    currentSurface.function = fn ? fn : STANDARD;
    currentSurface.parameters.clear();

    if (params && nbytes > 0) {
        const size_t elementSize = sizeof(std::max_align_t);
        const size_t count =
            nbytes / elementSize + (nbytes % elementSize != 0);

        currentSurface.parameters.resize(count);

        std::memcpy(
            currentSurface.parameters.data(),
            params,
            nbytes
        );
    }
}
// RtVoid RiDisplacement(RtShaderFn fn, const void *params, size_t nbytes) { (void)fn, (void)params, (void)nbytes; }
RtVoid RiDisplacement(RtShaderFn fn,
                      const void *params,
                      size_t nbytes)
{
    currentDisplacement.function = fn;
    currentDisplacement.parameters.clear();

    // NULL removes displacement.
    if (!fn) {
        return;
    }

    if (params && nbytes > 0) {
        const size_t elementSize = sizeof(std::max_align_t);
        const size_t count =
            nbytes / elementSize + (nbytes % elementSize != 0);

        currentDisplacement.parameters.resize(count);

        std::memcpy(
            currentDisplacement.parameters.data(),
            params,
            nbytes
        );
    }
}

/* Load an image into one of 8 texture slots with stb_image (look for the
 * file as given, then under textures/), and look it up bilinearly with
 * repeat wrap: s runs left to right, t from the TOP row (t = 0) to the
 * bottom (Section 6). An empty slot reads black. */
RtVoid RiMakeTexture(const char *file, RtInt slot)
{
    if (!file || slot < 0 || slot >= 8) {
        return;
    }

    int width = 0;
    int height = 0;
    int channels = 0;

    // stb's first row is the image's top row.
    stbi_set_flip_vertically_on_load(0);

    unsigned char *data = stbi_load(
        file, &width, &height, &channels, 3
    );

    if (!data) {
        const std::string fallback =
            std::string("textures/") + file;

        data = stbi_load(
            fallback.c_str(),
            &width, &height, &channels, 3
        );
    }

    if (!data) {
        textures[slot] = TextureImage{};

        const char *reason = stbi_failure_reason();
        std::fprintf(stderr, "RiMakeTexture: cannot load %s: %s\n",
                     file, reason ? reason : "unknown error");
        return;
    }

    TextureImage &texture = textures[slot];
    texture.width = width;
    texture.height = height;

    const size_t byteCount =
        static_cast<size_t>(width) * height * 3;

    texture.pixels.assign(data, data + byteCount);

    stbi_image_free(data);
}

RtVoid RiTexture(RtInt slot,
                  RtFloat s,
                  RtFloat t,
                  RtColor result)
{
    for (int channel = 0; channel < 3; ++channel) {
        result[channel] = 0.0f;
    }

    if (slot < 0 || slot >= 8 ||
        !std::isfinite(s) || !std::isfinite(t)) {
        return;
    }

    const TextureImage &texture = textures[slot];

    if (texture.width <= 0 ||
        texture.height <= 0 ||
        texture.pixels.empty()) {
        return;
    }

    /* Repeat wrap, including negative texture coordinates. */
    const double wrappedS =
        static_cast<double>(s) - std::floor(static_cast<double>(s));
    const double wrappedT =
        static_cast<double>(t) - std::floor(static_cast<double>(t));

    // Texel centers are at (i + 0.5) / size.
    const double x = wrappedS * texture.width - 0.5;
    const double y = wrappedT * texture.height - 0.5;

    const int x0 = static_cast<int>(std::floor(x));
    const int y0 = static_cast<int>(std::floor(y));

    const double fx = x - std::floor(x);
    const double fy = y - std::floor(y);

    auto wrapIndex = [](int index, int size) {
        const int remainder = index % size;
        return remainder < 0 ? remainder + size : remainder;
    };

    auto texel = [&](int px, int py, int channel) {
        px = wrapIndex(px, texture.width);
        py = wrapIndex(py, texture.height);

        const size_t offset =
            (static_cast<size_t>(py) * texture.width + px) * 3;

        return texture.pixels[offset + channel] / 255.0;
    };

    for (int channel = 0; channel < 3; ++channel) {
        const double top =
            (1.0 - fx) * texel(x0,     y0, channel) +
            fx         * texel(x0 + 1, y0, channel);

        const double bottom =
            (1.0 - fx) * texel(x0,     y0 + 1, channel) +
            fx         * texel(x0 + 1, y0 + 1, channel);

        result[channel] = static_cast<float>(
            (1.0 - fy) * top + fy * bottom
        );
    }
}

/* ------------------------------------------------------------ output files */

namespace {

/* Lowercase extension of a file name, without the dot ("" if none). */
std::string extension(const std::string &name)
{
    size_t dot = name.find_last_of('.');
    if (dot == std::string::npos)
        return "";
    std::string ext = name.substr(dot + 1);
    for (char &ch : ext)
        ch = (char)std::tolower((unsigned char)ch);
    return ext;
}

/* A value clamped to [0, 1], as an 8-bit integer. */
unsigned char to8(float v)
{
    return (unsigned char)std::floor(std::max(0.0f, std::min(1.0f, v)) * 255 + 0.5f);
}

/* Write the picture to renders/<RiDisplay name>, in the format its
 * extension names. Complete: call it from RiWorldEnd with your filtered
 * pixels, xres * yres * 4 floats, top row first: each pixel's color as
 * compositing leaves it, premultiplied by alpha (that is, over the black
 * background), then its alpha, the mean of its three opacity channels.
 * Don't clamp or convert them; this does, per format:
 *   .png, .tga  keep alpha. Both store straight color, so each pixel's
 *               color is divided by its alpha: a pixel half covered by a
 *               white surface is stored as white with alpha one half, and
 *               whatever shows the file multiplies the alpha back in.
 *   .jpg, .bmp  are written without alpha: JPEG has none, and most
 *               programs ignore a BMP's. They get the picture over the
 *               black background, which is the premultiplied color itself.
 * Values are clamped to [0, 1] and rounded to 8 bits. */
void writeImage(const float *rgba)
{
    const int w = options.xres, h = options.yres;
    const std::string path = "renders/" + options.display;
    const std::string ext = extension(options.display);
    const bool png = ext == "png", tga = ext == "tga", bmp = ext == "bmp";
    const bool jpg = ext == "jpg" || ext == "jpeg";
    if (!png && !tga && !bmp && !jpg) {
        std::fprintf(stderr, "cannot write %s: use .png, .bmp, .tga or .jpg\n", path.c_str());
        return;
    }

    const int comp = (png || tga) ? 4 : 3; /* with alpha, or without */
    std::vector<unsigned char> bytes((size_t)w * h * comp);
    for (size_t p = 0; p < (size_t)w * h; ++p) {
        const float *in = rgba + 4 * p;
        unsigned char *out = &bytes[comp * p];
        float alpha = std::max(0.0f, std::min(1.0f, in[3]));
        for (int c = 0; c < 3; ++c)
            out[c] = to8(comp == 3 ? in[c] : alpha > 0 ? in[c] / alpha : 0.0f);
        if (comp == 4)
            out[3] = to8(alpha);
    }

    int ok;
    if (png)
        ok = stbi_write_png(path.c_str(), w, h, comp, bytes.data(), w * comp);
    else if (tga)
        ok = stbi_write_tga(path.c_str(), w, h, comp, bytes.data());
    else if (bmp)
        ok = stbi_write_bmp(path.c_str(), w, h, comp, bytes.data());
    else
        ok = stbi_write_jpg(path.c_str(), w, h, comp, bytes.data(), 95);
    if (ok)
        std::printf("wrote %s (%d x %d)\n", path.c_str(), w, h);
    else
        std::fprintf(stderr, "could not write %s (run reyes from the repo root)\n", path.c_str());
}

/* Write the depth image to renders/<RiDisplay name> in the handout's
 * format: "Rz01", int32 width and height, then one float per pixel, top
 * row first, all little-endian (the camera-space z of the nearest surface
 * at least half opaque, +infinity where there is none). Complete: call it
 * from RiWorldEnd when RiDisplay asked for depth. The scene that reads the
 * file back knows the camera it was rendered with. */
void writeDepth(const float *depth)
{
    const int w = options.xres, h = options.yres;
    const std::string path = "renders/" + options.display;
    std::FILE *f = std::fopen(path.c_str(), "wb");
    if (!f) {
        std::fprintf(stderr, "could not write %s (run reyes from the repo root)\n", path.c_str());
        return;
    }
    const int32_t size[2] = {w, h};
    bool ok = std::fwrite("Rz01", 1, 4, f) == 4 && std::fwrite(size, sizeof size[0], 2, f) == 2 &&
              std::fwrite(depth, sizeof depth[0], (size_t)w * h, f) == (size_t)w * h;
    ok = std::fclose(f) == 0 && ok;
    if (ok)
        std::printf("wrote %s (%d x %d depth)\n", path.c_str(), w, h);
    else
        std::fprintf(stderr, "could not write %s\n", path.c_str());
}

} // namespace
