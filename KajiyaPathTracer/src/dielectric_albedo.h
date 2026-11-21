/*
 * dielectric_albedo.h: the reflected directional albedo of a rough GGX
 * dielectric interface (HW4 handout: frosted glass), given complete with
 * roughdielectric.h. The generated lookup is private to
 * roughdielectric.cpp; the pigment sees what that reflection leaves, from
 * either side of any positive index ratio. The average integrates the
 * same piecewise-linear curve that the directional lookup evaluates.
 */
#ifndef DIELECTRIC_ALBEDO_H
#define DIELECTRIC_ALBEDO_H

#include <algorithm>
#include <cmath>

namespace dielectricAlbedo {

const int R = 64, ETA = 257, MU = 129;

inline double roughnessAt(int i)
{
    const double minimum = std::sqrt(0.001), x = (double)i / (R - 1);
    return minimum + (1 - minimum) * x * x;
}

/* A bounded index coordinate: rho = (eta_i - eta_t)/(eta_i + eta_t).
 * Cubic spacing resolves nearly matched indices as well as the limits
 * eta_i/eta_t = 0 and infinity, whose Fresnel reflectance is 1. */
inline double rhoAt(int j)
{
    const double x = 2.0 * j / (ETA - 1) - 1;
    return x * x * x;
}

inline double criticalCosine(double rho)
{
    if (!(rho > 0)) {
        return 0;
    }
    const double invEta = (1 - rho) / (1 + rho);
    return std::sqrt(std::max(0.0, 1 - invEta * invEta));
}

/* Rational cubic spacing resolves polished critical-angle transitions at
 * both endpoints of each segment. */
inline double clustered(double x)
{
    const double a = x * x * x, b = (1 - x) * (1 - x) * (1 - x);
    return a / (a + b);
}

inline double unclustered(double y)
{
    const double a = std::cbrt(std::max(0.0, y));
    const double b = std::cbrt(std::max(0.0, 1 - y));
    return a / (a + b);
}

inline double muAt(int k, double critical)
{
    if (!(critical > 0)) {
        return clustered((double)k / (MU - 1));
    }
    const int middle = (MU - 1) / 2;
    return k <= middle ? critical * clustered((double)k / middle)
                       : critical + (1 - critical) * clustered((double)(k - middle) / middle);
}

inline double muCoordinate(double mu, double critical)
{
    mu = std::min(1.0, std::max(0.0, mu));
    if (!(critical > 0)) {
        return (MU - 1) * unclustered(mu);
    }
    const int middle = (MU - 1) / 2;
    return mu <= critical ? middle * unclustered(mu / critical)
                          : middle + middle * unclustered((mu - critical) / (1 - critical));
}

} // namespace dielectricAlbedo

class RoughDielectricCoat {
public:
    RoughDielectricCoat() : denominator(1) {}
    void set(float roughness, float etaI, float etaT);
    double albedo(float mu) const;
    float factor(float muO, float muI) const;

private:
    const float *rows[4] = {nullptr, nullptr, nullptr, nullptr};
    double weights[4] = {0, 0, 0, 0};
    double critical = 0;
    double denominator;
};

#endif /* DIELECTRIC_ALBEDO_H */
