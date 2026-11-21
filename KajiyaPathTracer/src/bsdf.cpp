/*
 * bsdf.cpp: your BSDF (HW4 handout; bsdf.h says what the model is). Write
 * the three GGX functions and the Bsdf's five, keeping the names and
 * signatures bsdf.h declares: the given bsdftest.cpp, warptest.cpp and
 * roughdielectric.cpp call them. Frosted glass, a transmissive material
 * with roughness above 0, is the given roughdielectric.cpp's (Walter et
 * al. 2007): the lines that hand it over are already here. Until you
 * write the rest, every other material is black and --bsdftest fails it.
 */
#include "bsdf.h"

#include "ggx_albedo.h"
#include "warp.h"

#include <algorithm>
#include <cmath>

/* GGX ------------------------------------------------------------------- */

/* TODO: D(wm), the distribution of microfacet normals, 0 below the
 * surface. Take sin^2(theta) as x^2 + y^2 rather than 1 - cos^2(theta),
 * here and in ggxLambda: near the pole, where a smooth lobe lives,
 * 1 - cos^2 cancels to a few significant bits in float. */
float ggxD(const Vec3 &wm, float alpha)
{
    (void)wm;
    (void)alpha;
    return 0;
}

/* TODO: Smith's Lambda(w), from which G1 = 1 / (1 + Lambda(w)) and the
 * height-correlated G2 = 1 / (1 + Lambda(wo) + Lambda(wi)). */
float ggxLambda(const Vec3 &w, float alpha)
{
    (void)w;
    (void)alpha;
    return 0;
}

/* TODO: a visible normal (Heitz 2018): stretch wo to the configuration
 * where the distribution is the hemisphere of radius 1, sample the
 * projected area of the visible half-disk, and unstretch. */
Vec3 sampleGgxVisibleNormal(const Vec3 &wo, float alpha, Sample2 u)
{
    (void)wo;
    (void)alpha;
    (void)u;
    return Vec3(0, 0, 1);
}

/* Bsdf ------------------------------------------------------------------ */

Bsdf::Bsdf(const RtMaterial &m, const Vec3 &N, const Vec3 &Ng, const Vec3 &dPdu, bool entering)
{
    /* TODO: the local frame. z is N; x and y are t and b of your
     * orthonormal basis about N from HW3. Do not use dPdu (BSDFs handout,
     * the local frame). */
    n = N;
    ng = Ng;
    t = b = Vec3(0, 0, 0);
    (void)dPdu; /* keep this line: dPdu is not used, and the line keeps the compiler from warning about it */

    /* Roughness frosts a transmissive material's interface: the given
     * RoughDielectric takes it whole, here and at the top of eval, pdf and
     * sample. Keep these lines after your frame. */
    frosted = RoughDielectric::applies(m);
    if (frosted) {
        rough.set(m, t, b, n, ng, entering);
        nonDelta = true;
        return;
    }

    /* TODO: every other material: the opaque one (the pigment, the coat's
     * factor, the lobe or the mirror) and the transmissive one over a
     * smooth interface (the two deltas, and the pigment when transmission
     * is below 1). Set nonDelta. */
    nonDelta = false;
}

/* TODO: the finite part of f. */
Vec3 Bsdf::eval(const Vec3 &wo, const Vec3 &wi) const
{
    if (frosted) {
        return rough.eval(wo, wi);
    }
    (void)wo;
    (void)wi;
    return Vec3(0, 0, 0);
}

/* TODO: the density sample() draws a finite-density wi with. */
float Bsdf::pdf(const Vec3 &wo, const Vec3 &wi) const
{
    if (frosted) {
        return rough.pdf(wo, wi);
    }
    (void)wo;
    (void)wi;
    return 0;
}

/* TODO: one direction: u1 picks the lobe, u2 the direction in it. */
bool Bsdf::sample(const Vec3 &wo, float u1, Sample2 u2, BsdfSample &bs) const
{
    if (frosted) {
        return rough.sample(wo, u1, u2, bs);
    }
    (void)wo;
    (void)u1;
    (void)u2;
    (void)bs;
    return false;
}

/* TODO: what a mirror or a smooth interface reflects and what the
 * interface transmits, for wo. */
void Bsdf::deltaMasses(const Vec3 &wo, Vec3 &reflected, Vec3 &transmitted) const
{
    reflected = transmitted = Vec3(0, 0, 0);
    if (frosted) {
        return; /* a rough interface has no deltas */
    }
    (void)wo;
}
