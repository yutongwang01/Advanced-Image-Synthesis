/*
 * whitted.cpp: the whitted integrator, HW2's shading (Whitted Illumination
 * handout), moved here from rt.cpp's traceRay unchanged but for where it
 * reads the world: the World of integrator.h instead of rt.cpp's graphics
 * state and the command line. Its images are HW2's.
 */
#include "integrator.h"

#include "debug.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>

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
Vec3 whittedRadiance(const World &world, const Ray &ray, int depth)
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
    const RtMaterial &m = ctx.m;
    const Vec3 kd(m.albedo), ks(m.specular);
    const float kt = m.transmission;
    const Vec3 ka = world.ambient; /* rtAmbient, or --ambient (rtWorldEnd resolved it) */
    const Vec3 Ng(ctx.Ng);
    const Vec3 N = normalize(Vec3(ctx.N)); /* a shader may have replaced it */
    const Vec3 V = -ray.d;
    RT_TRACE("%*smaterial: albedo (%g, %g, %g) specular (%g, %g, %g) roughness %g transmission %g ior %g "
             "emission (%g, %g, %g)", sub, "", RT_V3(kd), RT_V3(ks), (double)m.roughness, (double)kt, (double)m.ior,
             (double)m.emission[0], (double)m.emission[1], (double)m.emission[2]);

    /* The Phong exponent from the roughness, not the roughness itself. */
    const float alpha = std::min(1.0f, std::max(0.05f, m.roughness * m.roughness));
    const float n = std::max(1.0f, 2.0f / (alpha * alpha) - 2.0f);
    const bool hasSpecular = ks.x > 0 || ks.y > 0 || ks.z > 0;

    const float kInvPi = 1.0f / 3.14159265358979323846f;
    Vec3 diffuse(0.0f), specular(0.0f);
    for (size_t j = 0; j < scene.lights.size(); ++j) {
        const Light &light = scene.lights[j];
        Vec3 E, L;
        float d = 0;
        const LightReach reach = lightReach(scene, light, hit.P, Ng, E, L, d);
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
                intersectScene(shadowRay(light, hit.P, Ng, L, d), scene, b, shadowSkip(light));
                const Vec3 ba = b.drawCall >= 0 ? Vec3(scene.drawCalls[(size_t)b.drawCall].material.albedo) : Vec3(0.0f);
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
        result += ks * whittedRadiance(world, reflected, depth + 1) * wr;
    }

    if (kt > 0.0f) {
        if (tir)
            RT_TRACE("%*srefract: total internal reflection", sub, "");
        else if (wt > 0.0f && depth < maxDepth) {
            Ray refracted(hit.P - Ng * kRayEpsilon, T, 0.0f, std::numeric_limits<float>::infinity(), depth + 1);
            refracted.time = ray.time;
            RT_TRACE("%*srefract: w=%g kt=%g", sub, "", (double)wt, (double)kt);
            result += whittedRadiance(world, refracted, depth + 1) * (wt * kt);
        }
    }

    /* The emission: an emitter's (its bound material emits) on its front
     * face only, as its stand-in shines forward only; anything else (a
     * shader's, later) from both sides. */
    if (!dc.emissive || entering)
        result += Vec3(m.emission);
    return result;
}
