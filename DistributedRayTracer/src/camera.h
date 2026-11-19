/*
 * camera.h: the pinhole camera (Workflow, step 5; Viewing Geometry handout).
 */
#ifndef CAMERA_H
#define CAMERA_H

#include "sampler.h"
#include "scene.h"
#include "vec.h"

struct Camera {
    Vec3 eye;
    Vec3 forward;
    Vec3 right;
    Vec3 up;

    float tanHalfFovy;
    float aspect;
};

Camera makeCamera(
    const Vec3& eye,
    const Vec3& lookat,
    const Vec3& up,
    float fovy,
    int width,
    int height
);

Ray generateCameraRay(
    const Camera& camera,
    int x,
    int y,
    int width,
    int height
);

/* A camera ray through any point of the image plane, given in raster
 * coordinates: (rasterX, rasterY) in [0, width) x [0, height), x to the
 * right and y down, pixel (x, y) covering [x, x + 1) x [y, y + 1). The
 * sampled pixel loop passes (x + u, y + v) with the film sample (u, v);
 * generateCameraRay is the same ray at the pixel's center, (x + 0.5, y + 0.5). */
Ray generateSampledCameraRay(
    const Camera& camera,
    float rasterX,
    float rasterY,
    int width,
    int height
);

/* The thin lens (HW3, workflow step 7): the ray through raster point
 * (rasterX, rasterY) from a point on the lens. The pinhole ray's direction
 * d meets the focus plane, focusDistance along forward, at
 *     Pf = eye + d * focusDistance / (d . forward);
 * the lens sample goes through the concentric disk warp to (dx, dy), the
 * ray starts at O = eye + lensRadius (dx right + dy up) and aims at Pf,
 * D = normalize(Pf - O): everything on the focus plane is sharp. A
 * lensRadius of 0 is a pinhole: generateSampledCameraRay's ray itself. */
Ray generateThinLensCameraRay(
    const Camera& camera,
    float rasterX,
    float rasterY,
    int width,
    int height,
    Sample2 lens,
    float lensRadius,
    float focusDistance
);

#endif

