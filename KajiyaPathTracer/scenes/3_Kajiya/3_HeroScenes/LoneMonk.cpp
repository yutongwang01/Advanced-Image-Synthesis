/*
 * ECE/CS 285 – HW4 Hero Scene: LoneMonk
 * A monastery library under a brick vault, looking out through an arch
 * into a sunlit cloister. No lamps, and no direct light under the vault:
 * it is lit by sunlight bounced off the courtyard, ten stops below it.
 * 6.1 million triangles. 1920 x 1080, 512 spp, halton, seed 0, depth 12.
 * Model: Lone Monk by Carlo Bergonzini (Monorender), blender.org demo
 * files, CC0.
 *
 * Output: renders/<integrator>/3_HeroScenes/LoneMonk.png
 */

#include "rt.h"
#include "obj.h"
#include "sceneutil.h"

#include <algorithm>
#include <cstring>

/* shaders ------------------------------------------------------------ */

/* TINTED: the file's node trees darken and desaturate most image maps
 * with a Hue/Saturation node before they become a base color, which an
 * MTL cannot say. The albedo is the map's color with its saturation
 * scaled about its luminance, times a value, clamped to 1. */
typedef struct {
    RtInt texture;      /* the map, from rtMakeTexture */
    RtFloat value;      /* the color is scaled by this */
    RtFloat saturation; /* 1 keeps the map's colors, 0 makes them grey */
} TintParams;

static void TINTED(RtShadeContext *c)
{
    const TintParams *p = (const TintParams *)c->params;
    RtFloat rgba[4];
    rtTexture(p->texture, c->u, c->v, rgba);
    const float lum = 0.2126f * rgba[0] + 0.7152f * rgba[1] + 0.0722f * rgba[2];
    for (int i = 0; i < 3; i++) {
        c->m.albedo[i] = std::min(1.0f, std::max(0.0f, p->value * (lum + p->saturation * (rgba[i] - lum))));
    }
}

/* The tinted materials by MTL name, with the value and saturation of
 * the last Hue/Saturation node in each tree (where a tree also mixes the
 * map with colors, the value is the ratio of the result's mean to the
 * map's); the arcade's are lighter than the file's, so that the vault
 * reads. linear: the map is read without sRGB decoding, as the file
 * reads it. */
static const struct Tint {
    const char *material;
    float value, saturation;
    int linear;
} TINTS[] = {
    {"brick_clay", 0.6f, 0.7f, 0}, /* the vaults and arches */
    {"brick_marble", 0.5f, 0.36f, 0},
    {"brick_marble_smooth", 0.5f, 0.36f, 0},
    {"column_marble", 0.7f, 0.9f, 0},
    {"church_brick", 1.0f, 1.0f, 1},
    {"grass_-_ground", 0.1f, 1.0f, 0},
    {"leather_-_book_-_cover_1", 0.85f, 0.8f, 0},
    {"leather_-_book_-_cover_2", 0.75f, 0.8f, 0},
    {"leather_-_book_-_cover_3", 0.37f, 1.0f, 0},
    {"outdoor_pavement", 0.2f, 1.0f, 0},
    {"paper_-_page", 1.5f, 0.5f, 0},
    {"pen", 0.1f, 0.8f, 0},
    {"plaster_med_old", 1.2f, 1.0f, 0},
    {"plaster_no_brick", 0.8f, 1.0f, 0},
    {"stone_pavement", 0.7f, 1.0f, 0}, /* the arcade's floor */
    {"wood_-_chair", 0.5f, 1.0f, 0},
    {"wood_-_charcoal", 0.3f, 1.0f, 0},
};
static const size_t NTINTS = sizeof TINTS / sizeof TINTS[0];

/* What the hook needs: the static OBJ, whose materials (the whole MTL,
 * which every OBJ of the model shares) give each map's path and, once
 * ObjDraw has drawn a material there, its texture handle. A material
 * only the instances use, or a linear map, is loaded here, once. */
typedef struct {
    const ObjMesh *model;
    RtInt handle[sizeof TINTS / sizeof TINTS[0]]; /* -2: not looked up yet, -1: no map */
} TintHook;

static void LoneMonkMaterials(const char *material, void *user)
{
    TintHook *hook = (TintHook *)user;
    if (!hook || !hook->model) {
        return;
    }
    for (size_t k = 0; k < NTINTS; k++) {
        if (std::strcmp(material, TINTS[k].material) != 0) {
            continue;
        }
        if (hook->handle[k] == -2) {
            hook->handle[k] = -1;
            for (int i = 0; i < hook->model->nmaterials; i++) {
                const ObjMaterial &om = hook->model->materials[i];
                if (std::strcmp(om.name, material) != 0 || !om.map_Kd[0]) {
                    continue;
                }
                if (!TINTS[k].linear && om.kdHandle >= 0) {
                    hook->handle[k] = om.kdHandle; /* what ObjDraw loaded for the static OBJ */
                } else {
                    hook->handle[k] = rtMakeTexture(om.map_Kd, !TINTS[k].linear);
                }
            }
        }
        if (hook->handle[k] >= 0) {
            TintParams p = {hook->handle[k], TINTS[k].value, TINTS[k].saturation};
            rtSurface(TINTED, &p, sizeof p);
        }
        return;
    }
}

/* scene -------------------------------------------------------------- */

void LoneMonk(void)
{
    /* The camera: at the back of the arcade, 0.9 m up, looking out along
     * -z through the arch, tilted 4 degrees up to take in the vault; 47.5
     * degrees high (a 23 mm lens on a 36 mm sensor). */
    RtPoint eye = {20.0f, 0.9f, -7.51293f}, lookat = {20.0f, 0.969756f, -8.510494f};
    RtVector up = {0, 1, 0};

    /* The sun: the sky node's disc (elevation 53, rotation 150 degrees, 1
     * degree across) as a directional light. Toward the sun is Blender's
     * (cos e sin r, cos e cos r, sin e) = (0.301, -0.521, 0.799), here
     * (0.301, 0.799, 0.521); the light travels the other way. The
     * irradiance is the disc integrated over its solid angle at sun
     * intensity 1, (148.6, 122.6, 96.3); on the ground, times sin 53
     * degrees, it is ten times the sky's (7.5, 9.4, 13.6). */
    RtVector sunDir = {-0.3009f, -0.7986f, -0.5212f};
    const RtColor sunE = {148.6f, 122.6f, 96.3f};

    /* Exposure: the sun and the sky are scaled by this, and the PNG goes
     * through the filmic curve at +3.5 stops (rtTonemap below), whose
     * shoulder holds the sunlit plaster (about 5) while the vault (about
     * 0.003) comes up. */
    const float EXPOSURE = 0.3f;
    RtColor sun = {sunE[0] * EXPOSURE, sunE[1] * EXPOSURE, sunE[2] * EXPOSURE};

    rtBegin();
        rtFormat(1920, 1080);
        rtDisplay("3_HeroScenes/LoneMonk");
        rtTonemap("filmic", 3.5f);
        rtCamera(eye, lookat, up, 47.52f);
        /* A 2.3 mm aperture (f/11 on a 25 mm lens) focused 2.64 m ahead:
         * the near books, the table and the far roofs all close to sharp. */
        rtLens(0.00114f, 2.636f);
        rtPixelSamples(512);
        rtSampler("halton");
        rtMaxDepth(12);
        /* The sky without its sun, baked straight into envmap.cpp's
         * convention (u = atan2(x, z) / 2 pi, v = acos(y) / pi in this
         * y-up space), so rotateY is 0. Blender's own panorama is the
         * mirror image of that, which no rotateY could undo, so the bake
         * reorders its columns. */
        rtEnvironment("env/lonemonk_sky.hdr", EXPOSURE, 0);
        rtWorldBegin(0);
            rtDirectionalLight(sunDir, sun);

            ObjMesh *model = obj_load("models/lonemonk/lonemonk.mesh");
            TintHook hook;
            hook.model = model;
            for (size_t k = 0; k < NTINTS; k++) {
                hook.handle[k] = -2;
            }
            /* The model was exported in the exporter's coat mode (its Ks the
             * weight of a Fresnel coat, not the reflectance head on):
             * OBJ_COAT_MODE turns it into what kajiya reads (rt.h,
             * specular). */
            ObjDraw(model, OBJ_COAT_MODE, LoneMonkMaterials, &hook);
            ObjDrawInstances("models/lonemonk/instances.txt", OBJ_COAT_MODE, LoneMonkMaterials, &hook);
            obj_free(model);
        rtWorldEnd();
    rtEnd();
}
