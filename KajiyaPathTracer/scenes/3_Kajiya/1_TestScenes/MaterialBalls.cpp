/*
 * ECE/CS 285 – HW4 Test Scene: MaterialBalls
 * A 4 x 4 grid of spheres on a grey floor under a partly cloudy sky with
 * a high sun. A row each, front to back: Lambert (albedo 0.1 to 0.9),
 * gold (roughness 0.05, 0.15, 0.3, 0.6), a coat of those roughnesses
 * over red, and glass (ior 1.33, 1.5, 1.8, 2.4).
 * 960 x 640, 256 spp, stratified, seed 0, max depth 12.
 * Sky: kloofendal_48d_partly_cloudy_puresky_2k.hdr (Poly Haven, CC0).
 *
 * Output: renders/<integrator>/1_TestScenes/MaterialBalls.png
 */

#include "rt.h"
#include "sceneutil.h"

void MaterialBalls(void)
{
    RtPoint eye = {0, 4.2f, 7.6f}, lookat = {0, 0.2f, 0.3f};
    RtVector up = {0, 1, 0};
    RtMaterial floorMat = MATTE(0.35f, 0.35f, 0.35f);
    const float albedos[4] = {0.1f, 0.3f, 0.6f, 0.9f};
    const float roughness[4] = {0.05f, 0.15f, 0.3f, 0.6f};
    const float iors[4] = {1.33f, 1.5f, 1.8f, 2.4f};

    rtBegin();
        rtFormat(960, 640);
        rtDisplay("1_TestScenes/MaterialBalls");
        rtCamera(eye, lookat, up, 28);
        rtPixelSamples(256);
        rtSampler("stratified");
        rtMaxDepth(12);
        rtEnvironment("env/kloofendal_48d_partly_cloudy_puresky_2k.hdr", 1, 90);
        rtWorldBegin(0);
            rtMaterial(&floorMat);
            Floor(30);
            for (int row = 0; row < 4; row++) {
                for (int i = 0; i < 4; i++) {
                    RtMaterial m;
                    if (row == 0) {
                        m = MATTE(albedos[i], albedos[i], albedos[i]);
                    } else if (row == 1) {
                        RtMaterial gold = {{0, 0, 0}, {1.0f, 0.71f, 0.29f}, roughness[i], 1, 0, 1.5f, {0, 0, 0}};
                        m = gold;
                    } else if (row == 2) {
                        RtMaterial coat = {{0.6f, 0.05f, 0.04f}, {1, 1, 1}, roughness[i], 0, 0, 1.5f, {0, 0, 0}};
                        m = coat;
                    } else {
                        RtMaterial glass = GLASS;
                        glass.ior = iors[i];
                        m = glass;
                    }
                    rtMaterial(&m);
                    rtTransformBegin();
                        rtTranslate(-1.65f + 1.1f * (float)i, 0.42f, 1.6f - 1.1f * (float)row);
                        rtSphere(0.42f);
                    rtTransformEnd();
                }
            }
        rtWorldEnd();
    rtEnd();
}
