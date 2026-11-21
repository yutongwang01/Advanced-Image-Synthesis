#include "camera.h"

#include "warp.h"

#include <cmath>

Camera makeCamera(
    const Vec3& eye,
    const Vec3& lookat,
    const Vec3& up,
    float fovy,
    int width,
    int height
) {
    Camera camera;
    camera.eye = eye;

    // forward = normalize(lookat - eye)
    camera.forward = normalize(lookat - eye);
    // right = normalize(cross(forward, up))
    camera.right = normalize(cross(camera.forward, up));
    // up = cross(right, forward)
    camera.up = cross(camera.right, camera.forward);
    // aspect = width / height
    camera.aspect = float(width) / float(height);
    // tanHalfFovy = tan(fovy * pi / 360)
    camera.tanHalfFovy = std::tan(fovy * 3.14159265358979323846f / 360.0f);

    return camera;
}

Ray generateCameraRay(
    const Camera& camera,
    int x,
    int y,
    int width,
    int height
) {
    // Map the pixel center (x+0.5, y+0.5) to [-1,1] screen coordinates; y points down, so it is flipped
    const float ndcX = 2.0f * (x + 0.5f) / float(width) - 1.0f;
    const float ndcY = 1.0f - 2.0f * (y + 0.5f) / float(height);

    // Scale by aspect and tanHalfFovy
    const float sx = ndcX * camera.aspect * camera.tanHalfFovy;
    const float sy = ndcY * camera.tanHalfFovy;

    // direction = normalize(forward + sx*right + sy*up)
    const Vec3 direction = normalize(camera.forward + camera.right * sx + camera.up * sy);

    // Return Ray(camera.eye, direction)
    return Ray(camera.eye, direction);
}

Ray generateSampledCameraRay(
    const Camera& camera,
    float rasterX,
    float rasterY,
    int width,
    int height
) {
    // Map the raster point (rasterX, rasterY) to [-1,1] screen coordinates; y points down, so it is flipped
    const float ndcX = 2.0f * rasterX / float(width) - 1.0f;
    const float ndcY = 1.0f - 2.0f * rasterY / float(height);

    // Scale by aspect and tanHalfFovy
    const float sx = ndcX * camera.aspect * camera.tanHalfFovy;
    const float sy = ndcY * camera.tanHalfFovy;

    // direction = normalize(forward + sx*right + sy*up)
    const Vec3 direction = normalize(camera.forward + camera.right * sx + camera.up * sy);

    // Return Ray(camera.eye, direction)
    return Ray(camera.eye, direction);
}

Ray generateThinLensCameraRay(
    const Camera& camera,
    float rasterX,
    float rasterY,
    int width,
    int height,
    Sample2 lens,
    float lensRadius,
    float focusDistance
) {
    const Ray pinhole = generateSampledCameraRay(camera, rasterX, rasterY, width, height);

    // A pinhole: the ray as it was
    if (lensRadius == 0.0f)
        return pinhole;

    // 1. The focus point: the focus distance is measured along forward, not along the oblique ray,
    //    so (Pf - eye) . forward = focusDistance gives t = focusDistance / (d . forward)
    const float t = focusDistance / dot(pinhole.d, camera.forward);
    const Vec3 Pf = pinhole.o + pinhole.d * t;

    // 2. The lens sample on the unit disk (the concentric map, Warps handout 3.1)
    const Vec3 disk = sampleConcentricDisk(lens);

    // 3. The ray starts on the lens, in the camera's right-up plane
    const Vec3 origin = pinhole.o + (camera.right * disk.x + camera.up * disk.y) * lensRadius;

    // 4. And aims at the focus point; the pinhole ray's other fields (tMin, tMax, time, depth) are kept
    Ray ray = pinhole;
    ray.o = origin;
    ray.d = normalize(Pf - origin);
    return ray;
}
