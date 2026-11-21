/*
 * ECE/CS 285 – HW4 Test Scene: VeachMIS
 * Veach's test of multiple importance sampling (1997, Figure 9.2): four
 * metal plates, roughness 0.035, 0.068, 0.133 and 0.26 from the back to the
 * front, each reflecting four sphere lights of radius 0.03 to 0.9 m and
 * equal power. Render it plain (MIS) and with --ISstrategy light and bsdf.
 * 960 x 640, 64 spp, independent, seed 0.
 *
 * Output: renders/<integrator>/1_TestScenes/VeachMIS.png, and
 *         VeachMIS_light.png, VeachMIS_bsdf.png with --ISstrategy light
 *         and bsdf
 */

#include "rt.h"
#include "sceneutil.h"

void VeachMIS(void)
{
    const Vec3 eye(0, 2, 15), lookat(0, -0.3f, 2.5f);
    const Vec3 lightRow(0, 2.5f, -1.5f); /* the middle of the row of lights */
    RtVector up = {0, 1, 0};
    RtColor black = {0, 0, 0};
    RtMaterial wall = MATTE(0.15f, 0.15f, 0.15f);
    const float radii[4] = {0.03f, 0.1f, 0.3f, 0.9f};
    /* The plates, nearest and lowest first: the roughest in front and the
     * smoothest at the back, as in Veach's image. alpha = roughness^2 goes
     * down by about 3.8 from plate to plate (0.068, 0.018, 0.0046, 0.0012),
     * an even step of gloss, and stays above the 0.001 below which a
     * surface is a mirror. The centers put equal gaps between the plates
     * on the image (39 pixels). */
    const float roughness[4] = {0.26f, 0.133f, 0.068f, 0.035f};
    const Vec3 plates[4] = {Vec3(0, -2.3f, 5.2f), Vec3(0, -1.82f, 4.067f), Vec3(0, -1.28f, 2.788f), Vec3(0, -0.65f, 1.3f)};

    RtPoint eyeP, lookatP;
    eye.store(eyeP);
    lookat.store(lookatP);

    rtBegin();
        rtFormat(960, 640);
        rtDisplay("1_TestScenes/VeachMIS");
        rtCamera(eyeP, lookatP, up, 32);
        rtPixelSamples(64);
        rtBackground(black);
        rtWorldBegin(0);
            /* The lights: equal power, Le r^2 = 0.8, colored as in Veach's
             * image (small to large: pink, pale yellow, pale green, light
             * blue). */
            const RtColor tints[4] = {{1.0f, 0.3f, 0.7f}, {1.0f, 0.9f, 0.3f}, {0.4f, 1.0f, 0.3f}, {0.3f, 0.65f, 1.0f}};
            for (int i = 0; i < 4; i++) {
                const float Le = 0.8f / (radii[i] * radii[i]);
                RtMaterial m = EMITTER(Le * tints[i][0], Le * tints[i][1], Le * tints[i][2]);
                rtMaterial(&m);
                rtTransformBegin();
                    rtTranslate(-3.75f + 2.5f * (float)i, lightRow.y, lightRow.z);
                    rtSphere(radii[i]);
                rtTransformEnd();
            }

            /* The plates: 8 m by 1 m, each facing the bisector of the
             * directions to the camera and to the lights, so the lights'
             * reflections sit on it. */
            for (int k = 0; k < 4; k++) {
                RtMaterial m = {{0, 0, 0}, {0.75f, 0.75f, 0.75f}, roughness[k], 1, 0, 1.5f, {0, 0, 0}};
                rtMaterial(&m);
                const Vec3 c = plates[k];
                const Vec3 n = normalize(normalize(eye - c) + normalize(lightRow - c));
                const Vec3 across(1, 0, 0);
                const Vec3 down = normalize(cross(n, across)); /* in the plate, its lower edge toward the camera */
                Quad(c - across * 4.0f - down * 0.5f, across * 8.0f, down * 1.0f);
            }

            /* A dim backdrop, so the plates' edges read. */
            rtMaterial(&wall);
            Quad(Vec3(-12, 9, -6), Vec3(24, 0, 0), Vec3(0, -14, 0)); /* back wall, facing +z */
            Quad(Vec3(-12, -3, -6), Vec3(24, 0, 0), Vec3(0, 0, 14)); /* floor, facing up */
        rtWorldEnd();
    rtEnd();
}
