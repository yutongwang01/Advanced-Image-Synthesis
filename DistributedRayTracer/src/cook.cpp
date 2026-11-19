/*
 * cook.cpp: the cook integrator, the distributed ray tracer of the "Cook
 * Shading Model" handout. Workflow step 1: it starts as a copy of
 * whitted's shading under its own name, recursing into itself, so that
 * --integrator cook renders what whitted does until the later steps
 * replace it term by term (sampled lights, the glossy lobe, the coat,
 * ambient occlusion, the environment light, transmission).
 */
#include "integrator.h"
#include "debug.h"
#include "lights.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>

/* Two auxiliary functions: 
 * The first one is responsible for Light Sample Stratification, 
 * and the second one is responsible for the visibility detection of Shadow Rays.*/
static Sample2 stratifyLightSample(
    Sample2 u, int k, int count)
{
    int m = static_cast<int>(
        std::sqrt(static_cast<float>(count))
    );

    while (count % m != 0)
        --m;

    const int n = count / m;

    const float upper =
        std::nextafter(1.0f, 0.0f);

    u.u = std::min(
        (static_cast<float>(k % m) + u.u) / m,
        upper
    );

    u.v = std::min(
        (static_cast<float>(k / m) + u.v) / n,
        upper
    );

    return u;
}

static bool lightSampleVisible(
    const Scene &scene,
    const Light &light,
    const Vec3 &P,
    const Vec3 &Ng,
    const LightSample &ls,
    float time)
{
    const Vec3 origin =
        P + Ng * kRayEpsilon;

    Vec3 direction;
    float tMax;

    if (light.type == LightType::Directional) {
        direction = ls.wi;
        tMax = std::numeric_limits<float>::infinity();
    } else {
        // Recalculate direction from offset origin.
        const Vec3 toTarget = ls.target - origin;
        const float distance = length(toTarget);

        if (!(distance > kRayEpsilon))
            return false;

        direction = toTarget / distance;
        tMax = distance - kRayEpsilon;        
    }

    Ray shadow(
        origin,
        direction,
        0.0f,
        tMax,
        -1
    );

    shadow.time = time;

    // No skipped draw call, including the emitter.
    return !occluded(shadow, scene, -1);
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
 * The mirror ray (step 10) and, for a dielectric, the refracted ray with
 * the Fresnel split (step 11) recurse one level deeper. The surface
 * shader runs before the lights; cutouts run inside the intersection. */
Vec3 cookRadiance(const World &world, const Ray &ray, int depth, Worker &worker)
{
    const Scene &scene = *world.scene;
    const int ind = 2 * depth;   /* this depth's lines */
    const int sub = ind + 2;     /* the lines under a hit */

    Hit hit;
    if (!intersectScene(ray, scene, hit)) {
        /* camera, mirror and refracted rays alike: all of them end here */
        const Vec3 env = environmentRadiance(world, ray.d);
        RT_TRACE("%*sdepth %d: miss, environment (%g, %g, %g)", ind, "", depth, RT_V3(env));
        return env;
    }

    const DrawCall &dc = scene.drawCalls[(size_t)hit.drawCall];

    /* Which face the ray met, from Ng before the flip (for the emission and
     * refraction); the shade context carries Ng and N turned toward the
     * ray, together (the facing rule). */
    const bool entering = hit.entering;
    RT_TRACE("%*sdepth %d: hit prim %d t=%g P=(%g, %g, %g) N=(%g, %g, %g) Ng=(%g, %g, %g) %s", ind, "", depth,
             hit.prim, (double)hit.t, RT_V3(hit.P), RT_V3(entering ? hit.N : -hit.N),
             RT_V3(entering ? hit.Ng : -hit.Ng), entering ? "entering" : "leaving");

    /* The shade context, then the surface shader bound at the call, which may
     * rewrite the material (and replace N); the lighting reads what it leaves
     * (Whitted Illumination handout, section 9: intersect, fill the shade
     * context, run the surface shader, then the lights). */
    RtShadeContext ctx;
    fillShadeContext(scene, ray, hit, ctx);
    if (dc.surface.fn) {
        ctx.params = dc.surface.data();
        dc.surface.fn(&ctx);
    }

    Sampler &sampler = *worker.sampler;
    const int numLightSamples =
        std::max(1, world.lightSamples);

    if (world.environment >= 0) {
        const Sample2 envSample = sampler.next2D();
        (void)envSample;
    }

    // The material has already been processed by its
    // surface shader above.
    const RtMaterial &m = ctx.m;

    const Vec3 kd(m.albedo);
    const Vec3 ks(m.specular);
    const float kt = m.transmission;

    const Vec3 ka = world.ambient;
    const Vec3 Ng(ctx.Ng);
    const Vec3 N = normalize(Vec3(ctx.N));

    const bool hasSpecular =
        ks.x > 0.0f ||
        ks.y > 0.0f ||
        ks.z > 0.0f;

    RT_TRACE(
        "%*smaterial: albedo (%g, %g, %g) "
        "specular (%g, %g, %g) roughness %g "
        "transmission %g ior %g "
        "emission (%g, %g, %g)",
        sub, "",
        RT_V3(kd),
        RT_V3(ks),
        (double)m.roughness,
        (double)kt,
        (double)m.ior,
        (double)m.emission[0],
        (double)m.emission[1],
        (double)m.emission[2]
    );

    const float kInvPi =
        1.0f / 3.14159265358979323846f;

    Vec3 diffuse(0.0f);

    // Every light in the scene's original light order.
    for (size_t j = 0; j < scene.lights.size(); ++j) {

        const Light &light = scene.lights[j];

        // An emitter receives N samples.
        // Delta lights only use sample k == 0.
        const bool isEmitter =
            light.type == LightType::Emissive;

        for (int k = 0; k < numLightSamples; ++k) {

            // Always consume a 2D sample, even when
            // the delta light will discard it.
            Sample2 u = sampler.next2D();

            if (!isEmitter && k > 0)
                continue;

            // Stratify only for non-independent samplers.
            if (numLightSamples > 1 &&
                !world.independentSampler) {

                u = stratifyLightSample(
                    u, k, numLightSamples
                );
            }

            LightSample ls;

            if (!sampleLight(
                    scene,
                    light,
                    hit.P,
                    u,
                    ray.time,
                    depth,
                    ls)) {
                continue;
            }

            // Geometric and shading normals must
            // both face the sampled light direction.
            if (dot(Ng, ls.wi) <= 0.0f)
                continue;

            const float NdotL = dot(N, ls.wi);

            if (NdotL <= 0.0f)
                continue;

            // Visibility from the offset shadow origin.
            if (!lightSampleVisible(
                    scene,
                    light,
                    hit.P,
                    Ng,
                    ls,
                    ray.time)) {
                continue;
            }

            // Only non-delta samples divide by PDF.
            if (!ls.delta && !(ls.pdf > 0.0f))
                continue;

            float weight =
                isEmitter
                    ? 1.0f / numLightSamples
                    : 1.0f;

            if (!ls.delta)
                weight /= ls.pdf;

            diffuse +=
                kd * ls.Le *
                (NdotL * kInvPi * weight);

            RT_TRACE(
                "%*slight %zu sample %d: lit "
                "wi=(%g, %g, %g) "
                "Le=(%g, %g, %g) pdf=%g",
                sub, "",
                j, k,
                RT_V3(ls.wi),
                RT_V3(ls.Le),
                (double)ls.pdf
            );
        }
    }

    // Preserve the sampler dimension order:
    // light samples -> lobe -> AO -> recursion.

    const Sample2 lobeSample = sampler.next2D();
    const Sample2 aoSample = sampler.next2D();

    (void)lobeSample;
    (void)aoSample;

    RT_TRACE(
        "%*ssamples: lobe (%g %g) ao (%g %g)",
        sub, "",
        (double)lobeSample.u,
        (double)lobeSample.v,
        (double)aoSample.u,
        (double)aoSample.v
    );

    // Cook's diffuse direct lighting.
    // The Phong highlight from HW2 is removed.
    // Glossy sampling is implemented in Step 12.
    const Vec3 ambientDiffuse = ka * kd + diffuse;

    Vec3 result =
        ambientDiffuse * (1.0f - kt);

    RT_TRACE(
        "%*sdirect: ambient+diffuse (%g, %g, %g) "
        "x (1 - %g)",
        sub, "",
        RT_V3(ambientDiffuse),
        (double)kt
    );

    /* The secondary rays (Whitted Illumination handout, sections 7 to 9),
     * each traced only while the depth is below rtMaxDepth (--max-depth
     * overrides it) and only if its weight times its coefficient is
     * nonzero; one level deeper, tMin = 0, no tMax, the parent's time.
     *
     * Opaque (kt = 0): the mirror ray, weighted by the smoothness
     *     wr = max(0, 1 - roughness^2 / 0.15).
     * Transparent (kt > 0), a dielectric: roughness plays no part.
     *   entering: (eta_i, eta_t) = (1, ior); leaving: (ior, 1); eta = eta_i / eta_t
     *   cos_i = -N . I (N faces the ray), sin^2_t = eta^2 (1 - cos_i^2)
     *   sin^2_t > 1: total internal reflection, the mirror ray alone, wr = 1
     *   else cos_t = sqrt(1 - sin^2_t), T = eta I + (eta cos_i - cos_t) N,
     *     Schlick: F0 = ((eta_t - eta_i) / (eta_t + eta_i))^2,
     *     F = F0 + (1 - F0) (1 - c)^5 with c = cos_i entering, cos_t leaving
     *     (the air side), F = 0 when eta_i = eta_t; wr = F, wt = 1 - F.
     * The mirror ray adds wr ks L(R), R = I - 2 (N . I) N, from P + Ng eps;
     * the refracted ray adds wt kt L(T), from P - Ng eps, the far side. */
    const int maxDepth = world.maxDepth; /* rtMaxDepth, or --max-depth */
    float wr = 0.0f, wt = 0.0f;
    bool tir = false;
    Vec3 T;
    if (kt > 0.0f) {
        const float etaI = entering ? 1.0f : m.ior;
        const float etaT = entering ? m.ior : 1.0f;
        const float eta = etaI / etaT;
        const float cosI = -dot(N, ray.d);
        const float sin2T = eta * eta * (1.0f - cosI * cosI);
        if (sin2T > 1.0f) {
            tir = true; /* only when leaving, beyond the critical angle */
            wr = 1.0f;
        } else {
            const float cosT = std::sqrt(1.0f - sin2T);
            T = ray.d * eta + N * (eta * cosI - cosT);
            float F = 0.0f;
            if (etaI != etaT) {
                const float r0 = (etaT - etaI) / (etaT + etaI);
                const float F0 = r0 * r0;
                const float c = entering ? cosI : cosT;
                const float x = 1.0f - c;
                F = F0 + (1.0f - F0) * x * x * x * x * x;
            }
            wr = F;
            wt = 1.0f - F;
        }
    } else
        wr = std::max(0.0f, 1.0f - m.roughness * m.roughness / 0.15f);

    if (hasSpecular && wr > 0.0f && depth < maxDepth) {
        const Vec3 R = ray.d - N * (2.0f * dot(N, ray.d));
        Ray reflected(hit.P + Ng * kRayEpsilon, R, 0.0f, std::numeric_limits<float>::infinity(), depth + 1);
        reflected.time = ray.time;
        RT_TRACE("%*sreflect: w=%g ks=(%g, %g, %g)", sub, "", (double)wr, RT_V3(ks));
        result += ks * cookRadiance(world, reflected, depth + 1, worker) * wr;
    }

    if (kt > 0.0f) {
        if (tir)
            RT_TRACE("%*srefract: total internal reflection", sub, "");
        else if (wt > 0.0f && depth < maxDepth) {
            Ray refracted(hit.P - Ng * kRayEpsilon, T, 0.0f, std::numeric_limits<float>::infinity(), depth + 1);
            refracted.time = ray.time;
            RT_TRACE("%*srefract: w=%g kt=%g", sub, "", (double)wt, (double)kt);
            result += cookRadiance(world, refracted, depth + 1, worker) * (wt * kt);
        }
    }

    /* The emission: an emitter's (its bound material emits) on its front
     * face only, as its stand-in shines forward only; anything else (a
     * shader's, later) from both sides. */
    if (!dc.emissive || entering)
        result += Vec3(m.emission);
    return result;
}
