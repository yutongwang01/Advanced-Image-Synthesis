#include "camera.h"

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