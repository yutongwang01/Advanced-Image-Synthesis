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

#include "camera.h"
#include "debug.h"
#include "image.h"
#include "options.h"
#include "scene.h"
#include "texture.h"
#include "timer.h"
#include "transform.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string>
#include <thread>
#include <vector>

/* Everything inside this unnamed namespace is private to this file; only
 * the rt* functions below are visible to the scenes. */
namespace {

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
    ShaderBinding<RtShaderFn> surface;
    ShaderBinding<RtCutoutFn> cutout;

    /* The scene data structure (Workflow, step 4), see scene.h: a list of
     * draw calls (what the primitives of one rtSphere or rtTriangleMesh
     * call share), a list of spheres, a list of meshes and one of their
     * triangles, and a list of lights. The world's contents: cleared by
     * clearWorld().
     * TODO: from step 14, the BVH. */
    Scene scene;
};

State state;

/* The world's contents gone (rtWorldBegin, and rtWorldEnd when it is done). */
void clearWorld()
{
    state.scene.clear(); /* the lists of draw calls, spheres, meshes, triangles and lights */
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
    Light light;
    light.type = LightType::Point;
    light.position = ctm().point(Vec3(position)); /* through the CTM now, not at render time */
    light.intensity = Vec3(intensity);
    state.scene.lights.push_back(light);
}

/* direction is where the light travels (light from above has a negative y),
 * not the direction from a surface toward the light: shading uses -direction. */
RtVoid rtDirectionalLight(RtVector direction, RtColor irradiance)
{
    Light light;
    light.type = LightType::Directional;
    light.direction = normalize(ctm().vector(Vec3(direction)));
    light.intensity = Vec3(irradiance);
    state.scene.lights.push_back(light);
}

/* The cone is stored as cosines; the penumbra is clamped into the cone. */
RtVoid rtSpotLight(RtPoint position, RtVector direction, RtColor intensity, RtFloat coneAngle, RtFloat penumbraAngle,
                   RtFloat maxDistance)
{
    const float kDegToRad = 3.14159265358979323846f / 180.0f;
    const float cone = std::max(0.0f, coneAngle);
    const float penumbra = std::min(std::max(0.0f, penumbraAngle), cone); /* into [0, cone] */

    Light light;
    light.type = LightType::Spot;
    light.position = ctm().point(Vec3(position));
    light.direction = normalize(ctm().vector(Vec3(direction))); /* where the light travels */
    light.intensity = Vec3(intensity);
    light.cosOuter = std::cos(cone * kDegToRad);              /* gone at the cone angle */
    light.cosInner = std::cos((cone - penumbra) * kDegToRad); /* full strength inside cone - penumbra */
    light.maxDistance = std::max(0.0f, maxDistance);          /* 0: no limit */
    state.scene.lights.push_back(light);
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

/* Ray-sphere intersection (Workflow, step 6; Intersection handout). The
 * ray is carried into the sphere's object space through the inverse CTM:
 * the origin as a point, the direction as a vector and NOT renormalized, so
 * t means the same point in both spaces. There the sphere is |P'| = r, and
 * |O' + t D'|^2 = r^2 is a t^2 + 2 b t + c = 0, solved in double. The
 * smaller root with tMin < t < min(tMax, hit.t) wins. The object-space normal P' / |P'| comes back
 * through the inverse transpose, n (M^-1)^T in the row-vector convention,
 * which stays perpendicular to the surface under a non-uniform scale.
 * Returns whether `hit` was updated; Ng and N are the outward normal,
 * hit.entering whether the ray met the outside. */
bool intersectSphere(const Ray &ray, const Sphere &sphere, int drawCallIndex, const DrawCall &dc, Hit &hit)
{
    /* 1. the ray in object space */
    const Vec3 o = dc.worldToObject.point(ray.o);
    const Vec3 d = dc.worldToObject.vector(ray.d);

    /* 2. the quadratic a t^2 + 2 b t + c = 0, in double precision against
     * cancellation in b^2 - a c (Intersection handout, section 2.2):
     * a = d'.d', b = o'.d', c = o'.o' - r^2, t0,1 = (-b -+ sqrt(b^2 - a c)) / a */
    const double ox = o.x, oy = o.y, oz = o.z, dx = d.x, dy = d.y, dz = d.z, r = sphere.radius;
    const double a = dx * dx + dy * dy + dz * dz;
    if (!(a > 0))
        return false; /* a degenerate direction */
    const double b = ox * dx + oy * dy + oz * dz;
    const double c = ox * ox + oy * oy + oz * oz - r * r;
    const double disc = b * b - a * c;
    if (disc < 0)
        return false; /* no intersection */
    const double sq = std::sqrt(disc);
    const double t0 = (-b - sq) / a; /* t0 <= t1 */
    const double t1 = (-b + sq) / a;

    /* The smallest root strictly ahead of tMin and nearer than the hit so
     * far: t0, else t1 (the origin inside the sphere). */
    const double tFar = std::min(ray.tMax, hit.t);
    double tHit;
    if (t0 > ray.tMin && t0 < tFar)
        tHit = t0;
    else if (t1 > ray.tMin && t1 < tFar)
        tHit = t1;
    else
        return false;
    const float t = (float)tHit;

    /* 3. the hit point and the normal: Po through (M^-1)^T, normalized */
    const Vec3 pObj = o + d * t;
    const Vec3 n = normalize(dc.worldToObject.transposed().vector(pObj));

    hit.t = t;
    hit.P = ray.at(t);
    hit.Ng = n;
    hit.N = n;
    hit.drawCall = drawCallIndex;
    hit.entering = dot(n, ray.d) < 0;
    hit.b1 = hit.b2 = 0;
    hit.hit = true;
    return true;
}

/* Ray-triangle intersection. A hit needs u >= 0, v >= 0, u + v <= 1 (exact, no tolerance, as the
 * reference) and tMin < t < min(tMax, hit.t).
 * Both sides count (no back-face culling): rays inside glass hit the back.
 * Ng is the unit e1 x e2, outward by the winding rtTriangleMesh kept; N is
 * the vertex normals interpolated with the barycentrics (1 - u - v, u, v)
 * when the mesh has them (Ng then turned to N's side), else Ng. Neither is
 * turned toward the ray here; hit.entering says which side was met, and
 * hit.b1, hit.b2 keep the barycentrics.
 * Returns whether `hit` was updated. */
bool intersectTriangle(const Ray &ray, const Triangle &tri, const Mesh &mesh, Hit &hit)
{
    const Vec3 p = cross(ray.d, tri.e2);
    const float det = dot(tri.e1, p);
    const Vec3 nGeom = cross(tri.e1, tri.e2);
    if (!(det * det > 1e-12f * lengthSquared(nGeom) * lengthSquared(ray.d)))
        return false;
    const float invDet = 1.0f / det;

    const Vec3 sv = ray.o - tri.p0;
    const float u = dot(sv, p) * invDet;
    if (u < 0.0f || u > 1.0f)
        return false;

    const Vec3 q = cross(sv, tri.e1);
    const float v = dot(ray.d, q) * invDet;
    if (v < 0.0f || u + v > 1.0f)
        return false;

    const float t = dot(tri.e2, q) * invDet;
    if (!(t > ray.tMin && t < std::min(ray.tMax, hit.t)))
        return false; /* strictly ahead of tMin (t > 0 for tMin = 0) and nearer than the hit so far */

    Vec3 ng = normalize(nGeom);
    Vec3 n = ng;
    if (mesh.hasNormals()) {
        const float w = 1.0f - u - v;
        n = normalize(mesh.N[(size_t)tri.v[0]] * w + mesh.N[(size_t)tri.v[1]] * u + mesh.N[(size_t)tri.v[2]] * v);
        /* Ng to the interpolated normal's side first, as the reference
         * does (Intersection handout, section 3.3). */
        if (dot(ng, n) < 0)
            ng = -ng;
    }

    hit.t = t;
    hit.P = ray.at(t);
    hit.Ng = ng;
    hit.N = n;
    hit.drawCall = mesh.drawCall;
    hit.entering = dot(ng, ray.d) < 0; /* from Ng as stored, before the facing flip */
    hit.b1 = u;
    hit.b2 = v;
    hit.hit = true;
    return true;
}

/* The nearest hit of the ray in the whole scene, spheres and triangles
 * alike, by brute force over every primitive (no BVH yet, Workflow step
 * 14). One Hit is shared by both loops: each intersect function accepts
 * only a hit nearer than the one already there, so what is left at the end
 * is the nearest, and occlusion between spheres and triangles is right. */
bool intersectScene(const Ray &ray, const Scene &scene, Hit &hit, int skipDrawCall = -1)
{
    bool found = false;
    for (size_t i = 0; i < scene.spheres.size(); ++i) {
        const Sphere &sphere = scene.spheres[i];
        if (sphere.drawCall == skipDrawCall)
            continue;
        if (intersectSphere(ray, sphere, sphere.drawCall, scene.drawCalls[(size_t)sphere.drawCall], hit)) {
            hit.prim = (int)i;
            found = true;
        }
    }
    for (size_t i = 0; i < scene.triangles.size(); ++i) {
        const Triangle &triangle = scene.triangles[i];
        const Mesh &mesh = scene.meshes[(size_t)triangle.mesh];
        if (mesh.drawCall == skipDrawCall)
            continue;
        if (intersectTriangle(ray, triangle, mesh, hit)) {
            hit.prim = (int)(scene.spheres.size() + i);
            found = true;
        }
    }
    return found;
}

/* The offset that keeps a secondary ray off the surface it starts on: the
 * ray starts at P + Ng * kRayEpsilon (Whitted Illumination handout, section
 * 4; Intersection handout, section 5). */
const float kRayEpsilon = 1e-4f;

/* Whether anything lies on the ray within [tMin, tMax]: an any-hit test for
 * shadow rays (Whitted Illumination handout, section 4). The first hit ends
 * the search, nearest or not. skipDrawCall names a draw call whose
 * primitives are ignored (the emitter a stand-in light sits inside), -1
 * for none. Transparent blockers block completely. Brute force, like
 * intersectScene, until the BVH. */
bool occluded(const Ray &ray, const Scene &scene, int skipDrawCall = -1)
{
    for (size_t i = 0; i < scene.spheres.size(); ++i) {
        const Sphere &sphere = scene.spheres[i];
        if (sphere.drawCall == skipDrawCall)
            continue;
        Hit hit;
        if (intersectSphere(ray, sphere, sphere.drawCall, scene.drawCalls[(size_t)sphere.drawCall], hit))
            return true;
    }
    for (size_t i = 0; i < scene.triangles.size(); ++i) {
        const Triangle &triangle = scene.triangles[i];
        const Mesh &mesh = scene.meshes[(size_t)triangle.mesh];
        if (mesh.drawCall == skipDrawCall)
            continue;
        Hit hit;
        if (intersectTriangle(ray, triangle, mesh, hit))
            return true;
    }
    return false;
}

/* The irradiance E and the unit direction L (from P toward the light) that
 * one light delivers at P (Whitted Illumination handout, section 3), and
 * the distance d to it (infinite for a directional light). Returns false
 * when it delivers nothing: outside a spot's cone or past its reach. */
bool lightAt(const Light &light, const Vec3 &P, Vec3 &E, Vec3 &L, float &d)
{
    if (light.type == LightType::Directional) {
        L = -light.direction; /* direction is where the light travels */
        E = light.intensity;  /* the irradiance, the same everywhere */
        d = std::numeric_limits<float>::infinity();
        return true;
    }

    /* Point, spot (and, later, the emitters' stand-ins): I / d^2 */
    const Vec3 toLight = light.position - P;
    const float d2 = lengthSquared(toLight);
    if (!(d2 > 0))
        return false;
    d = std::sqrt(d2);
    L = toLight / d;
    E = light.intensity / d2;

    if (light.type == LightType::Emissive && light.oneSided) {
        /* a flat emitter's stand-in shines forward only: I(w) = Le A max(n . w, 0),
         * w the direction from the light toward P */
        const float c = dot(light.normal, -L);
        if (!(c > 0))
            return false;
        E = E * c;
    }

    if (light.type == LightType::Spot) {
        /* cone: 1 inside cone - penumbra, 0 outside the cone, smoothstep
         * u^2 (3 - 2u) of the cosine's position in between */
        const float c = dot(light.direction, -L); /* axis . (light toward P) */
        float cone;
        if (c >= light.cosInner)
            cone = 1.0f;
        else if (c <= light.cosOuter)
            cone = 0.0f;
        else {
            const float u = (c - light.cosOuter) / (light.cosInner - light.cosOuter);
            cone = u * u * (3.0f - 2.0f * u);
        }
        /* reach: (1 - (d / dmax)^2)^2 before dmax, 0 beyond, 1 for dmax = 0 */
        float reach = 1.0f;
        if (light.maxDistance > 0) {
            if (d >= light.maxDistance)
                reach = 0.0f;
            else {
                const float r = 1.0f - d2 / (light.maxDistance * light.maxDistance);
                reach = r * r;
            }
        }
        if (!(cone * reach > 0))
            return false;
        E = E * (cone * reach);
    }
    return true;
}

/* The shadow ray toward a light (Whitted Illumination handout, section 4):
 * from P + Ng eps, Ng facing the light, up to d - eps for a point, spot or
 * stand-in light and without end for a directional one; depth -1 marks it
 * a shadow ray (the cutout shaders' convention). */
Ray shadowRay(const Light &light, const Vec3 &P, const Vec3 &Ng, const Vec3 &L, float d)
{
    const float tMax = light.type == LightType::Directional ? std::numeric_limits<float>::infinity() : d - kRayEpsilon;
    return Ray(P + Ng * kRayEpsilon, L, 0.0f, tMax, -1);
}

/* The draw call a shadow ray toward this light ignores: a stand-in sits
 * inside its own emitter. -1 for the other lights. */
int shadowSkip(const Light &light) { return light.type == LightType::Emissive ? light.drawCall : -1; }

/* Whether light j reaches P, in the handout's order: the light's range
 * (a spot's cone and reach, a flat emitter's back), the geometric horizon
 * (Ng . L <= 0: no shadow ray), then the shadow ray. Ng faces the ray.
 * Shared by the shading and --aov shadow, so the two always agree. */
enum class LightReach { OutOfReach, Behind, Shadowed, Visible };

LightReach lightReach(const Light &light, const Vec3 &P, const Vec3 &Ng, Vec3 &E, Vec3 &L, float &d)
{
    if (!lightAt(light, P, E, L, d))
        return LightReach::OutOfReach;
    if (dot(Ng, L) <= 0)
        return LightReach::Behind;
    if (occluded(shadowRay(light, P, Ng, L, d), state.scene, shadowSkip(light)))
        return LightReach::Shadowed;
    return LightReach::Visible;
}

/* The emitters' stand-in point lights, added after the scene's own lights at rtWorldEnd: one per
 * draw call whose bound material emits, with Le that emission.
 * Zero-area triangles count neither for the area, the centroid nor the
 * flatness test; a draw call with no area gets no light. */
void addEmitterLights()
{
    const float kPi = 3.14159265358979323846f;
    Scene &scene = state.scene;
    for (size_t dci = 0; dci < scene.drawCalls.size(); ++dci) {
        const DrawCall &dc = scene.drawCalls[dci];
        if (!dc.emissive)
            continue;
        const Vec3 Le(dc.material.emission);

        Light light;
        light.type = LightType::Emissive;
        light.drawCall = (int)dci;
        bool made = false;

        for (const Sphere &sphere : scene.spheres) {
            if (sphere.drawCall != (int)dci)
                continue;
            const Mat4 &M = dc.objectToWorld;
            const float meanScale = (length(M.vector(Vec3(1, 0, 0))) + length(M.vector(Vec3(0, 1, 0))) +
                                     length(M.vector(Vec3(0, 0, 1)))) / 3.0f;
            const float r = sphere.radius * meanScale;
            if (!(r > 0))
                break;
            light.position = M.point(Vec3(0, 0, 0));
            light.intensity = Le * (kPi * r * r);
            made = true;
            break;
        }

        for (const Mesh &mesh : scene.meshes) {
            if (made || mesh.drawCall != (int)dci)
                continue;
            float area = 0;
            Vec3 centroid(0.0f), n0(0.0f);
            bool haveN0 = false, flat = true;
            for (int k = mesh.firstTriangle; k < mesh.firstTriangle + mesh.triangleCount; ++k) {
                const Triangle &tri = scene.triangles[(size_t)k];
                const Vec3 c = cross(tri.e1, tri.e2);
                const float a = 0.5f * length(c);
                if (!(a > 0))
                    continue; /* zero area: skipped */
                const Vec3 ni = c / (2.0f * a);
                if (!haveN0) {
                    n0 = ni;
                    haveN0 = true;
                } else if (!(dot(ni, n0) > 0.9998f))
                    flat = false;
                area += a;
                centroid += (tri.p0 + (tri.e1 + tri.e2) / 3.0f) * a;
            }
            if (!(area > 0))
                break;
            light.position = centroid / area;
            if (flat) {
                light.oneSided = true;
                light.normal = n0;
                light.intensity = Le * area;
            } else
                light.intensity = Le * (area / 4.0f);
            made = true;
            break;
        }

        if (made)
            scene.lights.push_back(light);
        else
            std::fprintf(stderr, "%s: emissive draw call %zu has no area, no light made for it\n", options.scene, dci);
    }
}

/* The radiance a ray returns (Whitted Illumination handout, Eq. 1). So far
 * the first two lines and the emission:
 *     (1 - kt) (ka kd + sum_j Sj Ej kd/pi max(N.Lj, 0)) + sum_j Sj Ej ks max(Rj.V, 0)^n + emission
 * with Rj = 2 (N.Lj) N - Lj, V = -I, alpha = clamp(roughness^2, 0.05, 1) and
 * n = max(1, 2 / alpha^2 - 2): Lambert with its 1/pi, unnormalized Phong
 * not dimmed by (1 - kt). Per light, in the handout's order: its range,
 * the horizon, the shadow ray (lightReach), then N . L > 0. A miss returns
 * the background. The emission of a draw call whose bound material emits
 * shows on its front face only. With --trace, the handout's log lines.
 * The mirror ray of an opaque surface (step 10) recurses one level deeper.
 * Not yet: the surface shader, the refracted ray (step 11). */
#define RT_V3(v) (double)(v).x, (double)(v).y, (double)(v).z
Vec3 traceRay(const Ray &ray, int depth)
{
    const int ind = 2 * depth;   /* this depth's lines */
    const int sub = ind + 2;     /* the lines under a hit */

    Hit hit;
    if (!intersectScene(ray, state.scene, hit)) {
        const Vec3 bg(state.background);
        RT_TRACE("%*sdepth %d: miss, environment (%g, %g, %g)", ind, "", depth, RT_V3(bg));
        return bg;
    }

    const DrawCall &dc = state.scene.drawCalls[(size_t)hit.drawCall];
    const RtMaterial &m = dc.material;
    const Vec3 kd(m.albedo), ks(m.specular);
    const float kt = m.transmission;
    const Vec3 ka = options.ambientSet ? Vec3(options.ambient) : Vec3(state.ambient); /* --ambient overrides */

    /* Which face the ray met, from Ng before the flip (kept for the
     * emission and, later, refraction), then the facing rule: Ng turned
     * toward the ray, N along with it. */
    const bool entering = hit.entering;
    Vec3 Ng = hit.Ng, N = hit.N;
    if (!entering) {
        Ng = -Ng;
        N = -N;
    }
    const Vec3 V = -ray.d;

    RT_TRACE("%*sdepth %d: hit prim %d t=%g P=(%g, %g, %g) N=(%g, %g, %g) Ng=(%g, %g, %g) %s", ind, "", depth,
             hit.prim, (double)hit.t, RT_V3(hit.P), RT_V3(N), RT_V3(Ng), entering ? "entering" : "leaving");
    RT_TRACE("%*smaterial: albedo (%g, %g, %g) specular (%g, %g, %g) roughness %g transmission %g ior %g "
             "emission (%g, %g, %g)", sub, "", RT_V3(kd), RT_V3(ks), (double)m.roughness, (double)kt, (double)m.ior,
             (double)m.emission[0], (double)m.emission[1], (double)m.emission[2]);

    /* The Phong exponent from the roughness, not the roughness itself. */
    const float alpha = std::min(1.0f, std::max(0.05f, m.roughness * m.roughness));
    const float n = std::max(1.0f, 2.0f / (alpha * alpha) - 2.0f);
    const bool hasSpecular = ks.x > 0 || ks.y > 0 || ks.z > 0;

    const float kInvPi = 1.0f / 3.14159265358979323846f;
    Vec3 diffuse(0.0f), specular(0.0f);
    for (size_t j = 0; j < state.scene.lights.size(); ++j) {
        const Light &light = state.scene.lights[j];
        Vec3 E, L;
        float d = 0;
        const LightReach reach = lightReach(light, hit.P, Ng, E, L, d);
        if (reach == LightReach::OutOfReach) {
            RT_TRACE("%*slight %zu: out of reach", sub, "", j);
            continue;
        }
        if (reach == LightReach::Behind) {
            RT_TRACE("%*slight %zu: behind the surface", sub, "", j);
            continue;
        }
        if (reach == LightReach::Shadowed) {
            if (tracing()) { /* the nearest blocker, for the log only */
                Hit b;
                intersectScene(shadowRay(light, hit.P, Ng, L, d), state.scene, b, shadowSkip(light));
                const Vec3 ba = b.drawCall >= 0 ? Vec3(state.scene.drawCalls[(size_t)b.drawCall].material.albedo) : Vec3(0.0f);
                RT_TRACE("%*slight %zu: shadowed, L=(%g, %g, %g) by prim %d of object %d at t=%g P=(%g, %g, %g), "
                         "albedo (%g, %g, %g)", sub, "", j, RT_V3(L), b.prim, b.drawCall, (double)b.t, RT_V3(b.P),
                         RT_V3(ba));
            }
            continue;
        }
        const float NdotL = dot(N, L);
        if (NdotL <= 0) {
            RT_TRACE("%*slight %zu: N . L <= 0", sub, "", j);
            continue; /* the shading normal faces away: both terms zero */
        }
        RT_TRACE("%*slight %zu: lit, L=(%g, %g, %g) E=(%g, %g, %g) N.L=%g", sub, "", j, RT_V3(L), RT_V3(E),
                 (double)NdotL);

        diffuse += E * kd * (NdotL * kInvPi);
        if (hasSpecular) {
            const Vec3 R = N * (2.0f * NdotL) - L;
            const float RdotV = std::max(0.0f, dot(R, V));
            specular += E * ks * std::pow(RdotV, n);
        }
    }

    const Vec3 ambientDiffuse = ka * kd + diffuse;
    RT_TRACE("%*sdirect: ambient+diffuse (%g, %g, %g) x (1 - %g) + specular (%g, %g, %g)", sub, "",
             RT_V3(ambientDiffuse), (double)kt, RT_V3(specular));

    Vec3 result = ambientDiffuse * (1.0f - kt) + specular;

    /* Reflection (Whitted Illumination handout, section 7): an opaque
     * surface with ks > 0 traces one mirror ray, weighted by the
     * smoothness wr = max(0, 1 - roughness^2 / 0.15), while the depth is
     * below rtMaxDepth (--max-depth overrides it). The ray starts at
     * P + Ng eps, Ng facing the ray, in the direction R = I - 2 (N . I) N,
     * with tMin = 0 and no tMax, one level deeper. */
    const float wr = std::max(0.0f, 1.0f - m.roughness * m.roughness / 0.15f);
    const int maxDepth = options.maxDepth >= 0 ? options.maxDepth : state.maxDepth;
    if (kt == 0.0f && hasSpecular && wr > 0.0f && depth < maxDepth) {
        const Vec3 R = ray.d - N * (2.0f * dot(N, ray.d));
        const Ray reflected(hit.P + Ng * kRayEpsilon, R, 0.0f, std::numeric_limits<float>::infinity(), depth + 1);
        RT_TRACE("%*sreflect: w=%g ks=(%g, %g, %g)", sub, "", (double)wr, RT_V3(ks));
        result += ks * traceRay(reflected, depth + 1) * wr;
    }

    /* The emission: an emitter's (its bound material emits) on its front
     * face only, as its stand-in shines forward only; anything else (a
     * shader's, later) from both sides. */
    if (!dc.emissive || entering)
        result += Vec3(m.emission);
    return result;
}

/* A sphere of the given radius at the CTM's origin. */
RtVoid rtSphere(RtFloat radius)
{
    if (!(radius > 0)) {
        std::fprintf(stderr, "%s: rtSphere with radius %g, skipped\n", options.scene, radius);
        return;
    }
    /* The draw call: everything copied now, since the CTM, the material and
     * the shaders may change after this call. */
    DrawCall dc;
    dc.objectToWorld = ctm();
    dc.worldToObject = dc.objectToWorld.inverse();
    dc.material = state.material;
    dc.surface = state.surface;
    dc.cutout = state.cutout;
    dc.emissive = state.material.emission[0] > 0 || state.material.emission[1] > 0 ||
                  state.material.emission[2] > 0;
    state.scene.drawCalls.push_back(dc);

    /* The sphere, naming the draw call just added. */
    Sphere sphere;
    sphere.radius = radius;
    sphere.drawCall = (int)state.scene.drawCalls.size() - 1;
    state.scene.spheres.push_back(sphere);
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
    /* The draw call: everything copied now, as in rtSphere. */
    DrawCall dc;
    dc.objectToWorld = ctm();
    dc.worldToObject = dc.objectToWorld.inverse();
    dc.material = state.material;
    dc.surface = state.surface;
    dc.cutout = state.cutout;
    dc.emissive = state.material.emission[0] > 0 || state.material.emission[1] > 0 ||
                  state.material.emission[2] > 0;
    state.scene.drawCalls.push_back(dc);
    const int drawCallIndex = (int)state.scene.drawCalls.size() - 1;

    /* The mesh, baked to world space. */
    const Mat4 &toWorld = dc.objectToWorld;
    const Mat4 normalMatrix = dc.worldToObject.transposed();

    Mesh mesh;
    mesh.drawCall = drawCallIndex;
    mesh.P.resize((size_t)nverts);
    for (int i = 0; i < nverts; ++i)
        mesh.P[(size_t)i] = toWorld.point(Vec3(P[i]));
    if (N) {
        mesh.N.resize((size_t)nverts);
        for (int i = 0; i < nverts; ++i)
            mesh.N[(size_t)i] = normalize(normalMatrix.vector(Vec3(N[i])));
    }
    if (st)
        mesh.st.assign(st, st + 2 * (size_t)nverts);
    if (Cs) {
        mesh.Cs.resize((size_t)nverts);
        for (int i = 0; i < nverts; ++i)
            mesh.Cs[(size_t)i] = Vec3(Cs[i]);
    }

    /* The triangles. */
    const bool mirrored = toWorld.determinant3() < 0;
    const int meshIndex = (int)state.scene.meshes.size();
    mesh.firstTriangle = (int)state.scene.triangles.size();
    int skipped = 0;
    for (int k = 0; k < ntris; ++k) {
        int i0 = indices[3 * k], i1 = indices[3 * k + 1], i2 = indices[3 * k + 2];
        if (i0 < 0 || i0 >= nverts || i1 < 0 || i1 >= nverts || i2 < 0 || i2 >= nverts) {
            ++skipped;
            continue;
        }
        if (mirrored)
            std::swap(i1, i2);

        Triangle tri;
        tri.p0 = mesh.P[(size_t)i0];
        tri.e1 = mesh.P[(size_t)i1] - tri.p0;
        tri.e2 = mesh.P[(size_t)i2] - tri.p0;

        /* The tangents dP/ds and dP/dt from e1 = ds1 dPdu + dt1 dPdv,
         * e2 = ds2 dPdu + dt2 dPdv (world space, as e1 and e2 are); zero
         * without st or when the st are degenerate. */
        tri.dPdu = Vec3(0, 0, 0);
        tri.dPdv = Vec3(0, 0, 0);
        if (mesh.hasTexCoords()) {
            const float ds1 = mesh.st[2 * (size_t)i1] - mesh.st[2 * (size_t)i0];
            const float ds2 = mesh.st[2 * (size_t)i2] - mesh.st[2 * (size_t)i0];
            const float dt1 = mesh.st[2 * (size_t)i1 + 1] - mesh.st[2 * (size_t)i0 + 1];
            const float dt2 = mesh.st[2 * (size_t)i2 + 1] - mesh.st[2 * (size_t)i0 + 1];
            const float det = ds1 * dt2 - dt1 * ds2;
            if (std::fabs(det) > 1e-12f) {
                const float inv = 1.0f / det;
                tri.dPdu = (tri.e1 * dt2 - tri.e2 * dt1) * inv;
                tri.dPdv = (tri.e2 * ds1 - tri.e1 * ds2) * inv;
            }
        }
        tri.v[0] = i0;
        tri.v[1] = i1;
        tri.v[2] = i2;
        tri.mesh = meshIndex;
        state.scene.triangles.push_back(tri);
    }
    mesh.triangleCount = (int)state.scene.triangles.size() - mesh.firstTriangle;
    if (skipped > 0)
        std::fprintf(stderr, "%s: rtTriangleMesh: %d of %d triangles have an index outside [0, %d), skipped\n",
                     options.scene, skipped, (int)ntris, (int)nverts);

    state.scene.meshes.push_back(std::move(mesh));
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
    const Camera camera = makeCamera(Vec3(state.eye), Vec3(state.lookat), Vec3(state.up), state.fovy, w, h);
    const int threads = threadCount();
    if (!options.aov.empty() && !isAov(options.aov)) {
        std::fprintf(stderr, "%s: --aov %s: not an image this renderer writes; it has raydir, normal, ng, depth, uv, albedo, "
                             "cost and shadow\n", options.scene, options.aov.c_str());
        std::exit(1);
    }

    /* Build: the light list gets the emissive draw calls' stand-ins after the
     * scene's own lights (Whitted Illumination handout, section 3).
     * TODO (Workflow, step 14): the BVH over every primitive. */
    addEmitterLights();
    const double build = total.seconds();

    /* TODO: render (Workflow, step 5 on): the pixel loop, one ray through
     * each pixel's center, shaded by the Whitted model. For now, a black
     * image. */
    Timer render;
    std::vector<float> rgb((size_t)w * h * 3, 0.0f);
    RenderBudget budget(options.timeLimit); /* --time-limit: the clock starts here (timer.h) */
    if (options.progress)
        budget.progress((long long)w * h); /* --progress: a line every two seconds */
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {

            traceBegin(x, y);

            const Ray ray = generateCameraRay(camera, x, y, w, h);

            const size_t index = 3 * ((size_t)y * w + x);

            RT_TRACE("pixel (%d, %d): camera ray o=(%g, %g, %g) d=(%g, %g, %g)", x, y, RT_V3(ray.o), RT_V3(ray.d));

            // --aov raydir: the camera ray's direction as a color. The beauty image stays black until shading exists.
            if (options.aov == "raydir") {
                float dir[3];
                ray.d.store(dir);
                aovNormalColor(dir, rgb.data() + index);
            } else if (options.aov == "normal") {
                // The nearest hit among every sphere and triangle.
                Hit hit;
                intersectScene(ray, state.scene, hit);

                // The shading normal as a color; a miss stays black.
                if (hit.hit) {
                    float n[3];
                    hit.N.store(n);
                    aovNormalColor(n, rgb.data() + index);
                }
            } else if (options.aov == "shadow") {
                // The fraction of the lights that reach the nearest hit: in
                // range, above the geometric horizon (Ng . L > 0) and
                // unblocked (lightReach, as the shading decides it). A miss,
                // or a scene without lights, stays black.
                Hit hit;
                if (intersectScene(ray, state.scene, hit)) {
                    const Vec3 Ng = hit.entering ? hit.Ng : -hit.Ng; // the facing rule
                    int unblocked = 0;
                    for (const Light &light : state.scene.lights) {
                        Vec3 E, L;
                        float d;
                        if (lightReach(light, hit.P, Ng, E, L, d) == LightReach::Visible)
                            ++unblocked;
                    }
                    aovShadowColor(unblocked, (int)state.scene.lights.size(), rgb.data() + index);
                }
            } else if (options.aov.empty()) {
                // The beauty image: the radiance of the camera ray.
                const Vec3 L = traceRay(ray, 0);
                L.store(rgb.data() + index);
                RT_TRACE("pixel (%d, %d): value (%g, %g, %g)", x, y, RT_V3(L));
            }

            traceEnd();
        }
    }
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
