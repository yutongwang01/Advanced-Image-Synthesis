/*
 * ECE/CS 285 – HW4 Hero Scene: HallBench
 * A long corridor seen down its length: four tall windows on the left, a
 * yellow wall of doors on the right, a checkered marble floor. The sun
 * lays slanted patches on the floor, and all the rest is bounce light.
 * 5.0 million triangles. 1080 x 1500, 1024 spp, halton, seed 0, depth 12.
 * Model: Hall Bench by Charles Nandeya Ehouman (Sharlybg), LuxCoreRender
 * example scenes, CC0.
 *
 * Output: renders/<integrator>/3_HeroScenes/HallBench.png
 */

#include "rt.h"
#include "obj.h"
#include "sceneutil.h"

void HallBench(void)
{
    /* LuxCore's camera (scene.scn), z up turned y up: it looks straight
     * down the corridor, level, from 1.48 m up, 1.8 m short of the
     * corridor's open end (the dark box around the camera keeps the sky
     * out of that end). */
    RtPoint eye = {0.44902f, 1.48373f, 9.07351f}, lookat = {0.44902f, 1.48373f, 8.07351f};
    RtVector up = {0, 1, 0};

    /* The sun: LuxCore's sphere light, power 2000 W at efficacy 17 lm/W,
     * gain (1, 0.964, 0.927): Phi = gain x 2000 x 17 (PointLight::
     * Preprocess), radiance Phi / (4 pi^2 r^2) = gain x 53.8 for r = 4
     * (SphereLight::Illuminate), times the image pipeline's 2
     * (TONEMAP_LINEAR), folded in here: Le = 2 x 53.8 x gain. */
    const RtColor sunLe = {107.65f, 103.73f, 99.75f};

    rtBegin();
        rtFormat(1080, 1500);
        rtDisplay("3_HeroScenes/HallBench");
        rtCamera(eye, lookat, up, 49.13f);
        rtPixelSamples(1024);
        rtSampler("halton");
        rtMaxDepth(12);
        /* The sky: LuxCore's sky2 (Hosek and Wilkie), turbidity 4, sun at
         * the zenith, gain 1e-4, no ground. Its cosine-weighted mean seen
         * from a vertical window is a radiance of (0.92, 1.19, 1.62), times
         * the pipeline's 2: about a quarter of the sun's light indoors. */
        rtBackground(Color(1.84f, 2.39f, 3.24f));
        rtWorldBegin(0);
            RtMaterial sun = EMITTER(sunLe[0], sunLe[1], sunLe[2]);
            rtMaterial(&sun);
            rtTransformBegin();
                rtTranslate(-8.76628f, 10.47412f, 0.31022f);
                rtSphere(4);
            rtTransformEnd();

            /* The model was exported in the exporter's coat mode (its Ks the
             * weight of a Fresnel coat, not the reflectance head on):
             * OBJ_COAT_MODE turns it into what kajiya reads (rt.h,
             * specular). */
            ObjMesh *hall = obj_load("models/hallbench/hallbench.mesh");
            ObjDraw(hall, OBJ_COAT_MODE);
            obj_free(hall);
        rtWorldEnd();
    rtEnd();
}
