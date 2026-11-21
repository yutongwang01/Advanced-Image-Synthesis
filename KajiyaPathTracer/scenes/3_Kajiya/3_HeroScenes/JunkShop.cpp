/*
 * ECE/CS 285 – HW4 Hero Scene: JunkShop
 * A cluttered junk shop at night, lit by its own 17 lamps alone (ten
 * emitting quads hidden from the camera, seven point lights), its
 * surfaces PBR texture sets read per pixel by the scene's own shader.
 * 2.2 million triangles. 1728 x 960, 512 spp, halton, seed 0, depth 12.
 * Model: The Junk Shop by Alex Treviño (concept Anaïs Maamar), the Blender
 * 2.81 splash, blender.org demo files, CC-BY.
 *
 * Output: renders/<integrator>/3_HeroScenes/JunkShop.png
 */

#include "rt.h"
#include "obj.h"
#include "sceneutil.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

/* shaders ------------------------------------------------------------ */

/* PBR_MAPS: a textured surface whose metallic and roughness come from
 * maps (the MTL's map_Pm and map_Pr, red channel), whose normal is bent
 * by a tangent-space normal map (norm) about the hit's dPdu and dPdv,
 * with the emission map (map_Ke) times a strength for the screens and
 * lamps. A handle of -1 leaves that field as bound. The specular is 1
 * for a dielectric (the Fresnel coat of its IOR), the base color for a
 * metal, and in between what gives a half-metal the Principled BSDF's
 * reflectance (below). */
typedef struct {
    RtInt base, metallic, roughness, emission, normal;
    RtFloat emissionScale;
    RtColor emissionTint;
    RtFloat baseEmission; /* emission from the base color itself, times the tint (a self-lit card) */
    RtInt transmission;  /* a map (red) of how opaque: glass (ior 1.45) where it is black; -1: none */
    RtFloat normalStrength; /* how far N goes toward the map's normal (the file's Normal Map strength) */
    RtFloat tiltDarkening;  /* 1: the albedo scaled by the mapped normal's cosine to the surface (below) */
} PbrMapsParams;

/* Cycles' ensure_valid_specular_reflection (kernel/osl/.../node_normal_map,
 * also run on every shading normal): N is bent back toward Ng, in the
 * plane of N and Ng, just far enough that the reflection of the view
 * direction V stays above the surface, so a vertex or mapped normal
 * that faces away from the viewer never blacks a pixel out. */
static Vec3 validReflection(const Vec3 &Ng, const Vec3 &V, const Vec3 &N)
{
    const Vec3 R = N * (2 * dot(N, V)) - V;
    const float Iz = dot(V, Ng);
    const float threshold = std::min(0.9f * Iz, 0.01f);
    if (dot(R, Ng) >= threshold) {
        return N;
    }
    const Vec3 Xraw = N - Ng * dot(N, Ng);
    if (lengthSquared(Xraw) < 1e-12f) {
        return Ng;
    }
    const Vec3 X = normalize(Xraw);
    const float Ix = dot(V, X), Ix2 = Ix * Ix, Iz2 = Iz * Iz;
    const float a = Ix2 + Iz2;
    const float b = std::sqrt(std::max(0.0f, Ix2 * (a - threshold * threshold)));
    const float cc = Iz * threshold + a;
    const float fac = 0.5f / a;
    const float N1z2 = fac * (b + cc), N2z2 = fac * (-b + cc);
    bool valid1 = N1z2 > 1e-5f && N1z2 <= 1 + 1e-5f, valid2 = N2z2 > 1e-5f && N2z2 <= 1 + 1e-5f;
    float nx, nz;
    if (valid1 && valid2) {
        const float n1x = std::sqrt(std::max(0.0f, 1 - N1z2)), n1z = std::sqrt(std::max(0.0f, N1z2));
        const float n2x = std::sqrt(std::max(0.0f, 1 - N2z2)), n2z = std::sqrt(std::max(0.0f, N2z2));
        const float R1 = 2 * (n1x * Ix + n1z * Iz) * n1z - Iz, R2 = 2 * (n2x * Ix + n2z * Iz) * n2z - Iz;
        valid1 = R1 >= 1e-5f;
        valid2 = R2 >= 1e-5f;
        const bool one = (valid1 && valid2) ? R1 < R2 : R1 > R2;
        nx = one ? n1x : n2x;
        nz = one ? n1z : n2z;
    } else if (valid1 || valid2) {
        const float z2 = valid1 ? N1z2 : N2z2;
        nx = std::sqrt(std::max(0.0f, 1 - z2));
        nz = std::sqrt(std::max(0.0f, z2));
    } else {
        return Ng;
    }
    return normalize(X * nx + Ng * nz);
}

static void PBR_MAPS(RtShadeContext *c)
{
    const PbrMapsParams *p = (const PbrMapsParams *)c->params;
    RtFloat rgba[4];
    float cosTilt = 1; /* the mapped normal's cosine to the surface */
    if (p->normal >= 0) {
        /* the map's (r, g, b) is the normal in the frame (dPdu, -dPdv, N),
         * each channel 0..1 for -1..1; -dPdv because t runs down the
         * texture here and the map's green runs up it (OpenGL convention) */
        rtTexture(p->normal, c->u, c->v, rgba);
        const Vec3 N(c->N), T = normalize(Vec3(c->dPdu)), B = normalize(Vec3(c->dPdv));
        if (lengthSquared(Vec3(c->dPdu)) > 0 && lengthSquared(Vec3(c->dPdv)) > 0) {
            const Vec3 mapped = normalize(T * (2 * rgba[0] - 1) - B * (2 * rgba[1] - 1) + N * (2 * rgba[2] - 1));
            const Vec3 n = normalize(N + (mapped - N) * p->normalStrength); /* Blender's strength: a lerp from N */
            if (dot(n, Vec3(c->Ng)) > 0) { /* never bent past the surface */
                n.store(c->N);
            }
            /* Cycles shades its diffuse with the mapped normal as it is and
             * bends it back (below) for the glossy lobe only. Here one N
             * serves both, and the bend is what keeps a surface seen at a
             * grazing angle from going black, so where the map tilts far
             * (the floor's scuffs, 45 degrees) the diffuse would stay flat:
             * the tilt's cosine darkens the albedo instead, which is what
             * the mapped normal does to N . L for the lamps overhead. */
            cosTilt = std::max(0.0f, dot(n, N));
        }
    }
    validReflection(Vec3(c->Ng), -Vec3(c->I), Vec3(c->N)).store(c->N);
    if (p->base >= 0) {
        rtTexture(p->base, c->u, c->v, rgba);
        for (int i = 0; i < 3; i++) {
            c->m.albedo[i] = rgba[i];
        }
    }
    if (p->tiltDarkening > 0) {
        for (int i = 0; i < 3; i++) {
            c->m.albedo[i] *= 1 - p->tiltDarkening * (1 - cosTilt);
        }
    }
    if (p->metallic >= 0) {
        rtTexture(p->metallic, c->u, c->v, rgba);
        c->m.metallic = rgba[0];
    }
    if (p->roughness >= 0) {
        rtTexture(p->roughness, c->u, c->v, rgba);
        c->m.roughness = rgba[0];
    }
    /* The specular is the reflectance head on (rt.h: Schlick's F0, metal
     * or not), so a pixel gets the Principled BSDF's: the ior's F0 for a
     * dielectric, the base color for a metal, lerped by the metalness. */
    {
        const float r = (c->m.ior - 1) / (c->m.ior + 1), F0 = r * r, m = c->m.metallic;
        for (int i = 0; i < 3; i++) {
            c->m.specular[i] = F0 * (1 - m) + m * c->m.albedo[i];
        }
    }
    if (p->transmission >= 0) {
        rtTexture(p->transmission, c->u, c->v, rgba);
        c->m.transmission = 1 - rgba[0];
        c->m.ior = 1.45f;
        if (c->m.transmission > 0.5f) {
            for (int i = 0; i < 3; i++) {
                c->m.albedo[i] = 0; /* clear glass: no body color, or the albedo would tint what it transmits */
            }
        }
    }
    if (p->emission >= 0) {
        rtTexture(p->emission, c->u, c->v, rgba);
        for (int i = 0; i < 3; i++) {
            c->m.emission[i] = p->emissionScale * p->emissionTint[i] * rgba[i];
        }
    }
    if (p->baseEmission > 0) { /* a pure glow card: no lamp prints on it */
        for (int i = 0; i < 3; i++) {
            c->m.emission[i] += p->baseEmission * p->emissionTint[i] * c->m.albedo[i];
            c->m.albedo[i] = 0;
            c->m.specular[i] = 0;
        }
    }
}

/* the hook ------------------------------------------------------------ */

/* The emission strengths of the file's Emission nodes (the MTL's Ke
 * holds only the map): the screens, the robots' eyes and the radio dials.
 * Emission a shader sets is seen but is not a light, so these draw calls
 * are bound without their Ke. The small robot's map holds its eyes,
 * 0.3 in the file; 2.5 here (its speech bubble is the Clay material,
 * below). */
static const struct { const char *material; float strength; RtColor tint; } EMISSIVE[] = {
    {"MrCatPC", 2.0f, {1, 1, 1}},
    {"Old_Robots", 1.0f, {1, 1, 1}},
    {"RadioNes", 3.0f, {1, 1, 1}},
    {"Stoplight", 1.5f, {1, 1, 1}},
    {"Wheel_Robot", 0.6f, {1, 1, 1}}, /* the file's 0.3; more self-lights its head */
};

/* The file's Normal Map strengths other than 1. */
static const struct { const char *material; float strength; } NORMAL_STRENGTH[] = {
    {"Cristal", 0.1f},
    {"Background_Wall", 0.5f},
};

enum { MAX_MATERIALS = 64 };

/* The map handles per MTL material, loaded the first time a group needs
 * them (-2: not yet). ObjDraw loads the base color itself. */
typedef struct {
    ObjMesh *mesh;
    RtInt metallic[MAX_MATERIALS], roughness[MAX_MATERIALS], emission[MAX_MATERIALS], normal[MAX_MATERIALS],
        transmission[MAX_MATERIALS];
} JunkHook;

/* The glass (the jars, the radio's tubes, the cat's visor, the scale's
 * front) is, in the file, transparent to shadow rays and to reflection
 * rays (a Light Path trick). Here the camera ray and its first bounce see
 * it, so a ray that refracted in leaves through the far wall; shadow rays
 * (depth = -1) and deeper rays pass through: the lamps light what sits
 * behind glass (the scale's dial by the strips inside its housing), no
 * light-piping of the filaments' glow around the tube domes, and the
 * view through a dome is not displaced. */
static RtInt GLASS_CUTOUT(RtShadeContext *c) { return c->depth >= 0 && c->depth <= 1 ? 1 : 0; }

/* A cutout by the MTL's opacity map with a threshold of its own: the
 * signs' tape is a quarter opaque in the map, and ObjDraw's cutout (half)
 * took it away. */
typedef struct { RtInt map; RtFloat threshold; } AlphaParams;
static RtInt OPACITY_CUTOUT(RtShadeContext *c)
{
    const AlphaParams *p = (const AlphaParams *)c->params;
    RtFloat rgba[4];
    rtTexture(p->map, c->u, c->v, rgba);
    return rgba[0] >= p->threshold ? 1 : 0;
}

static RtInt LoadMap(RtInt &handle, const char *path, int srgb)
{
    if (handle == -2) {
        handle = path[0] ? rtMakeTexture(path, srgb) : -1;
    }
    return handle;
}

static void JunkShopMaterials(const char *material, void *user)
{
    JunkHook *hook = (JunkHook *)user;
    const ObjMesh *mesh = hook->mesh;
    int k = 0;
    while (k < mesh->nmaterials && std::strcmp(mesh->materials[k].name, material) != 0) {
        k++;
    }
    if (k >= mesh->nmaterials || k >= MAX_MATERIALS) {
        return;
    }
    const ObjMaterial &om = mesh->materials[k];
    if (!om.map_Kd[0] && !om.map_Pm[0] && !om.map_Pr[0] && !om.map_Ke[0] && !om.map_norm[0]) {
        return; /* the glass, the hair, the clay: what the MTL says */
    }

    float strength = 0, baseEmission = 0, normalStrength = 1;
    const float tiltDarkening = std::strncmp(material, "Floor_", 6) == 0 ? 1.0f : 0.0f;
    RtColor tint = {1, 1, 1};
    for (size_t i = 0; i < sizeof NORMAL_STRENGTH / sizeof NORMAL_STRENGTH[0]; i++) {
        if (std::strcmp(material, NORMAL_STRENGTH[i].material) == 0) {
            normalStrength = NORMAL_STRENGTH[i].strength;
        }
    }
    for (size_t i = 0; i < sizeof EMISSIVE / sizeof EMISSIVE[0]; i++) {
        if (std::strcmp(material, EMISSIVE[i].material) == 0) {
            strength = EMISSIVE[i].strength;
            for (int j = 0; j < 3; j++) {
                tint[j] = EMISSIVE[i].tint[j];
            }
        }
    }
    /* The small robot's speech bubble (a card cut out of its map; its own
     * material "Bubble" since the file keeps it in one object with the
     * bin of junk and a box) has no emission in the file: its glow there
     * is the compositor's. Lit by the lamps, it casts the key's shadow on
     * the floor beside the robot, as in the file; a glowing card lit the
     * floor there instead. */
    if (om.map_Ke[0]) {
        RtMaterial m = ObjMaterialToRt(om);
        m.emission[0] = m.emission[1] = m.emission[2] = 0;
        rtMaterial(&m);
    }
    /* The tablecloth and the glass vase on the counter share a material
     * whose opacity map says where it is glass (the file drives the
     * transmission with it); the MTL made that a cutout, which took the
     * vase away. Here the map is the transmission instead: no cutout,
     * glass where it is black. */
    RtInt transmission = -1;
    if (std::strcmp(material, "Table_cloth") == 0 && om.map_d[0]) {
        rtCutout(NULL, NULL, 0);
        transmission = LoadMap(hook->transmission[k], om.map_d, 0);
    }
    if (std::strcmp(material, "Cristal") == 0) {
        rtCutout(GLASS_CUTOUT, NULL, 0);
    }
    if (std::strcmp(material, "Ads") == 0 && om.map_d[0]) {
        AlphaParams a = {LoadMap(hook->transmission[k], om.map_d, 0), 0.15f};
        rtCutout(OPACITY_CUTOUT, &a, sizeof a);
    }
    PbrMapsParams p = {om.kdHandle, LoadMap(hook->metallic[k], om.map_Pm, 0), LoadMap(hook->roughness[k], om.map_Pr, 0),
                       strength > 0 ? LoadMap(hook->emission[k], om.map_Ke, 1) : -1,
                       LoadMap(hook->normal[k], om.map_norm, 0), strength,
                       {tint[0], tint[1], tint[2]}, baseEmission, transmission, normalStrength, tiltDarkening};
    rtSurface(PBR_MAPS, &p, sizeof p);
}

/* lights -------------------------------------------------------------- */

/* The file's ten area lights as emitting quads where the file has them
 * (center, the lamp's own right and up axes, size, and Cycles' radiance
 * for P watts over area A, P / (pi A), times the color), and its seven
 * point lights (three more are 0 W). Cycles hides lamps from the camera
 * and lets them cast no shadow, and so does LAMP: a cutout on each lamp
 * that rejects camera rays (depth = 0) and shadow rays (depth = -1) and
 * keeps bounce rays, so the 9 x 10 m key over the shop, which is in the
 * frame, lights the shop from where the file put it (just above the
 * ceiling, shining in under its front edge onto the counter, the
 * shopkeeper and the small robot) without being seen or shading. */
static RtInt LAMP(RtShadeContext *c) { return c->depth > 0 ? 1 : 0; }

/* the smoke card ------------------------------------------------------ */

/* The robot's puff of smoke is a volume in the file, which nothing here
 * renders: a card facing the camera carries the puff as Cycles rendered
 * it alone, an RGBA image whose color is the puff's radiance and whose
 * alpha its density. SMOKE_CARD shows that
 * color as emission (lighting nothing); COVERAGE makes the card solid
 * with the probability the alpha gives, by a hash of the hit point and
 * the ray's direction, so a pixel's samples and its shadow rays average
 * to the puff's translucency. */
typedef struct { RtInt map; } SmokeParams;

static void SMOKE_CARD(RtShadeContext *c)
{
    const SmokeParams *p = (const SmokeParams *)c->params;
    RtFloat rgba[4];
    rtTexture(p->map, c->u, c->v, rgba);
    for (int i = 0; i < 3; i++) {
        c->m.albedo[i] = 0;
        c->m.specular[i] = 0;
        c->m.emission[i] = rgba[i];
    }
}

/* A number in [0, 1) from the bits of a few floats (FNV-1a, then
 * MurmurHash3's finalizer to spread the bits). */
static float Hash01(const float *x, int n)
{
    uint32_t h = 2166136261u;
    for (int i = 0; i < n; i++) {
        uint32_t bits;
        std::memcpy(&bits, &x[i], sizeof bits);
        h = (h ^ bits) * 16777619u;
    }
    h ^= h >> 16;
    h *= 0x85ebca6bu;
    h ^= h >> 13;
    h *= 0xc2b2ae35u;
    h ^= h >> 16;
    return (float)(h >> 8) * (1.0f / 16777216.0f);
}

static RtInt COVERAGE(RtShadeContext *c)
{
    const SmokeParams *p = (const SmokeParams *)c->params;
    RtFloat rgba[4];
    rtTexture(p->map, c->u, c->v, rgba);
    const float key[6] = {c->P[0], c->P[1], c->P[2], c->I[0], c->I[1], c->I[2]};
    return Hash01(key, 6) < rgba[3];
}

/* Hide the scale's two strips from camera and shadow rays, while light
 * samples and indirect rays see the same emitting surface. Their glass
 * reflections can be bright and need enough samples to converge. */
static RtInt LAMP_INDIRECT(RtShadeContext *c) { return c->depth > 0; }


static const struct AreaLight {
    const char *name;
    Vec3 center, right, up;
    float width, height;
    RtColor Le;
    int sampledOnly; /* 1: camera-hidden lamp, LAMP_INDIRECT */
} AREA_LIGHTS[] = {
    {"key, 9 x 10 m over the shop", {0.423f, 5.877f, 0.882f}, {1, 0, 0}, {0, 0, -1}, 9.0f, 10.0f, {3.854f, 2.612f, 1.492f}, 0}, /* the file's x 2: the banner's key is brighter */
    {"rim, girl 2", {0.708f, 3.505f, -1.274f}, {-0.9298f, 0.0001f, 0.3681f}, {0.1363f, 0.9290f, 0.3441f}, 1.0f, 1.0f, {1.735f, 1.169f, 0.676f}, 0},
    {"scale strip, right", {0.183f, 1.083f, 1.084f}, {1, 0, 0}, {0, 0, 1}, 0.3f, 0.025f, {185.03f, 113.71f, 83.71f}, 1}, /* the file's x 4: the dial's glow */
    {"scale strip, left", {-0.443f, 1.083f, 1.084f}, {1, 0, 0}, {0, 0, 1}, 0.3f, 0.025f, {185.03f, 113.71f, 83.71f}, 1},
    {"PO box, red", {-1.721f, 2.936f, -1.173f}, {1, 0, 0}, {0, 0.9761f, 0.2173f}, 1.0f, 1.0f, {3.469f, 1.333f, 0.918f}, 0},
    {"rim, girl", {1.260f, 2.858f, -1.271f}, {-1, 0, -0.0001f}, {0, 1, 0}, 1.0f, 1.0f, {3.469f, 2.339f, 1.353f}, 0},
    {"shop front, left", {-3.196f, 4.912f, -0.223f}, {1, 0, 0}, {0, 0, -1}, 0.5f, 0.5f, {24.285f, 16.372f, 9.467f}, 0},
    {"bounce, cat head", {5.291f, 3.416f, 5.114f}, {1, 0, 0}, {0, 1, 0}, 0.25f, 2.0f, {14.230f, 16.351f, 17.347f}, 0},
    {"backlight, red", {17.011f, 0.254f, -13.950f}, {-0.5901f, -0.8073f, 0}, {0, 0, -1}, 30.0f, 30.0f, {1.250f, 0.427f, 0.086f}, 0},
    {"backlight, blue", {-62.375f, 45.247f, -5.762f}, {0.7071f, 0.7071f, 0}, {0, 0, -1}, 30.0f, 30.0f, {0.792f, 3.350f, 8.334f}, 0}, /* the file's x 1.5: the banner's left wall is brighter */
};

/* The point lights, P watts as I = P / (4 pi) times the color, with the
 * file's 1 m radius: emitting spheres of radiance I / (pi r^2), hidden
 * from the camera like the quads. What sits inside one (the floor and
 * the small robot about the bounce lamp, the counter and the radio
 * about its three lamps) is lit as Cycles lights it, by a disc of that
 * radius at the lamp's center turned toward the point. */
static const struct PointLight {
    const char *name;
    RtPoint position;
    RtColor I;
} POINT_LIGHTS[] = {
    {"conversation", {-2.394f, 2.070f, 2.436f}, {13.635f, 15.551f, 16.565f}},
    {"cat, right", {1.231f, 0.222f, 1.859f}, {1.657f, 1.117f, 0.646f}},
    {"radio 3", {1.373f, 0.904f, 1.461f}, {0.663f, 0.447f, 0.258f}},
    {"radio 2", {1.824f, 0.904f, 1.343f}, {0.663f, 0.447f, 0.258f}},
    {"radio 1", {1.594f, 0.904f, 1.392f}, {0.663f, 0.447f, 0.258f}},
    {"bounce, robot", {-1.199f, 0.180f, 2.986f}, {0.501f, 0.536f, 0.552f}},
    {"bounce, environment", {-8.517f, 3.808f, 5.114f}, {82.824f, 56.100f, 32.066f}}, /* the file's x 2: the banner's left wall is brighter */
};

/* The lights at their own strength but the key, the environment bounce,
 * the blue backlight and the scale's strips (above); the PNG goes
 * through the filmic curve at +1.7 stops (rtTonemap below). Set against
 * the Blender banner: +1.3 and the file's lights left the image 0.72 as
 * bright, the shadows a third. */
static const float EXPOSURE = 1.0f;
static const float LAMP_RADIUS = 1.0f; /* the file's */

/* The numbers after the scene's name on the command line (gauchoRT
 * JunkShop 1 1 1 1), for tests: the key, the environment bounce lamp,
 * the blue backlight and the scale's two strips, times these; 1 is the
 * scene as listed. */
static float keyScale = 1, bounceScale = 1, backlightScale = 1, stripScale = 1;

static void JunkShopLights(void)
{
    for (size_t i = 0; i < sizeof AREA_LIGHTS / sizeof AREA_LIGHTS[0]; i++) {
        const AreaLight &a = AREA_LIGHTS[i];
        const float k = EXPOSURE * (i == 0 ? keyScale : std::strcmp(a.name, "backlight, blue") == 0 ? backlightScale
                                                 : a.sampledOnly ? stripScale : 1.0f);
        RtMaterial m = EMITTER(a.Le[0] * k, a.Le[1] * k, a.Le[2] * k);
        rtMaterial(&m);
        rtCutout(a.sampledOnly ? LAMP_INDIRECT : LAMP, NULL, 0);
        const Vec3 eu = a.right * a.width, ev = a.up * a.height;
        Quad(a.center - eu * 0.5f - ev * 0.5f, eu, ev); /* faces up x right: the lamp's -z */
    }
    rtCutout(NULL, NULL, 0);
    const float area = 3.14159265f * LAMP_RADIUS * LAMP_RADIUS;
    for (size_t i = 0; i < sizeof POINT_LIGHTS / sizeof POINT_LIGHTS[0]; i++) {
        const PointLight &p = POINT_LIGHTS[i];
        const float k = EXPOSURE / area * (std::strcmp(p.name, "bounce, environment") == 0 ? bounceScale : 1.0f);
        RtMaterial m = EMITTER(p.I[0] * k, p.I[1] * k, p.I[2] * k);
        rtMaterial(&m);
        rtCutout(LAMP_SPHERE, NULL, 0);
        rtTransformBegin();
            rtTranslate(p.position[0], p.position[1], p.position[2]);
            rtSphere(LAMP_RADIUS);
        rtTransformEnd();
    }
    rtCutout(NULL, NULL, 0);
}

/* scene -------------------------------------------------------------- */

void JunkShop(void)
{
    /* The camera: a 25 mm lens on a 28.8 x 16 mm frame, 1.8:1, 35.5
     * degrees tall, at f/0.28 (a 45 mm lens radius: the blur stays
     * slight, the scene is 8 to 15 m deep), focused 8.73 m out, on the
     * girl. */
    RtPoint eye = {4.49636f, 0.63494f, 8.03663f}, lookat = {3.91559f, 0.83716f, 7.24808f};
    RtVector up = {0.11990f, 0.97934f, 0.16285f};
    RtColor black = {0, 0, 0};
    const float fovy = 35.527f;
    keyScale = rtSceneArg(0, 1.0f);
    bounceScale = rtSceneArg(1, 1.0f);
    backlightScale = rtSceneArg(2, 1.0f);
    stripScale = rtSceneArg(3, 1.0f);

    rtBegin();
        rtFormat(1728, 960);
        rtDisplay("3_HeroScenes/JunkShop");
        rtTonemap("filmic", 1.7f);
        rtCamera(eye, lookat, up, fovy);
        rtLens(0.045f, 8.73f);
        rtPixelSamples(512);
        rtSampler("halton");
        rtMaxDepth(12);
        rtBackground(black);
        rtWorldBegin(0);
            JunkShopLights();
            ObjMesh *model = obj_load("models/junkshop/junkshop.mesh");
            JunkHook hook;
            hook.mesh = model;
            for (int k = 0; k < MAX_MATERIALS; k++) {
                hook.metallic[k] = hook.roughness[k] = hook.emission[k] = hook.normal[k] = hook.transmission[k] = -2;
            }
            /* The model was exported in the exporter's coat mode (its Ks the
             * weight of a Fresnel coat, not the reflectance head on):
             * OBJ_COAT_MODE turns it into what kajiya reads (rt.h, specular)
             * for the materials the shader above does not set per pixel. */
            ObjDraw(model, OBJ_COAT_MODE, JunkShopMaterials, &hook);
            obj_free(model);
            /* The puff over the small robot: its card, 58 x 87 cm on the
             * plane through the puff's center 7.4 m out, facing the camera
             * (a pixel of the 1920-wide bake spans 4.9 mm there). */
            RtMaterial smoke = MATTE(0, 0, 0);
            SmokeParams sp = {rtMakeTexture("models/junkshop/textures/smoke_puff.png", 1)};
            rtMaterial(&smoke);
            rtSurface(SMOKE_CARD, &sp, sizeof sp);
            rtCutout(COVERAGE, &sp, sizeof sp);
            Quad(Vec3(-2.1542f, 2.0466f, 4.0634f), Vec3(0.4526f, 0, -0.3675f), Vec3(-0.1108f, -0.8517f, -0.1365f));
            rtSurface(NULL, NULL, 0);
            rtCutout(NULL, NULL, 0);
        rtWorldEnd();
    rtEnd();
}
