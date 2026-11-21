/*
 * roughdielectric.h: frosted glass, given complete (HW4 handout: frosted
 * glass). A transmissive material with roughness above 0 has a GGX
 * microfacet interface (Walter et al. 2007) in place of the smooth
 * interface's two deltas: a reflection and a transmission of finite width,
 * and the pigment under it when transmission is below 1. You do not write
 * it. Your Bsdf hands such a material over whole:
 *   - in the constructor, once your frame is built, ask
 *     RoughDielectric::applies(m), keep the answer, and call set() when it
 *     is true;
 *   - open eval(), pdf() and sample() with: if frosted, return this
 *     class's;
 *   - hasNonDelta() is true for it, and deltaMasses() gives zeros.
 * It calls your ggxD, ggxLambda and sampleGgxVisibleNormal (bsdf.h) and
 * your sampleCosineHemisphere (warp.h), so it is right when they are.
 */
#ifndef ROUGHDIELECTRIC_H
#define ROUGHDIELECTRIC_H

#include "dielectric_albedo.h"
#include "rt.h"
#include "sampler.h"
#include "vec.h"

struct BsdfSample; /* bsdf.h */

class RoughDielectric {
public:
    /* Whether material m is frosted glass: transmission above 0, alpha =
     * roughness^2 at or above MIRROR_ALPHA, and an index of refraction that
     * differs from 1 (an interface between media of one index bends
     * nothing, rough or not: it stays your smooth interface). */
    static bool applies(const RtMaterial &m);

    /* The interface at a hit, from what your Bsdf's constructor has: the
     * material, your frame (T, B and N orthonormal, N the shading normal),
     * the geometric normal, and whether the ray arrived at the front of
     * the surface. */
    void set(const RtMaterial &m, const Vec3 &T, const Vec3 &B, const Vec3 &N, const Vec3 &Ng, bool entering);

    /* As Bsdf's three, directions in world space pointing away from the
     * surface: f without the cosine, the density sample() draws wi with,
     * and one sampled direction (never a delta). */
    Vec3 eval(const Vec3 &wo, const Vec3 &wi) const;
    float pdf(const Vec3 &wo, const Vec3 &wi) const;
    bool sample(const Vec3 &wo, float u1, Sample2 u2, BsdfSample &bs) const;

private:
    Vec3 toLocal(const Vec3 &v) const { return Vec3(dot(v, t), dot(v, b), dot(v, n)); }
    Vec3 toWorld(const Vec3 &v) const { return t * v.x + b * v.y + n * v.z; }
    float interfaceProbability(float mu) const;
    bool halfVector(const Vec3 &lo, const Vec3 &li, Vec3 &wm) const;
    Vec3 evalLocal(const Vec3 &lo, const Vec3 &li) const;
    float pdfLocal(const Vec3 &lo, const Vec3 &li) const;

    Vec3 t, b, n, ng;
    Vec3 pigment;                   /* albedo (1 - metallic) (1 - kt): the diffuse before the coat's factor */
    bool hasPigment;
    RoughDielectricCoat coat;       /* the interface's reflected directional albedo, the pigment's coat */
    float alpha;                    /* GGX alpha = roughness^2 */
    float kt;                       /* transmission */
    Vec3 reflectTint, transmitTint; /* specular; kt times the body tint */
    float etaI, etaT;               /* the index on wo's side (the side the ray came from) and the other */
};

#endif /* ROUGHDIELECTRIC_H */
