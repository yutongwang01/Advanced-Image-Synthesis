/*
 * integrator.h: the interface the integrators share (HW3, workflow step 1).
 *
 * An integrator is a shading function: the radiance a ray brings back,
 * computed one level of recursion at a time. whitted (whitted.cpp) is
 * HW2's; cook (cook.cpp) starts as a copy of it and becomes the
 * distributed ray tracer of the "Cook Shading Model" handout. rtWorldEnd
 * (rt.cpp) picks one by --integrator through integratorFor() and calls it
 * through a ShadingFn for every camera ray.
 *
 * What a shading function may read is the World below, filled once at
 * rtWorldEnd, and the shared functions declared after it (defined in
 * rt.cpp): it never reaches into rt.cpp's graphics state or the command
 * line itself.
 */
#ifndef INTEGRATOR_H
#define INTEGRATOR_H

#include "rt.h"
#include "scene.h"
#include "vec.h"
#include "sampler.h"

#include <memory>
#include <string>

/* The world as the integrators see it: the scene data structure (scene.h)
 * and the settings of the shading that rtWorldEnd has already resolved
 * against the command line, so an integrator never reads `options`. */
struct World {
    const Scene *scene = nullptr; /* draw calls, primitives, lights, BVH */
    Vec3 ambient;                 /* ka: rtAmbient, or --ambient */
    Vec3 background;              /* rtBackground: an escaped ray's radiance when there is no map */
    int environment = -1;         /* rtEnvironment's texture handle; -1: none, the background shows */
    float environmentScale = 1;   /* rtEnvironment's scale */
    float environmentRotateY = 0; /* rtEnvironment's rotation about +y, degrees */
    int maxDepth = 8;             /* rtMaxDepth, or --max-depth: a ray at depth d recurses only if d < maxDepth */
    int lightSamples = 1;         /* --light-samples N: cook's samples of every light (and the environment) per hit */
    bool independentSampler = false;
};

/* What one rendering thread owns (Suggested Workflow, step 2): its own
 * sampler, made once per thread by rtWorldEnd's pixel loop and positioned
 * with startPixelSample before every pixel sample. A sampler's values
 * depend only on the pixel, the sample, the dimension and the seed, so
 * which thread renders a pixel never changes the image. */
struct Worker {
    std::unique_ptr<Sampler> sampler;
};

/* A shading function: the radiance the ray returns, `depth` its level of
 * recursion (0 for a camera ray), `worker` the calling thread's, whose
 * sampler the integrator draws its sample dimensions from. whitted never
 * draws from it; cook does from step 3 on. */
using ShadingFn = Vec3 (*)(const World &world, const Ray &ray, int depth, Worker &worker);

/* The integrators. */
Vec3 whittedRadiance(const World &world, const Ray &ray, int depth, Worker &worker); /* whitted.cpp */
Vec3 cookRadiance(const World &world, const Ray &ray, int depth, Worker &worker);    /* cook.cpp */

/* The shading function of an integrator's name ("whitted", "cook"), or
 * nullptr for a name this renderer has none of. rt.cpp. */
ShadingFn integratorFor(const std::string &name);

/* Shared by the integrators, defined in rt.cpp --------------------------- */

/* The offset that keeps a secondary ray off the surface it starts on: the
 * ray starts at P + Ng * kRayEpsilon (Whitted Illumination handout, section
 * 4; Intersection handout, section 5). */
constexpr float kRayEpsilon = 1e-4f;

/* The nearest hit of the ray in the scene (the BVH unless --no-accel);
 * skipDrawCall names a draw call whose primitives are ignored, -1 none. */
bool intersectScene(const Ray &ray, const Scene &scene, Hit &hit, int skipDrawCall = -1);

bool occluded(const Ray &ray, const Scene &scene, int skipDrawCall = -1);

/* The shade context at a hit (Intersection handout, Table 1): P, Po, the
 * normals turned toward the ray, u v, the tangents, I, time, depth and the
 * draw call's material; params left NULL for the caller. */
void fillShadeContext(const Scene &scene, const Ray &ray, const Hit &hit, RtShadeContext &c);

/* What a ray that leaves the scene returns: the environment map's
 * radiance in its direction, or the background. */
Vec3 environmentRadiance(const World &world, const Vec3 &dir);

/* Whether light j reaches P, in the handout's order: its range, the
 * geometric horizon, then the shadow ray. Fills the irradiance E, the
 * unit direction L toward the light and the distance d. */
enum class LightReach { OutOfReach, Behind, Shadowed, Visible };
LightReach lightReach(const Scene &scene, const Light &light, const Vec3 &P, const Vec3 &Ng, Vec3 &E, Vec3 &L,
                      float &d);

/* The shadow ray toward a light, and the draw call it skips (a stand-in's
 * own emitter; -1 for the other lights). */
Ray shadowRay(const Light &light, const Vec3 &P, const Vec3 &Ng, const Vec3 &L, float d);
int shadowSkip(const Light &light);

/* A vector's three components as doubles, for RT_TRACE's printf formats. */
#define RT_V3(v) (double)(v).x, (double)(v).y, (double)(v).z

#endif /* INTEGRATOR_H */
