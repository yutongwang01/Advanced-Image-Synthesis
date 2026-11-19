#ifndef LIGHTS_H
#define LIGHTS_H

#include "scene.h"
#include "sampler.h"

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

#endif
