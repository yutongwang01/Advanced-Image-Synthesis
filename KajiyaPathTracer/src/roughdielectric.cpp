/*
 * roughdielectric.cpp: frosted glass, given complete (HW4 handout: frosted
 * glass; roughdielectric.h says how your Bsdf uses it). The interface of a
 * transmissive material with roughness above 0 is a GGX microfacet
 * dielectric (Walter et al. 2007): the reflection F D G2 / (4 cos cos)
 * with the exact Fresnel term at the microfacet, the transmission Walter's
 * equation 21 in radiance transport, both sampled by a visible normal
 * (your sampleGgxVisibleNormal) reflected or refracted by the Fresnel term
 * at that normal, and under the interface the pigment, whose coat is the
 * interface's reflected directional albedo, tabulated over roughness,
 * index ratio and direction (rough_dielectric_albedo_data.h).
 */
#include "roughdielectric.h"

#include "bsdf.h"
#include "warp.h"
#include "rough_dielectric_albedo_data.h"

#include <algorithm>
#include <cmath>

static_assert(sizeof ROUGH_DIELECTRIC_E / sizeof ROUGH_DIELECTRIC_E[0] == dielectricAlbedo::R &&
              sizeof ROUGH_DIELECTRIC_E[0] / sizeof ROUGH_DIELECTRIC_E[0][0] == dielectricAlbedo::ETA &&
              sizeof ROUGH_DIELECTRIC_E[0][0] / sizeof(float) == dielectricAlbedo::MU,
              "Regenerate the rough dielectric albedo table after changing its grid");

namespace {

const float PI = 3.14159265358979323846f;

float luminance(const Vec3 &c) { return 0.2126f * c.x + 0.7152f * c.y + 0.0722f * c.z; }

float clamp01(float x) { return std::min(1.0f, std::max(0.0f, x)); }

/* The exact Fresnel reflectance of a smooth dielectric for unpolarized
 * light arriving at cos(theta_i) from the side of index etaI; 1 under
 * total internal reflection. (This file's own copy, so that it needs
 * nothing of yours but the GGX functions.) */
float interfaceFresnel(float cosThetaI, float etaI, float etaT)
{
    if (etaI == etaT) {
        return 0;
    }
    const double c = std::min(1.0, std::max(0.0, (double)cosThetaI));
    const double eta = (double)etaI / etaT;
    const double sin2T = eta * eta * (1 - c * c);
    if (sin2T >= 1) {
        return 1; /* total internal reflection */
    }
    const double cosT = std::sqrt(1 - sin2T);
    const double rs = (eta * c - cosT) / (eta * c + cosT);
    const double rp = (c - eta * cosT) / (c + eta * cosT);
    return (float)(0.5 * (rs * rs + rp * rp));
}

/* The color the glass gives the light it transmits (HW3 and HW4 handouts,
 * the body tint): 1 - (max(kd) - kd) per channel. */
Vec3 interfaceBodyTint(const Vec3 &kd)
{
    const Vec3 c(clamp01(kd.x), clamp01(kd.y), clamp01(kd.z));
    const float top = maxComponent(c);
    return Vec3(1 - (top - c.x), 1 - (top - c.y), 1 - (top - c.z));
}

} // namespace

/* The interface's reflected directional albedo -------------------------- */

void RoughDielectricCoat::set(float roughness, float etaI, float etaT)
{
    using namespace dielectricAlbedo;
    const double rho = ((double)etaI - etaT) / ((double)etaI + etaT);
    const double minimum = roughnessAt(0);
    const double r = std::min(1.0, std::max(minimum, (double)roughness));
    const double x = std::sqrt((r - minimum) / (1 - minimum)) * (R - 1);
    const double y = (std::cbrt(rho) + 1) * 0.5 * (ETA - 1);
    const int i = std::min(R - 2, (int)x), j = std::min(ETA - 2, (int)y);
    const double fx = (r - roughnessAt(i)) / (roughnessAt(i + 1) - roughnessAt(i));
    const double fy = (rho - rhoAt(j)) / (rhoAt(j + 1) - rhoAt(j));
    critical = criticalCosine(rho);
    double average = 0;
    for (int a = 0; a < 2; a++) {
        for (int b = 0; b < 2; b++) {
            const int k = 2 * a + b;
            rows[k] = ROUGH_DIELECTRIC_E[i + a][j + b];
            weights[k] = (a ? fx : 1 - fx) * (b ? fy : 1 - fy);
            const double *moments = ROUGH_DIELECTRIC_MOMENTS[i + a][j + b];
            const double mean = rho > 0 ? critical * critical * moments[0] +
                                             2 * critical * (1 - critical) * moments[1] +
                                             (1 - critical) * (1 - critical) * moments[2]
                                       : moments[0];
            average += weights[k] * mean;
        }
    }
    denominator = 1 - average;
}

double RoughDielectricCoat::albedo(float mu) const
{
    using namespace dielectricAlbedo;
    const double m = std::min(1.0, std::max(0.0, (double)mu));
    const int j = std::min(MU - 2, (int)muCoordinate(m, critical));
    const double a = muAt(j, critical), b = muAt(j + 1, critical);
    const double f = b > a ? (m - a) / (b - a) : 0;
    double e = 0;
    for (int k = 0; k < 4; k++) {
        const double e0 = rows[k][j], e1 = rows[k][j + 1];
        e += weights[k] * (e0 + f * (e1 - e0));
    }
    return e;
}

float RoughDielectricCoat::factor(float muO, float muI) const
{
    return denominator > 1e-12 ? (float)((1 - albedo(muO)) * (1 - albedo(muI)) / denominator) : 0;
}

/* The material ---------------------------------------------------------- */

/* At nearly matched indices the transmitted lobe is narrower than float
 * directions can resolve, and Walter's transmission term is singular at
 * equal ones (its Jacobian divides by eta_i cos_o + eta_t cos_i, which
 * vanishes), so such an interface stays the smooth pass-through, as pbrt
 * treats eta = 1. */
bool RoughDielectric::applies(const RtMaterial &m)
{
    const float roughness = clamp01(m.roughness);
    const float contrast = std::fabs(1.0f - m.ior) / std::max(1.0f, m.ior);
    return clamp01(m.transmission) > 0 && !(roughness * roughness < MIRROR_ALPHA || contrast <= 1e-4f);
}

/* The two-case rule of every transmissive material: specular tints the
 * interface's reflection (1 on real glass); the transmitted light takes
 * the body tint and the transmission's share; the pigment under the
 * interface the rest. */
void RoughDielectric::set(const RtMaterial &m, const Vec3 &T, const Vec3 &B, const Vec3 &N, const Vec3 &Ng,
                          bool entering)
{
    t = T;
    b = B;
    n = N;
    ng = Ng;
    pigment = Vec3(m.albedo) * (1 - clamp01(m.metallic));
    kt = clamp01(m.transmission);
    reflectTint = Vec3(m.specular);
    transmitTint = interfaceBodyTint(Vec3(m.albedo)) * kt;
    etaI = entering ? 1.0f : m.ior;
    etaT = entering ? m.ior : 1.0f;
    pigment = pigment * (1 - kt);
    hasPigment = maxComponent(pigment) > 0;
    const float roughness = clamp01(m.roughness);
    alpha = roughness * roughness;
    if (hasPigment) {
        coat.set(roughness, etaI, etaT);
    }
}

/* The three functions --------------------------------------------------- */

Vec3 RoughDielectric::eval(const Vec3 &wo, const Vec3 &wi) const
{
    const Vec3 lo = toLocal(wo), li = toLocal(wi);
    if (!(lo.z > 0) || !(dot(ng, wo) > 0)) {
        return Vec3(0, 0, 0);
    }
    /* A rough interface reflects (wi above N and Ng) or transmits (wi
     * below both); a direction across one normal only gives nothing. */
    if (li.z > 0 ? !(dot(ng, wi) > 0) : !(li.z < 0 && dot(ng, wi) < 0)) {
        return Vec3(0, 0, 0);
    }
    return evalLocal(lo, li);
}

float RoughDielectric::pdf(const Vec3 &wo, const Vec3 &wi) const
{
    const Vec3 lo = toLocal(wo), li = toLocal(wi);
    if (!(lo.z > 0) || !(dot(ng, wo) > 0)) {
        return 0;
    }
    if (li.z > 0 ? !(dot(ng, wi) > 0) : !(li.z < 0 && dot(ng, wi) < 0)) {
        return 0;
    }
    return pdfLocal(lo, li);
}

/* One direction from the rough interface or the pigment: the pigment with
 * its own directional budget (interfaceProbability), a cosine-weighted
 * direction; otherwise a visible normal wm, then reflected off it with the
 * probability of the Fresnel term at wm and refracted through it otherwise
 * (never refused: total internal reflection at the facet is F = 1), so
 * that a transmission the macro normal forbids past its critical angle is
 * still reached through a tilted facet. u1 is reused within the interface. */
bool RoughDielectric::sample(const Vec3 &wo, float u1, Sample2 u2, BsdfSample &bs) const
{
    const Vec3 lo = toLocal(wo);
    if (!(lo.z > 0) || !(dot(ng, wo) > 0)) {
        return false;
    }
    const float pInterface = interfaceProbability(lo.z);
    Vec3 li;
    if (u1 < pInterface) {
        const Vec3 wm = sampleGgxVisibleNormal(lo, alpha, u2);
        const float cosO = dot(lo, wm);
        if (!(cosO > 0)) {
            return false;
        }
        const float F = interfaceFresnel(cosO, etaI, etaT);
        if (u1 / pInterface < F) {
            li = wm * (2 * cosO) - lo;
            if (!(li.z > 0)) {
                return false;
            }
        } else {
            const double eta = (double)etaI / etaT;
            const double sin2T = eta * eta * std::max(0.0, 1 - (double)cosO * cosO);
            if (sin2T >= 1) {
                return false; /* unreachable: F is 1 there */
            }
            const double q = eta * cosO - std::sqrt(1 - sin2T);
            li = Vec3((float)(-eta * lo.x + q * wm.x), (float)(-eta * lo.y + q * wm.y),
                      (float)(-eta * lo.z + q * wm.z));
            if (!(li.z < 0)) {
                return false;
            }
        }
    } else {
        if (!hasPigment) {
            return false;
        }
        li = sampleCosineHemisphere(u2);
        if (!(li.z > 0)) {
            return false;
        }
    }
    bs.wi = toWorld(li);
    if (li.z > 0 ? !(dot(ng, bs.wi) > 0) : !(dot(ng, bs.wi) < 0)) {
        return false;
    }
    bs.pdf = pdfLocal(lo, li);
    bs.f = evalLocal(lo, li);
    bs.delta = false;
    return bs.pdf > 0;
}

/* In the local frame ---------------------------------------------------- */

/* How often sample() takes the interface rather than the pigment. Both
 * proposals keep support even beyond the macro critical angle, where
 * tilted facets still transmit and expose the pigment. */
float RoughDielectric::interfaceProbability(float mu) const
{
    if (!hasPigment) {
        return 1;
    }
    const float er = (float)coat.albedo(mu);
    const float wd = luminance(pigment) * (1 - er);
    const float wi = er + kt * (1 - er);
    if (!(wd > 0)) {
        return 1;
    }
    return std::min(0.75f, std::max(0.25f, wi / (wi + wd)));
}

/* The microfacet normal that takes wo (above) to wi, in the upper
 * hemisphere: the half vector for a reflection, the generalized one
 * normalize(eta_i wo + eta_t wi) turned up for a transmission. False for a
 * pair no facet connects (wo behind the facet, or wi on the wrong side of
 * it: a backfacing microfacet). */
bool RoughDielectric::halfVector(const Vec3 &lo, const Vec3 &li, Vec3 &wm) const
{
    const bool reflect = li.z > 0;
    const double x = reflect ? (double)lo.x + li.x : (double)lo.x * etaI + (double)li.x * etaT;
    const double y = reflect ? (double)lo.y + li.y : (double)lo.y * etaI + (double)li.y * etaT;
    const double z = reflect ? (double)lo.z + li.z : (double)lo.z * etaI + (double)li.z * etaT;
    const double norm = std::sqrt(x * x + y * y + z * z);
    if (!(norm > 0)) {
        return false;
    }
    wm = Vec3((float)(x / norm), (float)(y / norm), (float)(z / norm));
    if (wm.z < 0) {
        wm = -wm;
    }
    const float cosO = dot(lo, wm), cosI = dot(li, wm);
    if (!(cosO > 0)) {
        return false;
    }
    return reflect ? cosI > 0 : cosI < 0;
}

/* f of the rough interface and the pigment under it, wo above, wi on
 * either side: the pigment (wi above) under the rough reflection's coat; the
 * reflection F D G2 / (4 cos_o cos_i) tinted by specular, F the exact
 * Fresnel term at the facet; the transmission (wi below), Walter's
 * equation 21 for the radiance leaving toward wo,
 *   |wi . wm| |wo . wm| / (|wi . n| |wo . n|) eta_i^2 (1 - F) G2 D
 *   / (eta_i (wo . wm) + eta_t (wi . wm))^2,
 * which carries the (eta_i / eta_t)^2 of the smooth refraction in its
 * eta_i^2 (the smooth limit is (1 - F) (eta_i / eta_t)^2), tinted by the
 * body color and the transmission's share. */
Vec3 RoughDielectric::evalLocal(const Vec3 &lo, const Vec3 &li) const
{
    Vec3 f(0, 0, 0);
    if (li.z > 0 && hasPigment) {
        f = pigment * (coat.factor(lo.z, li.z) / PI);
    }
    Vec3 wm;
    if (!halfVector(lo, li, wm)) {
        return f;
    }
    const float cosO = dot(lo, wm), cosI = dot(li, wm);
    const float F = interfaceFresnel(cosO, etaI, etaT);
    const float D = ggxD(wm, alpha);
    const float G2 = 1 / (1 + ggxLambda(lo, alpha) + ggxLambda(li, alpha));
    if (li.z > 0) {
        f += reflectTint * (F * D * G2 / (4 * lo.z * li.z));
    } else if (F < 1) {
        const double denom = (double)etaI * cosO + (double)etaT * cosI;
        if (denom != 0) {
            f += transmitTint * (float)((1 - F) * D * G2 * std::fabs((double)cosO * cosI) * etaI * etaI /
                                        (denom * denom * lo.z * std::fabs(li.z)));
        }
    }
    return f;
}

/* The density of sample(): the pigment's share times cos / pi above; the
 * interface's share times the visible-normal density of wm,
 * G1(wo) (wo . wm) D(wm) / (wo . n), times F and the reflection's Jacobian
 * 1 / (4 wo . wm) above, times 1 - F and the refraction's Jacobian
 * eta_t^2 |wi . wm| / (eta_i (wo . wm) + eta_t (wi . wm))^2 below. */
float RoughDielectric::pdfLocal(const Vec3 &lo, const Vec3 &li) const
{
    const float pInterface = interfaceProbability(lo.z);
    float p = 0;
    if (li.z > 0 && hasPigment) {
        p += (1 - pInterface) * li.z / PI;
    }
    Vec3 wm;
    if (!halfVector(lo, li, wm)) {
        return p;
    }
    const float cosO = dot(lo, wm), cosI = dot(li, wm);
    const float F = interfaceFresnel(cosO, etaI, etaT);
    const float pm = ggxD(wm, alpha) * cosO / ((1 + ggxLambda(lo, alpha)) * lo.z);
    if (li.z > 0) {
        p += pInterface * F * pm / (4 * cosO);
    } else if (F < 1) {
        const double denom = (double)etaI * cosO + (double)etaT * cosI;
        if (denom != 0) {
            p += (float)(pInterface * (1 - F) * pm * (double)etaT * etaT * std::fabs(cosI) / (denom * denom));
        }
    }
    return p;
}
