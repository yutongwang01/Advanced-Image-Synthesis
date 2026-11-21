/*
 * ECE/CS 285 – HW4 Test Scene: NormalMap
 * A flat wall of round studs that exist in the shading normal alone: its
 * shader tilts the normal about the hit's tangents dPdu and dPdv, as a
 * normal map does, under a lamp that rakes across it. Without the
 * tangents the wall stays flat. Tests them before JunkShop needs them.
 * 480 x 360, 64 spp, halton, seed 0.
 *
 * Output: renders/<integrator>/1_TestScenes/NormalMap.png
 */

#include "rt.h"
#include "sceneutil.h"

#include <cmath>

/* A grid of 8 x 6 studs over the wall's (u, v). Inside a stud the normal
 * tilts away from the stud's center, most at half its radius and not at
 * all at its center and at its rim: N + a T + b B, normalized, with T and
 * B the unit tangents along dPdu and dPdv. With zero tangents the normal
 * is left alone. */
static void STUDS(RtShadeContext *c)
{
    const Vec3 N(c->N), dPdu(c->dPdu), dPdv(c->dPdv);
    if (!(lengthSquared(dPdu) > 0 && lengthSquared(dPdv) > 0)) {
        return;
    }
    const float x = c->u * 8 - std::floor(c->u * 8) - 0.5f; /* the place in the stud's cell, -0.5 to 0.5 */
    const float y = c->v * 6 - std::floor(c->v * 6) - 0.5f;
    const float r = std::sqrt(x * x + y * y), R = 0.38f;
    if (r > 0 && r < R) {
        const float slope = 1.2f * std::sin(3.14159265f * r / R);
        const Vec3 T = normalize(dPdu), B = normalize(dPdv);
        const Vec3 n = normalize(N + T * (slope * x / r) + B * (slope * y / r));
        if (dot(n, Vec3(c->Ng)) > 0) { /* never tilted past the surface */
            n.store(c->N);
        }
    }
}

void NormalMap(void)
{
    RtPoint eye = {0, 0, 4.2f}, lookat = {0, 0, 0};
    RtVector up = {0, 1, 0};
    RtColor sky = {0.03f, 0.03f, 0.03f};
    RtMaterial wall = MATTE(0.8f, 0.8f, 0.8f);
    RtMaterial lamp = EMITTER(480, 450, 400);

    rtBegin();
        rtFormat(480, 360);
        rtDisplay("1_TestScenes/NormalMap");
        rtCamera(eye, lookat, up, 35);
        rtPixelSamples(64);
        rtSampler("halton");
        rtBackground(sky);
        rtWorldBegin(0);
            /* the lamp: far to the upper left and near the wall's plane, so its light rakes */
            rtMaterial(&lamp);
            rtTransformBegin(); rtTranslate(-8, 5, 2.4f); rtSphere(0.5f); rtTransformEnd();

            /* the wall, facing the camera: (u, v) = (0, 0) at its upper left corner, v running down */
            rtMaterial(&wall);
            rtSurface(STUDS, NULL, 0);
            Quad(Vec3(-1.6f, 1.2f, 0), Vec3(3.2f, 0, 0), Vec3(0, -2.4f, 0));
            rtSurface(NULL, NULL, 0);
        rtWorldEnd();
    rtEnd();
}
