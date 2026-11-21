/*
 * warp.cpp: your warps ("Warps" handout; workflow step 6). Write each
 * function as the handout derives it, keeping the name and signature that
 * warp.h declares: the given warptest.cpp calls them. Until you do, each
 * returns zero, and tools/warptest.py fails it.
 *
 * Each warp maps a 2D sample (u.u, u.v) = (xi1, xi2) in [0, 1)^2 onto its
 * domain, and its density function gives the density of what comes out:
 * per area for the disk and the triangle, per solid angle for the four
 * direction warps, whose directions are in a local frame about +z (z the
 * cosine with the axis; the caller turns them into world space). Which of
 * xi1 and xi2 does what is part of the definition: the reference's images
 * depend on it.
 */
#include "warp.h"

#include <algorithm>
#include <cmath>

namespace {
const float PI = 3.14159265358979323846f;
}

/* The disk: Shirley and Chiu's concentric map (Section 3.1), exactly as the
 * handout gives it. The square [-1, 1]^2 is cut along its diagonals into
 * four wedges; in the two where |a| > |b|, r = a and phi = (pi/4)(b/a), in
 * the other two r = b and phi = pi/2 - (pi/4)(a/b). r may be negative:
 * r cos(phi), r sin(phi) place the point right. Density 1/pi per area. */
Vec3 sampleConcentricDisk(Sample2 u)
{
    const float a = 2 * u.u - 1, b = 2 * u.v - 1;
    if (a == 0 && b == 0) {
        return Vec3(0, 0, 0);
    }
    float r, phi;
    if (std::fabs(a) > std::fabs(b)) {
        r = a;
        phi = (PI / 4) * (b / a);
    } else {
        r = b;
        phi = PI / 2 - (PI / 4) * (a / b);
    }
    return Vec3(r * std::cos(phi), r * std::sin(phi), 0);
}
float concentricDiskPdf() { return 1 / PI; }

/* The cosine-weighted hemisphere: Malley's method (Section 3.2). A uniform
 * disk point (x, y) lifted straight up, z = sqrt(max(0, 1 - x^2 - y^2)).
 * The disk's 1/pi per area becomes cos(theta)/pi per solid angle, since a
 * patch d omega projects onto the disk as cos(theta) d omega. The density
 * takes the direction's z, cos(theta), and returns max(cos(theta), 0)/pi. */
Vec3 sampleCosineHemisphere(Sample2 u)
{
    const Vec3 d = sampleConcentricDisk(u);
    const float z = std::sqrt(std::max(0.0f, 1 - d.x * d.x - d.y * d.y));
    return Vec3(d.x, d.y, z);
}
float cosineHemispherePdf(float cosTheta) { return std::max(cosTheta, 0.0f) / PI; }

/* The uniform sphere (Section 3.3): z = cos(theta) uniform on [-1, 1]
 * (Archimedes: d omega = dz dphi) and phi uniform on [0, 2 pi),
 *     z = 1 - 2 xi1,  phi = 2 pi xi2,  (x, y) = sqrt(1 - z^2) (cos phi, sin phi).
 * Density 1/(4 pi). */
Vec3 sampleUniformSphere(Sample2 u)
{
    const float z = 1 - 2 * u.u;
    const float r = std::sqrt(std::max(0.0f, 1 - z * z));
    const float phi = 2 * PI * u.v;
    return Vec3(r * std::cos(phi), r * std::sin(phi), z);
}
float uniformSpherePdf() { return 1 / (4 * PI); }

/* The uniform cone of half-angle thetaMax about +z, given as cosThetaMax
 * (Section 3.4): z uniform on [cosThetaMax, 1],
 *     z = 1 - xi1 (1 - cosThetaMax),  phi = 2 pi xi2.
 * Density 1/(2 pi (1 - cosThetaMax)) inside the cone (its solid angle is
 * 2 pi (1 - cosThetaMax)); the caller returns 0 for a direction with
 * z < cosThetaMax. (The sphere light computes 1 - cosThetaMax its own,
 * precise way in double: "Area Lights" handout; this warp stays as is.) */
Vec3 sampleUniformCone(Sample2 u, float cosThetaMax)
{
    const float z = 1 - u.u * (1 - cosThetaMax);
    const float r = std::sqrt(std::max(0.0f, 1 - z * z));
    const float phi = 2 * PI * u.v;
    return Vec3(r * std::cos(phi), r * std::sin(phi), z);
}
float uniformConePdf(float cosThetaMax) { return 1 / (2 * PI * (1 - cosThetaMax)); }

/* The uniform triangle (Section 3.5): barycentric weights (b0, b1, b2) of
 * the vertices (P0, P1, P2), by the inversion method,
 *     s = sqrt(xi1),  b0 = 1 - s,  b1 = xi2 s,  b2 = 1 - b0 - b1,
 * b2 computed as 1 - b0 - b1, as the reference does ((1 - xi2) s is the
 * same only up to rounding). The point is P0 + b1 e1 + b2 e2: b0 weights
 * P0. Density 2 in the (b0, b1) plane, where the triangle has area 1/2
 * (1/A on a triangle of area A). */
Vec3 sampleUniformTriangle(Sample2 u)
{
    const float s = std::sqrt(u.u);
    const float b0 = 1 - s;
    const float b1 = u.v * s;
    const float b2 = 1 - b0 - b1;
    return Vec3(b0, b1, b2);
}
float uniformTrianglePdf() { return 2; }

/* The normalized Phong lobe of exponent n about +z (Section 3.6),
 * D = (n + 1)/(2 pi) cos^n(theta): the marginal's CDF 1 - cos^(n+1)(theta)
 * inverted (with 1 - xi1 as uniform as xi1),
 *     cos(theta) = xi1^(1/(n+1)),  phi = 2 pi xi2.
 * The density takes the direction's z and n: (n + 1)/(2 pi) z^n for
 * z > 0, else 0. It also evaluates directions from light sampling, which
 * need not have come from this warp. */
Vec3 samplePhongLobe(Sample2 u, float n)
{
    const float z = std::pow(u.u, 1 / (n + 1));
    const float r = std::sqrt(std::max(0.0f, 1 - z * z));
    const float phi = 2 * PI * u.v;
    return Vec3(r * std::cos(phi), r * std::sin(phi), z);
}
float phongLobePdf(float cosTheta, float n)
{
    if (!(cosTheta > 0)) {
        return 0;
    }
    return (n + 1) / (2 * PI) * std::pow(cosTheta, n);
}
