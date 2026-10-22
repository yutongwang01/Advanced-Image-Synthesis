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
    for (int i = 0; i < 3; ++i) {
        c->Ci[i] = c->Cs[i];
        c->Oi[i] = c->Os[i];
    }
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
/* TODO: the lighting. For now, the surface color, unlit. */
void STANDARD(RtShaderContext *c)
{
    struct Light {
        float position[3];
        float color[3];
        float intensity;
    };

    const Light lights[] = {
        {{-6.0f, 8.0f, -4.0f}, {1.0f, 0.95f, 0.85f}, 1.0f},
        {{ 7.0f, 3.0f, -2.0f}, {0.6f, 0.7f,  1.0f},  0.4f},
        {{ 0.0f, 5.0f, 18.0f}, {1.0f, 1.0f,  1.0f},  0.5f}
    };

    constexpr float ka = 0.15f;
    constexpr float kd = 0.8f;
    constexpr float ks = 0.3f;

    auto normalize = [](float vector[3]) {
        const float length = std::sqrt(
            vector[0] * vector[0] +
            vector[1] * vector[1] +
            vector[2] * vector[2]
        );

        if (length > 0.0f) {
            for (int i = 0; i < 3; ++i) {
                vector[i] /= length;
            }
        }
    };

    auto dot = [](const float a[3], const float b[3]) {
        return a[0] * b[0] +
               a[1] * b[1] +
               a[2] * b[2];
    };

    float N[3] = {
        c->N[0], c->N[1], c->N[2]
    };

    float V[3] = {
        -c->P[0], -c->P[1], -c->P[2]
    };

    normalize(N);
    normalize(V);

    // Start with ambient illumination.
    for (int channel = 0; channel < 3; ++channel) {
        c->Ci[channel] = ka * c->Cs[channel];
        c->Oi[channel] = c->Os[channel];
    }

    for (const Light &light : lights) {
        float L[3] = {
            light.position[0] - c->P[0],
            light.position[1] - c->P[1],
            light.position[2] - c->P[2]
        };

        normalize(L);

        const float ndotl = dot(N, L);
        const float diffuse = std::max(ndotl, 0.0f);

        float R[3] = {
            2.0f * ndotl * N[0] - L[0],
            2.0f * ndotl * N[1] - L[1],
            2.0f * ndotl * N[2] - L[2]
        };

        const float specular =
            std::pow(std::max(dot(R, V), 0.0f), 32.0f);

        for (int channel = 0; channel < 3; ++channel) {
            const float illumination =
                light.intensity * light.color[channel];

            c->Ci[channel] +=
                illumination *
                (kd * c->Cs[channel] * diffuse +
                 ks * specular);
        }
    }
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

/* TODO: the checks. For now, every check is lit as the light one. */
void CHECKERBOARD(RtShaderContext *c)
{
    // Default: 8 x 4 checks, with STANDARD on both branches.
    // The dark branch uses black Cs.
    const CheckerParams defaults = {8, 4};

    const CheckerParams *params = c->params
        ? static_cast<const CheckerParams *>(c->params)
        : &defaults;

    const int sizex = std::max(1, params->sizex);
    const int sizey = std::max(1, params->sizey);

    // Clamp endpoints so u=1 and v=1 belong to the last check.
    const double u = std::clamp(
        static_cast<double>(c->u), 0.0, 1.0);
    const double v = std::clamp(
        static_cast<double>(c->v), 0.0, 1.0);

    const int i = std::min(
        static_cast<int>(std::floor(u * sizex)),
        sizex - 1
    );

    const int j = std::min(
        static_cast<int>(std::floor(v * sizey)),
        sizey - 1
    );

    const bool lightCheck = ((i % 2) == (j % 2));

    // Save inputs that this dispatcher temporarily changes.
    const void *savedParams = c->params;
    RtColor savedColor;

    for (int channel = 0; channel < 3; ++channel) {
        savedColor[channel] = c->Cs[channel];
    }

    RtShaderFn shader;

    if (lightCheck) {
        shader = params->light ? params->light : STANDARD;
        c->params = params->light_params;
    } else {
        shader = params->dark ? params->dark : STANDARD;
        c->params = params->dark_params;

        // Only the default dark shader forces Cs to black.
        if (!params->dark) {
            for (int channel = 0; channel < 3; ++channel) {
                c->Cs[channel] = 0.0f;
            }
        }
    }

    shader(c);

    // Keep Ci/Oi from the selected shader; restore its inputs.
    c->params = savedParams;

    for (int channel = 0; channel < 3; ++channel) {
        c->Cs[channel] = savedColor[channel];
    }
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
/* TODO: the noise. For now, none, so BUMPY moves nothing. */
double perlinNoise(double x, double y, double z)
{
    static const int permutation[256] = {
        151,160,137,91,90,15,131,13,201,95,96,53,194,233,7,225,
        140,36,103,30,69,142,8,99,37,240,21,10,23,190,6,148,
        247,120,234,75,0,26,197,62,94,252,219,203,117,35,11,32,
        57,177,33,88,237,149,56,87,174,20,125,136,171,168,68,175,
        74,165,71,134,139,48,27,166,77,146,158,231,83,111,229,122,
        60,211,133,230,220,105,92,41,55,46,245,40,244,102,143,54,
        65,25,63,161,1,216,80,73,209,76,132,187,208,89,18,169,
        200,196,135,130,116,188,159,86,164,100,109,198,173,186,3,64,
        52,217,226,250,124,123,5,202,38,147,118,126,255,82,85,212,
        207,206,59,227,47,16,58,17,182,189,28,42,223,183,170,213,
        119,248,152,2,44,154,163,70,221,153,101,155,167,43,172,9,
        129,22,39,253,19,98,108,110,79,113,224,232,178,185,112,104,
        218,246,97,228,251,34,242,193,238,210,144,12,191,179,162,241,
        81,51,145,235,249,14,239,107,49,192,214,31,181,199,106,157,
        184,84,204,176,115,121,50,45,127,4,150,254,138,236,205,93,
        222,114,67,29,24,72,243,141,128,195,78,66,215,61,156,180
    };

    auto perm = [&](int index) {
        return permutation[index & 255];
    };

    auto fade = [](double t) {
        return t * t * t * (t * (6.0 * t - 15.0) + 10.0);
    };

    auto lerp = [](double a, double b, double t) {
        return a + t * (b - a);
    };

    auto gradient = [](int hash, double dx, double dy, double dz) {
        const int h = hash & 15;
        const double first = h < 8 ? dx : dy;
        const double second =
            h < 4 ? dy : ((h == 12 || h == 14) ? dx : dz);

        return ((h & 1) ? -first : first)
             + ((h & 2) ? -second : second);
    };

    const double floorX = std::floor(x);
    const double floorY = std::floor(y);
    const double floorZ = std::floor(z);

    // Wrap before integer conversion, including negative coordinates.
    auto latticeIndex = [](double coordinate) {
        double wrapped = std::fmod(coordinate, 256.0);
        if (wrapped < 0.0) {
            wrapped += 256.0;
        }
        return static_cast<int>(wrapped);
    };

    const int ix = latticeIndex(floorX);
    const int iy = latticeIndex(floorY);
    const int iz = latticeIndex(floorZ);

    const double fx = x - floorX;
    const double fy = y - floorY;
    const double fz = z - floorZ;

    const double wx = fade(fx);
    const double wy = fade(fy);
    const double wz = fade(fz);

    double corners[2][2][2];

    for (int dz = 0; dz < 2; ++dz) {
        for (int dy = 0; dy < 2; ++dy) {
            for (int dx = 0; dx < 2; ++dx) {
                const int hash =
                    perm(perm(perm(ix + dx) + iy + dy) + iz + dz);

                corners[dz][dy][dx] = gradient(
                    hash, fx - dx, fy - dy, fz - dz
                );
            }
        }
    }

    const double lower = lerp(
        lerp(corners[0][0][0], corners[0][0][1], wx),
        lerp(corners[0][1][0], corners[0][1][1], wx),
        wy
    );

    const double upper = lerp(
        lerp(corners[1][0][0], corners[1][0][1], wx),
        lerp(corners[1][1][0], corners[1][1][1], wx),
        wy
    );

    return lerp(lower, upper, wz);
}

/* ---------------------------------------------------------------- BUMPY */

/* Displace P along the unit normal N by
 *   h = amplitude * sum(w_e * perlinNoise(2^e * Po)) / sum(w_e)
 * over e = min_freq_exp .. max_freq_exp, with w_e = 2^(min_freq_exp - e):
 * each octave counts half as much as the one below it, and dividing by the
 * total weight keeps |h| <= amplitude whatever the octaves. Evaluate the
 * noise at Po (object space), so the bumps stick to the object as it
 * moves. The parameters are a BumpParams (shaders.h) at c->params. */
/* TODO: the displacement. For now, P stays where it is. */
void BUMPY(RtShaderContext *c)
{
    if (!c->params) {
        return;
    }

    const BumpParams *params =
        static_cast<const BumpParams *>(c->params);

    if (params->amplitude == 0.0f ||
        params->min_freq_exp > params->max_freq_exp) {
        return;
    }

    double normal[3] = {
        c->N[0], c->N[1], c->N[2]
    };

    const double length = std::sqrt(
        normal[0] * normal[0] +
        normal[1] * normal[1] +
        normal[2] * normal[2]
    );

    if (length == 0.0) {
        return;
    }

    for (int channel = 0; channel < 3; ++channel) {
        normal[channel] /= length;
    }

    double noiseSum = 0.0;
    double weightSum = 0.0;
    double weight = 1.0;

    int exponent = params->min_freq_exp;

    while (true) {
        const double frequency = std::ldexp(1.0, exponent);

        noiseSum += weight * perlinNoise(
            frequency * c->Po[0],
            frequency * c->Po[1],
            frequency * c->Po[2]
        );

        weightSum += weight;

        if (exponent == params->max_freq_exp) {
            break;
        }

        weight *= 0.5;
        ++exponent;
    }

    const double height =
        params->amplitude * noiseSum / weightSum;

    for (int channel = 0; channel < 3; ++channel) {
        c->P[channel] +=
            static_cast<float>(height * normal[channel]);
    }
}