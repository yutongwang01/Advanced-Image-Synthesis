/*
 * ECE/CS 285 – HW4 Hero Scene: Colonnato
 * A palace interior after the Palazzo dell'Università in Genoa: a marble
 * stair between arcades under groin vaults, and a courtyard beyond the
 * arches. The sun reaches a few hard-edged patches and the sky comes
 * through the arches; the vaults and the stair are lit by bounce light.
 * 3.7 million triangles. 1920 x 1080, 512 spp, halton, seed 0, depth 12.
 * Model: Colonnato by lucasassone (blendswap 11277, CC0).
 *
 * Output: renders/<integrator>/3_HeroScenes/Colonnato.png
 */

#include "rt.h"
#include "obj.h"
#include "sceneutil.h"

void Colonnato(void)
{
    /* Blender's active camera (Camera.002): an 18 mm lens on a 32 mm
     * sensor, 83.3 degrees across, framed 1280 x 1024; at 16:9 the width
     * is kept and fovy = 2 atan(tan(83.27 / 2) 9 / 16) = 53.13 degrees.
     * The up vector carries the camera's half-degree roll. No lens. */
    RtPoint eye = {6.52767f, -0.94873f, 15.58954f}, lookat = {6.12149f, -0.79929f, 14.68804f};
    RtVector up = {0.05278f, 0.98873f, 0.14011f};

    /* The sun: Blender strength 2 W/m^2, white, so E = 2; it travels toward
     * the camera, 32 degrees down. Its 143 degree Blender angle casts
     * almost no shadow in Cycles; a directional light is a delta light,
     * so here the patches are hard, as in the 2.5 original. */
    RtVector sunDir = {-0.23735f, -0.53608f, 0.81011f};
    /* Blender's numbers render dim (a median of 0.17 in sRGB), so the sun
     * and the sky are both 2.8 times Blender's, a stop and a half. */
    RtColor sunE = {5.6f, 5.6f, 5.6f};

    rtBegin();
        rtFormat(1920, 1080);
        rtDisplay("3_HeroScenes/Colonnato");
        rtCamera(eye, lookat, up, 53.13f);
        rtPixelSamples(512);
        rtSampler("halton");
        rtMaxDepth(12);
        /* The world is a flat color, (0.7, 0.79, 0.87) at strength 1, under
         * kajiya a uniform environment light; times the stop and a half
         * above. Blender's Standard view, exposure 0, gamma 1 is the plain
         * sRGB this renderer writes, so no other exposure factor. */
        rtBackground(Color(1.96f, 2.21f, 2.44f));
        rtWorldBegin(0);
            rtDirectionalLight(sunDir, sunE);

            /* The file's seven spot lamps, left out: in the Blender 2.5
             * original they drove halos (sun shafts in dusty air); with no
             * medium they add seven faint spots on the floor. 200 W each,
             * (1, 0.731, 0.323), so I = 200 c / (4 pi) W/sr, cone half
             * angle 0.75 degrees, penumbra 0.11, about 95 m out along the
             * sun. To try them, delete the lines that open and close the
             * block below. */
            /*
            RtColor spotI = {15.92f, 11.63f, 5.14f};
            RtPoint spotPos[7] = {
                {19.57731f, 51.81458f, -72.75124f}, {25.56409f, 51.81458f, -72.75124f},
                {13.57731f, 51.81458f, -72.75124f}, {14.60973f, 51.81458f, -72.75124f},
                {26.59651f, 51.81458f, -72.75124f}, {20.60973f, 51.81458f, -72.75124f},
                {26.27172f, 51.81458f, -88.88416f}};
            for (int i = 0; i < 7; i++)
                rtSpotLight(spotPos[i], sunDir, spotI, 0.75f, 0.11f, 0);
            */

            /* The model was exported in the exporter's coat mode (its Ks the
             * weight of a Fresnel coat, not the reflectance head on):
             * OBJ_COAT_MODE turns it into what kajiya reads (rt.h,
             * specular). */
            ObjMesh *hall = obj_load("models/colonnato/colonnato.mesh");
            ObjDraw(hall, OBJ_COAT_MODE);
            obj_free(hall);
            ObjDrawInstances("models/colonnato/instances.txt", OBJ_COAT_MODE);
        rtWorldEnd();
    rtEnd();
}
