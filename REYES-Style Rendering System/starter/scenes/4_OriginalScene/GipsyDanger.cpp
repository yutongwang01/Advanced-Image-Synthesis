/*
 * Original scene: "Gipsy Danger" -- a close, low-angle shot of the Jaeger 
 *
 * Assets: the Gipsy Danger OBJ model and its seven textures under
 * models/GipsyDanger/ (see README for source).  The hangar, rain,
 * fog, beams, glows, sky and mountain are REYES geometry plus scene-local
 * shaders.
 *
 * Renderer features exercised by this scene:
 *   - polygon meshes (OBJ with vertex normals/st, quads, boxes)
 *   - textures (RiMakeTexture / RiTexture on the hero armor)
 *   - scene-local surface shaders (dirty concrete, hero, fog, beams, glows,
 *     storm sky, mountain, vignette), built on the renderer's perlinNoise()
 *   - transparency (fog layers, rain streaks, light beams, glows, vignette)
 *   - depth output + shadow map: a depth pass from the floodlight (RiDisplay
 *     "zfile"), looked up with zfile_visibility(); the Jaeger's shadow falls
 *     on the left wall
 *
 * Output: renders/4_OriginalScene/GipsyDanger.png
 */

#include <filesystem>
#include "Ri.h"
#include "obj.h"
#include "sceneutil.h"
#include "shadowmap.h"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace {

const int kXRes = 960;
const int kYRes = 600;
const int kSamples = 4;
const float kFov = 39.0f;

/* Close, low camera looking steeply up at the Jaeger: the head sits near the
 * top of the frame, the knees just below the bottom edge. */
const double EYE[3] = {1.15, 0.45, -2.05};
const double TARGET[3] = {0.45, 1.55, 0.00};

/* Points on the robot (world space), triangulated from two test renders. */
const float REACTOR[3]    = {0.22f, 1.95f, -0.30f};
const float HEAD_LAMP[3]  = {0.04f, 2.54f, -0.02f};
/* Shoulder lamps, placed on the top front edge of each shoulder pad (fitted
 * to a test render from this camera). */
const float SHOULDER_R[3] = {0.53f, 2.07f, -0.25f};
const float SHOULDER_L[3] = {-0.16f, 2.20f, -0.49f};
const float ROBOT_CENTER[3] = {0.15f, 1.30f, -0.15f};

/* The shadow-casting floodlight: right of and just behind the camera, so the
 * robot's shadow lands on the left wall beside it. */
const float KEY_WORLD[3] = {3.6f, 1.6f, -3.2f};

/* Both sit just outside the frame: only their beams and the light they
 * throw are seen, not the lamps themselves. */
const float LAMP_A[3] = {820.0f, -70.0f, 6.0f};   /* big lamp, above top right */
const float LAMP_B[3] = {1040.0f, 175.0f, 5.0f};  /* lamp beyond the right edge */
const float BACKGLOW[3] = {400.0f, 300.0f, 8.0f}; /* haze behind the robot */

/* Camera basis, filled by SetupCamera(). */
float gEye[3], gF[3], gR[3], gU[3];

struct PointLight {
    float pos[3];       /* camera space (what the shaders see in P) */
    float color[3];
    float intensity;
    float radius;       /* distance at which attenuation is 1/2 */
};
PointLight gEnv[3];     /* lamp A, lamp B, weak front-left fill */
PointLight gLocal[3];   /* reactor, right shoulder, head */
PointLight gKey;        /* the shadow-casting floodlight */
float gReactorCam[3];   /* reactor center in camera space, for its emission */

/* Shadow map of gKey, set between the two passes (NULL during the depth
 * pass); gCamToWorld takes the shaders' camera-space P back to world space. */
const ZFile *gShadowMap = NULL;
double gCamToWorld[4][4];

/* ------------------------------------------------------------------ */
/* Math helpers                                                        */
/* ------------------------------------------------------------------ */

float Clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }
float Mix(float a, float b, float t) { return a + (b - a) * t; }

float Smoothstep(float a, float b, float x)
{
    const float t = Clamp01((x - a) / (b - a));
    return t * t * (3.0f - 2.0f * t);
}

float Dot3(const float a[3], const float b[3])
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

void Cross3(const float a[3], const float b[3], float out[3])
{
    out[0] = a[1] * b[2] - a[2] * b[1];
    out[1] = a[2] * b[0] - a[0] * b[2];
    out[2] = a[0] * b[1] - a[1] * b[0];
}

void Normalize3(float v[3])
{
    const float length = std::sqrt(Dot3(v, v));
    if (length > 0.0f) {
        v[0] /= length;
        v[1] /= length;
        v[2] /= length;
    }
}

float TanHalfFovY(void)
{
    return std::tan(0.5f * kFov * 3.14159265f / 180.0f);
}

float TanHalfFovX(void)
{
    return TanHalfFovY() * static_cast<float>(kXRes) / kYRes;
}

void SetupCamera(void)
{
    for (int i = 0; i < 3; ++i) {
        gEye[i] = static_cast<float>(EYE[i]);
        gF[i] = static_cast<float>(TARGET[i] - EYE[i]);
    }
    Normalize3(gF);
    const float up[3] = {0.0f, 1.0f, 0.0f};
    Cross3(up, gF, gR);
    Normalize3(gR);
    Cross3(gF, gR, gU);
}

/* World point -> camera space (x right, y up, z forward), as shaders see P. */
void ToCam(const float w[3], float out[3])
{
    const float v[3] = {w[0] - gEye[0], w[1] - gEye[1], w[2] - gEye[2]};
    out[0] = Dot3(v, gR);
    out[1] = Dot3(v, gU);
    out[2] = Dot3(v, gF);
}

/* Pixel position + depth along the view axis -> world point. */
void ScreenPoint(float px, float py, float depth, float out[3])
{
    const float sx = (px - 0.5f * kXRes) / (0.5f * kXRes) * TanHalfFovX() * depth;
    const float sy = (0.5f * kYRes - py) / (0.5f * kYRes) * TanHalfFovY() * depth;
    for (int i = 0; i < 3; ++i)
        out[i] = gEye[i] + gR[i] * sx + gU[i] * sy + gF[i] * depth;
}

void ScreenPoint(const float spec[3], float out[3])
{
    ScreenPoint(spec[0], spec[1], spec[2], out);
}

/* Nudge a point toward the camera (keeps glows in front of the armor). */
void TowardCamera(const float p[3], float dist, float out[3])
{
    float d[3] = {gEye[0] - p[0], gEye[1] - p[1], gEye[2] - p[2]};
    Normalize3(d);
    for (int i = 0; i < 3; ++i)
        out[i] = p[i] + d[i] * dist;
}

float Rand01(unsigned int &seed)
{
    seed = seed * 1664525u + 1013904223u;
    return static_cast<float>(seed >> 8) / 16777216.0f;
}

/* ------------------------------------------------------------------ */
/* Lighting                                                            */
/* ------------------------------------------------------------------ */

void Accumulate(const PointLight &light, const float P[3], const float n[3],
                const float v[3], float gloss,
                float diffuse[3], float spec[3], float gain = 1.0f)
{
    if (gain <= 0.0f)
        return;
    float l[3] = {light.pos[0] - P[0], light.pos[1] - P[1],
                  light.pos[2] - P[2]};
    const float d = std::sqrt(Dot3(l, l));
    if (d <= 1e-4f)
        return;
    l[0] /= d; l[1] /= d; l[2] /= d;
    const float ratio = d / light.radius;
    const float att = gain * light.intensity / (1.0f + ratio * ratio);
    const float ndotl = std::fmax(0.0f, Dot3(n, l));
    if (ndotl <= 0.0f)
        return;
    const float r[3] = {2.0f * ndotl * n[0] - l[0],
                        2.0f * ndotl * n[1] - l[1],
                        2.0f * ndotl * n[2] - l[2]};
    const float sp = std::pow(std::fmax(0.0f, Dot3(r, v)), gloss);
    for (int ch = 0; ch < 3; ++ch) {
        diffuse[ch] += att * light.color[ch] * ndotl;
        spec[ch] += att * light.color[ch] * sp;
    }
}

/* Fraction of the floodlight reaching the camera-space point P (1 = lit). */
float ShadowLit(const float P[3])
{
    if (!gShadowMap)
        return 1.0f;
    double Pw[3];
    for (int i = 0; i < 3; ++i)
        Pw[i] = gCamToWorld[i][0] * P[0] + gCamToWorld[i][1] * P[1] +
                gCamToWorld[i][2] * P[2] + gCamToWorld[i][3];
    return zfile_visibility(gShadowMap, Pw);
}

/* ------------------------------------------------------------------ */
/* Scene-local art shaders                                             */
/* ------------------------------------------------------------------ */

struct HangarParams {
    float panel;      /* >0: panel seams + tone variation (walls) */
    float gloss;      /* Phong exponent */
    float specular;   /* specular strength */
};

/* Dirty, rain-streaked industrial concrete lit by the hangar lamps and the
 * shadowed floodlight. */
void HangarSurface(RtShaderContext *c)
{
    const HangarParams *hp = static_cast<const HangarParams *>(c->params);
    const float panel = hp ? hp->panel : 0.0f;
    const float gloss = hp ? hp->gloss : 20.0f;
    const float specular = hp ? hp->specular : 0.1f;

    const double px = c->Po[0], py = c->Po[1], pz = c->Po[2];
    const double along = px + pz;   /* runs along any wall face */

    float albedo[3] = {c->Cs[0], c->Cs[1], c->Cs[2]};

    /* Fine grain plus large blotches of grime. */
    const float grain = 1.0f + 0.12f * static_cast<float>(
        perlinNoise(6.1 * px, 6.3 * py, 6.7 * pz));
    const float blotch = 0.5f + 0.5f * static_cast<float>(
        0.65 * perlinNoise(0.55 * px, 0.55 * py, 0.55 * pz) +
        0.35 * perlinNoise(1.6 * px + 7.1, 1.6 * py, 1.6 * pz));

    float tone = grain * (0.62f + 0.58f * blotch);
    if (panel > 0.0f) {
        /* Irregular cast-concrete panels: soft seams, per-panel tone. */
        const float s = static_cast<float>(along / 0.85);
        const float t = static_cast<float>(py / 0.62);
        const float cs = std::floor(s);
        const float ct = std::floor(t);
        const float fx = s - cs;
        const float fy = t - ct;
        const float edge = std::fmin(std::fmin(fx, 1.0f - fx) / 0.03f,
                                     std::fmin(fy, 1.0f - fy) / 0.05f);
        const float seam = Clamp01(edge);
        const float variation = static_cast<float>(
            perlinNoise(cs * 1.37 + 0.31, ct * 2.11 + 0.17, 3.3));
        /* Long vertical water stains running down from each ledge. */
        const float stain = 0.5f + 0.5f * static_cast<float>(
            perlinNoise(2.3 * along, 0.12 * py, 1.7));
        tone *= (1.0f + 0.22f * variation) * (0.70f + 0.30f * seam) *
                (0.65f + 0.45f * stain);
    }
    for (int ch = 0; ch < 3; ++ch)
        albedo[ch] *= tone;

    float n[3] = {c->N[0], c->N[1], c->N[2]};
    float v[3] = {-c->P[0], -c->P[1], -c->P[2]};
    Normalize3(n);
    Normalize3(v);

    float diffuse[3] = {0.0f, 0.0f, 0.0f};
    float spec[3] = {0.0f, 0.0f, 0.0f};
    for (int i = 0; i < 3; ++i)
        Accumulate(gEnv[i], c->P, n, v, gloss, diffuse, spec);
    /* Only the floodlight is shadowed (by the robot). */
    Accumulate(gKey, c->P, n, v, gloss, diffuse, spec, ShadowLit(c->P));
    for (int i = 0; i < 3; ++i)
        Accumulate(gLocal[i], c->P, n, v, gloss, diffuse, spec);

    /* Atmospheric perspective: far surfaces sink into the cool fog. */
    const float fogAmount = 0.70f * (1.0f - std::exp(-c->P[2] * 0.050f));
    const float ambient[3] = {0.026f, 0.040f, 0.062f};
    const float fogColor[3] = {0.050f, 0.085f, 0.120f};
    for (int ch = 0; ch < 3; ++ch) {
        const float lit = albedo[ch] * (ambient[ch] + 0.9f * diffuse[ch]) +
                          specular * spec[ch];
        c->Ci[ch] = Mix(lit, fogColor[ch], fogAmount);
        c->Oi[ch] = c->Os[ch];
    }
}

struct GipsySurfaceParams {
    RtInt slot;
};

/* Hero armor: dark wet metal that keeps its texture, backlit by the hangar
 * lamps with a cool rim, softly touched by the floodlight (which also shadows
 * it), and glowing orange around the arc reactor. */
void GipsyHeroSurface(RtShaderContext *c)
{
    const GipsySurfaceParams *params =
        static_cast<const GipsySurfaceParams *>(c->params);
    if (params && params->slot >= 0)
        RiTexture(params->slot, c->u, c->v, c->Cs);
    const float texLum = 0.30f * c->Cs[0] + 0.59f * c->Cs[1] + 0.11f * c->Cs[2];
    for (int ch = 0; ch < 3; ++ch)
        c->Cs[ch] *= 0.78f;

    float n[3] = {c->N[0], c->N[1], c->N[2]};
    float v[3] = {-c->P[0], -c->P[1], -c->P[2]};
    Normalize3(n);
    Normalize3(v);

    float diffuse[3] = {0.0f, 0.0f, 0.0f};
    float spec[3] = {0.0f, 0.0f, 0.0f};
    for (int i = 0; i < 3; ++i)
        Accumulate(gEnv[i], c->P, n, v, 36.0f, diffuse, spec, 1.25f);
    Accumulate(gKey, c->P, n, v, 36.0f, diffuse, spec, 0.40f * ShadowLit(c->P));
    for (int i = 0; i < 3; ++i)
        Accumulate(gLocal[i], c->P, n, v, 36.0f, diffuse, spec);

    const float upFacing = 0.5f * n[1] + 0.5f;
    const float skyAmbient[3] = {0.035f, 0.060f, 0.105f};
    const float bounceAmbient[3] = {0.030f, 0.035f, 0.045f};
    const float silhouette = std::pow(
        1.0f - std::fmin(1.0f, std::fabs(Dot3(n, v))), 2.6f);
    const float rim[3] = {0.075f, 0.210f, 0.420f};

    /* Arc reactor: emission falling off around its center. */
    const float dr[3] = {c->P[0] - gReactorCam[0], c->P[1] - gReactorCam[1],
                         c->P[2] - gReactorCam[2]};
    const float sigma = 0.075f;
    const float emit = std::exp(-Dot3(dr, dr) / (2.0f * sigma * sigma)) *
                       (0.45f + 1.6f * texLum);
    const float reactorColor[3] = {1.00f, 0.42f, 0.07f};

    for (int ch = 0; ch < 3; ++ch) {
        const float ambient = Mix(bounceAmbient[ch], skyAmbient[ch], upFacing);
        c->Ci[ch] = c->Cs[ch] * (ambient + 0.90f * diffuse[ch]) +
                    0.85f * spec[ch] + rim[ch] * silhouette +
                    1.10f * emit * reactorColor[ch];
        c->Oi[ch] = c->Os[ch];
    }
}

/* Soft radial glow (lamp halos, reactor glow, backlit haze).  Color is
 * RiColor, peak opacity RiOpacity; the quad supplies st in [0,1]. */
void GlowSurface(RtShaderContext *c)
{
    const float dx = 2.0f * c->u - 1.0f;
    const float dy = 2.0f * c->v - 1.0f;
    const float d2 = dx * dx + dy * dy;
    float f = d2 >= 1.0f ? 0.0f : (1.0f - d2);
    f = f * f * f;
    for (int ch = 0; ch < 3; ++ch) {
        c->Ci[ch] = c->Cs[ch];
        c->Oi[ch] = c->Os[ch] * f;
    }
}

/* Light beam ribbon: u runs across, v along (0 at the lamp).  A soft
 * cross-section and an exponential falloff make it read as lit mist. */
void BeamSurface(RtShaderContext *c)
{
    const float across = 1.0f - std::fabs(2.0f * c->u - 1.0f);
    const float profile = across * across;
    const float along = std::exp(-2.4f * c->v) *
                        Smoothstep(0.0f, 0.03f, c->v) *
                        (1.0f - Smoothstep(0.75f, 1.0f, c->v));
    for (int ch = 0; ch < 3; ++ch) {
        c->Ci[ch] = c->Cs[ch];
        c->Oi[ch] = c->Os[ch] * profile * along;
    }
}

/* Drifting fog: Os is the layer's peak opacity, modulated by slow noise. */
void FogSurface(RtShaderContext *c)
{
    const float n = 0.5f + 0.5f * static_cast<float>(
        perlinNoise(0.35 * c->Po[0], 0.30 * c->Po[1], 0.35 * c->Po[2]));
    const float k = 0.55f + 0.65f * n;
    for (int ch = 0; ch < 3; ++ch) {
        c->Ci[ch] = c->Cs[ch];
        c->Oi[ch] = c->Os[ch] * k;
    }
}

/* Storm sky beyond the door: dark blue-grey, a little brighter low down,
 * with slow rolling clouds and a pale glow toward the top-right lamp. */
void StormSkySurface(RtShaderContext *c)
{
    const double x = c->Po[0], y = c->Po[1];
    const float t = Clamp01(static_cast<float>((y - 5.0) / 60.0));
    const float clouds = 0.5f + 0.5f * static_cast<float>(
        0.6 * perlinNoise(0.045 * x, 0.060 * y, 2.3) +
        0.4 * perlinNoise(0.14 * x, 0.18 * y, 7.9));
    const float base[3] = {Mix(0.085f, 0.030f, t), Mix(0.120f, 0.048f, t),
                           Mix(0.155f, 0.068f, t)};
    const float k = 0.70f + 0.60f * clouds;
    for (int ch = 0; ch < 3; ++ch) {
        c->Ci[ch] = base[ch] * k;
        c->Oi[ch] = c->Os[ch];
    }
}

/* Distant mountain: an almost black mass, its rock faintly mottled, sinking
 * into lighter rain haze toward its foot. */
void MountainSurface(RtShaderContext *c)
{
    const double x = c->Po[0], y = c->Po[1];
    const float rock = 0.5f + 0.5f * static_cast<float>(
        0.6 * perlinNoise(0.25 * x, 0.25 * y, 4.1) +
        0.4 * perlinNoise(0.9 * x, 0.9 * y, 1.3));
    const float haze = 1.0f - Smoothstep(-2.0f, 22.0f, static_cast<float>(y));
    const float dark[3] = {0.016f, 0.022f, 0.030f};
    const float mist[3] = {0.060f, 0.085f, 0.110f};
    for (int ch = 0; ch < 3; ++ch) {
        const float r = dark[ch] * (0.65f + 0.70f * rock);
        c->Ci[ch] = Mix(r, mist[ch], 0.75f * haze);
        c->Oi[ch] = c->Os[ch];
    }
}

/* Lens vignette: black, transparent in the middle, darkening the corners.
 * Os is the corner opacity; st spans the frame. */
void VignetteSurface(RtShaderContext *c)
{
    const float aspect = static_cast<float>(kXRes) / kYRes;
    const float dx = (2.0f * c->u - 1.0f) * aspect / 1.25f;
    const float dy = (2.0f * c->v - 1.0f);
    const float r = std::sqrt(dx * dx + dy * dy);
    const float k = Smoothstep(0.55f, 1.45f, r);
    for (int ch = 0; ch < 3; ++ch) {
        c->Ci[ch] = 0.0f;
        c->Oi[ch] = c->Os[ch] * k;
    }
}

/* ------------------------------------------------------------------ */
/* Geometry helpers                                                    */
/* ------------------------------------------------------------------ */

void SetColor(const float r, const float g, const float b)
{
    RtColor c = {r, g, b};
    RiColor(c);
}

/* RiOpacity persists until changed, so every user resets it to 1. */
void SetOpacity(float a)
{
    RtColor o = {a, a, a};
    RiOpacity(o);
}

void Box(float x0, float y0, float z0, float x1, float y1, float z1)
{
    RtPoint P[8] = {
        {x0, y0, z0}, {x1, y0, z0}, {x1, y1, z0}, {x0, y1, z0},
        {x0, y0, z1}, {x1, y0, z1}, {x1, y1, z1}, {x0, y1, z1}
    };
    RtInt nverts[6] = {4, 4, 4, 4, 4, 4};
    RtInt verts[24] = {
        0, 1, 2, 3,  5, 4, 7, 6,  4, 0, 3, 7,
        1, 5, 6, 2,  3, 2, 6, 7,  4, 5, 1, 0
    };
    RiPointsPolygons(6, nverts, verts, P, NULL, NULL);
}

void QuadXZ(float x0, float x1, float z0, float z1, float y)
{
    RtPoint P[4] = {{x0, y, z0}, {x1, y, z0}, {x1, y, z1}, {x0, y, z1}};
    RtInt nverts[1] = {4};
    RtInt verts[4] = {0, 1, 2, 3};
    RiPointsPolygons(1, nverts, verts, P, NULL, NULL);
}

void QuadYZ(float x, float y0, float y1, float z0, float z1)
{
    RtPoint P[4] = {{x, y0, z0}, {x, y0, z1}, {x, y1, z1}, {x, y1, z0}};
    RtInt nverts[1] = {4};
    RtInt verts[4] = {0, 1, 2, 3};
    RiPointsPolygons(1, nverts, verts, P, NULL, NULL);
}

void QuadXY(float z, float x0, float x1, float y0, float y1)
{
    RtPoint P[4] = {{x0, y0, z}, {x1, y0, z}, {x1, y1, z}, {x0, y1, z}};
    RtInt nverts[1] = {4};
    RtInt verts[4] = {0, 1, 2, 3};
    RiPointsPolygons(1, nverts, verts, P, NULL, NULL);
}

/* Camera-facing quad centered on a world point, half-sizes rx, ry, with st
 * in [0,1] (for GlowSurface and VignetteSurface). */
void Billboard(const float p[3], float rx, float ry)
{
    RtPoint P[4];
    const float sx[4] = {-1.0f, 1.0f, 1.0f, -1.0f};
    const float sy[4] = {-1.0f, -1.0f, 1.0f, 1.0f};
    for (int i = 0; i < 4; ++i)
        for (int k = 0; k < 3; ++k)
            P[i][k] = p[k] + gR[k] * sx[i] * rx + gU[k] * sy[i] * ry;
    RtFloat st[8] = {0, 1, 1, 1, 1, 0, 0, 0};
    RtInt nverts[1] = {4};
    RtInt verts[4] = {0, 1, 2, 3};
    RiPointsPolygons(1, nverts, verts, P, NULL, st);
}

void GlowBillboard(const float p[3], float radius)
{
    Billboard(p, radius, radius);
}

/* Camera-facing ribbon from a to b.  withSt: u across, v along (for beams). */
void Ribbon(const float a[3], const float b[3], float w0, float w1,
            bool withSt)
{
    const float axis[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]};
    float view[3] = {a[0] - gEye[0], a[1] - gEye[1], a[2] - gEye[2]};
    Normalize3(view);
    float side[3];
    Cross3(view, axis, side);
    Normalize3(side);
    RtPoint P[4];
    for (int k = 0; k < 3; ++k) {
        P[0][k] = a[k] - side[k] * w0;
        P[1][k] = a[k] + side[k] * w0;
        P[2][k] = b[k] + side[k] * w1;
        P[3][k] = b[k] - side[k] * w1;
    }
    RtFloat st[8] = {0, 0, 1, 0, 1, 1, 0, 1};
    RtInt nverts[1] = {4};
    RtInt verts[4] = {0, 1, 2, 3};
    RiPointsPolygons(1, nverts, verts, P, NULL, withSt ? st : NULL);
}

/* A soft beam leaving world point s in a camera-relative direction (right,
 * up, forward): a narrow bright core plus a wide faint halo. */
void SoftBeam(const float s[3], float dr, float du, float df, float len,
              float width, float opacity)
{
    float d[3] = {gR[0] * dr + gU[0] * du + gF[0] * df,
                  gR[1] * dr + gU[1] * du + gF[1] * df,
                  gR[2] * dr + gU[2] * du + gF[2] * df};
    Normalize3(d);
    const float e[3] = {s[0] + d[0] * len, s[1] + d[1] * len,
                        s[2] + d[2] * len};
    SetOpacity(opacity);
    Ribbon(s, e, 0.10f * width, width, true);
    SetOpacity(0.35f * opacity);
    Ribbon(s, e, 0.30f * width, 2.6f * width, true);
}

/* ------------------------------------------------------------------ */
/* Hangar                                                              */
/* ------------------------------------------------------------------ */

const float X_LEFT = -4.2f;     /* left wall (the facade left of the door) */
const float JAMB_X0 = 1.00f;    /* the door's right jamb */
const float JAMB_X1 = 1.45f;
const float JAMB_Z0 = 4.40f;
const float JAMB_Z1 = 4.85f;
const float Z_BACK = 24.0f;     /* back wall of the hangar interior */
const float Y_CEIL = 10.0f;

void DrawHangar(void)
{
    /* Left wall: warm, dirty brown concrete, in segments so each dices
     * finely; piers and ledges give it relief.  The floodlight lights it
     * and the robot's shadow falls across it. */
    HangarParams wallP = {1.0f, 14.0f, 0.04f};
    RiSurface(HangarSurface, &wallP, sizeof wallP);
    SetColor(0.36f, 0.29f, 0.22f);
    for (int i = 0; i < 6; ++i) {
        const float z0 = -6.0f + 5.0f * i;
        QuadYZ(X_LEFT, 0.0f, Y_CEIL, z0, z0 + 5.0f);
    }
    SetColor(0.30f, 0.25f, 0.20f);
    for (int i = 0; i < 5; ++i) {
        const float z = 1.0f + 4.5f * i;
        Box(X_LEFT, 0.0f, z, X_LEFT + 0.55f, Y_CEIL, z + 0.55f);
    }
    SetColor(0.27f, 0.23f, 0.19f);
    Box(X_LEFT, 2.1f, -6.0f, X_LEFT + 0.35f, 2.35f, Z_BACK);
    Box(X_LEFT, 4.6f, -6.0f, X_LEFT + 0.35f, 4.85f, Z_BACK);
    Box(X_LEFT, 7.1f, -6.0f, X_LEFT + 0.35f, 7.35f, Z_BACK);

    /* Back wall of the hangar interior, seen through the door. */
    SetColor(0.17f, 0.19f, 0.22f);
    QuadXY(Z_BACK, X_LEFT, JAMB_X1, 0.0f, Y_CEIL);

    /* The door's right jamb: a slim pale column with stepped ledges on its
     * left face, splitting the frame.  Right of it is the outdoors. */
    HangarParams jambP = {1.0f, 24.0f, 0.10f};
    RiSurface(HangarSurface, &jambP, sizeof jambP);
    SetColor(0.46f, 0.47f, 0.47f);
    Box(JAMB_X0, -0.5f, JAMB_Z0, JAMB_X1, 14.0f, JAMB_Z1);
    SetColor(0.38f, 0.39f, 0.39f);
    const float ledges[5] = {2.4f, 4.3f, 6.2f, 8.1f, 10.0f};
    for (int i = 0; i < 5; ++i)
        Box(JAMB_X0 - 0.22f, ledges[i], JAMB_Z0 - 0.08f,
            JAMB_X0, ledges[i] + 0.26f, JAMB_Z1 + 0.08f);

    /* Hangar ceiling and trusses, inside the door only (out of frame, but
     * they keep the interior closed above the lit back wall). */
    HangarParams ceilP = {0.0f, 12.0f, 0.03f};
    RiSurface(HangarSurface, &ceilP, sizeof ceilP);
    SetColor(0.12f, 0.13f, 0.15f);
    QuadXZ(X_LEFT, JAMB_X1, JAMB_Z0, Z_BACK, Y_CEIL);
    SetColor(0.18f, 0.19f, 0.21f);
    for (int i = 0; i < 5; ++i) {
        const float z = 6.0f + 3.6f * i;
        Box(X_LEFT, 9.2f, z, JAMB_X1, 9.8f, z + 0.35f);
    }
}

/* The outdoors beyond the jamb: a storm sky and a dark mountain whose ridge
 * nearly reaches the top of the frame. */
void DrawOutdoors(void)
{
    SetColor(1.0f, 1.0f, 1.0f);

    RiSurface(StormSkySurface, NULL, 0);
    for (int i = 0; i < 3; ++i) {
        const float x0 = -10.0f + 40.0f * i;
        QuadXY(70.0f, x0, x0 + 40.0f, -10.0f, 90.0f);
    }

    /* Ridge: a strip of quads whose top edge follows a jagged profile. */
    enum { N = 40 };
    const float zM = 35.0f, xa = -2.0f, xb = 30.0f;
    RtPoint P[2 * (N + 1)];
    for (int i = 0; i <= N; ++i) {
        const float x = xa + (xb - xa) * i / N;
        const float ridge = 28.5f +
            3.0f * static_cast<float>(perlinNoise(0.11 * x, 0.5, 0.3)) +
            1.2f * static_cast<float>(perlinNoise(0.45 * x, 1.7, 0.9)) -
            0.10f * (x - 6.0f);
        P[2 * i][0] = x;     P[2 * i][1] = -4.0f;  P[2 * i][2] = zM;
        P[2 * i + 1][0] = x; P[2 * i + 1][1] = ridge; P[2 * i + 1][2] = zM;
    }
    RtInt nverts[N];
    RtInt verts[4 * N];
    for (int i = 0; i < N; ++i) {
        nverts[i] = 4;
        verts[4 * i + 0] = 2 * i;
        verts[4 * i + 1] = 2 * i + 2;
        verts[4 * i + 2] = 2 * i + 3;
        verts[4 * i + 3] = 2 * i + 1;
    }
    RiSurface(MountainSurface, NULL, 0);
    RiPointsPolygons(N, nverts, verts, P, NULL, NULL);
}

/* Cool fog sheets at fixed depths: behind the robot but in front of the
 * hangar, washing the far walls toward blue-gray. */
void DrawFog(void)
{
    const float depths[3] = {5.5f, 9.0f, 14.0f};
    const float peaks[3] = {0.10f, 0.14f, 0.18f};

    RiSurface(FogSurface, NULL, 0);
    SetColor(0.13f, 0.21f, 0.29f);
    for (int i = 0; i < 3; ++i) {
        const float z = depths[i];
        const float hx = 1.15f * TanHalfFovX() * z;
        const float hy = 1.15f * TanHalfFovY() * z;
        RtPoint P[4];
        const float sx[4] = {-1.0f, 1.0f, 1.0f, -1.0f};
        const float sy[4] = {-1.0f, -1.0f, 1.0f, 1.0f};
        for (int v = 0; v < 4; ++v)
            for (int k = 0; k < 3; ++k)
                P[v][k] = gEye[k] + gF[k] * z + gR[k] * sx[v] * hx +
                          gU[k] * sy[v] * hy;
        RtInt nverts[1] = {4};
        RtInt verts[4] = {0, 1, 2, 3};
        SetOpacity(peaks[i]);
        RiPointsPolygons(1, nverts, verts, P, NULL, NULL);
    }
    SetOpacity(1.0f);
}

/* ------------------------------------------------------------------ */
/* Lights, beams, glows, rain                                          */
/* ------------------------------------------------------------------ */

void SetLight(PointLight &l, const float world[3], float r, float g, float b,
              float intensity, float radius)
{
    ToCam(world, l.pos);
    l.color[0] = r; l.color[1] = g; l.color[2] = b;
    l.intensity = intensity;
    l.radius = radius;
}

void SetupLights(void)
{
    float w[3];

    ScreenPoint(LAMP_A, w);
    SetLight(gEnv[0], w, 0.60f, 0.78f, 1.00f, 1.50f, 5.0f);
    ScreenPoint(LAMP_B, w);
    SetLight(gEnv[1], w, 0.65f, 0.80f, 1.00f, 1.10f, 4.0f);
    /* Weak cool fill from the front left so the shadow side still reads. */
    ScreenPoint(60.0f, 380.0f, 3.0f, w);
    SetLight(gEnv[2], w, 0.30f, 0.45f, 0.70f, 0.30f, 6.0f);

    SetLight(gKey, KEY_WORLD, 0.82f, 0.90f, 1.00f, 2.20f, 5.0f);

    TowardCamera(REACTOR, 0.15f, w);
    /* Tight falloff: it only warms the chest plates around the reactor. */
    SetLight(gLocal[0], w, 1.00f, 0.45f, 0.10f, 0.60f, 0.22f);
    TowardCamera(SHOULDER_R, 0.15f, w);
    SetLight(gLocal[1], w, 0.80f, 0.90f, 1.00f, 0.55f, 0.9f);
    TowardCamera(HEAD_LAMP, 0.15f, w);
    SetLight(gLocal[2], w, 0.80f, 0.90f, 1.00f, 0.50f, 0.9f);

    ToCam(REACTOR, gReactorCam);
}

/* Bright cool haze directly behind the robot: it turns the Jaeger into a
 * silhouette against the light, as in the reference shot. */
void DrawBackGlow(void)
{
    float p[3];
    RiSurface(GlowSurface, NULL, 0);
    ScreenPoint(BACKGLOW, p);
    SetColor(0.55f, 0.68f, 0.82f);
    SetOpacity(0.55f);
    Billboard(p, 3.3f, 3.9f);
    SetColor(0.75f, 0.85f, 0.95f);
    SetOpacity(0.35f);
    Billboard(p, 1.5f, 2.2f);
    SetOpacity(1.0f);
}

void DrawBeamsAndGlows(void)
{
    float p[3];

    /* Light beams: the Jaeger's head and shoulder lamps, and the hangar's. */
    RiSurface(BeamSurface, NULL, 0);
    SetColor(0.78f, 0.90f, 1.00f);

    TowardCamera(HEAD_LAMP, 0.10f, p);
    SoftBeam(p, 0.85f, 0.55f, -0.05f, 2.6f, 0.20f, 0.55f);
    TowardCamera(SHOULDER_R, 0.10f, p);
    SoftBeam(p, 0.95f, 0.20f, 0.10f, 2.8f, 0.22f, 0.50f);
    TowardCamera(SHOULDER_L, 0.10f, p);
    SoftBeam(p, -0.80f, 0.50f, 0.20f, 2.0f, 0.18f, 0.35f);
    /* The hangar lamps' beams enter from beyond the frame edge. */
    ScreenPoint(LAMP_B, p);
    SoftBeam(p, -1.00f, -0.25f, 0.10f, 5.5f, 0.35f, 0.46f);
    ScreenPoint(LAMP_A, p);
    SoftBeam(p, -0.50f, -0.85f, 0.00f, 5.5f, 0.40f, 0.36f);
    /* A searchlight from the mountainside, off the lower right edge. */
    ScreenPoint(1010.0f, 470.0f, 9.0f, p);
    SoftBeam(p, -0.85f, 0.45f, 0.05f, 6.5f, 0.45f, 0.30f);

    /* Glows. */
    RiSurface(GlowSurface, NULL, 0);

    /* (The hangar lamps are out of frame, so they get no glow.) */
    SetColor(0.70f, 0.85f, 1.00f);

    /* Small work lights on the left wall. */
    const float wallLights[2][3] = {{-3.85f, 3.0f, 5.0f}, {-3.85f, 5.6f, 6.0f}};
    for (int i = 0; i < 2; ++i) {
        SetOpacity(0.40f);
        GlowBillboard(wallLights[i], 0.30f);
        SetOpacity(0.95f);
        GlowBillboard(wallLights[i], 0.06f);
    }

    /* The Jaeger's own lamps. */
    SetColor(0.88f, 0.95f, 1.00f);
    const float *robotLamps[3] = {HEAD_LAMP, SHOULDER_R, SHOULDER_L};
    for (int i = 0; i < 3; ++i) {
        TowardCamera(robotLamps[i], 0.12f, p);
        SetOpacity(0.40f);
        GlowBillboard(p, 0.12f);
        SetOpacity(0.95f);
        GlowBillboard(p, 0.035f);
    }

    /* Arc reactor: a soft orange bloom over the emissive chest. */
    TowardCamera(REACTOR, 0.15f, p);
    SetColor(1.00f, 0.45f, 0.08f);
    SetOpacity(0.30f);
    GlowBillboard(p, 0.22f);
    SetColor(1.00f, 0.70f, 0.30f);
    SetOpacity(0.35f);
    GlowBillboard(p, 0.07f);

    SetOpacity(1.0f);
}

/* Rain: dense, short, slanted hairline streaks.  Each streak is brightest
 * where it crosses a light (near the lamps or in front of the back haze),
 * so the rain reads mostly where it is lit, as on film. */
void DrawRain(void)
{
    const int kStreaks = 1700;
    const float pixel = 2.0f * TanHalfFovY() / static_cast<float>(kYRes);
    const float hot[4][4] = {
        /* screen x, y, radius (px), strength */
        {LAMP_A[0], LAMP_A[1], 190.0f, 1.0f},
        {LAMP_B[0], LAMP_B[1], 160.0f, 0.9f},
        {BACKGLOW[0], BACKGLOW[1], 260.0f, 0.55f},
        {960.0f, 430.0f, 260.0f, 0.45f}     /* floodlight from the right */
    };
    unsigned int seed = 987654u;

    RiSurface(CONSTANT, NULL, 0);
    SetColor(0.72f, 0.84f, 0.96f);

    for (int i = 0; i < kStreaks; ++i) {
        const float depth = 1.2f + 10.8f * Rand01(seed);
        const float sxn = 2.0f * Rand01(seed) - 1.0f;     /* -1..1 across */
        const float syn = 2.0f * Rand01(seed) - 1.0f;
        const float lenPx = 7.0f + 15.0f * Rand01(seed);
        const float slant = -0.20f + 0.06f * (Rand01(seed) - 0.5f);

        const float px = 0.5f * kXRes * (1.0f + sxn);
        const float py = 0.5f * kYRes * (1.0f - syn);
        float lit = 0.0f;
        for (int h = 0; h < 4; ++h) {
            const float dx = px - hot[h][0];
            const float dy = py - hot[h][1];
            lit += hot[h][3] * std::exp(-(dx * dx + dy * dy) /
                                        (2.0f * hot[h][2] * hot[h][2]));
        }
        const float nearness = 1.0f - (depth - 1.2f) / 10.8f;
        const float opacity = Clamp01((0.045f + 0.40f * lit) *
                                      (0.55f + 0.45f * nearness));
        if (opacity < 0.03f)
            continue;

        const float xc = sxn * 1.02f * TanHalfFovX() * depth;
        const float yc = syn * 1.02f * TanHalfFovY() * depth;
        float a[3];
        for (int k = 0; k < 3; ++k)
            a[k] = gEye[k] + gR[k] * xc + gU[k] * yc + gF[k] * depth;
        float down[3];
        for (int k = 0; k < 3; ++k)
            down[k] = -gU[k] + slant * gR[k];
        Normalize3(down);
        const float len = lenPx * pixel * depth;
        const float b[3] = {a[0] + down[0] * len, a[1] + down[1] * len,
                            a[2] + down[2] * len};
        const float width = 0.45f * pixel * depth;

        SetOpacity(opacity);
        Ribbon(a, b, width, width, false);
    }
    SetOpacity(1.0f);
}

/* Lens vignette just in front of the camera. */
void DrawVignette(void)
{
    const float depth = 0.6f;
    float p[3];
    for (int k = 0; k < 3; ++k)
        p[k] = gEye[k] + gF[k] * depth;
    RiSurface(VignetteSurface, NULL, 0);
    SetColor(0.0f, 0.0f, 0.0f);
    SetOpacity(0.80f);
    Billboard(p, 1.02f * TanHalfFovX() * depth, 1.02f * TanHalfFovY() * depth);
    SetOpacity(1.0f);
}

/* ------------------------------------------------------------------ */
/* Hero                                                                */
/* ------------------------------------------------------------------ */

int materialSlot(const char *name)
{
    const char *materials[] = {
        "GypsyDanger_UpperLeg",
        "GypsyDanger_LowerLeg",
        "GypsyDanger_Body",
        "GypsyDanger_Head",
        "GypsyDanger_Shoulder",
        "GypsyDanger_ArmUpper",
        "GypsyDanger_ArmLower"
    };

    for (int i = 0; i < 7; ++i) {
        if (std::strcmp(name, materials[i]) == 0)
            return i;
    }
    return -1;
}

void DrawGipsyDanger(const ObjMesh *mesh)
{
    RtColor white = {1, 1, 1};
    RtColor opaque = {1, 1, 1};
    RiColor(white);
    RiOpacity(opaque);

    for (int k = 0; mesh && k < mesh->ngroups; ++k) {
        const ObjGroup &g = mesh->groups[k];
        GipsySurfaceParams gp = {materialSlot(g.material)};

        if (gp.slot >= 0)
            RiSurface(GipsyHeroSurface, &gp, sizeof gp);
        else
            RiSurface(NULL, NULL, 0);

        RiPointsPolygons(g.npolys, g.nverts, g.verts, g.P, g.N, g.st);
    }
}

/* Depth pass geometry: only the Jaeger can shadow anything under the
 * floodlight; the hangar, fog, rain and glows are skipped. */
void DrawOccluders(const ObjMesh *mesh)
{
    RtColor opaque = {1, 1, 1};
    RiColor(opaque);
    RiOpacity(opaque);
    RiSurface(CONSTANT, NULL, 0);      /* the depth pass needs no shading */
    RiTransformBegin();
        RiRotate(140.0f, 0.0f, 1.0f, 0.0f);
        for (int k = 0; mesh && k < mesh->ngroups; ++k) {
            const ObjGroup &g = mesh->groups[k];
            RiPointsPolygons(g.npolys, g.nverts, g.verts, g.P, g.N, g.st);
        }
    RiTransformEnd();
}

} // namespace

void GipsyDanger(void)
{
    std::error_code ignored;
    std::filesystem::create_directories("renders/4_OriginalScene", ignored);
    
    float fov = kFov;
    float lightFov = 45.0f;
    RtMatrix camera;
    LookAt(EYE, TARGET, camera, gCamToWorld);
    SetupCamera();
    SetupLights();

    /* A camera at the floodlight, looking at the Jaeger, for the depth pass. */
    const double lamp[3] = {KEY_WORLD[0], KEY_WORLD[1], KEY_WORLD[2]};
    const double lampTarget[3] = {ROBOT_CENTER[0], ROBOT_CENTER[1],
                                  ROBOT_CENTER[2]};
    RtMatrix lightCamera;
    LookAt(lamp, lampTarget, lightCamera);

    ObjMesh *mesh = obj_load("models/GipsyDanger/gipsy_danger.obj");

    RiBegin(RI_NULL);
        /* Pass 1: depth seen from the floodlight. */
        gShadowMap = NULL;
        RiFrameBegin(0);
            RiFormat(1024, 1024);
            RiPixelSamples(1, 1);
            RiDisplay("4_OriginalScene/GipsyDanger_key.z", "zfile", "z");
            RiProjection(RI_PERSPECTIVE, "fov", &lightFov);
            RiConcatTransform(lightCamera);
            RiWorldBegin();
                DrawOccluders(mesh);
            RiWorldEnd();
        RiFrameEnd();

        /* Pass 2: the picture, shadowed by that depth map. */
        ZFile *map = zfile_load("4_OriginalScene/GipsyDanger_key.z",
                                lightCamera, lightFov);
        gShadowMap = map;
        RiFrameBegin(1);
            RiFormat(kXRes, kYRes);
            RiPixelSamples(kSamples, kSamples);
            RiDisplay("4_OriginalScene/GipsyDanger.png", "", "");
            RiProjection(RI_PERSPECTIVE, "fov", &fov);
            RiConcatTransform(camera);

            /* The model's seven established OBJ material textures. */
            const char *files[7] = {
                "Image_0.jpg", "Image_2.jpg", "Image_4.jpg", "Image_6.jpg",
                "Image_8.jpg", "Image_10.jpg", "Image_12.jpg"
            };
            for (int i = 0; i < 7; ++i) {
                char path[256];
                std::snprintf(path, sizeof path,
                              "models/GipsyDanger/textures/%s", files[i]);
                RiMakeTexture(path, i);
            }

            RiWorldBegin();
                DrawOutdoors();
                DrawHangar();
                DrawFog();
                DrawBackGlow();

                RiTransformBegin();
                    RiRotate(140.0f, 0.0f, 1.0f, 0.0f);
                    DrawGipsyDanger(mesh);
                RiTransformEnd();

                DrawBeamsAndGlows();
                DrawRain();
                DrawVignette();
            RiWorldEnd();
        RiFrameEnd();
        gShadowMap = NULL;
        zfile_free(map);
    RiEnd();
    obj_free(mesh);
}