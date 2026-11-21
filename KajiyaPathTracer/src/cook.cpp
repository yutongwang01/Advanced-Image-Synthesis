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
#include "ggx_albedo.h"
#include "warp.h"

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

/* The glossy lobe's record and the light rule (Cook Shading Model,
 * Sections 5 and 6), as the reference has them ---------------------------- */

/* What a lobe's ray carries: the latest lobe it left (its point and
 * half-width cosine), the widest lobe before it (cosHalfW = 2: none), and
 * whether a mirror or refracted ray has been traced since the latest lobe.
 * A smaller cosine is a wider lobe. */
struct LobeOrigin {
    Vec3 P; float cosHalf;   // the latest lobe: where the ray left, half-width
    Vec3 Pw; float cosHalfW; // the widest lobe before it (cosHalfW = 2: none)
    bool bounced;            // a mirror or refracted ray since the latest lobe
};

// whether the light is narrower than a lobe of this half-width at P (light rule)
static bool lobeSamplesLight(const Light &light, const Vec3 &P, float cosHalf)
{
    return lightCosAngularRadius(light, P) >= cosHalf;
}

// the caustic weight: 1 for a light as wide as the lobe, 0 under half as wide
static float causticWeight(const Light &light, const Vec3 &P, float cosHalf)
{
    const float lobe = std::acos(std::min(1.0f, cosHalf));
    if (lobe <= 0) {
        return 1; // a mirror-sharp lobe finds every image
    }
    const float radius = std::acos(std::min(1.0f, lightCosAngularRadius(light, P)));
    const float t = std::min(1.0f, std::max(0.0f, 2 * radius / lobe - 1));
    return t * t * (3 - 2 * t);
}

static LobeOrigin lobeOrigin(const LobeOrigin *before, const Vec3 &P, float cosHalf)
{
    LobeOrigin o = {P, cosHalf, P, 2.0f, false};
    if (before) {
        o.Pw = before->Pw;
        o.cosHalfW = before->cosHalfW;
        if (before->cosHalf < o.cosHalfW) { // the old latest lobe was the widest
            o.Pw = before->P;
            o.cosHalfW = before->cosHalf;
        }
    }
    return o;
}

// at the surface the ray reached: the weight of this light's lobe term
static float causticFactor(const Light &light, const LobeOrigin &o)
{
    Vec3 Pw = o.Pw;
    float cosHalfW = o.cosHalfW;
    if (o.cosHalf < cosHalfW) { // the widest, the latest included
        Pw = o.P;
        cosHalfW = o.cosHalf;
    }
    return cosHalfW <= 1 ? causticWeight(light, Pw, cosHalfW) : 1.0f;
}

// at an emitter the ray hits: the weight of its emission
static float emissionWeight(const Light &light, const LobeOrigin &o)
{
    if (!o.bounced && lobeSamplesLight(light, o.P, o.cosHalf)) {
        return 0; // the latest lobe sampled it
    }
    Vec3 Pw = o.Pw;
    float cosHalfW = o.cosHalfW;
    if (o.bounced && o.cosHalf < cosHalfW) { // after a bounce, the latest too
        Pw = o.P;
        cosHalfW = o.cosHalf;
    }
    return cosHalfW <= 1 ? causticWeight(light, Pw, cosHalfW) : 1.0f;
}

/* The light an emissive draw call became at rtWorldEnd, or nullptr. */
static const Light *lightOfDrawCall(const Scene &scene, int drawCall)
{
    for (const Light &light : scene.lights)
        if (light.type == LightType::Emissive && light.drawCall == drawCall)
            return &light;
    return nullptr;
}

/* Schlick's Fresnel per channel: ks head on, 1 at grazing. */
static Vec3 schlick(const Vec3 &ks, float cosTheta)
{
    const float x = 1.0f - std::min(1.0f, std::max(0.0f, cosTheta));
    const float x5 = x * x * x * x * x;
    return ks + (Vec3(1.0f) - ks) * x5;
}

/* The environment light (Area Lights handout, Section 11) ---------------- */

static const double kEnvQ = 1.0 / 64; /* the share sampled uniformly over the map's (s, t) */

/* The largest i in [0, n - 1] with cdf[i] <= xi, and the offset of xi
 * inside [cdf[i], cdf[i + 1]) (0.5 for an interval of zero width), kept
 * in [0, 1). cdf has n + 1 entries, 0 to 1. */
static int findInterval(const double *cdf, int n, double xi, double &offset)
{
    int i = (int)(std::upper_bound(cdf, cdf + n + 1, xi) - cdf) - 1;
    i = std::min(std::max(i, 0), n - 1);
    const double width = cdf[i + 1] - cdf[i];
    offset = width > 0 ? (xi - cdf[i]) / width : 0.5;
    offset = std::min(std::max(offset, 0.0), std::nextafter(1.0, 0.0));
    return i;
}

/* The density per solid angle with which sampleEnvironment chooses a
 * direction: the direction into the map's frame (turned by -rotateY), its
 * (s, t) and sin(theta) in double from atan2 (not acos, whose float input
 * rounds to +-1 near the poles), its texel's probability read from the
 * CDF intervals, mixed with the uniform 1/64, over 2 pi^2 sin(theta). */
static float environmentPdf(const World &world, const Vec3 &direction)
{
    const EnvironmentDistribution &dist = world.environmentDist;
    const int w = dist.width, h = dist.height;
    if (w <= 0 || h <= 0)
        return 0.0f;
    const double pi = 3.14159265358979323846;
    const Vec3 m = Mat4::rotate(-world.environmentRotateY, 0, 1, 0).vector(direction);
    const double mx = m.x, my = m.y, mz = m.z;
    double s = std::atan2(mx, mz) / (2 * pi);
    if (s < 0)
        s += 1;
    const double rho = std::sqrt(mx * mx + mz * mz);
    const double t = std::atan2(rho, my) / pi;
    const double sinTheta = rho / std::sqrt(rho * rho + my * my);
    if (!(sinTheta > 0))
        return 0.0f;
    const int x = std::min((int)std::floor(s * w), w - 1);
    const int y = std::min((int)std::floor(t * h), h - 1);
    const double *col = dist.columnCDF.data() + size_t(y) * (size_t(w) + 1);
    const double pcdf = double(w) * h * (dist.rowCDF[size_t(y) + 1] - dist.rowCDF[size_t(y)]) * (col[x + 1] - col[x]);
    return (float)((kEnvQ + (1 - kEnvQ) * pcdf) / (2 * pi * pi * sinTheta));
}

/* One direction from the map's distribution (handout 11.3): with
 * probability 1/64 a point uniform over (s, t), else by the running sums,
 * the row from u.u and the column from u.v, the same two numbers in both
 * branches. The direction is computed in double and stored as floats, unit
 * by construction (not normalized again), then turned by +rotateY. */
static bool sampleEnvironment(const World &world, Sample2 u, Vec3 &wi, float &pdf)
{
    const EnvironmentDistribution &dist = world.environmentDist;
    const int w = dist.width, h = dist.height;
    if (w <= 0 || h <= 0)
        return false;
    const double pi = 3.14159265358979323846;
    double s, t;
    if (u.u < kEnvQ) {
        s = u.v;
        t = u.u / kEnvQ;
    } else {
        double dy, dx;
        const int y = findInterval(dist.rowCDF.data(), h, (u.u - kEnvQ) / (1 - kEnvQ), dy);
        const int x = findInterval(dist.columnCDF.data() + size_t(y) * (size_t(w) + 1), w, u.v, dx);
        s = (x + dx) / w;
        t = (y + dy) / h;
    }
    const double theta = pi * t, phi = 2 * pi * s, sinTheta = std::sin(theta);
    const Vec3 m((float)(sinTheta * std::sin(phi)), (float)std::cos(theta), (float)(sinTheta * std::cos(phi)));
    wi = Mat4::rotate(world.environmentRotateY, 0, 1, 0).vector(m);
    pdf = environmentPdf(world, wi);
    return pdf > 0.0f;
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
static Vec3 cookTrace(const World &world, const Ray &ray, int depth, Worker &worker, const LobeOrigin *origin)
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

    /* The Cook model's pieces (Cook Shading Model, Sections 4 and 8). V back
     * along the ray, R the mirror direction of V about N. Opaque: kt = 0.
     * A roughness of exactly 0 is a mirror; above 0 the normalized Phong
     * lobe about R of exponent n = max(1, 2/alpha^2 - 2), alpha =
     * roughness^2 (no lower clamp), half-width cos(theta_h) = 2^(-1/n). */
    const Vec3 V = -ray.d;
    const float NdotV = dot(N, V);
    const Vec3 R = normalize(N * (2.0f * NdotV) - V);
    const bool opaque = !(kt > 0.0f);
    const float roughness = m.roughness;
    const bool smooth = !(roughness > 0.0f);
    const float alpha = roughness * roughness;
    const float lobeN = smooth ? 0.0f : std::max(1.0f, 2.0f / (alpha * alpha) - 2.0f);
    const float cosHalf = smooth ? 1.0f : std::pow(2.0f, -1.0f / lobeN);
    const bool glossy = opaque && hasSpecular && !smooth; /* a lobe ray and the lobe terms */

    /* A dielectric's Fresnel split (kt > 0; Section 8), HW2's rules, worked
     * out before the lights since a rough dielectric's reflected lobe takes
     * the lobe terms with wr ks in place of F (Section 5):
     *   entering: (eta_i, eta_t) = (1, ior); leaving: (ior, 1); eta = eta_i / eta_t
     *   cos_i = N . V, sin^2_t = eta^2 (1 - cos_i^2) > 1: total internal
     *   reflection (wr = 1, no refracted ray); else T = eta I + (eta cos_i -
     *   cos_t) N, Schlick's F with F0 = ((eta_t - eta_i)/(eta_t + eta_i))^2
     *   at cos_i entering and cos_t leaving (the air side): wr = F, wt = 1 - F. */
    float wr = 0.0f, wt = 0.0f, etaI = 1.0f, etaT = 1.0f;
    bool tir = false;
    Vec3 T;
    if (!opaque) {
        etaI = entering ? 1.0f : m.ior;
        etaT = entering ? m.ior : 1.0f;
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
    }
    /* A rough dielectric (frosted glass) sends both rays through the lobe;
     * its reflected lobe takes the lobe terms of the lights narrower than it. */
    const bool frostedReflect = !opaque && !smooth && hasSpecular && wr > 0.0f;
    const bool lobeTerms = glossy || frostedReflect;

    /* The pigment under an opaque coat (Section 4): kd (1 - metallic) times
     * (1 - E(mu_o)) (1 - E(mu_i)) / (1 - Ebar) per channel, E the coat's
     * directional albedo: Schlick's F itself for a mirror (roughness 0, or
     * roughness^2 below 0.001), else the given GGX table at the material's
     * roughness. No coat (ks = 0): the factor is 1. A channel whose
     * 1 - Ebar is 0 passes nothing. The ambient uses 1 - E(mu_o). */
    const Vec3 K = kd * (1.0f - m.metallic);
    const bool coat = opaque && hasSpecular;
    const bool mirrorCoat = smooth || alpha < 0.001f;
    auto coatE = [&](float mu) {
        mu = std::min(1.0f, std::max(0.0f, mu));
        if (mirrorCoat)
            return schlick(ks, mu);
        return Vec3(ggxAlbedo(roughness, mu, ks.x), ggxAlbedo(roughness, mu, ks.y), ggxAlbedo(roughness, mu, ks.z));
    };
    Vec3 coatOut(1.0f), coatScale(1.0f); /* 1 - E(mu_o), and that over 1 - Ebar */
    if (coat) {
        const Vec3 Ebar = mirrorCoat ? ks + (Vec3(1.0f) - ks) * (1.0f / 21.0f)
                                     : Vec3(ggxAlbedoAverage(roughness, ks.x), ggxAlbedoAverage(roughness, ks.y),
                                            ggxAlbedoAverage(roughness, ks.z));
        coatOut = Vec3(1.0f) - coatE(NdotV);
        for (int c = 0; c < 3; ++c) {
            const float denom = 1.0f - Ebar[c];
            coatScale[c] = denom > 0.0f ? coatOut[c] / denom : 0.0f;
        }
    }
    /* A partly transparent material's pigment (0 < kt < 1) sits under its
     * own glass surface, its coat: E(mu) = R0 + (1 - R0)(1 - c)^5 with
     * R0 = ((ior - 1)/(ior + 1))^2, c = mu from outside and the refracted
     * cosine from inside (Schlick on the air side), E = 1 under total
     * internal reflection; Ebar = R0 + (1 - R0)/21 outside and
     * 1 - (20/21)(1 - R0)/ior^2 inside. Clear glass (kt = 1) has no pigment
     * share, so nothing changes for it. */
    float dielectricOut = 1.0f, dielectricScale = 1.0f;
    const float ior = m.ior;
    const float R0 = ((ior - 1.0f) / (ior + 1.0f)) * ((ior - 1.0f) / (ior + 1.0f));
    auto dielectricE = [&](float mu) {
        mu = std::min(1.0f, std::max(0.0f, mu));
        float c = mu;
        if (!entering) {
            const float sin2T = ior * ior * (1.0f - mu * mu);
            if (sin2T > 1.0f)
                return 1.0f; /* total internal reflection keeps it all */
            c = std::sqrt(1.0f - sin2T);
        }
        const float x = 1.0f - c;
        return R0 + (1.0f - R0) * x * x * x * x * x;
    };
    if (!opaque) {
        const float Ebar = entering ? R0 + (1.0f - R0) / 21.0f
                                    : 1.0f - (20.0f / 21.0f) * (1.0f - R0) / (ior * ior);
        dielectricOut = 1.0f - dielectricE(NdotV);
        dielectricScale = (1.0f - Ebar) > 0.0f ? dielectricOut / (1.0f - Ebar) : 0.0f;
        coatOut = Vec3(dielectricOut);
    }

    /* The pigment's factor for light arriving at the cosine mu_i. */
    auto pigment = [&](float muI) {
        if (!opaque)
            return K * (dielectricScale * (1.0f - dielectricE(muI)));
        return coat ? K * coatScale * (Vec3(1.0f) - coatE(muI)) : K;
    };

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
    Vec3 specular(0.0f); /* the lobe terms of the lights narrower than the lobe (Section 5) */

    // The environment light first, N samples (the dimension order): each
    // a shadow ray to infinity, Le the map's radiance, weight 1/(N pdf).
    if (world.environment >= 0 && world.environmentDist.totalWeight > 0.0) {
        for (int k = 0; k < numLightSamples; ++k) {
            Sample2 u = sampler.next2D();
            if (numLightSamples > 1 && !world.independentSampler)
                u = stratifyLightSample(u, k, numLightSamples);
            Vec3 wi;
            float pdf = 0.0f;
            if (!sampleEnvironment(world, u, wi, pdf))
                continue;
            if (!(pdf > 0.0f))
                continue;
            if (dot(Ng, wi) <= 0.0f)
                continue;
            const float NdotL = dot(N, wi);
            if (NdotL <= 0.0f)
                continue;
            Ray shadow(hit.P + Ng * kRayEpsilon, wi, 0.0f, std::numeric_limits<float>::infinity(), -1);
            shadow.time = ray.time;
            if (occluded(shadow, scene, -1))
                continue;
            const Vec3 Le = environmentRadiance(world, wi);
            const float weight = 1.0f / (float(numLightSamples) * pdf);
            diffuse += pigment(NdotL) * Le * (NdotL * kInvPi * weight);
        }
    }

    // Every light in the scene's original light order.
    for (size_t j = 0; j < scene.lights.size(); ++j) {

        const Light &light = scene.lights[j];

        // An emitter receives N samples.
        // Delta lights only use sample k == 0.
        const bool isEmitter =
            light.type == LightType::Emissive;

        /* Section 5: a light narrower than this surface's lobe is the lobe
         * term's (the lobe's ray will ignore its emission); its weight at a
         * surface reached through earlier lobes is the caustic factor. */
        const bool lobeTerm = lobeTerms && lobeSamplesLight(light, hit.P, cosHalf);
        const float caustic = lobeTerm && origin ? causticFactor(light, *origin) : 1.0f;

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
                pigment(NdotL) * ls.Le *
                (NdotL * kInvPi * weight);

            /* F Le D(R . w) / p (times 1/N for an emitter; a delta light's
             * E D(R . L)), F at the half vector of V and w; not dimmed by
             * (1 - kt). */
            if (lobeTerm) {
                const Vec3 F = opaque ? schlick(ks, dot(V, normalize(V + ls.wi))) : ks * wr;
                const float D = phongLobePdf(dot(R, ls.wi), lobeN);
                specular += F * ls.Le * (D * weight * caustic);
            }

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
    /* Ambient occlusion (Cook Shading Model, Section 3): with
     * rtAmbientOcclusion(d), d > 0, the ambient term is multiplied by the
     * visibility of one direction, drawn from this hit's AO sample by the
     * cosine-weighted hemisphere warp about N (the reference's frame), and
     * traced from P + Ng eps to d at depth -1 (cutouts see a shadow ray) as
     * an any-hit test, whatever its direction (no test against Ng), emitters
     * occluding like anything else: 0 if anything is within d, 1 if not. The
     * ray is traced only when the ambient term is nonzero; the sample was
     * drawn regardless. The direct light is left as it is. */
    const Vec3 ambient = ka * K * coatOut * (1.0f - kt);
    float aoVisibility = 1.0f;
    if (world.aoDistance > 0.0f && (ambient.x > 0.0f || ambient.y > 0.0f || ambient.z > 0.0f)) {
        Vec3 t, b;
        orthonormalBasis(N, t, b);
        const Vec3 local = sampleCosineHemisphere(aoSample);
        const Vec3 wi = normalize(t * local.x + b * local.y + N * local.z);
        Ray aoRay(hit.P + Ng * kRayEpsilon, wi, 0.0f, world.aoDistance, -1);
        aoRay.time = ray.time;
        if (occluded(aoRay, scene, -1))
            aoVisibility = 0.0f;
        RT_TRACE("%*sao: wi=(%g, %g, %g) %s", sub, "", RT_V3(wi), aoVisibility > 0 ? "open" : "occluded");
    }

    const Vec3 ambientDiffuse = ka * K * coatOut * aoVisibility + diffuse;

    Vec3 result =
        ambientDiffuse * (1.0f - kt) + specular;

    RT_TRACE(
        "%*sdirect: ambient+diffuse (%g, %g, %g) "
        "x (1 - %g)",
        sub, "",
        RT_V3(ambientDiffuse),
        (double)kt
    );

    /* The secondary rays (Cook Shading Model, Section 8), only while the
     * depth is below rtMaxDepth, one level deeper, the parent's time:
     *   a dielectric (kt > 0): HW2's Fresnel pair, wr ks L(R) + wt kt L(T)
     *     (step 13 adds the body tint, the (eta_i/eta_t)^2 and frosted glass);
     *   an opaque mirror (ks > 0, roughness 0): one mirror ray weighted by
     *     Schlick's F at N . V, no smoothness fade, no highlight;
     *   an opaque glossy surface (ks > 0, roughness > 0): one ray from the
     *     Phong lobe about R (this hit's lobe sample, in the frame about R),
     *     weighted by F at the half vector of V and its direction alone, not
     *     traced below the surface (N . w <= 0).
     * A mirror or refracted ray carries the path's record on with bounced
     * set (none if the path has not left a lobe); a lobe ray carries a new
     * record naming this surface as the latest lobe. */
    const int maxDepth = world.maxDepth; /* rtMaxDepth, or --max-depth */
    LobeOrigin bouncedRecord;
    const LobeOrigin *bounced = nullptr;
    if (origin) {
        bouncedRecord = *origin;
        bouncedRecord.bounced = true;
        bounced = &bouncedRecord;
    }

    if (kt > 0.0f) {
        /* A dielectric (Section 8): wr ks L(reflected) + wt kt k~d (eta_i /
         * eta_t)^2 L(refracted), the reflection first. k~d = 1 - (max kd - kd)
         * per channel, the body tint at each interface the light crosses;
         * (eta_i/eta_t)^2 the radiance's change across the interface.
         *   clear (roughness 0): the mirror ray R and the refracted ray T,
         *     both carrying the path's record with bounced set;
         *   frosted (roughness > 0): both from the Phong lobe of the
         *     roughness and this hit's one lobe sample, the reflected about R
         *     (a new record naming this surface, not traced below the
         *     surface) and the refracted about T (the path's record with
         *     bounced set, so it counts every emission it hits; not traced if
         *     it came out above the surface). */
        const Vec3 tint = Vec3(1.0f) - (Vec3(maxComponent(kd)) - kd);
        const float etaRatio = etaI / etaT;
        const Vec3 transmit = tint * (wt * kt * etaRatio * etaRatio);
        const Vec3 Rd = ray.d - N * (2.0f * dot(N, ray.d));
        if (smooth) {
            if (hasSpecular && wr > 0.0f && depth < maxDepth) {
                Ray reflected(hit.P + Ng * kRayEpsilon, Rd, 0.0f, std::numeric_limits<float>::infinity(), depth + 1);
                reflected.time = ray.time;
                RT_TRACE("%*sreflect: w=%g ks=(%g, %g, %g)", sub, "", (double)wr, RT_V3(ks));
                result += ks * cookTrace(world, reflected, depth + 1, worker, bounced) * wr;
            }
            if (tir)
                RT_TRACE("%*srefract: total internal reflection", sub, "");
            else if (wt > 0.0f && depth < maxDepth) {
                Ray refracted(hit.P - Ng * kRayEpsilon, T, 0.0f, std::numeric_limits<float>::infinity(), depth + 1);
                refracted.time = ray.time;
                RT_TRACE("%*srefract: w=%g kt=%g", sub, "", (double)wt, (double)kt);
                result += transmit * cookTrace(world, refracted, depth + 1, worker, bounced);
            }
        } else {
            const Vec3 local = samplePhongLobe(lobeSample, lobeN);
            if (frostedReflect && depth < maxDepth) {
                Vec3 t, b;
                orthonormalBasis(R, t, b);
                const Vec3 w = normalize(t * local.x + b * local.y + R * local.z);
                if (dot(N, w) > 0.0f) {
                    const LobeOrigin record = lobeOrigin(origin, hit.P, cosHalf);
                    Ray reflected(hit.P + Ng * kRayEpsilon, w, 0.0f, std::numeric_limits<float>::infinity(), depth + 1);
                    reflected.time = ray.time;
                    RT_TRACE("%*sfrosted reflect: w=%g n=%g", sub, "", (double)wr, (double)lobeN);
                    result += ks * cookTrace(world, reflected, depth + 1, worker, &record) * wr;
                }
            }
            if (tir)
                RT_TRACE("%*srefract: total internal reflection", sub, "");
            else if (wt > 0.0f && depth < maxDepth) {
                const Vec3 Tn = normalize(T);
                Vec3 t, b;
                orthonormalBasis(Tn, t, b);
                const Vec3 w = normalize(t * local.x + b * local.y + Tn * local.z);
                if (dot(N, w) < 0.0f) {
                    Ray refracted(hit.P - Ng * kRayEpsilon, w, 0.0f, std::numeric_limits<float>::infinity(), depth + 1);
                    refracted.time = ray.time;
                    RT_TRACE("%*sfrosted refract: w=%g kt=%g n=%g", sub, "", (double)wt, (double)kt, (double)lobeN);
                    result += transmit * cookTrace(world, refracted, depth + 1, worker, bounced);
                }
            }
        }
    } else if (hasSpecular && smooth) {
        if (depth < maxDepth) {
            const Vec3 F = schlick(ks, NdotV);
            const Vec3 Rd = ray.d - N * (2.0f * dot(N, ray.d));
            Ray reflected(hit.P + Ng * kRayEpsilon, Rd, 0.0f, std::numeric_limits<float>::infinity(), depth + 1);
            reflected.time = ray.time;
            RT_TRACE("%*smirror: F=(%g, %g, %g)", sub, "", RT_V3(F));
            result += F * cookTrace(world, reflected, depth + 1, worker, bounced);
        }
    } else if (glossy) {
        Vec3 t, b;
        orthonormalBasis(R, t, b);
        const Vec3 local = samplePhongLobe(lobeSample, lobeN);
        const Vec3 w = normalize(t * local.x + b * local.y + R * local.z);
        if (dot(N, w) > 0.0f && depth < maxDepth) {
            const Vec3 F = schlick(ks, dot(V, normalize(V + w)));
            const LobeOrigin record = lobeOrigin(origin, hit.P, cosHalf);
            Ray lobeRay(hit.P + Ng * kRayEpsilon, w, 0.0f, std::numeric_limits<float>::infinity(), depth + 1);
            lobeRay.time = ray.time;
            RT_TRACE("%*slobe: n=%g w=(%g, %g, %g) F=(%g, %g, %g)", sub, "", (double)lobeN, RT_V3(w), RT_V3(F));
            result += F * cookTrace(world, lobeRay, depth + 1, worker, &record);
        }
    }

    /* The emission: an emitter's (its bound material emits) on its front
     * face only, as its stand-in shines forward only; anything else (a
     * shader's, later) from both sides. */
    if (!dc.emissive || entering) {
        float w = 1.0f;
        if (origin && dc.emissive)
            if (const Light *light = lightOfDrawCall(scene, hit.drawCall))
                w = emissionWeight(*light, *origin);
        result += Vec3(m.emission) * w;
    }
    return result;
}

/* The cook integrator: the camera ray carries no lobe record. */
Vec3 cookRadiance(const World &world, const Ray &ray, int depth, Worker &worker)
{
    return cookTrace(world, ray, depth, worker, nullptr);
}
