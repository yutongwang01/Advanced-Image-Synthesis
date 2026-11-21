/*
 * bsdf.h: the BSDF of kajiya (HW4 handout), one per shading point from
 * the material the surface shader leaves. The public part is fixed: the
 * given bsdftest.cpp, warptest.cpp and roughdielectric.cpp call it by
 * these names. The private part is yours. Opaque (transmission 0): one
 * GGX lobe with Schlick's Fresnel, F0 = specular (the reflectance head on
 * under every integrator), over a pigment albedo (1 - metallic) / pi that
 * sees the coat's factor (the given ggx_albedo.h, as under cook: what the
 * coat lets through to it and back), so that coat and pigment together
 * never return more than 1; metallic only removes the pigment, and the
 * ior is for refraction alone. GGX alpha = roughness^2; below MIRROR_ALPHA
 * the lobe is a mirror (a delta). Transmissive (transmission kt > 0): a
 * mixture of two materials over one smooth interface of index ior,
 * (1 - kt) of a coated pigment and kt of a dielectric: the interface's
 * reflection, a delta with the exact Fresnel term tinted by specular (1 on
 * real glass; a lower value takes a reflection away); its refraction, a
 * delta with 1 - F scaled by (eta_i / eta_t)^2 and tinted by the body
 * color, weighted kt; and the pigment weighted 1 - kt under the
 * interface's own factor. kt = 1 is plain glass, kt = 0 the opaque case.
 * A transmissive material with roughness above 0 (alpha at or above
 * MIRROR_ALPHA) is frosted: its interface is a GGX microfacet dielectric
 * (Walter et al. 2007), given complete in roughdielectric.h, which the
 * Bsdf hands such a material to whole (RoughDielectric::applies, then its
 * eval, pdf and sample). Directions are world space, pointing away from
 * the surface; f is without the cosine; the local frame is the
 * orthonormal basis about N (z along N), which no model here needs a
 * tangent for. Sidedness: wo and wi on the same side of N but opposite
 * sides of Ng, or the reverse, give nothing.
 */
#ifndef BSDF_H
#define BSDF_H

#include "roughdielectric.h"
#include "rt.h"
#include "sampler.h"
#include "vec.h"

/* Below this alpha (roughness^2) a lobe is a mirror, a delta (HW4 handout). */
const float MIRROR_ALPHA = 0.001f;

/* One sampled direction. For a delta lobe pdf is the probability of
 * choosing it and f is set so that f |cos(theta_i)| / pdf is the
 * throughput; otherwise both are densities per steradian. */
struct BsdfSample {
    Vec3 wi;
    Vec3 f;
    float pdf;
    bool delta;
};

class Bsdf {
public:
    /* The BSDF at a hit: the material, the shading and geometric normals
     * (Ng facing the incoming ray, N on its side), dPdu (not used: every
     * model here is isotropic, and the frame is your basis about N), and
     * whether the ray arrived at the front of the surface (for a
     * dielectric's sides). */
    Bsdf(const RtMaterial &m, const Vec3 &N, const Vec3 &Ng, const Vec3 &dPdu, bool entering);

    /* The finite part of f (the pigment and the rough lobe); the deltas
     * are sample()'s alone. */
    Vec3 eval(const Vec3 &wo, const Vec3 &wi) const;
    /* The density per steradian with which sample() draws a finite-density
     * direction wi (the deltas' probabilities are not in it). */
    float pdf(const Vec3 &wo, const Vec3 &wi) const;
    /* u1 picks the lobe, u2 the direction in it. False when no direction
     * comes out (wo below the surface, a sample below the horizon). */
    bool sample(const Vec3 &wo, float u1, Sample2 u2, BsdfSample &bs) const;

    /* Whether any lobe is not a delta, so that next-event estimation can
     * reach it (delta BSDFs skip NEE; HW4 handout). */
    bool hasNonDelta() const { return nonDelta; }

    /* The deltas' masses for wo, per channel: what a mirror or an interface
     * reflects and what an interface transmits (the transmitted mass as
     * power, without the (eta_i / eta_t)^2 of its radiance). For
     * --bsdftest, which sums them with the finite part's albedo. */
    void deltaMasses(const Vec3 &wo, Vec3 &reflected, Vec3 &transmitted) const;

private:
    /* Yours from here on: add what your models need. What is here is a
     * start: the frame, and what hands frosted glass to the given
     * RoughDielectric. */
    Vec3 toLocal(const Vec3 &v) const { return Vec3(dot(v, t), dot(v, b), dot(v, n)); }
    Vec3 toWorld(const Vec3 &v) const { return t * v.x + b * v.y + n * v.z; }

    Vec3 t, b, n, ng;  /* the local frame (your orthonormal basis about N, z along N) and the geometric normal */
    bool frosted;      /* a transmissive material with roughness above 0: the given RoughDielectric's, whole */
    RoughDielectric rough;
    bool nonDelta;
};

/* GGX, isotropic, in a local frame with z along the normal (exposed for
 * --warptest ggx and --bsdftest). */
float ggxD(const Vec3 &wm, float alpha);
float ggxLambda(const Vec3 &w, float alpha);
/* A microfacet normal from the distribution of normals visible from wo
 * (Heitz 2018, "Sampling the GGX Distribution of Visible Normals"), wo
 * in the upper hemisphere; its density is G1(wo) max(wo . wm, 0) D(wm) / wo.z. */
Vec3 sampleGgxVisibleNormal(const Vec3 &wo, float alpha, Sample2 u);

/* --bsdftest [N], given complete in bsdftest.cpp: the table of checks of
 * every model's energy and of its sampling against its pdf. Have your
 * bsdfTest() in rt.cpp call it: return runBsdfTest(o); */
struct Options;
int runBsdfTest(const Options &o);

#endif /* BSDF_H */
