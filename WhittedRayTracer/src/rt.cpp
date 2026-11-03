/*
 * rt.cpp: the renderer behind include/rt.h: the graphics state, the rt*
 * entry points and the pixel loop.
 *
 * This is a stub. Every function the scenes call is here, the ones that
 * only store something already work, and rtWorldEnd writes a black
 * image of the right size and name and prints the TIME line, so the whole
 * kit builds and runs before you have written a line. Replace the stubs
 * with your ray tracer, following the Suggested Workflow: the camera and
 * the pixel loop first (Spheres --aov raydir is the first picture, then --aov normal), then
 * spheres and triangles, lights and shadows, the Whitted model with its
 * reflection and refraction, the shaders' binding, the BVH, threads. The
 * comment on each function says what the finished version must do and
 * where the handout describes it. Split the file up as it grows: the
 * build takes every .cpp under src/.
 *
 * What is given complete, and not here: the transforms (transform.cpp,
 * the CTM behind rtTranslate and friends, which you read with ctm()),
 * writeImage and readImage (image.cpp), perlinNoise (noise.cpp), the
 * command line and the scene tables (cmdline.cpp, options.h: the options
 * land in `options`, which you read at rtWorldEnd), the timer and the time
 * budget (timer.h), the trace log and the AOV encoders (debug.h), the
 * texture table and its lookup (texture.cpp, behind rtMakeTexture and
 * rtTexture below) and the four shaders (shaders.cpp; you wrote their like
 * in HW1). Everything the scene files include is in include/ and must
 * not change: rt.h and shaders.h are the interface; scenes.h, obj.h,
 * sceneutil.h and vec.h are helpers for the scene files (vec.h is yours
 * to use too, or any vector library you like).
 *
 * The calls marked [HW3] exist now so that no header changes later:
 * store what they say; nothing reads it until HW3.
 */
#include "rt.h"

#include "debug.h"
#include "image.h"
#include "options.h"
#include "texture.h"
#include "timer.h"
#include "transform.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

/* Everything inside this unnamed namespace is private to this file; only
 * the rt* functions below are visible to the scenes. */
namespace {

/* A bound shader: the function and its own copy of the parameter block,
 * made at bind time (HW2 handout: shaders). The copy is shallow: what the
 * block points to stays the scene's. */
template <typename Fn> struct Binding {
    Fn fn = nullptr;
    std::vector<unsigned char> params;

    void bind(Fn f, const void *p, size_t nbytes)
    {
        fn = f;
        params.assign((const unsigned char *)p, (const unsigned char *)p + (f && p ? nbytes : 0));
    }
};

/* The graphics state. rtBegin resets it to the defaults listed at
 * rtBegin. Options and attributes persist across rtWorldBegin/End pairs;
 * the world's contents (lights, primitives) are cleared at rtWorldBegin, so a
 * scene that renders several images builds each world afresh. The
 * finished renderer keeps more here: the lights, the primitives, the
 * textures, the BVH. */
struct State {
    int xres = 640, yres = 480;
    int spp = 1;
    int maxDepth = 8;
    float ambient[3] = {0, 0, 0};
    float background[3] = {0, 0, 0};
    int environment = -1; /* rtEnvironment's map, a texture handle (-1: none, the background shows) */
    float environmentScale = 1, environmentRotateY = 0;
    float eye[3] = {0, 0, 0}, lookat[3] = {0, 0, -1}, up[3] = {0, 1, 0};
    float fovy = 45;
    std::string display = "out"; /* rtDisplay's name, without extension */
    std::string tonemap = "none"; /* rtTonemap: the PNG's tone map and exposure (stops) */
    float exposure = 0;

    /* The [HW3] calls, stored now and read by cook (HW3): whitted
     * renders through a pinhole, one ray per pixel center, with plain
     * ambient. */
    float lensRadius = 0, focusDistance = 1;
    std::string sampler = "independent";
    float aoDistance = 0;
    /* rtEnvironmentPortal's quad, world space: sampling the environment through
     * it is HW3 extra credit, and nothing reads it until then. */
    bool hasPortal = false;
    float portal[4][3];

    RtMaterial material;
    Binding<RtShaderFn> surface;
    Binding<RtCutoutFn> cutout;

    /* TODO: the scene data structure (Workflow, step 4): a list of draw
     * calls (what the primitives of one rtSphere or rtTriangleMesh call
     * share), a list of spheres, a list of meshes and one of their
     * triangles, a list of lights, and from step 14 the BVH. */
};

State state;

/* The world's contents gone (rtWorldBegin, and rtWorldEnd when it is done). */
void clearWorld()
{
    /* TODO: empty the lists of draw calls, spheres, meshes, triangles and
     * lights (Workflow, step 4) */
}

RtMaterial defaultMaterial()
{
    RtMaterial m;
    m.albedo[0] = m.albedo[1] = m.albedo[2] = 0.8f;
    m.specular[0] = m.specular[1] = m.specular[2] = 0;
    m.roughness = 0.5f;
    m.metallic = 0;
    m.transmission = 0;
    m.ior = 1.5f;
    m.emission[0] = m.emission[1] = m.emission[2] = 0;
    return m;
}

/* The worker count: --threads, or every core. */
int threadCount()
{
    if (options.threads > 0)
        return options.threads;
    return std::max(1u, std::thread::hardware_concurrency());
}

} // namespace

/* session, options ------------------------------------------------------ */

/* Start a session and reset everything to these defaults:
 * 640 x 480, 1 spp, max depth 8, no ambient, a black background and no
 * environment, the camera at the origin looking down -z with +y up and
 * fovy 45, a pinhole, the independent sampler, no ambient occlusion, the
 * default material, no shaders, the CTM the identity with an empty stack.
 * rtEnd frees the textures and the geometry. */
RtVoid rtBegin(void)
{
    state = State();
    state.material = defaultMaterial();
    ctmReset();
    texturesClear();
}

RtVoid rtEnd(void)
{
    state = State();
    state.material = defaultMaterial();
    texturesClear();
}

/* The image size in pixels (--width and --height override it). */
RtVoid rtFormat(RtInt xres, RtInt yres)
{
    state.xres = std::max(1, xres);
    state.yres = std::max(1, yres);
}

/* The output name without extension: rtWorldEnd writes
 * renders/<integrator>/<name>.png, plus the .pfm with --pfm (imagePath in
 * cmdline.cpp; --out replaces the whole path). */
RtVoid rtDisplay(const char *name) { state.display = name ? name : "out"; }

/* The scene's own arguments: the numbers after its name on the command
 * line (cmdline.cpp), or the default where none was given. */
RtFloat rtSceneArg(RtInt index, RtFloat defaultValue)
{
    return index >= 0 && (size_t)index < options.sceneArgs.size() ? options.sceneArgs[(size_t)index] : defaultValue;
}

/* The PNG's look: stored here, applied by writeImage (image.cpp, given
 * complete); --tonemap and --exposure override it. */
RtVoid rtTonemap(const char *op, RtFloat exposure)
{
    state.tonemap = op ? op : "none";
    state.exposure = exposure;
}

/* Samples per pixel. whitted renders one whatever it says; cook and
 * kajiya (HW3, HW4) use it. */
RtVoid rtPixelSamples(RtInt spp) { state.spp = std::max(1, spp); }

/* The deepest secondary ray: a ray at depth d spawns children only if
 * d < maxDepth (Whitted Illumination handout). */
RtVoid rtMaxDepth(RtInt depth) { state.maxDepth = std::max(0, depth); }

/* The ambient term: ka * albedo at every lit point (Whitted Illumination
 * handout); cook scales it by occlusion, kajiya ignores it. */
RtVoid rtAmbient(RtColor ka) { std::memcpy(state.ambient, ka, sizeof state.ambient); }

/* The radiance a ray that leaves the scene returns, at any depth, when no
 * environment map is set. */
RtVoid rtBackground(RtColor radiance) { std::memcpy(state.background, radiance, sizeof state.background); }

/* A latitude-longitude map in place of the background (Workflow, step 13):
 * loaded now as a texture (PFM or HDR, linear; an 8-bit image is decoded
 * from sRGB), looked up at render time by the direction turned by -rotateY
 * about +y (the map turns by +rotateY, as rtRotate turns a textured
 * sphere), at the sphere's u and v of that direction. NULL removes it; a
 * file that does not load leaves the background showing. */
RtVoid rtEnvironment(const char *file, RtFloat scale, RtFloat rotateY)
{
    state.environment = -1; /* TODO (step 13): file ? rtMakeTexture(file, 1) : -1 */
    (void)file;
    state.environmentScale = scale;
    state.environmentRotateY = rotateY;
}

/* The camera in the world: eye, look-at, up and the full vertical field of
 * view in degrees (Viewing Geometry handout); the CTM does not apply. Stored
 * here; rtWorldEnd builds the camera's frame from it (Workflow, step 5).
 * ([HW3, extra credit]: inside a motion block the call keys the camera at the block's
 * time, ctmMotionTime(); store that too. A renderer with motion blur
 * reads it; nothing does until then.) */
RtVoid rtCamera(RtPoint eye, RtPoint lookat, RtVector up, RtFloat fovy)
{
    std::memcpy(state.eye, eye, sizeof state.eye);
    std::memcpy(state.lookat, lookat, sizeof state.lookat);
    std::memcpy(state.up, up, sizeof state.up);
    state.fovy = fovy;
}

/* lights ----------------------------------------------------------------- */

/* All three lights go through the CTM at the time of the call: the position
 * as a point, the direction as a vector (ctm().point and ctm().vector).
 * Intensities are radiometric (HW2 handout: the interface). Each adds a
 * light to the list in State (Workflow, step 9); the emissive draw calls
 * join them as stand-in point lights at rtWorldEnd (Whitted Illumination
 * handout). */
RtVoid rtPointLight(RtPoint position, RtColor intensity)
{
    (void)position;
    (void)intensity;
    /* TODO: add the light to the list, its position through the CTM */
}

RtVoid rtDirectionalLight(RtVector direction, RtColor irradiance)
{
    (void)direction;
    (void)irradiance;
    /* TODO: add the light to the list, its direction through the CTM, normalized */
}

/* The cone is stored as cosines; the penumbra is clamped into the cone. */
RtVoid rtSpotLight(RtPoint position, RtVector direction, RtColor intensity, RtFloat coneAngle, RtFloat penumbraAngle,
                   RtFloat maxDistance)
{
    (void)position;
    (void)direction;
    (void)intensity;
    (void)coneAngle;
    (void)penumbraAngle;
    (void)maxDistance;
    /* TODO: add the light to the list: position and direction through the
     * CTM, the cosines of the outer angle and of outer - penumbra, the reach */
}

/* attributes ------------------------------------------------------------- */

/* Attributes stay set until changed (rtTransformEnd does not restore them).
 * The material is copied now and becomes c->m at every hit on the
 * primitives drawn after it. */
RtVoid rtMaterial(const RtMaterial *m)
{
    if (m)
        state.material = *m;
}

/* Bind a shader to the primitives that follow, copying nbytes of params
 * now: the scene's struct may be gone by the time the shader runs. A
 * binding stays until the next call, so rtSurface(NULL, NULL, 0) unbinds:
 * the bound material then reaches a hit unchanged; rtCutout(NULL, NULL, 0)
 * unbinds too, and the surface is solid everywhere. */
RtVoid rtSurface(RtShaderFn fn, const void *params, size_t nbytes) { state.surface.bind(fn, params, nbytes); }

RtVoid rtCutout(RtCutoutFn fn, const void *params, size_t nbytes) { state.cutout.bind(fn, params, nbytes); }

/* primitives ------------------------------------------------------------- */

/* The CTM is a value: ctm() is the transformation as it stands at this
 * call, and a primitive keeps its own copy, so later transform calls
 * change the CTM but not what was already drawn. A sphere keeps the copy
 * (object to world), its inverse (world to object) and its radius: a ray
 * is carried into the sphere's object space through the inverse, where
 * the sphere is the one of that radius at the origin (so an ellipsoid or
 * a sheared sphere works), and its normal comes back through the inverse
 * transpose. A mesh is baked instead: its vertices go through the CTM
 * now, its normals through the inverse transpose, and only the inverse is
 * kept, for Po (the hit point in object space, which solid shaders read)
 * and the tangents. What the primitives of one call share (the copy, its
 * inverse, the material and the shaders bound at the call) is one
 * draw-call record they name by index (Intersection handout). */

/* A sphere of the given radius at the CTM's origin. */
RtVoid rtSphere(RtFloat radius)
{
    if (!(radius > 0)) {
        std::fprintf(stderr, "%s: rtSphere with radius %g, skipped\n", options.scene, radius);
        return;
    }
    /* TODO: add a draw call (a copy of ctm(), object to world; its inverse,
     * world to object; the material and the shaders bound now) and the
     * sphere, its radius and its draw call, to their lists (Workflow, step 6) */
}

/* ntris triangles over nverts vertices, counterclockwise from outside; N,
 * st and Cs may be NULL. Vertices go through the CTM, normals through its
 * inverse transpose; each triangle keeps its first vertex, two edges and
 * constant tangents (Intersection handout). A mirroring CTM (a negative
 * determinant, ctm().determinant3()) swaps the winding so the outside
 * stays outside; a bad index skips the triangle. */
RtVoid rtTriangleMesh(RtInt nverts, const RtPoint P[], const RtNormal N[], const RtFloat st[], const RtColor Cs[],
                      RtInt ntris, const RtInt indices[])
{
    if (nverts <= 0 || ntris <= 0 || !P || !indices)
        return;
    (void)N;
    (void)st;
    (void)Cs;
    /* TODO: add a draw call, then bake the mesh to world space and add it and
     * its triangles to their lists (Workflow, step 7) */
}

/* textures --------------------------------------------------------------- */

/* The texture table is given (texture.h): textureLoad reads the file
 * through readImage (image.h: PNG, JPEG, BMP, TGA, HDR and PFM as linear
 * floats, the 8-bit color channels decoded from sRGB when asked), trying
 * the path as given and then under textures/, and returns a handle (-1
 * with a message when it cannot); handles live until rtEnd. */
RtInt rtMakeTexture(const char *file, RtInt srgb) { return textureLoad(file ? file : "", srgb != 0); }

/* Bilinear lookup with repeat wrap; s left to right, t top to bottom (t = 0
 * is the top row); alpha 1 when the image has none. Given (texture.h). */
RtVoid rtTexture(RtInt handle, RtFloat s, RtFloat t, RtFloat rgba[4]) { textureLookup(handle, s, t, rgba); }

/* [HW3] calls ------------------------------------------------------------- */

/* Stored in the graphics state; whitted ignores all four. cook (HW3)
 * reads the lens, the sampler and the AO distance; the portal is HW3 extra
 * credit, read by no integrator yet. */
RtVoid rtLens(RtFloat radius, RtFloat focusDistance)
{
    state.lensRadius = std::max(0.0f, radius);
    state.focusDistance = focusDistance > 0 ? focusDistance : 1;
}

RtVoid rtSampler(const char *name)
{
    const std::string s = name ? name : "";
    if (s == "independent" || s == "stratified" || s == "halton" || s == "sobol")
        state.sampler = s;
    else
        std::fprintf(stderr, "%s: rtSampler(\"%s\"): not a sampler, keeping %s\n", options.scene, s.c_str(),
                     state.sampler.c_str());
}

RtVoid rtAmbientOcclusion(RtFloat maxDistance) { state.aoDistance = std::max(0.0f, maxDistance); }

RtVoid rtEnvironmentPortal(const RtPoint corners[4])
{
    state.hasPortal = corners != NULL;
    for (int i = 0; corners && i < 4; i++) {
        const Vec3 p = ctm().point(Vec3(corners[i]));
        state.portal[i][0] = p.x;
        state.portal[i][1] = p.y;
        state.portal[i][2] = p.z;
    }
}

/* --warptest (HW3) and --bsdftest (HW4): cmdline.cpp calls these instead of
 * rendering. Until those homeworks they only say so. */
int warpTest(const Options &o)
{
    std::fprintf(stderr, "--warptest %s: the warps are HW3's; this renderer has none yet\n", o.warp.c_str());
    return 1;
}

int bsdfTest(const Options &o)
{
    (void)o;
    std::fprintf(stderr, "--bsdftest: the BSDFs are HW4's; this renderer has none yet\n");
    return 1;
}

/* world ------------------------------------------------------------------ */

/* rtWorldBegin starts a world: the lights and primitives drawn from here to
 * rtWorldEnd are what the image shows. The options and attributes set so
 * far carry over. Returns whether --frames wants this frame; a movie skips
 * the draw calls of one it does not (rt.h). */
RtInt rtWorldBegin(RtInt frame)
{
    clearWorld();
    return options.frameFirst < 0 || (frame >= options.frameFirst && frame <= options.frameLast);
}

/* Build the acceleration structure, render, write the image and print the
 * TIME line (HW2 handout: timing):
 *     TIME <scene> build=<s> render=<s> total=<s> threads=<n>
 * build is the light list and the BVH, render the pixel loop, total
 * includes writing the image; all three come from timer.h's Timer. The
 * command line overrides the scene's format, samples and depth here
 * (options.width, .height, .spp, .maxDepth, .ambient; options.aov picks
 * a debug image, options.lens the lens).
 *
 * The finished pixel loop (Workflow, steps 5 and 15): 16 x 16 tiles handed out from an atomic
 * counter to a pool of std::thread workers, each writing only its tile,
 * so the image is the same at any thread count; traceBegin(px, py) before
 * a pixel and traceEnd() after it (debug.h); the time budget of timer.h
 * (RenderBudget: its tileOrder, expired() before each tile and row, done()
 * per row, report() after the TIME line). Under whitted one ray through
 * each pixel's center, time 0. */
/* The debug images this renderer writes (--aov; debug.h has the encoders).
 * HW3 adds ao. */
static bool isAov(const std::string &name)
{
    static const char *const names[] = {"raydir", "normal", "ng", "depth", "uv", "albedo", "cost", "shadow"};
    for (const char *n : names)
        if (name == n)
            return true;
    return false;
}

RtVoid rtWorldEnd(void)
{
    Timer total;

    const int w = options.width > 0 ? options.width : state.xres;
    const int h = options.height > 0 ? options.height : state.yres;
    const int threads = threadCount();
    if (!options.aov.empty() && !isAov(options.aov)) {
        std::fprintf(stderr, "%s: --aov %s: not an image this renderer writes; it has raydir, normal, ng, depth, uv, albedo, "
                             "cost and shadow\n", options.scene, options.aov.c_str());
        std::exit(1);
    }

    /* TODO: build (Workflow, steps 9 and 14): the light list (the delta
     * lights and the emissive draw calls' stand-ins) and the BVH over every
     * primitive. */
    const double build = total.seconds();

    /* TODO: render (Workflow, step 5 on): the pixel loop, one ray through
     * each pixel's center, shaded by the Whitted model. For now, a black
     * image. */
    Timer render;
    std::vector<float> rgb((size_t)w * h * 3, 0.0f);
    RenderBudget budget(options.timeLimit); /* --time-limit: the clock starts here (timer.h) */
    if (options.progress)
        budget.progress((long long)w * h); /* --progress: a line every two seconds */
    budget.done((long long)w * h);
    budget.finish();
    const double renderTime = render.seconds();

    /* The scene's tone map and exposure, unless the command line says
     * otherwise; a debug image (--aov) is written as it is. */
    const bool aov = !options.aov.empty();
    const std::string tonemap = aov ? "none" : options.tonemap.empty() ? state.tonemap : options.tonemap;
    const float exposure = aov ? 0.0f : options.exposureSet ? options.exposure : state.exposure;
    writeImage(imagePath(state.display), w, h, rgb.data(), options.pfm, tonemap, exposure, options.format);

    const char *sceneName = options.scene[0] ? options.scene : state.display.c_str(); /* the simple main sets none */
    std::printf("TIME %s build=%.3f render=%.3f total=%.3f threads=%d\n", sceneName, build, renderTime,
                total.seconds(), threads);
    budget.report(sceneName, renderTime, (long long)w * h, 1);
    std::fflush(stdout);

    clearWorld();
}
