/*
 * Original scene V6: cinematic Gipsy Danger in a rain-wet Hong Kong harbor.
 * The water, waves, wet quay, puddles, reflections and industrial details use
 * only REYES geometry and small scene-local shaders; no extra assets are needed.
 *
 * Output: renders/4_OriginalScene/GipsyDanger.png
 */

#include "Ri.h"
#include "obj.h"
#include "sceneutil.h"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace {

/* Low, off-axis camera: a three-quarter hero view with both feet in frame. */
const double EYE[3] = {2.30, 0.64, -4.75};
const double TARGET[3] = {-0.22, 1.42, 0.15};

const RtColor WATER_DEEP = {0.018f, 0.055f, 0.085f};
const RtColor WATER_HORIZON = {0.045f, 0.115f, 0.155f};
const RtColor CONCRETE = {0.13f, 0.15f, 0.17f};
const RtColor CONCRETE_EDGE = {0.20f, 0.22f, 0.22f};
const RtColor EDGE_YELLOW = {0.52f, 0.32f, 0.055f};
const RtColor LAMP_WARM = {0.92f, 0.53f, 0.16f};
const RtColor WINDOW_WARM = {0.82f, 0.51f, 0.19f};
const RtColor WINDOW_COOL = {0.36f, 0.57f, 0.77f};

/* Scene-local art shaders.  They only consume the public shader context and
 * deliberately leave the required renderer/shader implementation untouched. */
void WetConcreteSurface(RtShaderContext *c)
{
    RtColor saved = {c->Cs[0], c->Cs[1], c->Cs[2]};
    const float grain = 0.90f + 0.10f * static_cast<float>(
        perlinNoise(8.0 * c->Po[0], 3.0, 8.0 * c->Po[2]));
    for (int i = 0; i < 3; ++i)
        c->Cs[i] *= grain;
    STANDARD(c);

    /* Rain-darkened concrete keeps a restrained, cool grazing sheen. */
    float view[3] = {-c->P[0], -c->P[1], -c->P[2]};
    const float len = std::sqrt(view[0] * view[0] + view[1] * view[1] +
                                view[2] * view[2]);
    const float facing = len > 0.0f
        ? std::fabs((c->N[0] * view[0] + c->N[1] * view[1] +
                     c->N[2] * view[2]) / len) : 1.0f;
    const float sheen = 0.055f * (1.0f - facing);
    c->Ci[0] += 0.25f * sheen;
    c->Ci[1] += 0.55f * sheen;
    c->Ci[2] += 0.78f * sheen;
    for (int i = 0; i < 3; ++i)
        c->Cs[i] = saved[i];
}

void HarborWaterSurface(RtShaderContext *c)
{
    const float ripple = static_cast<float>(
        0.5 + 0.5 * perlinNoise(1.5 * c->Po[0], 0.0, 4.0 * c->Po[2]));
    const float distance = std::fmax(0.0f, std::fmin(1.0f,
        (c->Po[2] - 4.2f) / 5.0f));
    c->Cs[0] = 0.010f + 0.018f * distance;
    c->Cs[1] = 0.045f + 0.050f * distance + 0.012f * ripple;
    c->Cs[2] = 0.075f + 0.075f * distance + 0.018f * ripple;
    STANDARD(c);

    /* Thin blue-white highlights make the displaced mesh read as water. */
    const float crest = std::fmax(0.0f, c->N[1] - 0.90f);
    c->Ci[0] += 0.008f + 0.055f * crest;
    c->Ci[1] += 0.025f + 0.14f * crest;
    c->Ci[2] += 0.040f + 0.22f * crest;
}

void PuddleSurface(RtShaderContext *c)
{
    c->Cs[0] = 0.025f;
    c->Cs[1] = 0.070f;
    c->Cs[2] = 0.095f;
    STANDARD(c);
    c->Ci[0] += 0.025f;
    c->Ci[1] += 0.055f;
    c->Ci[2] += 0.075f;
}

struct GipsySurfaceParams {
    RtInt slot;
};

float Dot3(const float a[3], const float b[3])
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
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

/* A scene-local, texture-preserving hero shader.  A hard cool key and warm
 * cross-light reveal the armor planes without changing any source texture. */
void GipsyCinematicSurface(RtShaderContext *c)
{
    const GipsySurfaceParams *params =
        static_cast<const GipsySurfaceParams *>(c->params);
    if (params && params->slot >= 0)
        RiTexture(params->slot, c->u, c->v, c->Cs);

    struct Light {
        float position[3];
        float color[3];
        float intensity;
    };
    const Light lights[] = {
        {{-5.5f, 5.8f, -3.8f}, {0.30f, 0.64f, 1.00f}, 1.28f},
        {{ 4.0f, 2.2f, -2.8f}, {1.00f, 0.34f, 0.07f}, 0.72f},
        {{-1.5f, 4.8f,  3.4f}, {0.12f, 0.36f, 1.00f}, 0.42f}
    };

    float n[3] = {c->N[0], c->N[1], c->N[2]};
    float view[3] = {-c->P[0], -c->P[1], -c->P[2]};
    Normalize3(n);
    Normalize3(view);

    float diffuse[3] = {0.0f, 0.0f, 0.0f};
    float specular[3] = {0.0f, 0.0f, 0.0f};
    for (const Light &light : lights) {
        float l[3] = {
            light.position[0] - c->P[0],
            light.position[1] - c->P[1],
            light.position[2] - c->P[2]
        };
        Normalize3(l);
        const float ndotl = std::fmax(0.0f, Dot3(n, l));
        float reflection[3] = {
            2.0f * ndotl * n[0] - l[0],
            2.0f * ndotl * n[1] - l[1],
            2.0f * ndotl * n[2] - l[2]
        };
        Normalize3(reflection);
        const float highlight = std::pow(
            std::fmax(0.0f, Dot3(reflection, view)), 44.0f);
        for (int channel = 0; channel < 3; ++channel) {
            const float energy = light.intensity * light.color[channel];
            diffuse[channel] += energy * ndotl;
            specular[channel] += energy * highlight;
        }
    }

    const float silhouette = std::pow(
        1.0f - std::fmin(1.0f, std::fabs(Dot3(n, view))), 2.2f);
    const float coolSide = std::pow(std::fmax(0.0f, -n[0]), 1.6f);
    const float warmSide = std::pow(std::fmax(0.0f,  n[0]), 1.7f);
    const float rim[3] = {0.025f, 0.105f, 0.26f};
    const float coolAccent[3] = {0.012f, 0.045f, 0.115f};
    const float warmAccent[3] = {0.16f, 0.046f, 0.008f};
    for (int channel = 0; channel < 3; ++channel) {
        c->Ci[channel] = c->Cs[channel] *
            (0.072f + 0.80f * diffuse[channel]) +
            0.32f * specular[channel] + rim[channel] * silhouette +
            coolAccent[channel] * coolSide +
            warmAccent[channel] * warmSide;
        c->Oi[channel] = c->Os[channel];
    }
}

void Box(float x0, float y0, float z0,
         float x1, float y1, float z1)
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

void Backdrop(float x0, float y0, float x1, float y1, float z)
{
    RtPoint P[4] = {
        {x0, y0, z}, {x1, y0, z}, {x1, y1, z}, {x0, y1, z}
    };
    RtInt nverts[1] = {4};
    RtInt verts[4] = {0, 1, 2, 3};
    RiPointsPolygons(1, nverts, verts, P, NULL, NULL);
}

void FlatPatch(const float points[][2], int count, float y)
{
    RtPoint P[12];
    RtInt verts[12];
    RtInt nverts[1] = {count};
    for (int i = 0; i < count; ++i) {
        const int source = count - 1 - i;
        P[i][0] = points[source][0];
        P[i][1] = y;
        P[i][2] = points[source][1];
        verts[i] = i;
    }
    RiPointsPolygons(1, nverts, verts, P, NULL, NULL);
}

void GroundStripe(float x0, float z0, float x1, float z1,
                  float width, float y)
{
    const float dx = x1 - x0;
    const float dz = z1 - z0;
    const float length = std::sqrt(dx * dx + dz * dz);
    const float nx = length > 0.0f ? -0.5f * width * dz / length : 0.0f;
    const float nz = length > 0.0f ?  0.5f * width * dx / length : 0.0f;
    RtPoint P[4] = {
        {x0 + nx, y, z0 + nz}, {x0 - nx, y, z0 - nz},
        {x1 - nx, y, z1 - nz}, {x1 + nx, y, z1 + nz}
    };
    RtInt nverts[1] = {4};
    RtInt verts[4] = {0, 1, 2, 3};
    RiPointsPolygons(1, nverts, verts, P, NULL, NULL);
}

void SetColor(const RtColor color)
{
    RtColor c = {color[0], color[1], color[2]};
    RiColor(c);
}

void DrawNightSky(void)
{
    const RtColor horizon = {0.035f, 0.085f, 0.155f};
    const RtColor lower = {0.020f, 0.050f, 0.115f};
    const RtColor middle = {0.012f, 0.028f, 0.075f};
    const RtColor upper = {0.006f, 0.014f, 0.043f};
    const RtColor zenith = {0.002f, 0.006f, 0.020f};
    const RtColor starWarm = {0.92f, 0.83f, 0.66f};
    const RtColor starCool = {0.67f, 0.79f, 0.95f};

    RiSurface(CONSTANT, NULL, 0);

    /* Five adjoining bands form a deep blue-black night gradient. */
    SetColor(horizon);
    Backdrop(-40.0f, -4.0f, 40.0f, 0.75f, 12.0f);
    SetColor(lower);
    Backdrop(-40.0f, 0.75f, 40.0f, 1.80f, 12.0f);
    SetColor(middle);
    Backdrop(-40.0f, 1.80f, 40.0f, 3.20f, 12.0f);
    SetColor(upper);
    Backdrop(-40.0f, 3.20f, 40.0f, 5.30f, 12.0f);
    SetColor(zenith);
    Backdrop(-40.0f, 5.30f, 40.0f, 14.0f, 12.0f);

    /* Sparse, deterministic stars add texture without competing with Gipsy. */
    const float stars[][3] = {
        {-8.1f, 4.8f, 0.030f}, {-7.0f, 3.4f, 0.018f},
        {-6.2f, 5.7f, 0.022f}, {-5.1f, 2.7f, 0.020f},
        {-4.4f, 4.1f, 0.026f}, {-3.6f, 6.3f, 0.019f},
        {-2.7f, 3.5f, 0.016f}, {-1.8f, 5.2f, 0.024f},
        {-0.9f, 4.4f, 0.017f}, { 0.1f, 6.0f, 0.021f},
        { 1.0f, 3.1f, 0.019f}, { 2.0f, 4.7f, 0.027f},
        { 3.1f, 5.8f, 0.018f}, { 4.0f, 3.8f, 0.022f},
        { 5.2f, 5.0f, 0.020f}, { 6.0f, 2.9f, 0.016f},
        { 7.1f, 4.2f, 0.025f}, { 8.0f, 6.1f, 0.019f}
    };
    for (int i = 0; i < 18; ++i) {
        const float x = stars[i][0];
        const float y = stars[i][1];
        const float r = stars[i][2];
        SetColor((i % 3 == 0) ? starWarm : starCool);
        Backdrop(x - r, y - r, x + r, y + r, 11.94f);
    }

    /* Dark water plus a slightly brighter horizon band. */
    SetColor(WATER_DEEP);
    Backdrop(-40.0f, -0.30f, 40.0f, 0.45f, 11.82f);
    SetColor(WATER_HORIZON);
    Backdrop(-40.0f, 0.30f, 40.0f, 0.48f, 11.80f);
}

struct Building {
    float x;
    float width;
    float height;
    float depth;
    int seed;
};

void DrawBuilding(const Building &b)
{
    const RtColor facadeA = {0.055f, 0.070f, 0.095f};
    const RtColor facadeB = {0.075f, 0.085f, 0.105f};
    const RtColor rooftop = {0.105f, 0.115f, 0.125f};
    const float front = 9.12f;

    SetColor((b.seed % 2 == 0) ? facadeA : facadeB);
    Box(b.x, -0.12f, front,
        b.x + b.width, b.height, front + b.depth);

    /* Small rooftop plant rooms give the skyline a varied silhouette. */
    SetColor(rooftop);
    const float roofWidth = b.width * 0.36f;
    Box(b.x + b.width * 0.32f, b.height, front + 0.10f,
        b.x + b.width * 0.32f + roofWidth, b.height + 0.13f,
        front + b.depth - 0.10f);

    /* Windows sit just in front of the facade for clean z-buffering. */
    int column = 0;
    for (float wx = b.x + 0.13f; wx < b.x + b.width - 0.10f;
         wx += 0.22f, ++column) {
        int row = 0;
        for (float wy = 0.18f; wy < b.height - 0.10f;
             wy += 0.25f, ++row) {
            const int pattern = (column * 5 + row * 3 + b.seed) % 9;
            if (pattern < 4) {
                SetColor((pattern == 3) ? WINDOW_COOL : WINDOW_WARM);
                Box(wx, wy, front - 0.030f,
                    wx + 0.090f, wy + 0.115f, front - 0.010f);
            }
        }
    }

    if (b.height > 1.85f) {
        const RtColor beacon = {1.0f, 0.08f, 0.04f};
        SetColor(beacon);
        Box(b.x + 0.5f * b.width - 0.025f, b.height + 0.13f,
            front + 0.5f * b.depth - 0.025f,
            b.x + 0.5f * b.width + 0.025f, b.height + 0.20f,
            front + 0.5f * b.depth + 0.025f);
    }
}

void DrawCity(void)
{
    const Building buildings[] = {
        {-8.4f, 1.10f, 0.95f, 0.72f, 1},
        {-7.1f, 0.90f, 1.55f, 0.80f, 4},
        {-6.0f, 1.20f, 2.10f, 0.74f, 7},
        {-4.6f, 1.05f, 1.25f, 0.86f, 3},
        {-3.4f, 1.15f, 1.72f, 0.76f, 8},
        {-2.0f, 0.82f, 1.05f, 0.68f, 5},
        {-0.9f, 1.16f, 2.38f, 0.90f, 2},
        { 0.5f, 1.08f, 1.45f, 0.76f, 6},
        { 1.8f, 1.05f, 1.92f, 0.82f, 9},
        { 3.1f, 1.22f, 1.20f, 0.72f, 0},
        { 4.6f, 1.12f, 1.67f, 0.88f, 5},
        { 5.9f, 1.38f, 1.12f, 0.78f, 2},
        { 7.5f, 1.05f, 1.86f, 0.82f, 7}
    };

    RiSurface(CONSTANT, NULL, 0);
    for (const Building &building : buildings)
        DrawBuilding(building);

    /* Broken vertical reflections tie selected windows to the water. */
    const float reflections[][2] = {
        {-6.55f, 0.045f}, {-5.45f, 0.030f}, {-3.02f, 0.040f},
        {-0.28f, 0.050f}, { 2.35f, 0.035f}, { 5.18f, 0.045f},
        { 7.92f, 0.030f}
    };
    SetColor(WINDOW_WARM);
    for (int i = 0; i < 7; ++i) {
        const float x = reflections[i][0];
        const float width = reflections[i][1];
        Backdrop(x - width, -0.18f, x + width, 0.24f, 11.72f);
    }
}

void ContainerDigit(int digit, float x, float y, float z)
{
    /* Seven-segment stencil: readable serial markings without a font asset. */
    static const unsigned char mask[10] = {
        0x3f, 0x06, 0x5b, 0x4f, 0x66,
        0x6d, 0x7d, 0x07, 0x7f, 0x6f
    };
    const float w = 0.045f;
    const float h = 0.075f;
    const float t = 0.009f;
    const float d = 0.007f;
    const unsigned char segments = mask[digit % 10];

    if (segments & 0x01) Box(x, y + 2.0f * h, z - d, x + w, y + 2.0f * h + t, z);
    if (segments & 0x02) Box(x + w - t, y + h, z - d, x + w, y + 2.0f * h, z);
    if (segments & 0x04) Box(x + w - t, y, z - d, x + w, y + h, z);
    if (segments & 0x08) Box(x, y, z - d, x + w, y + t, z);
    if (segments & 0x10) Box(x, y, z - d, x + t, y + h, z);
    if (segments & 0x20) Box(x, y + h, z - d, x + t, y + 2.0f * h, z);
    if (segments & 0x40) Box(x, y + h, z - d, x + w, y + h + t, z);
}

/* A corrugated, framed shipping container in local coordinates. */
void Container(float x, float y, float z, float angle,
               const RtColor color, int serial)
{
    const float width = 0.92f;
    const float height = 0.34f;
    const float length = 1.82f;
    const float x0 = -0.5f * width, x1 = 0.5f * width;
    const float y0 = 0.0f, y1 = height;
    const float z0 = -0.5f * length, z1 = 0.5f * length;
    RtColor trim = {
        0.58f * color[0], 0.58f * color[1], 0.58f * color[2]
    };

    RiTransformBegin();
        RiTranslate(x, y, z);
        RiRotate(angle, 0.0f, 1.0f, 0.0f);

        SetColor(color);
        Box(x0, y0, z0, x1, y1, z1);

        /* ISO-style perimeter rails and corner posts. */
        SetColor(trim);
        Box(x0 - 0.018f, y0, z0 - 0.018f,
            x0 + 0.035f, y1, z1 + 0.018f);
        Box(x1 - 0.035f, y0, z0 - 0.018f,
            x1 + 0.018f, y1, z1 + 0.018f);
        Box(x0, y0 - 0.012f, z0 - 0.025f,
            x1, y0 + 0.035f, z1 + 0.025f);
        Box(x0, y1 - 0.035f, z0 - 0.025f,
            x1, y1 + 0.012f, z1 + 0.025f);

        /* Front doors: center seam, corrugation and paired locking bars. */
        Box(-0.012f, y0 + 0.022f, z0 - 0.030f,
             0.012f, y1 - 0.022f, z0 + 0.010f);
        for (int i = 1; i <= 7; ++i) {
            const float rx = x0 + width * i / 8.0f;
            Box(rx - 0.014f, y0 + 0.035f, z0 - 0.025f,
                rx + 0.014f, y1 - 0.035f, z0 + 0.015f);
        }
        const RtColor hardware = {0.58f, 0.61f, 0.59f};
        SetColor(hardware);
        const float lockX[4] = {-0.33f, -0.10f, 0.10f, 0.33f};
        for (int i = 0; i < 4; ++i) {
            Box(lockX[i] - 0.008f, y0 + 0.045f, z0 - 0.042f,
                lockX[i] + 0.008f, y1 - 0.045f, z0 - 0.026f);
            Box(lockX[i] - 0.025f, y0 + 0.105f, z0 - 0.047f,
                lockX[i] + 0.025f, y0 + 0.122f, z0 - 0.022f);
            Box(lockX[i] - 0.025f, y1 - 0.122f, z0 - 0.047f,
                lockX[i] + 0.025f, y1 - 0.105f, z0 - 0.022f);
        }

        /* Long-wall ribs make side-on stacks read at this small resolution. */
        SetColor(trim);
        for (int i = 1; i <= 11; ++i) {
            const float rz = z0 + length * i / 12.0f;
            Box(x0 - 0.024f, y0 + 0.035f, rz - 0.012f,
                x0 + 0.012f, y1 - 0.035f, rz + 0.012f);
            Box(x1 - 0.012f, y0 + 0.035f, rz - 0.012f,
                x1 + 0.024f, y1 - 0.035f, rz + 0.012f);
        }

        /* High-contrast serial plate and two stencil digits on the door. */
        const RtColor plate = {0.82f, 0.82f, 0.72f};
        const RtColor ink = {0.06f, 0.07f, 0.07f};
        SetColor(plate);
        Box(x0 + 0.075f, y0 + 0.075f, z0 - 0.043f,
            x0 + 0.225f, y0 + 0.285f, z0 - 0.032f);
        SetColor(ink);
        ContainerDigit((serial / 10) % 10, x0 + 0.095f, y0 + 0.095f,
                       z0 - 0.045f);
        ContainerDigit(serial % 10, x0 + 0.160f, y0 + 0.095f,
                       z0 - 0.045f);
    RiTransformEnd();
}

void BeamXY(float x0, float y0, float x1, float y1,
            float z, float thickness)
{
    const float dx = x1 - x0;
    const float dy = y1 - y0;
    const float length = std::sqrt(dx * dx + dy * dy);
    const float angle = std::atan2(dy, dx) * 57.2957795f;

    RiTransformBegin();
        RiTranslate(0.5f * (x0 + x1), 0.5f * (y0 + y1), z);
        RiRotate(angle, 0.0f, 0.0f, 1.0f);
        Box(-0.5f * length, -0.5f * thickness, -0.5f * thickness,
             0.5f * length,  0.5f * thickness,  0.5f * thickness);
    RiTransformEnd();
}

void DrawContainerSpreader(float x, float y, float z)
{
    const RtColor steel = {0.10f, 0.115f, 0.12f};
    const RtColor steelEdge = {0.21f, 0.23f, 0.23f};
    const RtColor cable = {0.055f, 0.060f, 0.062f};
    const RtColor safety = {0.48f, 0.31f, 0.055f};
    const float halfLength = 0.79f;
    const float halfWidth = 0.34f;

    RiSurface(NULL, NULL, 0);

    /* Four separate hoist lines terminate at the spreader's lifting frame. */
    SetColor(cable);
    BeamXY(x - 0.13f, 3.19f, x - 0.55f, y + 0.17f,
           z - halfWidth + 0.035f, 0.018f);
    BeamXY(x + 0.13f, 3.19f, x + 0.55f, y + 0.17f,
           z - halfWidth + 0.035f, 0.018f);
    BeamXY(x - 0.13f, 3.19f, x - 0.55f, y + 0.17f,
           z + halfWidth - 0.035f, 0.018f);
    BeamXY(x + 0.13f, 3.19f, x + 0.55f, y + 0.17f,
           z + halfWidth - 0.035f, 0.018f);

    /* Rectangular telescopic frame, sized to read against the nearby boxes. */
    SetColor(steel);
    Box(x - halfLength, y - 0.055f, z - halfWidth,
        x + halfLength, y + 0.055f, z - halfWidth + 0.075f);
    Box(x - halfLength, y - 0.055f, z + halfWidth - 0.075f,
        x + halfLength, y + 0.055f, z + halfWidth);
    Box(x - halfLength, y - 0.055f, z - halfWidth,
        x - halfLength + 0.095f, y + 0.055f, z + halfWidth);
    Box(x + halfLength - 0.095f, y - 0.055f, z - halfWidth,
        x + halfLength, y + 0.055f, z + halfWidth);

    SetColor(steelEdge);
    Box(x - 0.24f, y - 0.075f, z - 0.20f,
        x + 0.24f, y + 0.095f, z + 0.20f);
    Box(x - 0.055f, y + 0.075f, z - 0.10f,
        x + 0.055f, y + 0.20f, z + 0.10f);

    /* Corner twist-lock housings hang below the frame; the muted safety
     * stripes are painted metal, not emissive geometry. */
    const float cornerX[2] = {x - halfLength + 0.07f,
                              x + halfLength - 0.07f};
    const float cornerZ[2] = {z - halfWidth + 0.055f,
                              z + halfWidth - 0.055f};
    for (int ix = 0; ix < 2; ++ix) {
        for (int iz = 0; iz < 2; ++iz) {
            SetColor(steelEdge);
            Box(cornerX[ix] - 0.055f, y - 0.13f,
                cornerZ[iz] - 0.055f,
                cornerX[ix] + 0.055f, y - 0.045f,
                cornerZ[iz] + 0.055f);
            SetColor(safety);
            Box(cornerX[ix] - 0.030f, y - 0.175f,
                cornerZ[iz] - 0.030f,
                cornerX[ix] + 0.030f, y - 0.125f,
                cornerZ[iz] + 0.030f);
        }
    }
}

void DrawGantryCrane(void)
{
    const RtColor craneOrange = {0.60f, 0.20f, 0.035f};
    const RtColor craneLit = {0.88f, 0.38f, 0.055f};
    const RtColor craneDark = {0.17f, 0.12f, 0.085f};
    const float frontZ = 4.88f;
    const float backZ = 5.88f;

    RiSurface(NULL, NULL, 0);
    SetColor(craneOrange);

    /* Wide portal and splayed legs sit behind the Jaeger in the midground. */
    BeamXY(-5.65f, 0.02f, -4.75f, 3.45f, frontZ, 0.18f);
    BeamXY( 5.65f, 0.02f,  4.75f, 3.45f, frontZ, 0.18f);
    BeamXY(-5.65f, 0.02f, -4.75f, 3.45f, backZ, 0.18f);
    BeamXY( 5.65f, 0.02f,  4.75f, 3.45f, backZ, 0.18f);
    Box(-4.95f, 3.34f, frontZ - 0.12f,
         4.95f, 3.57f, frontZ + 0.12f);
    Box(-4.95f, 3.34f, backZ - 0.12f,
         4.95f, 3.57f, backZ + 0.12f);
    Box(-4.86f, 3.40f, frontZ, -4.68f, 3.52f, backZ);
    Box( 4.68f, 3.40f, frontZ,  4.86f, 3.52f, backZ);

    /* Triangular braces give the structure a readable industrial silhouette. */
    SetColor(craneLit);
    for (int i = 0; i < 5; ++i) {
        const float xa = -4.65f + i * 1.86f;
        const float xb = xa + 1.86f;
        if ((i & 1) == 0)
            BeamXY(xa, 3.38f, xb, 3.53f, frontZ - 0.14f, 0.055f);
        else
            BeamXY(xa, 3.53f, xb, 3.38f, frontZ - 0.14f, 0.055f);
    }
    BeamXY(-5.48f, 0.36f, -4.82f, 2.95f, frontZ - 0.13f, 0.055f);
    BeamXY(-5.38f, 0.75f, -4.93f, 2.55f, frontZ - 0.14f, 0.055f);
    BeamXY( 5.48f, 0.36f,  4.82f, 2.95f, frontZ - 0.13f, 0.055f);
    BeamXY( 5.38f, 0.75f,  4.93f, 2.55f, frontZ - 0.14f, 0.055f);

    /* Trolley and a properly scaled container spreader form the hoist. */
    SetColor(craneDark);
    Box(1.00f, 3.18f, frontZ - 0.23f,
        1.58f, 3.43f, frontZ + 0.23f);
    DrawContainerSpreader(1.29f, 1.63f, frontZ);
    SetColor(craneDark);
    Box(-5.80f, -0.02f, frontZ - 0.28f,
        -5.30f, 0.16f, frontZ + 0.28f);
    Box( 5.30f, -0.02f, frontZ - 0.28f,
         5.80f, 0.16f, frontZ + 0.28f);
}

void HarborLamp(float x, float z, float height)
{
    const RtColor metal = {0.16f, 0.17f, 0.17f};
    const RtColor housing = {0.08f, 0.09f, 0.10f};

    RiSurface(NULL, NULL, 0);
    SetColor(metal);
    Box(x - 0.035f, 0.0f, z - 0.035f,
        x + 0.035f, height, z + 0.035f);
    Box(x - 0.28f, height - 0.045f, z - 0.045f,
        x + 0.28f, height + 0.045f, z + 0.045f);

    SetColor(housing);
    Box(x - 0.25f, height - 0.15f, z - 0.10f,
        x - 0.06f, height - 0.025f, z + 0.09f);
    Box(x + 0.06f, height - 0.15f, z - 0.10f,
        x + 0.25f, height - 0.025f, z + 0.09f);

    RiSurface(CONSTANT, NULL, 0);
    SetColor(LAMP_WARM);
    Box(x - 0.225f, height - 0.135f, z - 0.115f,
        x - 0.085f, height - 0.055f, z - 0.095f);
    Box(x + 0.085f, height - 0.135f, z - 0.115f,
        x + 0.225f, height - 0.055f, z - 0.095f);
}

void DrawHarborWater(void)
{
    enum { NX = 36, NZ = 9 };
    RtPoint P[(NX + 1) * (NZ + 1)];
    RtInt nverts[NX * NZ];
    RtInt verts[4 * NX * NZ];

    for (int iz = 0; iz <= NZ; ++iz) {
        const float z = 4.22f + 4.97f * iz / NZ;
        for (int ix = 0; ix <= NX; ++ix) {
            const float x = -12.0f + 24.0f * ix / NX;
            const float wave = 0.018f * std::sin(2.55f * x + 4.35f * z) +
                               0.010f * std::sin(5.10f * x - 2.20f * z);
            const int k = iz * (NX + 1) + ix;
            P[k][0] = x;
            P[k][1] = -0.005f + wave;
            P[k][2] = z;
        }
    }
    int p = 0;
    for (int iz = 0; iz < NZ; ++iz) {
        for (int ix = 0; ix < NX; ++ix) {
            const int a = iz * (NX + 1) + ix;
            const int b = a + 1;
            const int d = a + NX + 1;
            const int c = d + 1;
            nverts[iz * NX + ix] = 4;
            /* Counter-clockwise from above: upward-facing water facets. */
            verts[p++] = a;
            verts[p++] = d;
            verts[p++] = c;
            verts[p++] = b;
        }
    }

    RiSurface(HarborWaterSurface, NULL, 0);
    SetColor(WATER_DEEP);
    RiPointsPolygons(NX * NZ, nverts, verts, P, NULL, NULL);

    /* Broken reflection tracks: deliberately segmented so they read as waves. */
    RiSurface(CONSTANT, NULL, 0);
    const RtColor warmWater = {0.26f, 0.12f, 0.025f};
    const RtColor coolWater = {0.035f, 0.14f, 0.22f};
    const RtColor redWater = {0.22f, 0.022f, 0.014f};
    for (int i = 0; i < 7; ++i) {
        const float z = 4.38f + 0.62f * i;
        const float width = 0.18f - 0.014f * i;
        SetColor((i & 1) ? coolWater : warmWater);
        Box(-4.80f - width, 0.020f, z,
            -4.80f + width, 0.027f, z + 0.16f);
        Box( 4.60f - width, 0.022f, z + 0.08f,
             4.60f + width, 0.029f, z + 0.26f);
    }
    SetColor(redWater);
    for (int i = 0; i < 5; ++i) {
        const float z = 4.72f + 0.75f * i;
        Box(0.88f - 0.025f * i, 0.021f, z,
            0.95f + 0.025f * i, 0.028f, z + 0.14f);
    }

    /* Sparse pale crests reinforce the water plane without dense geometry. */
    const RtColor foam = {0.11f, 0.26f, 0.34f};
    SetColor(foam);
    const float crests[][4] = {
        {-8.2f, 4.72f, -6.9f, 4.76f}, {-2.8f, 5.42f, -1.1f, 5.46f},
        { 1.8f, 4.98f,  3.0f, 5.02f}, { 6.2f, 6.10f,  8.0f, 6.14f},
        {-5.8f, 7.08f, -4.3f, 7.12f}, { 3.8f, 7.75f,  5.0f, 7.79f}
    };
    for (const auto &crest : crests)
        Box(crest[0], 0.018f, crest[1], crest[2], 0.025f, crest[3]);
}

/* A foreground harbor basin opens the distant channel into the right side of
 * the composition.  Its modest 12 x 20 grid is enough for readable facets at
 * 480 x 270 without creating an expensive micro-polygon field. */
void DrawForegroundBasin(void)
{
    enum { NX = 12, NZ = 20 };
    RtPoint P[(NX + 1) * (NZ + 1)];
    RtInt nverts[NX * NZ];
    RtInt verts[4 * NX * NZ];

    for (int iz = 0; iz <= NZ; ++iz) {
        const float z = 0.18f + 4.16f * iz / NZ;
        for (int ix = 0; ix <= NX; ++ix) {
            const float x = 1.82f + 1.86f * ix / NX;
            const float wave = 0.014f * std::sin(3.25f * x + 5.10f * z) +
                               0.008f * std::sin(6.20f * x - 2.80f * z);
            const int k = iz * (NX + 1) + ix;
            P[k][0] = x;
            P[k][1] = -0.012f + wave;
            P[k][2] = z;
        }
    }

    int p = 0;
    for (int iz = 0; iz < NZ; ++iz) {
        for (int ix = 0; ix < NX; ++ix) {
            const int a = iz * (NX + 1) + ix;
            const int b = a + 1;
            const int d = a + NX + 1;
            const int c = d + 1;
            nverts[iz * NX + ix] = 4;
            verts[p++] = a;
            verts[p++] = d;
            verts[p++] = c;
            verts[p++] = b;
        }
    }

    RiSurface(HarborWaterSurface, NULL, 0);
    SetColor(WATER_DEEP);
    RiPointsPolygons(NX * NZ, nverts, verts, P, NULL, NULL);

    /* Long, discontinuous tracks mimic lamps and windows broken by ripples. */
    RiSurface(CONSTANT, NULL, 0);
    const RtColor amber = {0.30f, 0.115f, 0.018f};
    const RtColor window = {0.045f, 0.17f, 0.22f};
    const RtColor red = {0.25f, 0.018f, 0.010f};
    for (int i = 0; i < 7; ++i) {
        const float z = 0.48f + i * 0.49f;
        const float taper = 0.080f - i * 0.006f;
        SetColor((i & 1) ? amber : window);
        Box(3.34f - taper, 0.022f, z,
            3.34f + taper, 0.027f, z + 0.105f);
        if (i > 1) {
            SetColor(window);
            Box(2.45f - 0.040f, 0.020f, z + 0.15f,
                2.45f + 0.040f, 0.026f, z + 0.235f);
        }
    }
    SetColor(red);
    for (int i = 0; i < 5; ++i) {
        const float z = 1.15f + i * 0.57f;
        Box(2.88f - 0.025f, 0.023f, z,
            2.88f + 0.025f, 0.028f, z + 0.11f);
    }

    const RtColor crest = {0.075f, 0.21f, 0.28f};
    SetColor(crest);
    const float ripples[][4] = {
        {2.05f, 0.72f, 2.72f, 0.742f},
        {3.02f, 1.46f, 3.49f, 1.482f},
        {2.28f, 2.18f, 3.08f, 2.202f},
        {2.98f, 2.92f, 3.50f, 2.942f},
        {2.02f, 3.56f, 2.80f, 3.582f}
    };
    for (const auto &ripple : ripples)
        Box(ripple[0], 0.024f, ripple[1],
            ripple[2], 0.031f, ripple[3]);
}

void DrawPuddlesAndReflections(void)
{
    const float puddleA[][2] = {
        {-5.85f, 2.40f}, {-4.20f, 2.18f}, {-3.45f, 2.62f},
        {-3.72f, 3.18f}, {-5.35f, 3.30f}, {-6.02f, 2.92f}
    };
    const float puddleB[][2] = {
        {4.12f, 2.55f}, {5.18f, 2.25f}, {6.25f, 2.62f},
        {6.12f, 3.38f}, {4.85f, 3.50f}, {4.02f, 3.10f}
    };
    const float puddleC[][2] = {
        {-1.48f, 0.72f}, {-0.36f, 0.56f}, {0.62f, 0.92f},
        {0.36f, 1.32f}, {-0.84f, 1.40f}, {-1.64f, 1.08f}
    };
    const float puddleD[][2] = {
        {0.48f, 2.95f}, {1.18f, 2.88f}, {1.60f, 3.20f},
        {1.52f, 3.70f}, {0.72f, 3.76f}
    };

    RiSurface(PuddleSurface, NULL, 0);
    FlatPatch(puddleA, 6, 0.008f);
    FlatPatch(puddleB, 6, 0.008f);
    FlatPatch(puddleC, 6, 0.008f);
    FlatPatch(puddleD, 5, 0.008f);

    /* Lamp, warning-light and arc-reactor streaks reflected in the puddles. */
    RiSurface(CONSTANT, NULL, 0);
    const RtColor warm = {0.38f, 0.17f, 0.028f};
    const RtColor reactor = {0.42f, 0.13f, 0.018f};
    const RtColor red = {0.34f, 0.025f, 0.014f};
    SetColor(warm);
    Box(-4.91f, 0.012f, 2.34f, -4.72f, 0.019f, 3.15f);
    Box( 4.38f, 0.012f, 2.48f,  4.59f, 0.019f, 3.35f);
    Box( 1.12f, 0.012f, 3.01f,  1.25f, 0.019f, 3.68f);
    SetColor(reactor);
    Box(-0.92f, 0.013f, 0.67f, -0.72f, 0.020f, 1.31f);
    Box(-0.42f, 0.013f, 0.62f, -0.29f, 0.020f, 1.23f);
    SetColor(red);
    Box(0.82f, 0.013f, 3.12f, 0.92f, 0.020f, 3.60f);
}

void Bollard(float x, float z)
{
    const RtColor base = {0.095f, 0.105f, 0.11f};
    const RtColor worn = {0.24f, 0.23f, 0.19f};
    RiSurface(NULL, NULL, 0);
    SetColor(base);
    Box(x - 0.13f, 0.0f, z - 0.12f, x + 0.13f, 0.055f, z + 0.12f);
    Box(x - 0.075f, 0.05f, z - 0.07f, x + 0.075f, 0.27f, z + 0.07f);
    SetColor(worn);
    Box(x - 0.14f, 0.24f, z - 0.10f, x + 0.14f, 0.31f, z + 0.10f);
}

void WarningBeacon(float x, float y, float z)
{
    const RtColor mount = {0.10f, 0.10f, 0.09f};
    const RtColor glow = {1.00f, 0.08f, 0.025f};
    RiSurface(NULL, NULL, 0);
    SetColor(mount);
    Box(x - 0.055f, y, z - 0.055f, x + 0.055f, y + 0.055f, z + 0.055f);
    RiSurface(CONSTANT, NULL, 0);
    SetColor(glow);
    Box(x - 0.038f, y + 0.055f, z - 0.038f,
        x + 0.038f, y + 0.145f, z + 0.038f);
}

void DrawDock(void)
{
    const RtColor red = {0.48f, 0.075f, 0.045f};
    const RtColor blue = {0.035f, 0.19f, 0.38f};
    const RtColor orange = {0.66f, 0.24f, 0.035f};
    const RtColor green = {0.055f, 0.29f, 0.15f};
    const RtColor teal = {0.025f, 0.30f, 0.33f};
    const RtColor cream = {0.48f, 0.40f, 0.25f};
    const RtColor rail = {0.24f, 0.25f, 0.24f};

    /* Two quay slabs leave a water-filled basin open on the right.  The main
     * slab remains wide beneath Gipsy, so both feet are on solid concrete. */
    RiSurface(WetConcreteSurface, NULL, 0);
    SetColor(CONCRETE);
    Box(-12.0f, -0.18f, -2.0f, 1.78f, 0.0f, 4.25f);
    Box( 3.72f, -0.18f, -2.0f, 12.0f, 0.0f, 4.25f);
    RiSurface(NULL, NULL, 0);
    SetColor(CONCRETE_EDGE);
    Box(-12.0f, -0.24f, 4.12f, 1.78f, 0.01f, 4.32f);
    Box( 3.72f, -0.24f, 4.12f, 12.0f, 0.01f, 4.32f);
    /* Tall sidewalls and a head wall make the inset basin unmistakable. */
    Box(1.70f, -0.30f, 0.12f, 1.84f, 0.035f, 4.25f);
    Box(3.66f, -0.30f, 0.12f, 3.80f, 0.035f, 4.25f);
    Box(1.70f, -0.30f, 0.08f, 3.80f, 0.035f, 0.23f);

    /* Quay safety stripe plus paired broken guides that converge on Gipsy. */
    RiSurface(CONSTANT, NULL, 0);
    SetColor(EDGE_YELLOW);
    Box(-12.0f, 0.003f, 3.92f, 1.72f, 0.018f, 4.12f);
    Box( 3.78f, 0.003f, 3.92f, 12.0f, 0.018f, 4.12f);
    Box(1.56f, 0.036f, 0.22f, 1.72f, 0.052f, 3.92f);
    Box(3.78f, 0.036f, 0.22f, 3.94f, 0.052f, 3.92f);
    Box(1.72f, 0.036f, 0.22f, 3.78f, 0.052f, 0.36f);
    for (int i = 0; i < 5; ++i) {
        const float z0 = -1.78f + i * 0.42f;
        const float z1 = z0 + 0.26f;
        const float t0 = (z0 + 1.78f) / 2.10f;
        const float t1 = (z1 + 1.78f) / 2.10f;
        GroundStripe(-1.55f + 1.05f * t0, z0,
                     -1.55f + 1.05f * t1, z1, 0.075f, 0.017f);
        GroundStripe( 1.35f - 0.80f * t0, z0,
                      1.35f - 0.80f * t1, z1, 0.075f, 0.017f);
    }

    /* Fine expansion joints and grated drains break up the wet quay plane. */
    const RtColor joint = {0.055f, 0.065f, 0.072f};
    const RtColor grate = {0.075f, 0.085f, 0.088f};
    SetColor(joint);
    for (int i = -3; i <= 3; ++i) {
        const float x = i * 2.25f + 0.65f;
        if (x > 1.70f && x < 3.80f)
            continue;
        Box(x - 0.012f, 0.002f, -1.70f, x + 0.012f, 0.010f, 4.05f);
    }
    SetColor(grate);
    Box(-6.4f, 0.004f, 3.72f, 1.58f, 0.016f, 3.84f);
    Box(3.92f, 0.004f, 3.72f, 7.2f, 0.016f, 3.84f);
    for (int i = -16; i <= 16; ++i) {
        const float x = i * 0.38f;
        if (x > 1.58f && x < 3.92f)
            continue;
        SetColor((i & 1) ? CONCRETE_EDGE : joint);
        Box(x - 0.018f, 0.016f, 3.71f, x + 0.018f, 0.022f, 3.85f);
    }

    /* Rubber fenders project from both basin walls at regular intervals. */
    const RtColor fender = {0.035f, 0.042f, 0.045f};
    const RtColor fenderBand = {0.16f, 0.17f, 0.16f};
    for (int i = 0; i < 5; ++i) {
        const float z = 0.68f + i * 0.72f;
        SetColor(fender);
        Box(1.78f, -0.10f, z - 0.13f, 1.94f, 0.16f, z + 0.13f);
        Box(3.56f, -0.10f, z - 0.13f, 3.72f, 0.16f, z + 0.13f);
        SetColor(fenderBand);
        Box(1.77f, 0.02f, z - 0.15f, 1.95f, 0.065f, z + 0.15f);
        Box(3.55f, 0.02f, z - 0.15f, 3.73f, 0.065f, z + 0.15f);
    }

    /* Warm pools and inset markers add industrial practical lighting. */
    const RtColor pool = {0.21f, 0.095f, 0.020f};
    const RtColor marker = {0.58f, 0.25f, 0.035f};
    SetColor(pool);
    Box(-5.45f, 0.002f, 2.48f, -4.15f, 0.010f, 3.82f);
    Box( 3.98f, 0.002f, 2.48f,  5.28f, 0.010f, 3.82f);
    SetColor(marker);
    for (int i = -5; i <= 5; ++i) {
        const float x = i * 1.05f;
        if (x > 1.65f && x < 3.85f)
            continue;
        Box(x - 0.055f, 0.004f, 3.82f,
            x + 0.055f, 0.025f, 3.92f);
    }

    DrawPuddlesAndReflections();

    /* Existing stacks step from large foreground masses to smaller midground
     * groups, keeping the converging center approach clear. */
    RiSurface(NULL, NULL, 0);
    Container(-3.05f, 0.01f, -0.32f, -7.0f, red, 17);
    Container(-3.05f, 0.36f, -0.32f, -7.0f, blue, 42);
    Container(-4.15f, 0.01f,  2.30f,  3.0f, orange, 28);
    Container(-4.15f, 0.36f,  2.30f,  3.0f, green, 61);
    Container(-4.15f, 0.71f,  2.30f,  3.0f, cream, 5);
    Container( 4.65f, 0.01f,  0.18f,  8.0f, teal, 34);
    Container( 4.65f, 0.36f,  0.18f,  8.0f, orange, 73);
    Container( 5.70f, 0.01f,  2.85f, -3.0f, blue, 49);
    Container( 5.70f, 0.36f,  2.85f, -3.0f, red, 86);

    HarborLamp(-4.80f, 3.75f, 2.25f);
    HarborLamp( 4.35f, 3.75f, 2.25f);

    /* Layered guard rail, detailed mooring bollards and red warning beacons. */
    RiSurface(NULL, NULL, 0);
    SetColor(rail);
    for (int i = -6; i <= 6; ++i) {
        const float x = i * 0.98f;
        if (x > 1.72f && x < 3.78f)
            continue;
        Box(x - 0.038f, 0.0f, 4.08f,
            x + 0.038f, 0.43f, 4.15f);
    }
    Box(-6.0f, 0.18f, 4.08f, 1.72f, 0.225f, 4.15f);
    Box(-6.0f, 0.38f, 4.08f, 1.72f, 0.435f, 4.15f);
    Box(3.78f, 0.18f, 4.08f, 6.0f, 0.225f, 4.15f);
    Box(3.78f, 0.38f, 4.08f, 6.0f, 0.435f, 4.15f);
    const RtColor railStripe = {0.42f, 0.30f, 0.045f};
    SetColor(railStripe);
    for (int i = -5; i <= 5; i += 2) {
        const float x = i * 0.98f;
        if (x > 1.72f && x < 3.78f)
            continue;
        Box(x - 0.045f, 0.18f, 4.06f,
            x + 0.045f, 0.43f, 4.17f);
    }

    Bollard(-2.05f, 3.96f);
    Bollard(-5.15f, 3.94f);
    Bollard( 1.43f, 1.08f);
    Bollard( 1.43f, 3.38f);
    Bollard( 4.07f, 1.08f);
    Bollard( 4.07f, 3.38f);
    WarningBeacon(-5.88f, 0.43f, 4.115f);
    WarningBeacon( 0.00f, 0.43f, 4.115f);
    WarningBeacon( 5.88f, 0.43f, 4.115f);
}

void DrawHarbor(void)
{
    DrawNightSky();
    DrawCity();
    DrawHarborWater();
    DrawForegroundBasin();
    DrawGantryCrane();
    DrawDock();
}

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
            RiSurface(GipsyCinematicSurface, &gp, sizeof gp);
        else
            RiSurface(NULL, NULL, 0);

        RiPointsPolygons(g.npolys, g.nverts, g.verts, g.P, g.N, g.st);
    }
}

} // namespace

void GipsyDanger(void)
{
    float fov = 36.0f;
    RtMatrix camera;
    LookAt(EYE, TARGET, camera);

    RiBegin(RI_NULL);
        RiFormat(480, 270);
        RiPixelSamples(2, 2);
        RiDisplay("4_OriginalScene/GipsyDanger.png", "", "");

        RiFrameBegin(0);
            RiProjection(RI_PERSPECTIVE, "fov", &fov);
            RiConcatTransform(camera);

            /* Preserve the model's seven established OBJ material textures. */
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
                DrawHarbor();

                ObjMesh *mesh = obj_load("models/GipsyDanger/gipsy_danger.obj");
                RiTransformBegin();
                    /* A small offset from the established 150-degree front
                     * reveals armor depth while keeping the face readable. */
                    RiRotate(140.0f, 0.0f, 1.0f, 0.0f);
                    DrawGipsyDanger(mesh);
                RiTransformEnd();
                obj_free(mesh);
            RiWorldEnd();
        RiFrameEnd();
    RiEnd();
}
