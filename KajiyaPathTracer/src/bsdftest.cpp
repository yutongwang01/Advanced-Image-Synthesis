/*
 * bsdftest.cpp: --bsdftest [N] (HW4 handout), given complete: the checks
 * of each BSDF's energy and of its sampling against its pdf, N samples a
 * row (default 400,000). For every model at several roughnesses and
 * angles: the integrals of pdf and of f cos over the sphere by quadrature,
 * the albedo against what the model must return (a white pigment under
 * any coat exactly 1; without a closed form, the reference renderer's
 * value within 1 percent), the samples' mean throughput (the albedo again,
 * by Monte Carlo), and the chi-square test of the sampled directions against
 * the pdf; then the transmissive materials, from both sides. It calls your
 * Bsdf and your sampler by the names bsdf.h and sampler.h fix; your
 * bsdfTest() in rt.cpp calls it.
 */
#include "bsdf.h"

#include "ggx_albedo.h"
#include "options.h"
#include "sampler.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

const float PI = 3.14159265358979323846f;

float luminance(const Vec3 &c) { return 0.2126f * c.x + 0.7152f * c.y + 0.0722f * c.z; }

/* The exact Fresnel reflectance of a smooth dielectric for unpolarized
 * light arriving at cos(theta_i) from the side of index etaI; 1 under
 * total internal reflection. The table's Fresnel column. */
float exactFresnel(float cosThetaI, float etaI, float etaT)
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

/* Q(a, x), the regularized upper incomplete gamma function, for the
 * chi-square p-value (Numerical Recipes, 6.2). */
double gammaQ(double a, double x)
{
    if (x <= 0) {
        return 1;
    }
    const double front = std::exp(-x + a * std::log(x) - std::lgamma(a));
    if (x < a + 1) {
        double term = 1 / a, total = term, ap = a;
        for (int i = 0; i < 100000 && std::fabs(term) > std::fabs(total) * 1e-15; i++) {
            ap += 1;
            term *= x / ap;
            total += term;
        }
        return std::max(0.0, 1 - total * front);
    }
    const double tiny = 1e-300;
    double bb = x + 1 - a, c = 1 / tiny, d = 1 / bb, h = d;
    for (int i = 1; i < 100000; i++) {
        const double an = -i * (i - a);
        bb += 2;
        d = an * d + bb;
        d = std::fabs(d) < tiny ? tiny : d;
        c = bb + an / c;
        c = std::fabs(c) < tiny ? tiny : c;
        d = 1 / d;
        const double delta = d * c;
        h *= delta;
        if (std::fabs(delta - 1) < 1e-15) {
            break;
        }
    }
    return front * h;
}

/* Pearson's chi-square over bins pooled from the least expected up until
 * each pool expects at least 5; the p-value. Bins expecting nothing must
 * hold nothing, or the test fails outright (p = 0). */
double chiSquareP(const std::vector<double> &observed, const std::vector<double> &expected, double &stat, int &dof)
{
    std::vector<std::pair<double, double>> bins; /* (expected, observed) */
    for (size_t i = 0; i < observed.size(); i++) {
        if (expected[i] <= 1e-9) {
            if (observed[i] > 0) {
                stat = INFINITY;
                dof = 0;
                return 0;
            }
            continue;
        }
        bins.push_back({expected[i], observed[i]});
    }
    /* By the expected count alone, ties in bin order (a stable sort): a
     * pool must not be chosen by what it holds. (Sorting the pairs would
     * put equally expected bins in the order of their counts, the empty
     * ones together, and a distribution symmetric about the normal, whose
     * rings of bins tie, would fail at a low N.) */
    std::stable_sort(bins.begin(), bins.end(),
                     [](const std::pair<double, double> &a, const std::pair<double, double> &b) { return a.first < b.first; });
    std::vector<std::pair<double, double>> pooled;
    double e = 0, o = 0;
    for (const auto &bin : bins) {
        e += bin.first;
        o += bin.second;
        if (e >= 5) {
            pooled.push_back({e, o});
            e = o = 0;
        }
    }
    if (e > 0) {
        if (pooled.empty()) {
            pooled.push_back({e, o});
        } else {
            pooled.back().first += e;
            pooled.back().second += o;
        }
    }
    stat = 0;
    for (const auto &p : pooled) {
        stat += (p.second - p.first) * (p.second - p.first) / p.first;
    }
    dof = (int)pooled.size() - 1;
    return dof > 0 ? gammaQ(dof / 2.0, stat / 2.0) : 1;
}

/* One opaque model to test: the material, and the albedo expected at each
 * of the four angles, the finite part by quadrature plus the mirror's
 * mass. A white pigment under any coat expects exactly 1; the conductor
 * at alpha 1, F0 1 expects the closed form 1 - mu ln((1 + mu) / mu); a
 * model without a closed form expects the reference renderer's albedo,
 * and passes within the tolerance plus the fraction `relative` of it. */
struct BsdfCase {
    const char *model;
    RtMaterial m;
    double expect[4];
    double tolerance, relative;
    bool mcOnly; /* alpha under 0.02: the lobe is narrower than the quadrature's cells, so only the Monte Carlo
                    mean is judged (the quadrature columns are printed as they come out) */
};

const float ANGLES[4] = {0, 30, 60, 80};

/* The chi-square test's significance level per case. With some hundred
 * cases a correct BSDF still fails one run in about a hundred by chance,
 * with p a little below this; a mistake gives p near 0 at every seed. */
const double CHI2_ALPHA = 1e-4;

/* A model without a closed form expects the reference renderer's own
 * number (the tables above runBsdfTest) and passes within this fraction
 * of it: a BSDF written to the HW4 handout's model returns the same
 * number, and a black or a dark one fails. */
const double MATCH = 0.01;

/* The quadrature of a BSDF's finite part at wo over the sphere, in
 * (theta, phi): the integrals of pdf and of f cos (luminance), and the pdf
 * binned on the chi-square grid (binsT x binsP) when bins is given. */
void integrateFinite(const Bsdf &bsdf, const Vec3 &wo, double &pdfIntegral, double &albedo, std::vector<double> *bins,
                     int binsT, int binsP, double belowScale = 1)
{
    const int subT = 60, subP = 12; /* 0.05 x 0.25 degree cells inside 3 x 3 degree bins */
    const int nT = binsT * subT, nP = binsP * subP;
    pdfIntegral = albedo = 0;
    for (int i = 0; i < nT; i++) {
        const double theta = (i + 0.5) * PI / nT;
        const double dOmega = std::sin(theta) * (PI / nT) * (2 * PI / nP);
        const float z = (float)std::cos(theta), s = (float)std::sin(theta);
        for (int j = 0; j < nP; j++) {
            const double phi = (j + 0.5) * 2 * PI / nP;
            const Vec3 wi(s * (float)std::cos(phi), s * (float)std::sin(phi), z);
            const double p = bsdf.pdf(wo, wi);
            pdfIntegral += p * dOmega;
            albedo += luminance(bsdf.eval(wo, wi)) * std::fabs(z) * dOmega * (z < 0 ? belowScale : 1);
            if (bins) {
                (*bins)[(size_t)(i / subT) * binsP + (size_t)(j / subP)] += p * dOmega;
            }
        }
    }
}

/* One opaque BSDF at one angle: the integrals above; the albedo, finite
 * part plus the mirror's mass, against what the case expects; the fraction
 * of samples sample() refuses against the pdf's missing mass; the mean
 * throughput of the samples (f cos / pdf, deltas included), which is the
 * albedo again by Monte Carlo; and the chi-square test of the sampled
 * directions against the pdf. */
bool testOne(const BsdfCase &c, int angle, int count, uint64_t seed)
{
    const Vec3 N(0, 0, 1);
    Bsdf bsdf(c.m, N, N, Vec3(0, 0, 0), true);
    const float to = ANGLES[angle] * PI / 180;
    const Vec3 wo(std::sin(to), 0, std::cos(to));

    const int binsT = 60, binsP = 120;
    std::vector<double> expected((size_t)binsT * binsP, 0.0);
    double pdfIntegral, finite;
    integrateFinite(bsdf, wo, pdfIntegral, finite, &expected, binsT, binsP);
    Vec3 reflected, transmitted;
    bsdf.deltaMasses(wo, reflected, transmitted);
    const double albedo = finite + luminance(reflected);

    std::unique_ptr<Sampler> sampler = makeSampler("independent", 1, seed);
    std::vector<double> observed(expected.size(), 0.0);
    int refused = 0, deltas = 0;
    double throughput = 0;
    for (int k = 0; k < count; k++) {
        sampler->startPixelSample(k, 1, 0);
        const float u1 = sampler->next1D();
        const Sample2 u2 = sampler->next2D();
        BsdfSample bs;
        if (!bsdf.sample(wo, u1, u2, bs)) {
            refused++;
            continue;
        }
        throughput += luminance(bs.f) * std::fabs(bs.wi.z) / bs.pdf;
        if (bs.delta) {
            deltas++; /* the mirror: not on the pdf's grid */
            continue;
        }
        const double theta = std::acos(std::min(1.0f, std::max(-1.0f, bs.wi.z)));
        double phi = std::atan2(bs.wi.y, bs.wi.x);
        if (phi < 0) {
            phi += 2 * PI;
        }
        int bi = std::min(binsT - 1, (int)(theta / PI * binsT));
        /* A direction a hair above the horizon has an angle that rounds to
         * pi / 2. It still belongs to the upper half's last ring, where the
         * pdf put it, and one a hair below to the lower half's first. */
        if (bs.wi.z > 0) {
            bi = std::min(bi, binsT / 2 - 1);
        } else if (bs.wi.z < 0) {
            bi = std::max(bi, binsT / 2);
        }
        const int bj = std::min(binsP - 1, (int)(phi / (2 * PI) * binsP));
        observed[(size_t)bi * binsP + (size_t)bj] += 1;
    }
    for (double &e : expected) {
        e *= count;
    }
    double stat;
    int dof;
    const double p = chiSquareP(observed, expected, stat, dof);
    throughput /= count;

    /* The refused fraction is a binomial proportion: within 4 standard
     * errors of the pdf's missing mass. A mirror coat's samples are in
     * neither the pdf nor the refusals, so their share comes out first. */
    const double lostFinite = std::max(0.0, 1 - pdfIntegral - (double)deltas / count);
    const double refusedFraction = (double)refused / count;
    const double se = std::sqrt(std::max(lostFinite * (1 - lostFinite), 1e-12) / count);
    const bool lostOk = std::fabs(refusedFraction - lostFinite) <= 4 * se + 3e-3;
    const double expect = c.expect[angle];
    const double tolerance = c.tolerance + c.relative * expect;
    const bool albedoOk = std::fabs(albedo - expect) <= tolerance;
    const bool mcOk = std::fabs(throughput - expect) <= tolerance + 0.01;
    const bool ok = c.mcOnly ? mcOk : pdfIntegral <= 1.002 && albedoOk && mcOk && lostOk && p > CHI2_ALPHA;
    std::printf("%-11s %5.2f %5.0f   %7.4f  %7.4f %7.4f   %7.4f %7.4f  %7.4f   %8.1f %5d %9.3g   %s%s\n", c.model,
                c.m.roughness, ANGLES[angle], pdfIntegral, lostFinite, refusedFraction, albedo, expect, throughput, stat,
                dof, p, ok ? "PASS" : "FAIL", c.mcOnly ? " (MC mean only: the lobe is narrower than the quadrature's cells)" : "");
    return ok;
}

/* A transmissive material at one side and angle: the exact Fresnel term,
 * the finite part by quadrature (the pigment; a rough interface's
 * reflection and transmission too, the transmitted radiance scaled to
 * power by (eta_t / eta_i)^2) and the two deltas' masses when the interface
 * is smooth, the transmitted one as power; their sum against what the case
 * expects (1 for a white pigment under specular 1 at any transmission;
 * for every other material the reference renderer's total: a rough
 * interface loses to masking like the conductor); the same sum by Monte
 * Carlo from the samples' throughputs; the fraction reflected; and for a
 * rough interface the chi-square test of the sampled directions against
 * the pdf over the whole sphere and the refused fraction against the
 * pdf's missing mass, as testOne does. row is the side and angle: 0 to 4
 * outside, 5 to 9 inside. */
struct DielectricCase {
    const char *model;
    RtMaterial m;
    const double *expect; /* the ten power totals the reference renderer returns, outside at the five angles and
                             then inside, matched within MATCH and the 0.003 of the quadrature and the rough
                             coat's lookup; NULL: exactly 1 at every angle and side */
};

bool testDielectric(const DielectricCase &c, bool entering, float degrees, int row, int count, uint64_t seed)
{
    const Vec3 N(0, 0, 1);
    Bsdf bsdf(c.m, N, N, Vec3(0, 0, 0), entering);
    const float to = degrees * PI / 180;
    const Vec3 wo(std::sin(to), 0, std::cos(to));
    const float etaI = entering ? 1.0f : c.m.ior, etaT = entering ? c.m.ior : 1.0f;
    const float F = exactFresnel(std::cos(to), etaI, etaT);
    const bool rough = c.m.roughness * c.m.roughness >= MIRROR_ALPHA;
    const double etaScale = (double)(etaT / etaI) * (etaT / etaI); /* radiance to power across the interface */

    const int binsT = 60, binsP = 120;
    std::vector<double> expected((size_t)binsT * binsP, 0.0);
    double pdfIntegral, finite;
    integrateFinite(bsdf, wo, pdfIntegral, finite, rough ? &expected : nullptr, binsT, binsP, etaScale);
    Vec3 reflected, transmitted;
    bsdf.deltaMasses(wo, reflected, transmitted);
    const double total = finite + luminance(reflected) + luminance(transmitted);

    std::unique_ptr<Sampler> sampler = makeSampler("independent", 1, seed);
    std::vector<double> observed(expected.size(), 0.0);
    int reflects = 0, refused = 0, deltas = 0;
    double power = 0, power2 = 0;
    for (int k = 0; k < count; k++) {
        sampler->startPixelSample(k, 2, 0);
        const float u1 = sampler->next1D();
        const Sample2 u2 = sampler->next2D();
        BsdfSample bs;
        if (!bsdf.sample(wo, u1, u2, bs)) {
            refused++;
            continue;
        }
        double w = luminance(bs.f) * std::fabs(bs.wi.z) / bs.pdf;
        if (bs.wi.z < 0) {
            w *= etaScale;
        }
        reflects += bs.wi.z > 0 && (bs.delta || rough);
        power += w;
        power2 += w * w;
        if (bs.delta) {
            deltas++;
            continue;
        }
        if (rough) {
            const double theta = std::acos(std::min(1.0f, std::max(-1.0f, bs.wi.z)));
            double phi = std::atan2(bs.wi.y, bs.wi.x);
            if (phi < 0) {
                phi += 2 * PI;
            }
            int bi = std::min(binsT - 1, (int)(theta / PI * binsT));
            /* A direction a hair above the horizon has an angle that rounds to
             * pi / 2. It still belongs to the upper half's last ring, where the
             * pdf put it, and one a hair below to the lower half's first. */
            if (bs.wi.z > 0) {
                bi = std::min(bi, binsT / 2 - 1);
            } else if (bs.wi.z < 0) {
                bi = std::max(bi, binsT / 2);
            }
            const int bj = std::min(binsP - 1, (int)(phi / (2 * PI) * binsP));
            observed[(size_t)bi * binsP + (size_t)bj] += 1;
        }
    }
    const double mc = power / count;
    const double se = std::sqrt(std::max(power2 / count - mc * mc, 0.0) / count);
    const double fraction = (double)reflects / count;
    const double expect = c.expect ? c.expect[row] : 1;
    const bool totalOk = std::fabs(total - expect) <= (c.expect ? 0.003 + MATCH * expect : 1e-3);
    const bool mcOk = std::fabs(mc - total) <= 4 * se + 1e-3;
    bool ok = pdfIntegral <= 1.002 && totalOk && mcOk;
    char tail[96] = "";
    if (rough) {
        for (double &e : expected) {
            e *= count;
        }
        double stat;
        int dof;
        const double p = chiSquareP(observed, expected, stat, dof);
        const double lostFinite = std::max(0.0, 1 - pdfIntegral - (double)deltas / count);
        const double refusedFraction = (double)refused / count;
        const double se2 = std::sqrt(std::max(lostFinite * (1 - lostFinite), 1e-12) / count);
        const bool lostOk = std::fabs(refusedFraction - lostFinite) <= 4 * se2 + 3e-3;
        ok = ok && lostOk && p > CHI2_ALPHA;
        std::snprintf(tail, sizeof tail, "   chi2 %8.1f dof %4d p %9.3g  refused %.4f of %.4f missing", stat, dof, p,
                      refusedFraction, lostFinite);
    }
    std::printf("%-9s %5.2f %-7s %5.0f   %7.4f   %7.4f %7.4f %7.4f   %7.4f %7.4f   %7.4f   %7.4f   %s%s\n", c.model,
                c.m.roughness, entering ? "outside" : "inside", degrees, F, luminance(reflected), luminance(transmitted),
                finite, total, expect, mc, fraction,
                ok ? "PASS" : "FAIL", tail);
    return ok;
}

/* What the reference renderer returns where the model has no closed form
 * (the HW4 handout's model, by the quadrature above): the opaque models'
 * albedos at 0, 30, 60 and 80 degrees, and the transmissive materials'
 * totals outside at 0, 30, 45, 60 and 80 degrees and then inside. Your
 * BSDF returns the same numbers, within MATCH. */
const double CONDUCTOR[4][4] = {{0.9983, 0.9980, 0.9955, 0.9681}, /* roughness 0.2 */
                                {0.9819, 0.9785, 0.9543, 0.8819}, /* 0.35 */
                                {0.9158, 0.9041, 0.8573, 0.8556}, /* 0.5 */
                                {0.6269, 0.6280, 0.6632, 0.7889}}; /* 0.75 */
const double COATED[3][4] = {{0.9991, 0.9990, 0.9977, 0.9844}, /* roughness 0.2 */
                             {0.9580, 0.9521, 0.9286, 0.9281}, /* 0.5 */
                             {0.6534, 0.6676, 0.7253, 0.8341}}; /* 1 */
const double DARK[4] = {0.2317, 0.2318, 0.2561, 0.4218};
const double BLACK_HALF[10] = {0.5200, 0.5208, 0.5251, 0.5446, 0.6939, 0.5200, 0.5276, 1.0000, 1.0000, 1.0000};
const double HONEY_HALF[10] = {0.6906, 0.6911, 0.6939, 0.7064, 0.8026, 0.6906, 0.6955, 1.0000, 1.0000, 1.0000};
const double DULL_HALF[10] = {0.9600, 0.9585, 0.9498, 0.9108, 0.6123, 0.9600, 0.9448, 0.0000, 0.0000, 0.0000};
const double INDEX_ONE[10] = {0.9000, 0.9000, 0.9000, 0.9000, 0.9000, 0.9000, 0.9000, 0.9000, 0.9000, 0.9000};
const double FROST_20[10] = {1.0002, 0.9997, 0.9995, 0.9988, 0.9826, 0.9985, 0.9978, 0.9949, 0.9947, 0.9661};
const double FROST_35[10] = {0.9983, 0.9973, 0.9951, 0.9885, 0.9361, 0.9833, 0.9774, 0.9576, 0.9455, 0.8641};
const double FROST_50[10] = {0.9921, 0.9880, 0.9797, 0.9608, 0.9128, 0.9292, 0.9064, 0.8642, 0.8271, 0.7955};
const double FROST_100[10] = {0.8933, 0.8545, 0.8022, 0.7422, 0.7086, 0.4171, 0.4550, 0.4949, 0.5128, 0.4969};
const double FROST_HALF[10] = {0.9961, 0.9958, 0.9952, 0.9934, 0.9704, 0.9921, 0.9904, 0.9846, 0.9817, 0.9435};
const double HONEY_FROST[10] = {0.6880, 0.6884, 0.6909, 0.7012, 0.7433, 0.6858, 0.7027, 0.9221, 0.9714, 0.9228};
const double WATER[10] = {0.6530, 0.6521, 0.6527, 0.6557, 0.6785, 0.6522, 0.6522, 0.6661, 0.7478, 0.7389};

} // namespace

/* --bsdftest [N] (HW4 handout): every opaque model at several
 * roughnesses and angles, N samples each (default 400,000), the white
 * pigments expecting exactly 1, the closed form at alpha 1, F0 1, the
 * other models the reference renderer's albedo; then the transmissive
 * materials, the smooth interface's deltas and the pigment under it,
 * from both sides. */
int runBsdfTest(const Options &o)
{
    int count = 400000;
    for (const std::string &a : o.bsdfTestArgs) {
        if (std::atoi(a.c_str()) > 0) {
            count = std::atoi(a.c_str());
        }
    }

    const double ONE[4] = {1, 1, 1, 1}, HALF[4] = {0.5, 0.5, 0.5, 0.5};
    double closedForm[4];
    for (int a = 0; a < 4; a++) {
        const double mu = std::cos(ANGLES[a] * PI / 180);
        closedForm[a] = 1 - mu * std::log((1 + mu) / mu); /* E_s at alpha 1, F = 1 */
    }
    std::vector<BsdfCase> cases;
    auto add = [&](const char *model, const RtMaterial &m, const double *expect, double tolerance, double relative) {
        BsdfCase c;
        c.model = model;
        c.m = m;
        for (int a = 0; a < 4; a++) {
            c.expect[a] = expect[a];
        }
        c.tolerance = tolerance;
        c.relative = relative;
        /* The quadrature's cells are 0.05 x 0.25 degrees; a lobe of alpha under 0.02 (about a degree wide) is
         * not resolved by them, and its rows are judged by the Monte Carlo mean alone. */
        const float alpha = m.roughness * m.roughness;
        c.mcOnly = m.specular[0] > 0 && alpha >= MIRROR_ALPHA && alpha < 0.02f;
        cases.push_back(c);
    };
    /* The table's lookup is good to 0.005; the quadrature here to about
     * 0.001. */
    const double TABLE = 0.006, EXACT = 0.002;
    add("lambert", {{1, 1, 1}, {0, 0, 0}, 1, 0, 0, 1.5f, {0, 0, 0}}, ONE, EXACT, 0);
    add("lambert", {{0.5f, 0.5f, 0.5f}, {0, 0, 0}, 0.3f, 0, 0, 1.5f, {0, 0, 0}}, HALF, EXACT, 0);
    const float conductorRoughness[4] = {0.2f, 0.35f, 0.5f, 0.75f};
    for (int i = 0; i < 4; i++) { /* no closed form: the reference renderer's albedo */
        add("conductor", {{0, 0, 0}, {1, 1, 1}, conductorRoughness[i], 1, 0, 1.5f, {0, 0, 0}}, CONDUCTOR[i], EXACT, MATCH);
    }
    add("conductor", {{0, 0, 0}, {1, 1, 1}, 1.0f, 1, 0, 1.5f, {0, 0, 0}}, closedForm, EXACT, 0); /* 1 - mu ln((1 + mu) / mu) */
    const float coatedRoughness[3] = {0.2f, 0.5f, 1.0f};
    for (int i = 0; i < 3; i++) {
        add("coated", {{0.5f, 0.5f, 0.5f}, {1, 1, 1}, coatedRoughness[i], 0, 0, 1.5f, {0, 0, 0}}, COATED[i], EXACT, MATCH);
    }
    for (float r : {0.1f, 0.15f, 0.2f, 0.5f, 1.0f}) { /* the white pigment under a 0.04 coat: exactly 1 */
        add("white/04", {{1, 1, 1}, {0.04f, 0.04f, 0.04f}, r, 0, 0, 1.5f, {0, 0, 0}}, ONE, TABLE, 0);
    }
    for (float r : {0.2f, 1.0f}) { /* under a fully reflecting rough coat: the pigment supplies the loss */
        add("white/1", {{1, 1, 1}, {1, 1, 1}, r, 0, 0, 1.5f, {0, 0, 0}}, ONE, TABLE, 0);
    }
    add("white/04", {{1, 1, 1}, {0.04f, 0.04f, 0.04f}, 0, 0, 0, 1.5f, {0, 0, 0}}, ONE, EXACT, 0); /* the mirror coat */
    add("white/1", {{1, 1, 1}, {1, 1, 1}, 0, 0, 0, 1.5f, {0, 0, 0}}, ONE, EXACT, 0); /* nothing reaches the pigment */
    add("white/rgb", {{1, 1, 1}, {1, 0.5f, 0}, 0, 0, 0, 1.5f, {0, 0, 0}}, ONE, EXACT, 0); /* a channel at each endpoint */
    add("white/rgb", {{1, 1, 1}, {1, 0.5f, 0}, 0.5f, 0, 0, 1.5f, {0, 0, 0}}, ONE, TABLE, 0);
    add("dark/04", {{0.2f, 0.2f, 0.2f}, {0.04f, 0.04f, 0.04f}, 0.3f, 0, 0, 1.5f, {0, 0, 0}}, DARK, EXACT, MATCH);
    add("white/04", {{1, 1, 1}, {0.04f, 0.04f, 0.04f}, 0.032f, 0, 0, 1.5f, {0, 0, 0}}, ONE, TABLE, 0); /* just above the mirror threshold */
    add("white/04", {{1, 1, 1}, {0.04f, 0.04f, 0.04f}, 0.031f, 0, 0, 1.5f, {0, 0, 0}}, ONE, EXACT, 0); /* just below it */

    std::printf("Opaque BSDFs: the integrals of pdf and of f cos (luminance) over the sphere by quadrature, the\n"
                "pdf's missing mass against the fraction of samples refused, the albedo (f cos plus a mirror's\n"
                "mass) against what the model expects, the mean throughput of %d samples (the albedo again,\n"
                "by Monte Carlo), and the chi-square test of the sampled directions against the pdf. A white\n"
                "pigment under any coat returns exactly 1; the conductor at roughness 1 the closed form\n"
                "1 - mu ln((1 + mu) / mu) (0.3069 head on); the other conductors, the grey pigment under a coat\n"
                "(coated, dark/04) the reference renderer's albedo, which yours must match within 1 percent.\n\n",
                count);
    std::printf("model       rough theta   int pdf  missing refused    albedo  expect   MC mean       chi2   dof"
                "         p\n");
    int failed = 0;
    for (const BsdfCase &c : cases) {
        for (int a = 0; a < 4; a++) {
            failed += !testOne(c, a, count, o.seed);
        }
    }

    /* The table and the coupling at the closed form: E_s(1) and Ebar at
     * alpha 1, F0 1 straight from the lookup. */
    {
        const double e1 = ggxAlbedo(1, 1, 1), ebar = ggxAlbedoAverage(1, 1);
        const double ln2 = std::log(2.0);
        std::printf("\nThe table at roughness 1, F0 1: E_s(1) %.5f (closed form 1 - ln 2 = %.5f), Ebar %.5f (4/3 (1 - ln 2) = "
                    "%.5f); 1 - Ebar = %.4f, the denominator of a fully reflecting rough coat, far from 0\n",
                    e1, 1 - ln2, ebar, 4.0 / 3 * (1 - ln2), 1 - ebar);
        if (std::fabs(e1 - (1 - ln2)) > 0.003 || std::fabs(ebar - 4.0 / 3 * (1 - ln2)) > 0.003) {
            std::printf("  FAIL: the table disagrees with the closed form\n");
            failed++;
        }
    }

    /* Transmissive materials: the pigment by quadrature, the deltas' masses
     * (transmission as power), and the samples' throughputs. */
    std::vector<DielectricCase> glass = {
        {"glass", {{0, 0, 0}, {1, 1, 1}, 0, 0, 1, 1.5f, {0, 0, 0}}, nullptr},
        {"blend/.5", {{1, 1, 1}, {1, 1, 1}, 0, 0, 0.5f, 1.5f, {0, 0, 0}}, nullptr},
        {"blend/.001", {{1, 1, 1}, {1, 1, 1}, 0, 0, 0.001f, 1.5f, {0, 0, 0}}, nullptr},
        {"blend/.999", {{1, 1, 1}, {1, 1, 1}, 0, 0, 0.999f, 1.5f, {0, 0, 0}}, nullptr},
        {"black/.5", {{0, 0, 0}, {1, 1, 1}, 0, 0, 0.5f, 1.5f, {0, 0, 0}}, BLACK_HALF},
        {"honey/.5", {{0.9f, 0.6f, 0.1f}, {1, 1, 1}, 0, 0, 0.5f, 1.5f, {0, 0, 0}}, HONEY_HALF},
        {"dull/.5", {{1, 1, 1}, {0, 0, 0}, 0, 0, 0.5f, 1.5f, {0, 0, 0}}, DULL_HALF}, /* specular 0: the reflection taken away */
        {"blend/1.1", {{1, 1, 1}, {1, 1, 1}, 0, 0, 0.5f, 1.1f, {0, 0, 0}}, nullptr},
        /* index 1, as the picture glass of BarbershopSunOnly: the given frosted glass refuses it whatever the
         * roughness, so it is the smooth interface with F = 0. Nothing is reflected, 0.8 goes straight through,
         * and the grey pigment returns the rest, 0.2 x 0.5. */
        {"ior1/.8", {{0.5f, 0.5f, 0.5f}, {1, 1, 1}, 0.49f, 0, 0.8f, 1.0f, {0, 0, 0}}, INDEX_ONE},
        /* rough interfaces: plain frosted glass at four roughnesses, under 1 (masking's loss, as the
         * conductor's), a white pigment under a rough interface, honey, and water seen at a pool's angles */
        {"frost", {{0, 0, 0}, {1, 1, 1}, 0.2f, 0, 1, 1.5f, {0, 0, 0}}, FROST_20},
        {"frost", {{0, 0, 0}, {1, 1, 1}, 0.35f, 0, 1, 1.5f, {0, 0, 0}}, FROST_35},
        {"frost", {{0, 0, 0}, {1, 1, 1}, 0.5f, 0, 1, 1.5f, {0, 0, 0}}, FROST_50},
        {"frost", {{0, 0, 0}, {1, 1, 1}, 1.0f, 0, 1, 1.5f, {0, 0, 0}}, FROST_100},
        {"frost/.5", {{1, 1, 1}, {1, 1, 1}, 0.3f, 0, 0.5f, 1.5f, {0, 0, 0}}, FROST_HALF},
        {"honeyfr", {{0.9f, 0.6f, 0.1f}, {1, 1, 1}, 0.3f, 0, 0.5f, 1.5f, {0, 0, 0}}, HONEY_FROST},
        {"water", {{0, 0, 0}, {0.75f, 0.75f, 0.75f}, 0.16f, 0, 0.65f, 1.33f, {0, 0, 0}}, WATER},
    };
    std::printf("\nTransmissive materials, ior as given: the exact Fresnel term F at wo; the masses of the two deltas\n"
                "(ks F reflected, kt (1 - F) transmitted, as power) and the finite part's albedo by quadrature (the\n"
                "pigment; a rough interface's reflection and transmission too, the latter as power); their sum\n"
                "against what the model expects (a white pigment under a smooth interface of specular 1 totals 1\n"
                "at every transmission, from both sides, total internal reflection included; every other\n"
                "material the reference renderer's total, which yours must match within 1 percent and the 0.003\n"
                "of the quadrature and the rough coat's lookup); the\n"
                "same by Monte Carlo (the samples' throughputs, the refracted ones scaled to\n"
                "power); the fraction reflected; for a rough interface the chi-square test of the sampled\n"
                "directions against the pdf over the sphere, and the refused fraction against the pdf's missing\n"
                "mass.\n\n");
    std::printf("model     rough side    theta   Fresnel   reflect transmit  finite     total expect   MC mean   reflected\n");
    const float glassAngles[5] = {0, 30, 45, 60, 80};
    for (const DielectricCase &c : glass) {
        for (int side = 0; side < 2; side++) {
            for (int a = 0; a < 5; a++) {
                failed += !testDielectric(c, side == 0, glassAngles[a], side * 5 + a, count, o.seed);
            }
        }
    }



    std::printf("\n%s\n", failed ? "SOME TESTS FAILED (a lone chi-square failure with p near 1e-4 can be chance: "
                                     "rerun with another --seed)"
                                   : "all pass");
    return failed ? 1 : 0;
}
