/*
 * shaders.cpp: the four required shaders (handout, Section 5) and the
 * noise function BUMPY is built on.
 *
 * CONSTANT, STANDARD, CHECKERBOARD, perlinNoise and BUMPY are stubs for you
 * to fill in, in that order. Every scene file binds the shaders by name
 * (RiSurface(CHECKERBOARD, &cp, sizeof cp)), and some call perlinNoise
 * directly, so keep the names and the parameter structs in shaders.h as
 * they are.
 *
 * A shader is an ordinary function. The renderer fills in the inputs of the
 * RtShaderContext (Ri.h) at every grid vertex and calls the shader; a
 * surface shader writes Ci and Oi, a displacement shader moves P.
 */
#include "shaders.h"

#include <algorithm>
#include <cmath>

/* ------------------------------------------------------------- CONSTANT */

/* The surface color, unlit: Ci = Cs and Oi = Os. The simplest shader, and
 * the first to write. FirstPrimitive binds it, so that its picture is the
 * plain RiColor whether or not your renderer runs shaders yet: once you
 * add shading, FirstPrimitive must look exactly as it did before. */
void CONSTANT(RtShaderContext *c)
{
    /* TODO: copy the color and the opacity. */
    (void)c;
}

/* ------------------------------------------------------------- STANDARD */

/* Ambient, plus Lambertian diffuse, plus Phong specular, with the three
 * lights and the coefficients given in the handout (Section 5):
 *   Ci = Cs (ka + kd sum max(N.L, 0) I C) + ks sum max(R.V, 0)^32 I C
 *   Oi = Os
 * Everything is in camera space: the eye is at the origin, so the direction
 * from P toward the eye is -P. Normalize N and the light directions. Write
 * Ci as the color itself, not multiplied by Oi: the hider applies the
 * opacity when it composites (Section 7). */
void STANDARD(RtShaderContext *c)
{
    /* TODO: the lighting. For now, the surface color, unlit. */
    CONSTANT(c);
}

/* --------------------------------------------------------- CHECKERBOARD */

/* The check (u, v) falls in is (floor(u sizex), floor(v sizey)), with u = 1
 * and v = 1 counted in the last check. Checks where i + j is even are
 * "light" and run the light sub-shader, the others the dark one. A NULL
 * light shader means STANDARD; a NULL dark shader means STANDARD with
 * Cs = black, so that only its highlights show.
 *
 * You don't choose the sub-shaders: the scene does, in the CheckerParams it
 * binds (shaders.h), just as it passes any shader its parameters. The
 * model scenes' floors, for example, do
 *     CheckerParams cp = { 2, 2, Material, &MATTE_GREY,
 *                                Material, &POLISHED_BLACK };
 *     RiSurface(CHECKERBOARD, &cp, sizeof cp);
 * and most scenes write { 8, 4 }, leaving both NULL. Your RiSurface copied
 * that struct, so here c->params points at it. A shader is a plain
 * function, so run a check's sub-shader by calling it, with c->params
 * pointed at the sub-shader's own parameters during the call:
 *     const CheckerParams *p = (const CheckerParams *)c->params;
 *     c->params = p->light_params;
 *     p->light(c);
 *     c->params = p;    (put back for whoever called CHECKERBOARD) */
void CHECKERBOARD(RtShaderContext *c)
{
    /* TODO: the checks. For now, every check is lit as the light one. */
    STANDARD(c);
}

/* ---------------------------------------------------------- perlinNoise */

/* Ken Perlin's improved noise, from his paper "Improving Noise" (SIGGRAPH
 * 2002); his reference implementation, a page of Java, is at
 * https://mrl.cs.nyu.edu/~perlin/noise/. BUMPY sums octaves of it, and so
 * do some scene files' own shaders (Fbm in sceneutil.h), so until it works
 * those surfaces come out smooth and plain.
 *
 * Follow his algorithm exactly: his permutation of 0..255, his fade curve
 * 6t^5 - 15t^4 + 10t^3 and his gradient function. Any other noise looks
 * just as good, but your bumps will not match reference_renders/. The
 * result is smooth and signed, about [-1, 1], and exactly 0 at every
 * integer point. Points may be negative or far from the origin, so make
 * sure the lattice index wraps into 0..255 for those too. */
double perlinNoise(double x, double y, double z)
{
    /* TODO: the noise. For now, none, so BUMPY moves nothing. */
    (void)x, (void)y, (void)z;
    return 0;
}

/* ---------------------------------------------------------------- BUMPY */

/* Displace P along the unit normal N by
 *   h = amplitude * sum(w_e * perlinNoise(2^e * Po)) / sum(w_e)
 * over e = min_freq_exp .. max_freq_exp, with w_e = 2^(min_freq_exp - e):
 * each octave counts half as much as the one below it, and dividing by the
 * total weight keeps |h| <= amplitude whatever the octaves. Evaluate the
 * noise at Po (object space), so the bumps stick to the object as it
 * moves. The parameters are a BumpParams (shaders.h) at c->params. */
void BUMPY(RtShaderContext *c)
{
    /* TODO: the displacement. For now, P stays where it is. */
    (void)c;
}
