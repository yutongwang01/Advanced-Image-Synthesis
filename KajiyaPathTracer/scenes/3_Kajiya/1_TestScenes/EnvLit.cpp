/*
 * ECE/CS 285 – HW4 Test Scene: EnvLit
 * A matte, a rough gold and a glass sphere and the Stanford bunny on a
 * light floor, lit only by a sky whose sun is a tiny disk holding half
 * the map's power: the test of sampling the sky by its brightness.
 * Render it plain and with --env-sampling uniform.
 * 960 x 540, 256 spp, stratified, seed 0.
 * Model: the Stanford bunny (Turk and Levoy, 1994), models/bunny/.
 *
 * Output: renders/<integrator>/1_TestScenes/EnvLit.png, and
 *         EnvLit_uniform.png with --env-sampling uniform
 */

#include "rt.h"
#include "obj.h"
#include "sceneutil.h"

void EnvLit(void)
{
    RtPoint eye = {0, 1.8f, 6.5f}, lookat = {0, 0.55f, 0};
    RtVector up = {0, 1, 0};
    RtMaterial floorMat = MATTE(0.6f, 0.58f, 0.55f);
    RtMaterial matte = MATTE(0.7f, 0.25f, 0.2f);
    RtMaterial gold = {{0, 0, 0}, {1.0f, 0.71f, 0.29f}, 0.3f, 1, 0, 1.5f, {0, 0, 0}};
    RtMaterial glass = GLASS;
    RtMaterial bunnyMat = MATTE(0.75f, 0.75f, 0.72f);

    rtBegin();
        rtFormat(960, 540);
        rtDisplay("1_TestScenes/EnvLit");
        rtCamera(eye, lookat, up, 35);
        rtPixelSamples(256);
        rtSampler("stratified");
        rtMaxDepth(10);
        rtEnvironment("env/kloofendal_48d_partly_cloudy_puresky_2k.hdr", 0.6f, 90);
        rtWorldBegin(0);
            rtMaterial(&floorMat);
            Floor(30);
            rtMaterial(&matte);
            rtTransformBegin(); rtTranslate(-2.1f, 0.5f, 0.2f); rtSphere(0.5f); rtTransformEnd();
            rtMaterial(&gold);
            rtTransformBegin(); rtTranslate(-0.7f, 0.5f, -0.6f); rtSphere(0.5f); rtTransformEnd();
            rtMaterial(&glass);
            rtTransformBegin(); rtTranslate(0.75f, 0.5f, 0.5f); rtSphere(0.5f); rtTransformEnd();

            /* The bunny, 1.2 m tall: the scan is about 0.155 m tall with
             * its feet at y = 0.033. */
            ObjMesh *bunny = obj_load("models/bunny/bunny.mesh");
            rtMaterial(&bunnyMat);
            rtTransformBegin();
                rtTranslate(2.2f, 0, -0.3f);
                rtRotate(-30, 0, 1, 0);
                rtScale(7.5f, 7.5f, 7.5f);
                rtTranslate(0.017f, -0.033f, -0.002f);
                ObjDraw(bunny, OBJ_KEEP_MATERIAL);
            rtTransformEnd();
            obj_free(bunny);
        rtWorldEnd();
    rtEnd();
}
