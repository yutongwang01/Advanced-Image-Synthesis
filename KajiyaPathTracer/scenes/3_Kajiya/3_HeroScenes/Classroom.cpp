/*
 * ECE/CS 285 – HW4 Hero Scene: Classroom
 * An empty classroom on a sunny afternoon, the sun low through the blinds
 * of eight windows. The sun lights only striped patches and the sky only
 * what faces the windows: the rest of the room is bounce light.
 * 1.25 million triangles. 1920 x 1080, 512 spp, halton, seed 0, depth 12.
 * Model: Classroom by Christophe Seux (blender.org demo files, CC0).
 *
 * Output: renders/<integrator>/3_HeroScenes/Classroom.png
 */

#include "rt.h"
#include "obj.h"
#include "sceneutil.h"

void Classroom(void)
{
    /* Blender's camera (renderCam): a 25 mm lens on a 32 x 18 mm sensor at
     * 1920 x 1080, 39.6 degrees tall; the up vector carries its slight
     * roll. f/2.08 is a lens radius of 0.025 / (2 x 2.083) = 6 mm, focused
     * on the "focus" empty by the blackboard, 7.96 m along the view. */
    RtPoint eye = {2.5764f, 1.09448f, 4.46575f}, lookat = {2.32435f, 1.10567f, 3.4981f};
    RtVector up = {0.00283f, 0.99994f, 0.01084f};

    /* Blender's view is Standard, look None, exposure 0: nothing to fold
     * into the lights. */

    /* The sun: strength 15 W/m^2 times Cycles' blackbody at 5800 K,
     * (1.109, 0.976, 0.918) at luminance 1. Its 1.7 degree disc becomes a
     * direction, so the stripes through the blinds are sharp. */
    RtVector sunDir = {-0.87395f, -0.47895f, -0.0825f};
    RtColor sunE = {16.64f, 14.64f, 13.77f};

    /* The corridor's ceiling light, seen through the door: 60 W of
     * (1, 0.889, 0.755), I = P c / (4 pi). */
    RtPoint hallPos = {-3.77245f, 2.81513f, -2.19698f};
    RtColor hallI = {4.775f, 4.244f, 3.606f};

    /* Left out: exterior_fillLight, a 1963 W area light outside the window
     * wall. It faces up and away from the room, toward the eaves, which no
     * path from the room reached past the original's sky plane; Cycles
     * renders the room the same with it or without it. */

    rtBegin();
        rtFormat(1920, 1080);
        rtDisplay("3_HeroScenes/Classroom");
        rtCamera(eye, lookat, up, 39.6f);
        rtLens(0.006f, 7.96f);
        rtPixelSamples(512);
        rtSampler("halton");
        rtMaxDepth(12);
        /* The sky, baked in the renderer's own lat-long convention
         * (envmap.cpp), so no turn and no scale. */
        rtEnvironment("env/classroom_sky.hdr", 1, 0);
        rtWorldBegin(0);
            rtDirectionalLight(sunDir, sunE);
            rtPointLight(hallPos, hallI);

            /* The model was exported in the exporter's coat mode (its Ks the
             * weight of a Fresnel coat, not the reflectance head on):
             * OBJ_COAT_MODE turns it into what kajiya reads (rt.h,
             * specular). */
            ObjMesh *room = obj_load("models/classroom/classroom.mesh");
            ObjDraw(room, OBJ_COAT_MODE);
            obj_free(room);
            ObjDrawInstances("models/classroom/instances.txt", OBJ_COAT_MODE);
        rtWorldEnd();
    rtEnd();
}
