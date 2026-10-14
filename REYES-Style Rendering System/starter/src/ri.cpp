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
    bool depthDisplay = false; /* RiDisplay(name, "zfile", "z") */
};

Options options;

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
RtVoid RiBegin(RtToken /*name*/) {}
RtVoid RiEnd(void) {}

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
RtVoid RiFrameBegin(RtInt /*frame*/) {}
RtVoid RiFrameEnd(void) {}

/* RiWorldBegin: the transformation built so far is the camera transform
 * (world to camera); from here on transformations place objects. Set up
 * the sample buffer. RiWorldEnd: all primitives have been drawn, so filter
 * the samples into pixels and write the image (Sections 2 and 4). */
RtVoid RiWorldBegin(void) {}

RtVoid RiWorldEnd(void)
{
    /* TODO: your pixels. For now, a black, fully transparent image (or an
     * empty depth image, +infinity everywhere). */
    if (options.depthDisplay) {
        std::vector<float> depth((size_t)options.xres * options.yres, INFINITY);
        writeDepth(depth.data());
    } else {
        std::vector<float> rgba((size_t)options.xres * options.yres * 4, 0.0f);
        writeImage(rgba.data());
    }
}

/* RiProjection(RI_PERSPECTIVE, "fov", &fov), fov in degrees over the
 * shorter side of the image, or RiProjection(RI_ORTHOGRAPHIC). It also
 * resets the current transformation to the identity: the camera transform
 * is built after it (Section 2). */
RtVoid RiProjection(RtToken name, RtToken token, RtPointer value)
{
    (void)name;
    (void)token;
    (void)value;
}

/* ----------------------------------------------------------- transformations */

/* The current transformation matrix (CTM) and its stack. Each call
 * post-multiplies the CTM, so the transformation called last is applied
 * to points first; RiTransformBegin pushes a copy and RiTransformEnd pops
 * it. Scenes nest these several deep. RiConcatTransform takes an RtMatrix
 * in RenderMan's row-vector form (p' = p M, translation in the bottom
 * row). Angles are degrees (Section 2). */
RtVoid RiTransformBegin(void) {}
RtVoid RiTransformEnd(void) {}
RtVoid RiConcatTransform(RtMatrix transform) { (void)transform; }
RtVoid RiTranslate(RtFloat dx, RtFloat dy, RtFloat dz) { (void)dx, (void)dy, (void)dz; }
RtVoid RiRotate(RtFloat angle, RtFloat dx, RtFloat dy, RtFloat dz) { (void)angle, (void)dx, (void)dy, (void)dz; }
RtVoid RiScale(RtFloat sx, RtFloat sy, RtFloat sz) { (void)sx, (void)sy, (void)sz; }

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
RtVoid RiSphere(RtFloat radius, RtFloat zmin, RtFloat zmax, RtFloat thetamax)
{
    (void)radius, (void)zmin, (void)zmax, (void)thetamax;
}

RtVoid RiCylinder(RtFloat radius, RtFloat zmin, RtFloat zmax, RtFloat thetamax)
{
    (void)radius, (void)zmin, (void)zmax, (void)thetamax;
}

RtVoid RiCone(RtFloat height, RtFloat radius, RtFloat thetamax)
{
    (void)height, (void)radius, (void)thetamax;
}

RtVoid RiTorus(RtFloat majorradius, RtFloat minorradius, RtFloat phimin, RtFloat phimax, RtFloat thetamax)
{
    (void)majorradius, (void)minorradius, (void)phimin, (void)phimax, (void)thetamax;
}

/* A mesh of polygons: polygon i has nverts[i] corners, whose indices into
 * P (and N and st, when they are not NULL) follow one another in verts.
 * Treat every polygon as its own primitive, bounded, diced, shaded and
 * hidden before the next: a quad is a bilinear patch, a triangle the same
 * patch with its last corner repeated, and a larger polygon a fan of
 * triangles from its first vertex (Section 3, "Polygon meshes"). */
RtVoid RiPointsPolygons(RtInt npolys, const RtInt nverts[], const RtInt verts[],
                        const RtPoint P[], const RtNormal N[], const RtFloat st[])
{
    (void)npolys, (void)nverts, (void)verts, (void)P, (void)N, (void)st;
}

/* ------------------------------------------------------------------ shading */

/* Attributes: they stay set until changed (RiTransformEnd does not restore
 * them). The color and opacity become Cs and Os in the shader context. */
RtVoid RiColor(RtColor color) { (void)color; }
RtVoid RiOpacity(RtColor opacity) { (void)opacity; }

/* Bind a shader to the primitives that follow. Copy nbytes of params now:
 * the scene's struct may be gone by the time the shader runs. fn NULL means
 * STANDARD for RiSurface and no displacement for RiDisplacement
 * (Section 5). */
RtVoid RiSurface(RtShaderFn fn, const void *params, size_t nbytes) { (void)fn, (void)params, (void)nbytes; }
RtVoid RiDisplacement(RtShaderFn fn, const void *params, size_t nbytes) { (void)fn, (void)params, (void)nbytes; }

/* Load an image into one of 8 texture slots with stb_image (look for the
 * file as given, then under textures/), and look it up bilinearly with
 * repeat wrap: s runs left to right, t from the TOP row (t = 0) to the
 * bottom (Section 6). An empty slot reads black. */
RtVoid RiMakeTexture(const char *file, RtInt slot) { (void)file, (void)slot; }

RtVoid RiTexture(RtInt slot, RtFloat s, RtFloat t, RtColor result)
{
    (void)slot, (void)s, (void)t;
    result[0] = result[1] = result[2] = 0;
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
