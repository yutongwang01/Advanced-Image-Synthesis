/*
 * texture.cpp: loading and sampling textures (HW2 handout: shaders and
 * textures). Given complete in both kits. An 8-bit image is kept as read, 4 bytes per
 * texel, and decoded to linear at lookup through a 256-entry table (sRGB
 * or plain), so Sponza's textures cost 200 MB rather than 800; PFM and
 * HDR are kept as linear floats. Rows run top first. Handles index one
 * table, no slot limit; lookups are read-only, so threads share it.
 */
#include "texture.h"

#include "image.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include "stb_image.h" /* the implementation is compiled in image.cpp */

namespace {

struct Texture {
    int w = 0, h = 0;
    std::vector<float> rgbaF;         /* float images: 4 floats per texel, top row first */
    std::vector<unsigned char> rgba8; /* 8-bit images: 4 bytes per texel */
    const float *decode = nullptr;    /* 8-bit images: the color channels' byte-to-linear table */
};

std::vector<Texture> textures;

/* The two byte-to-linear tables: sRGB decoded, and plain (alpha, masks,
 * images loaded with srgb = 0). Built once. */
struct DecodeTables {
    float srgb[256], plain[256];
    DecodeTables()
    {
        for (int i = 0; i < 256; i++) {
            plain[i] = i / 255.0f;
            srgb[i] = srgbToLinear(plain[i]);
        }
    }
};
const DecodeTables &tables()
{
    static const DecodeTables t;
    return t;
}

/* Lowercase extension of a file name, without the dot ("" if none). */
std::string extension(const std::string &name)
{
    size_t dot = name.find_last_of('.');
    if (dot == std::string::npos)
        return "";
    std::string ext = name.substr(dot + 1);
    for (char &ch : ext)
        ch = (char)((ch >= 'A' && ch <= 'Z') ? ch - 'A' + 'a' : ch);
    return ext;
}

bool fileExists(const std::string &path)
{
    std::FILE *f = std::fopen(path.c_str(), "rb");
    if (f)
        std::fclose(f);
    return f != nullptr;
}

/* PFM and HDR through the given readImage (image.h), kept as four floats
 * per texel; a grey image repeats its channel. */
bool loadFloat(const std::string &path, Texture &tex)
{
    Image img;
    if (!readImage(path, false, img))
        return false;
    tex.w = img.width;
    tex.h = img.height;
    tex.rgbaF.resize((size_t)tex.w * tex.h * 4);
    const int n = img.channels;
    for (size_t i = 0; i < (size_t)tex.w * tex.h; i++) {
        const float *p = &img.texels[i * (size_t)n];
        float *o = &tex.rgbaF[4 * i];
        if (n <= 2) {
            o[0] = o[1] = o[2] = p[0];
            o[3] = n == 2 ? p[1] : 1;
        } else {
            o[0] = p[0];
            o[1] = p[1];
            o[2] = p[2];
            o[3] = n == 4 ? p[3] : 1;
        }
    }
    return true;
}

/* An 8-bit image through stb_image: grey, grey + alpha, RGB or RGBA,
 * stored as 4 bytes per texel. The color channels are decoded at lookup,
 * from sRGB when asked; the alpha channel is coverage and never is. */
bool load8bit(const std::string &path, bool srgb, Texture &tex)
{
    int w, h, n;
    unsigned char *data = stbi_load(path.c_str(), &w, &h, &n, 0);
    if (!data)
        return false;
    tex.w = w;
    tex.h = h;
    tex.decode = srgb ? tables().srgb : tables().plain;
    tex.rgba8.resize((size_t)w * h * 4);
    for (size_t i = 0; i < (size_t)w * h; i++) {
        const unsigned char *p = data + i * n;
        unsigned char *o = tex.rgba8.data() + 4 * i;
        if (n <= 2) {
            o[0] = o[1] = o[2] = p[0];
            o[3] = n == 2 ? p[1] : 255;
        } else {
            o[0] = p[0];
            o[1] = p[1];
            o[2] = p[2];
            o[3] = n == 4 ? p[3] : 255;
        }
    }
    stbi_image_free(data);
    return true;
}

} // namespace

int textureLoad(const std::string &file, bool srgb)
{
    std::string path = file;
    if (!fileExists(path))
        path = "textures/" + file;
    if (!fileExists(path)) {
        std::fprintf(stderr, "rtMakeTexture: cannot find %s (as given or under textures/; run gauchoRT from the repo root)\n",
                     file.c_str());
        return -1;
    }

    Texture tex;
    const std::string ext = extension(path);
    bool ok;
    if (ext == "pfm" || ext == "hdr")
        ok = loadFloat(path, tex); /* readImage says why when it fails */
    else if (!(ok = load8bit(path, srgb, tex)))
        std::fprintf(stderr, "rtMakeTexture: cannot read %s (%s)\n", path.c_str(), stbi_failure_reason());
    if (!ok)
        return -1;
    textures.push_back(std::move(tex));
    return (int)textures.size() - 1;
}

/* i wrapped into [0, n) for any sign. */
static inline int wrap(int i, int n)
{
    i %= n;
    return i < 0 ? i + n : i;
}

/* The bilinear lookup behind both public ones; wrapRows says whether t
 * repeats like s or clamps to the first and last rows. */
static void lookup(int handle, float s, float t, float rgba[4], bool wrapRows)
{
    if (handle < 0 || handle >= (int)textures.size() || textures[(size_t)handle].w == 0) {
        rgba[0] = rgba[1] = rgba[2] = 0;
        rgba[3] = 1;
        return;
    }
    const Texture &tex = textures[(size_t)handle];
    /* Texel centers sit at (i + 0.5) / w, so the sample point in texel
     * units is s w - 0.5; blend the four texels around it, in linear
     * values (an 8-bit texel is decoded first, so the blend is linear). */
    const float x = s * (float)tex.w - 0.5f, y = t * (float)tex.h - 0.5f;
    const float fx0 = std::floor(x), fy0 = std::floor(y);
    const float ax = x - fx0, ay = y - fy0;
    const int x0 = wrap((int)fx0, tex.w), x1 = wrap((int)fx0 + 1, tex.w);
    const int y0 = wrapRows ? wrap((int)fy0, tex.h) : std::max(0, std::min(tex.h - 1, (int)fy0));
    const int y1 = wrapRows ? wrap((int)fy0 + 1, tex.h) : std::max(0, std::min(tex.h - 1, (int)fy0 + 1));
    const size_t i00 = (size_t)y0 * tex.w + x0, i10 = (size_t)y0 * tex.w + x1;
    const size_t i01 = (size_t)y1 * tex.w + x0, i11 = (size_t)y1 * tex.w + x1;
    const float w00 = (1 - ax) * (1 - ay), w10 = ax * (1 - ay), w01 = (1 - ax) * ay, w11 = ax * ay;
    if (tex.rgba8.empty()) {
        const float *p00 = &tex.rgbaF[4 * i00], *p10 = &tex.rgbaF[4 * i10];
        const float *p01 = &tex.rgbaF[4 * i01], *p11 = &tex.rgbaF[4 * i11];
        for (int c = 0; c < 4; c++)
            rgba[c] = p00[c] * w00 + p10[c] * w10 + p01[c] * w01 + p11[c] * w11;
    } else {
        const unsigned char *p00 = &tex.rgba8[4 * i00], *p10 = &tex.rgba8[4 * i10];
        const unsigned char *p01 = &tex.rgba8[4 * i01], *p11 = &tex.rgba8[4 * i11];
        const float *plain = tables().plain;
        for (int c = 0; c < 3; c++)
            rgba[c] = tex.decode[p00[c]] * w00 + tex.decode[p10[c]] * w10 + tex.decode[p01[c]] * w01 +
                      tex.decode[p11[c]] * w11;
        rgba[3] = plain[p00[3]] * w00 + plain[p10[3]] * w10 + plain[p01[3]] * w01 + plain[p11[3]] * w11;
    }
}

void textureLookup(int handle, float s, float t, float rgba[4]) { lookup(handle, s, t, rgba, true); }

void textureLookupLatLong(int handle, float s, float t, float rgba[4]) { lookup(handle, s, t, rgba, false); }

bool textureSize(int handle, int &w, int &h)
{
    if (handle < 0 || handle >= (int)textures.size() || textures[(size_t)handle].w == 0)
        return false;
    w = textures[(size_t)handle].w;
    h = textures[(size_t)handle].h;
    return true;
}

void textureTexel(int handle, int x, int y, float rgba[4])
{
    const Texture &tex = textures[(size_t)handle];
    const size_t i = (size_t)y * tex.w + x;
    if (tex.rgba8.empty()) {
        for (int c = 0; c < 4; c++)
            rgba[c] = tex.rgbaF[4 * i + c];
    } else {
        for (int c = 0; c < 3; c++)
            rgba[c] = tex.decode[tex.rgba8[4 * i + c]];
        rgba[3] = tables().plain[tex.rgba8[4 * i + 3]];
    }
}

void texturesClear() { textures.clear(); }
