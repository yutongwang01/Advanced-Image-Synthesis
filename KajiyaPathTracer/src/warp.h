/*
 * warp.h: the warps ("Warps" handout), given complete: the names and
 * signatures of the six warps you write in warp.cpp, each a map from a 2D
 * sample in [0, 1)^2 to a point on its domain, with its density. Keep them
 * as they are: the given warptest.cpp calls them. Directions are in a
 * local frame with the axis of symmetry along +z; the caller turns them
 * into world space. A constant density takes no point; the others take
 * the direction's cosine (and the lobe its exponent). The caller checks
 * the support. Which of u and v does what is part of the definition:
 * swapping them keeps a renderer correct, but its images no longer match
 * the reference's.
 */
#ifndef WARP_H
#define WARP_H

#include "sampler.h"
#include "vec.h"

/* A point in the unit disk, (x, y, 0), by Shirley and Chiu's concentric
 * map (1997): the square's concentric squares go to the disk's concentric
 * circles, so strata stay compact. Density 1 / pi over the disk's area. */
Vec3 sampleConcentricDisk(Sample2 u);
float concentricDiskPdf();

/* A direction in the hemisphere about +z with density cos(theta) / pi per
 * steradian, by Malley's method: a concentric-disk point lifted onto the
 * hemisphere. */
Vec3 sampleCosineHemisphere(Sample2 u);
float cosineHemispherePdf(float cosTheta);

/* A direction uniform on the unit sphere: z = 1 - 2u, phi = 2 pi v.
 * Density 1 / (4 pi). */
Vec3 sampleUniformSphere(Sample2 u);
float uniformSpherePdf();

/* A direction uniform in the cone about +z of half-angle thetaMax, given
 * as cosThetaMax (in [-1, 1)): cos(theta) = 1 - u (1 - cosThetaMax),
 * phi = 2 pi v. Density 1 / (2 pi (1 - cosThetaMax)) inside the cone;
 * the sphere light's sampler ("Area Lights" handout). */
Vec3 sampleUniformCone(Sample2 u, float cosThetaMax);
float uniformConePdf(float cosThetaMax);

/* Barycentric coordinates (b0, b1, b2) uniform over a triangle: b0 = 1 -
 * sqrt(u), b1 = v sqrt(u), b2 = 1 - b0 - b1. Density 2 over the (b0, b1)
 * plane, where the triangle has area 1/2; on a triangle of area A it is
 * 1 / A. */
Vec3 sampleUniformTriangle(Sample2 u);
float uniformTrianglePdf();

/* A direction about +z from the normalized Phong lobe of exponent n:
 * cos(theta) = u^(1 / (n + 1)), phi = 2 pi v. Density
 * (n + 1) / (2 pi) cos^n(theta) per steradian. */
Vec3 samplePhongLobe(Sample2 u, float n);
float phongLobePdf(float cosTheta, float n);

/* --warptest WARP N [PARAM], given complete in warptest.cpp: N samples of
 * a warp, one "x y z pdf" line each, for tools/warptest.py. Have your
 * warpTest() in rt.cpp call it: return runWarpTest(o); */
struct Options;
int runWarpTest(const Options &o);

#endif /* WARP_H */
