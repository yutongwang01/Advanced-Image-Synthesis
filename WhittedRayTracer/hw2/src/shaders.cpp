/*
 * shaders.cpp: the four required shaders (HW2 handout: shaders); perlinNoise is
 * in noise.cpp. Given complete in both kits: HW1 had you write their like,
 * and here the renderer's side, running them at a hit, is yours (Workflow,
 * step 12). Scenes bind the shaders by name (rtSurface(CHECKER, &cp,
 * sizeof cp)), so the names and the parameter structs in shaders.h stay
 * as they are. A shader is an ordinary function: the renderer fills the
 * inputs of the RtShadeContext (rt.h) at a hit and calls it; a surface
 * shader edits c->m, a cutout shader returns 0 where the surface does not
 * exist. A shader knows nothing of lights and keeps no state (threads).
 */
#include "shaders.h"

#include <algorithm>
#include <cmath>

/* TEXTURED ------------------------------------------------------------- */

/* The albedo from an image: c->m.albedo = rtTexture(albedoTex, u, v).rgb,
 * or left alone when albedoTex is -1. Reads TexturedParams; ignores
 * alphaTex (that is ALPHA_CUTOUT's). */
void TEXTURED(RtShadeContext *c)
{
    const TexturedParams *p = (const TexturedParams *)c->params;
    if (p->albedoTex < 0)
        return;
    RtFloat rgba[4];
    rtTexture(p->albedoTex, c->u, c->v, rgba);
    c->m.albedo[0] = rgba[0];
    c->m.albedo[1] = rgba[1];
    c->m.albedo[2] = rgba[2];
}

/* CHECKER -------------------------------------------------------------- */

/* The check (u, v) falls in is (floor(u sizex), floor(v sizey)), with u = 1
 * and v = 1 counted in the last check. Checks where i + j is even get
 * material a, the others material b, whole (albedo, specular, roughness,
 * transmission, everything). Reads CheckerParams. */

namespace {

/* Which of `size` checks x falls in; x = 1 counts in the last one, and
 * coordinates outside [0, 1] keep tiling. */
int checkIndex(float x, int size)
{
    int i = (int)std::floor(x * (float)size);
    if (i == size && x <= 1)
        i = size - 1;
    return i;
}

/* True on the a checks (i + j even), false on the b checks. */
bool onCheckA(const RtShadeContext *c, const CheckerParams *p)
{
    return ((checkIndex(c->u, p->sizex) + checkIndex(c->v, p->sizey)) & 1) == 0;
}

} // namespace

void CHECKER(RtShadeContext *c)
{
    const CheckerParams *p = (const CheckerParams *)c->params;
    c->m = onCheckA(c, p) ? p->a : p->b;
}

/* ALPHA_CUTOUT --------------------------------------------------------- */

/* Solid (1) where the mask is >= threshold, a hole (0) elsewhere. The mask
 * is alphaTex's red channel when alphaTex >= 0, else albedoTex's alpha
 * channel. Reads CutoutParams. Runs on every candidate hit of every ray,
 * shadow rays included, so the holes show in the shadows too. */
RtInt ALPHA_CUTOUT(RtShadeContext *c)
{
    const CutoutParams *p = (const CutoutParams *)c->params;
    RtFloat rgba[4];
    float mask;
    if (p->tex.alphaTex >= 0) {
        rtTexture(p->tex.alphaTex, c->u, c->v, rgba);
        mask = rgba[0];
    } else if (p->tex.albedoTex >= 0) {
        rtTexture(p->tex.albedoTex, c->u, c->v, rgba);
        mask = rgba[3];
    } else {
        return 1; /* no mask at all: solid */
    }
    return mask >= p->threshold ? 1 : 0;
}

/* CHECKER_CUTOUT ------------------------------------------------------- */

/* The same checks as CHECKER: solid on the a checks, a hole on the b
 * checks. Reads CheckerParams (the two materials are ignored). */
RtInt CHECKER_CUTOUT(RtShadeContext *c)
{
    const CheckerParams *p = (const CheckerParams *)c->params;
    return onCheckA(c, p) ? 1 : 0;
}
