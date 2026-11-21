/*
 * ECE/CS 285 – HW4 Compare Scene: SponzaGI
 * HW2's Sponza from the same camera, its point lights and ambient term
 * replaced by a sky whose sun falls into the atrium through the open
 * roof: everything inside is lit by sunlight bounced off the stone.
 * 1920 x 1080, 256 spp, stratified, seed 0, max depth 8.
 * Model: Crytek Sponza (Frank Meinl, Crytek; McGuire's Computer Graphics
 * Archive, CC BY 3.0). Sky: Poly Haven's kloofendal_48d (CC0).
 *
 * Output: renders/<integrator>/2_CompareScenes/SponzaGI.png
 */

#include "rt.h"
#include "obj.h"
#include "sceneutil.h"

void SponzaGI(void)
{
    RtPoint eye = {-11.5f, 2.3f, -0.8f}, lookat = {4.0f, 2.6f, 0.3f};
    RtVector up = {0, 1, 0};

    rtBegin();
        rtFormat(1920, 1080);
        rtDisplay("2_CompareScenes/SponzaGI");
        rtCamera(eye, lookat, up, 55);
        rtPixelSamples(256);
        rtSampler("stratified");
        rtMaxDepth(8);
        rtEnvironment("env/kloofendal_48d_partly_cloudy_puresky_2k.hdr", 1, -124); /* the sun ahead, along the atrium */
        rtWorldBegin(0);
            ObjMesh *sponza = obj_load("models/sponza/sponza.mesh");
            rtTransformBegin();
                rtScale(0.01f, 0.01f, 0.01f);
                ObjDraw(sponza);
            rtTransformEnd();
            obj_free(sponza);
        rtWorldEnd();
    rtEnd();
}
