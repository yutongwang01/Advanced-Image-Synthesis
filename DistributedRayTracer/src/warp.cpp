/*
 * warp.cpp: your warps ("Warps" handout; workflow step 6). Write each
 * function as the handout derives it, keeping the name and signature that
 * warp.h declares: the given warptest.cpp calls them. Until you do, each
 * returns zero, and tools/warptest.py fails it.
 */
#include "warp.h"

#include <cmath>

/* TODO: the concentric map (the handout gives the code). */
Vec3 sampleConcentricDisk(Sample2 u)
{
    (void)u;
    return Vec3(0, 0, 0);
}
float concentricDiskPdf()
{
    return 0;
}

/* TODO: Malley's method, the disk point lifted onto the hemisphere. */
Vec3 sampleCosineHemisphere(Sample2 u)
{
    (void)u;
    return Vec3(0, 0, 0);
}
float cosineHemispherePdf(float cosTheta)
{
    (void)cosTheta;
    return 0;
}

/* TODO: a direction uniform on the sphere. */
Vec3 sampleUniformSphere(Sample2 u)
{
    (void)u;
    return Vec3(0, 0, 0);
}
float uniformSpherePdf()
{
    return 0;
}

/* TODO: a direction uniform in the cone of cosThetaMax about +z. */
Vec3 sampleUniformCone(Sample2 u, float cosThetaMax)
{
    (void)u;
    (void)cosThetaMax;
    return Vec3(0, 0, 0);
}
float uniformConePdf(float cosThetaMax)
{
    (void)cosThetaMax;
    return 0;
}

/* TODO: barycentric coordinates uniform over the triangle. */
Vec3 sampleUniformTriangle(Sample2 u)
{
    (void)u;
    return Vec3(0, 0, 0);
}
float uniformTrianglePdf()
{
    return 0;
}

/* TODO: a direction from the normalized Phong lobe of exponent n. */
Vec3 samplePhongLobe(Sample2 u, float n)
{
    (void)u;
    (void)n;
    return Vec3(0, 0, 0);
}
float phongLobePdf(float cosTheta, float n)
{
    (void)cosTheta;
    (void)n;
    return 0;
}
