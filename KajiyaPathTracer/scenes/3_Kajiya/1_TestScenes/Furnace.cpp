/*
 * ECE/CS 285 – HW4 Test Scene: Furnace
 * The white furnace: five white spheres in a sky of radiance 1 in every
 * direction and nothing else. A Lambert sphere, a pigment under a coat
 * (specular 0.04, roughness 0.3), and three conductors of roughness 0.1,
 * 0.5 and 1.0: what each returns of a light that is 1 everywhere.
 * 960 x 320, 256 spp, independent, seed 0, max depth 32.
 *
 * Output: renders/<integrator>/1_TestScenes/Furnace.png
 */

#include "rt.h"
#include "sceneutil.h"

void Furnace(void)
{
    RtPoint eye = {0, 0, 9}, lookat = {0, 0, 0};
    RtVector up = {0, 1, 0};
    RtColor white = {1, 1, 1};
    const float roughness[3] = {0.1f, 0.5f, 1.0f};
    RtMaterial lambert = MATTE(1, 1, 1);
    RtMaterial coated = {{1, 1, 1}, {0.04f, 0.04f, 0.04f}, 0.3f, 0, 0, 1.5f, {0, 0, 0}};

    rtBegin();
        rtFormat(960, 320);
        rtDisplay("1_TestScenes/Furnace");
        rtCamera(eye, lookat, up, 25);
        rtPixelSamples(256);
        rtMaxDepth(32);
        rtBackground(white);
        rtWorldBegin(0);
            rtMaterial(&lambert);
            rtTransformBegin(); rtTranslate(-4.4f, 0, 0); rtSphere(1); rtTransformEnd();
            rtMaterial(&coated);
            rtTransformBegin(); rtTranslate(-2.2f, 0, 0); rtSphere(1); rtTransformEnd();
            for (int i = 0; i < 3; i++) {
                RtMaterial conductor = {{0, 0, 0}, {1, 1, 1}, roughness[i], 1, 0, 1.5f, {0, 0, 0}};
                rtMaterial(&conductor);
                rtTransformBegin(); rtTranslate(2.2f * (float)i, 0, 0); rtSphere(1); rtTransformEnd();
            }
        rtWorldEnd();
    rtEnd();
}
