#ifndef LIGHTS_H
#define LIGHTS_H

#include "scene.h"
#include "sampler.h"

#include <cmath>

// The result of sampling one light toward a shading point P.
struct LightSample {
    Vec3 wi = Vec3(0.0f);       // Unit direction from P to light
    Vec3 Le = Vec3(0.0f);       // Incident radiance / delta irradiance
    float pdf = 0.0f;          // PDF per solid angle (1 for delta)
    Vec3 target = Vec3(0.0f);   // Shadow ray endpoint
    bool delta = false;        // Whether sample is delta
};

// Sample one light. Visibility is checked separately in Step 9.
bool sampleLight(
    const Scene &scene,
    const Light &light,
    const Vec3 &P,
    Sample2 u,
    float time,
    int depth,
    LightSample &ls
);

// The orthonormal frame (t, b, n) about the unit n (Warps handout,
// Section 2), exactly as the reference builds it: the lights' cone and
// disc, the glossy lobe and ambient occlusion all turn their directions
// into the world with it.
inline void orthonormalBasis(const Vec3 &n, Vec3 &t, Vec3 &b)
{
    const Vec3 a = std::fabs(n.x) < 0.9f ? Vec3(1, 0, 0) : Vec3(0, 1, 0);
    t = normalize(cross(a, n));
    b = cross(n, t);
}

// The cosine of the light's angular radius as seen from P (Area Lights,
// Section 3): 1 for a delta light, sqrt(1 - extent^2 / d^2) for an emitter
// at distance d from its position, and -1 from inside that sphere, where it
// fills the view. The Cook Shading Model's light rule compares it with a
// glossy lobe's half-width.
float lightCosAngularRadius(const Light &light, const Vec3 &P);

#endif
