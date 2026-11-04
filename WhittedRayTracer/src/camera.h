/*
 * camera.h: the pinhole camera (Workflow, step 5; Viewing Geometry handout).
 */
#ifndef CAMERA_H
#define CAMERA_H

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

#endif

