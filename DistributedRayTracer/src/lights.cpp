#include "lights.h"

#include "integrator.h"
#include "warp.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace {

bool sampleDeltaLight(
    const Light &light,
    const Vec3 &P,
    LightSample &ls)
{
    ls.delta = true;
    ls.pdf = 1.0f;

    if (light.type == LightType::Directional) {
        ls.wi = -light.direction;
        ls.Le = light.intensity;
        return true;
    }

    const Vec3 toLight = light.position - P;
    const float d2 = lengthSquared(toLight);

    if (!(d2 > 0.0f))
        return false;

    const float d = std::sqrt(d2);

    ls.wi = toLight / d;
    ls.target = light.position;
    ls.Le = light.intensity / d2;

    if (light.type == LightType::Spot) {
        const float c = dot(light.direction, -ls.wi);

        float cone = 0.0f;

        if (c >= light.cosInner) {
            cone = 1.0f;
        } else if (c <= light.cosOuter) {
            cone = 0.0f;
        } else {
            const float t =
                (c - light.cosOuter) /
                (light.cosInner - light.cosOuter);

            cone = t * t * (3.0f - 2.0f * t);
        }

        float reach = 1.0f;

        if (light.maxDistance > 0.0f) {
            if (d >= light.maxDistance) {
                reach = 0.0f;
            } else {
                const float r =
                    1.0f -
                    d2 / (light.maxDistance * light.maxDistance);

                reach = r * r;
            }
        }

        if (!(cone * reach > 0.0f))
            return false;

        ls.Le = ls.Le * (cone * reach);
    }

    return true;
}

const float kPi = 3.14159265358979323846f;

// Area Lights 6.1: from outside, a direction uniform in the cone the
// sphere subtends; 1 - cos(thetaMax) and the near side in double.
bool sampleSphereOutside(
    const Scene &scene,
    const Light &light,
    const Vec3 &P,
    Sample2 u,
    float time,
    int depth,
    LightSample &ls)
{
    const Vec3 c = light.position;
    const float r = light.sphereRadius;

    // c - P, every component in double from the floats
    const double cx = (double)c.x - P.x, cy = (double)c.y - P.y, cz = (double)c.z - P.z;
    const double d2 = cx * cx + cy * cy + cz * cz;
    const double r2 = (double)r * r;
    const double d = std::sqrt(d2);
    const Vec3 w((float)(cx / d), (float)(cy / d), (float)(cz / d));

    const double oneMinusCosMax = (r2 / d2) / (1 + std::sqrt(1 - r2 / d2));
    const double a = u.u * oneMinusCosMax;
    const float sinTheta = (float)std::sqrt(a * (2 - a)), phi = 2 * kPi * u.v;
    const Vec3 local(sinTheta * std::cos(phi), sinTheta * std::sin(phi), (float)(1 - a));
    Vec3 t, b;
    orthonormalBasis(w, t, b);
    ls.wi = normalize(t * local.x + b * local.y + w * local.z);

    // the near side: every input double, aq the direction's own |wi|^2
    const double wx = ls.wi.x, wy = ls.wi.y, wz = ls.wi.z;
    const double aq = wx * wx + wy * wy + wz * wz;
    const double bq = -(wx * cx + wy * cy + wz * cz);
    const double cq = cx * cx + cy * cy + cz * cz - r2;
    const double disc = std::max(0.0, bq * bq - aq * cq);
    const float dist = (float)((-bq - std::sqrt(disc)) / aq);

    ls.pdf = (float)(1 / (2 * kPi * oneMinusCosMax));
    ls.target = P + ls.wi * dist;
    ls.Le = light.emission;
    ls.delta = false;

    // Section 7: the sampled point is a hit; run its cutout and shader.
    const DrawCall &dc = scene.drawCalls[(size_t)light.drawCall];
    if (!dc.cutout.fn && !dc.surface.fn)
        return true;
    const Vec3 n = normalize(dc.worldToObject.transposed().vector(dc.worldToObject.point(ls.target)));
    Hit hit;
    hit.hit = true;
    hit.t = dist;
    hit.P = ls.target;
    hit.Ng = n;
    hit.N = n;
    hit.drawCall = light.drawCall;
    hit.prim = light.emitterSphere;
    hit.entering = dot(n, ls.wi) < 0.0f;
    Ray ray(P, ls.wi, 0.0f, std::numeric_limits<float>::infinity(), depth + 1);
    ray.time = time;
    RtShadeContext ctx;
    fillShadeContext(scene, ray, hit, ctx);
    if (dc.cutout.fn) {
        ctx.params = dc.cutout.data();
        if (dc.cutout.fn(&ctx) == 0)
            return false;
    }
    if (dc.surface.fn) {
        ctx.params = dc.surface.data();
        dc.surface.fn(&ctx);
        ls.Le = Vec3(ctx.m.emission);
    }
    return true;
}

// Area Lights 6.2: from inside, the disc of radius r at the center turned
// toward P, polar warp; preweighted, so delta (no division by the pdf).
bool sampleSphereInside(
    const Light &light,
    const Vec3 &P,
    Sample2 u,
    LightSample &ls)
{
    const Vec3 c = light.position;
    const float r = light.sphereRadius;
    const float d = length(P - c);
    if (!(d > 0.0f))
        return false; // exactly at the center: no disc normal

    const Vec3 n = (P - c) / d;
    const float rho = r * std::sqrt(u.u), phi = 2 * kPi * u.v; // polar, not concentric
    Vec3 t, b;
    orthonormalBasis(n, t, b);
    const Vec3 y = c + t * (rho * std::cos(phi)) + b * (rho * std::sin(phi));

    const Vec3 toY = y - P;
    const float dist2 = lengthSquared(toY), dist = std::sqrt(dist2);
    if (!(dist > 0.0f))
        return false;

    ls.wi = toY / dist;
    const float cosY = d / dist;
    ls.Le = light.emission * (kPi * r * r * cosY / dist2 * (d * d + r * r) / (r * r));
    ls.pdf = 1.0f;
    ls.target = y;
    ls.delta = true;
    return true;
}

} // namespace


bool sampleLight(
    const Scene &scene,
    const Light &light,
    const Vec3 &P,
    Sample2 u,
    float time,
    int depth,
    LightSample &ls)
{
    ls = LightSample();

    // 1. Ordinary delta lights.
    if (light.type == LightType::Point ||
        light.type == LightType::Directional ||
        light.type == LightType::Spot) {

        return sampleDeltaLight(light, P, ls);
    }

    // 2. Sphere emitters (Step 10): the cone from outside, the disc from inside.
    if (light.type == LightType::Emissive && light.sphereEmitter) {
        const double cx = (double)light.position.x - P.x;
        const double cy = (double)light.position.y - P.y;
        const double cz = (double)light.position.z - P.z;
        const double r = light.sphereRadius;
        if (cx * cx + cy * cy + cz * cz <= r * r)
            return sampleSphereInside(light, P, u, ls);
        return sampleSphereOutside(scene, light, P, u, time, depth, ls);
    }

    // Everything else must be a mesh emitter.
    if (light.type != LightType::Emissive ||
        !light.meshEmitter) {
        return false;
    }

    // 3. Validate the mesh emitter.
    if (!(light.totalArea > 0.0f) ||
        light.emitterTriangles.empty() ||
        light.cumulativeAreas.empty() ||
        light.emitterMesh < 0) {
        return false;
    }

    // 4. Select a triangle proportionally to its area.
    const float areaSample = u.u * light.totalArea;

    auto it = std::upper_bound(
        light.cumulativeAreas.begin(),
        light.cumulativeAreas.end(),
        areaSample
    );

    size_t index = static_cast<size_t>(
        it - light.cumulativeAreas.begin()
    );

    if (index >= light.emitterTriangles.size())
        index = light.emitterTriangles.size() - 1;

    const float areaBefore =
        index == 0 ? 0.0f : light.cumulativeAreas[index - 1];

    const float areaAfter = light.cumulativeAreas[index];
    const float triangleArea = areaAfter - areaBefore;

    // Remap the first random coordinate to this triangle.
    const float remappedU = triangleArea > 0.0f
        ? std::min((areaSample - areaBefore) / triangleArea, std::nextafter(1.0f, 0.0f))
        : 0.0f;

    const Sample2 triangleSample = {remappedU, u.v};

    // 5. Uniform sample in the selected triangle.
    const Vec3 bary = sampleUniformTriangle(triangleSample);

    const int triangleIndex = light.emitterTriangles[index];
    const Triangle &tri = scene.triangles[
        static_cast<size_t>(triangleIndex)
    ];

    const Mesh &mesh = scene.meshes[
        static_cast<size_t>(tri.mesh)
    ];

    const Vec3 y =
        tri.p0 + tri.e1 * bary.y + tri.e2 * bary.z;

    // 6. Direction from shading point to sampled light point.
    const Vec3 toLight = y - P;
    const float d2 = lengthSquared(toLight);

    if (!(d2 > 0.0f))
        return false;

    const float d = std::sqrt(d2);
    const Vec3 wi = toLight / d;

    // 7. Geometric normal and one-sided emission.
    Vec3 Ng = normalize(cross(tri.e1, tri.e2));
    Vec3 N = Ng;

    if (mesh.hasNormals()) {
        const size_t i0 = static_cast<size_t>(tri.v[0]);
        const size_t i1 = static_cast<size_t>(tri.v[1]);
        const size_t i2 = static_cast<size_t>(tri.v[2]);

        N = normalize(
            mesh.N[i0] * bary.x +
            mesh.N[i1] * bary.y +
            mesh.N[i2] * bary.z
        );

        if (dot(Ng, N) < 0.0f)
            Ng = -Ng;
    }

    const float cosLight = dot(Ng, -wi);

    if (!(cosLight > 0.0f))
        return false;

    // 8. Area PDF -> solid angle PDF.
    const float pdf =
        d2 / (light.totalArea * cosLight);

    const DrawCall &dc = scene.drawCalls[
        static_cast<size_t>(mesh.drawCall)
    ];

    if (!dc.cutout.fn && !dc.surface.fn) {
        ls.wi = wi;
        ls.Le = light.emission;
        ls.pdf = pdf;
        ls.target = y;
        ls.delta = false;
        return true;
    }

    // 9. Build a hit at the sampled emitter point.
    // This allows us to reuse the existing shader context.
    Hit hit;
    hit.hit = true;
    hit.t = d;
    hit.P = y;
    hit.Ng = Ng;
    hit.N = N;
    hit.drawCall = mesh.drawCall;
    hit.prim = static_cast<int>(scene.spheres.size()) +
               triangleIndex;
    hit.entering = dot(Ng, wi) < 0.0f;
    hit.b1 = bary.y;
    hit.b2 = bary.z;

    // A light sample stands for a ray leaving P one bounce deeper, not a
    // shadow ray: depth + 1, so a lamp hidden from the camera and from
    // shadow rays (a cutout accepting depth > 0) still lights the scene.
    Ray ray(
        P,
        wi,
        0.0f,
        std::numeric_limits<float>::infinity(),
        depth + 1
    );

    ray.time = time;

    RtShadeContext ctx;
    fillShadeContext(scene, ray, hit, ctx);

    // 10. Cutout shader: a removed point emits nothing.
    if (dc.cutout.fn) {
        ctx.params = dc.cutout.data();

        if (dc.cutout.fn(&ctx) == 0)
            return false;
    }

    // 11. Surface shader: use emission after shading.
    if (dc.surface.fn) {
        ctx.params = dc.surface.data();
        dc.surface.fn(&ctx);
    }

    // 12. Return the light sample.
    ls.wi = wi;
    ls.Le = Vec3(ctx.m.emission);
    ls.pdf = pdf;
    ls.target = y;
    ls.delta = false;

    return true;

}

float lightCosAngularRadius(const Light &light, const Vec3 &P)
{
    if (light.type != LightType::Emissive)
        return 1.0f; // a point, spot or directional light: no size
    const float d = length(light.position - P);
    if (!(d > light.extent))
        return -1.0f; // inside the sphere that holds it: it fills the view
    return std::sqrt(1.0f - light.extent * light.extent / (d * d));
}
