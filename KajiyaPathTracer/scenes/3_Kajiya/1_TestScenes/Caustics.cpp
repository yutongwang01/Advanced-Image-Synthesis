/*
 * ECE/CS 285 – HW4 Test Scene: Caustics
 * A clear glass ball and an upright pane of blue glass on a matte floor
 * before a wall, under a point light and a small sphere lamp. The light
 * each sends through the glass is a caustic: kajiya finds the lamp's
 * slowly and the point light's never. Finding both is extra credit.
 * 960 x 540, 256 spp, halton, seed 0.
 *
 * Output: renders/<integrator>/1_TestScenes/Caustics.png
 */

#include "rt.h"
#include "sceneutil.h"

void Caustics(void)
{
    RtPoint eye = {0, 2.3f, 6.2f}, lookat = {0, 0.35f, 0};
    RtVector up = {0, 1, 0};
    RtColor black = {0, 0, 0};
    RtMaterial matte = MATTE(0.7f, 0.7f, 0.7f);
    RtMaterial glass = GLASS;
    /* blue glass: an albedo's chroma tints what a dielectric transmits */
    RtMaterial blueGlass = {{0.25f, 0.65f, 0.9f}, {1, 1, 1}, 0, 0, 1, 1.5f, {0, 0, 0}};
    RtMaterial lamp = EMITTER(3800, 3400, 2800); /* radius 5 cm: 30 W/sr straight out, a little warm */
    RtPoint lightAt = {-3.0f, 3.2f, 1.6f};
    RtColor lightI = {26, 28, 31}; /* a little cool, so the two lights' caustics can be told apart */

    rtBegin();
        rtFormat(960, 540);
        rtDisplay("1_TestScenes/Caustics");
        rtCamera(eye, lookat, up, 35);
        rtPixelSamples(256);
        rtSampler("halton");
        rtAmbient(black);
        rtBackground(black);
        rtWorldBegin(0);
            rtPointLight(lightAt, lightI);
            rtMaterial(&lamp);
            rtTransformBegin(); rtTranslate(2.6f, 2.8f, -2.2f); rtSphere(0.05f); rtTransformEnd();

            rtMaterial(&matte);
            Floor(16);
            Quad(Vec3(-8, 6, -6.5f), Vec3(16, 0, 0), Vec3(0, -6, 0)); /* the back wall, past the frame's sides */

            rtMaterial(&glass);
            rtTransformBegin(); rtTranslate(-1.25f, 0.5f, 0.2f); rtSphere(0.5f); rtTransformEnd();

            rtMaterial(&blueGlass);
            rtTransformBegin();
                rtTranslate(1.35f, 0, -0.1f);
                rtRotate(-18, 0, 1, 0);
                /* 1 cm off the floor: a face in the floor's plane is at the floor's own distance along
                 * a ray, and which of the two a renderer then hits is its own affair */
                Box(Vec3(-0.6f, 0.01f, -0.09f), Vec3(0.6f, 1.2f, 0.09f));
            rtTransformEnd();
        rtWorldEnd();
    rtEnd();
}
