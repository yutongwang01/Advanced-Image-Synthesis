/*
 * image.cpp: writing the rendered image (HW2 handout: the interface)
 * and reading the images textures come from (readImage, at the
 * end). Complete in both kits; nothing to change. The PNG is what you
 * look at and what the benchmark compares: linear radiance times
 * 2^exposure, through the scene's tone map (rtTonemap: a clamp to [0, 1]
 * by default, or a curve with a shoulder that keeps the bright parts of
 * an image whose sun is a thousand times its shade), sRGB encoded, 8 bits
 * per channel, no alpha. The PFM (--pfm only) keeps the floats as
 * rendered, for compare.py and the HW3 studies. This is the one
 * translation unit that compiles the stb implementations.
 */
#include "image.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

float linearToSrgb(float v)
{
    if (v <= 0.0031308f)
        return 12.92f * v;
    return 1.055f * std::pow(v, 1.0f / 2.4f) - 0.055f;
}

float srgbToLinear(float v)
{
    if (v <= 0.04045f)
        return v / 12.92f;
    return std::pow((v + 0.055f) / 1.055f, 2.4f);
}

namespace {

/* Tables for the sRGB encode, built once: the code at the start of each
 * of 4096 equal bins of [0, 1] (the one just below the bin's start), and
 * per code its exact linear value. The curve is steepest at 0, where a
 * bin spans 0.8 codes, so a bin never holds more than two codes: one
 * comparison. */
struct EncodeTable {
    unsigned char code[4096];
    float level[257];
    EncodeTable()
    {
        for (int i = 0; i < 4096; i++)
            code[i] = (unsigned char)std::floor(linearToSrgb(i / 4096.0f) * 255.0f);
        for (int c = 0; c <= 256; c++)
            level[c] = srgbToLinear(c / 255.0f);
    }
};

/* A linear value clamped to [0, 1], sRGB encoded to 8 bits with a dither:
 * the code just below it, plus one when u in [0, 1) falls under the
 * value's position between the two codes, so the code's expectation is
 * the value and a slow gradient in a dark region breaks into fine noise
 * instead of bands (u = 0.5 is plain rounding). By table: a pow per
 * channel is six million pows for a 1920 x 1080 image. */
unsigned char to8(float v, float u)
{
    static const EncodeTable table;
    if (!(v > 0))
        return 0; /* also NaN */
    if (v >= 1)
        return 255;
    int c = table.code[(int)(v * 4096.0f)];
    c += (c < 255) & (v >= table.level[c + 1]);
    if (c < 255)
        c += u < (v - table.level[c]) / (table.level[c + 1] - table.level[c]);
    return (unsigned char)c;
}

/* The tone maps, per channel on linear radiance x >= 0, each 0 at 0 and
 * rising to 1 with a shoulder instead of the clamp's hard edge. */
float toneNone(float x) { return x; }
float toneReinhard(float x) { return x / (1 + x); } /* Reinhard et al. 2002, the simple form */
float toneAces(float x) /* Narkowicz 2015, a fit of the ACES filmic curve */
{
    return (x * (2.51f * x + 0.03f)) / (x * (2.43f * x + 0.59f) + 0.14f);
}
float toneFilmic(float x) /* Hable 2010 ("Uncharted 2"), white at 11.2 */
{
    const float A = 0.15f, B = 0.50f, C = 0.10f, D = 0.20f, E = 0.02f, F = 0.30f, W = 11.2f;
    const float num = (x * (A * x + C * B) + D * E) / (x * (A * x + B) + D * F) - E / F;
    const float white = (W * (A * W + C * B) + D * E) / (W * (A * W + B) + D * F) - E / F;
    return num / white;
}

float (*toneMap(const std::string &name))(float)
{
    if (name == "reinhard")
        return toneReinhard;
    if (name == "aces")
        return toneAces;
    if (name == "filmic")
        return toneFilmic;
    if (name != "none" && !name.empty())
        std::fprintf(stderr, "rtTonemap: none, reinhard, aces or filmic, not '%s'; clamping\n", name.c_str());
    return toneNone;
}

/* A number in [0, 1) from a pixel's index (a 32-bit integer hash), the
 * same for its three channels: the dither below. */
static float pixelNoise(size_t pixel)
{
    uint32_t h = (uint32_t)pixel * 2654435761u;
    h ^= h >> 15;
    h *= 2246822519u;
    h ^= h >> 13;
    h *= 3266489917u;
    h ^= h >> 16;
    return (float)(h >> 8) * (1.0f / 16777216.0f);
}

/* The 8-bit image, PNG or JPEG by format: exposure, tone map, sRGB and
 * the dither below. */
bool write8bit(const std::string &path, int w, int h, const float *rgb, const std::string &tonemap, float exposure,
               const std::string &format)
{
    float (*const tone)(float) = toneMap(tonemap);
    const float scale = std::pow(2.0f, exposure);
    std::vector<unsigned char> bytes((size_t)w * h * 3);
    /* Dithered: a value between two 8-bit levels goes to the upper one
     * with the probability of its position between them, decided by a
     * hash of the pixel, so a slow gradient in a dark region breaks into
     * fine noise instead of bands (a deterministic hash: the same image
     * every run). The PFM carries the exact values. */
    for (size_t i = 0; i < bytes.size(); i++)
        bytes[i] = to8(tone(rgb[i] > 0 ? rgb[i] * scale : 0.0f), pixelNoise(i / 3));
    /* stb's default deflate takes longer than rendering a simple scene,
     * and the TIME line's total includes this write: the lowest level and
     * no row filtering take a third off. Still about 0.1 s at
     * 1920 x 1080, so the benchmark ranks by build + render. */
    stbi_write_png_compression_level = 1;
    stbi_write_force_png_filter = 0;
    const bool written = format == "jpg" ? stbi_write_jpg(path.c_str(), w, h, 3, bytes.data(), 92) != 0
                                         : stbi_write_png(path.c_str(), w, h, 3, bytes.data(), w * 3) != 0;
    if (!written) {
        std::fprintf(stderr, "could not write %s (run gauchoRT from the repo root)\n", path.c_str());
        return false;
    }
    std::printf("wrote %s (%d x %d)\n", path.c_str(), w, h);
    return true;
}

/* Portable float map: "PF", the size, a negative scale (meaning
 * little-endian), then the rows bottom to top, three floats per pixel. */
bool writePfm(const std::string &path, int w, int h, const float *rgb)
{
    std::FILE *f = std::fopen(path.c_str(), "wb");
    if (!f) {
        std::fprintf(stderr, "could not write %s (run gauchoRT from the repo root)\n", path.c_str());
        return false;
    }
    bool ok = std::fprintf(f, "PF\n%d %d\n-1.0\n", w, h) > 0;
    for (int y = h - 1; y >= 0 && ok; y--)
        ok = std::fwrite(rgb + (size_t)y * w * 3, sizeof(float), (size_t)w * 3, f) == (size_t)w * 3;
    ok = std::fclose(f) == 0 && ok;
    if (ok)
        std::printf("wrote %s (%d x %d)\n", path.c_str(), w, h);
    else
        std::fprintf(stderr, "could not write %s\n", path.c_str());
    return ok;
}

} // namespace

bool writeImage(const std::string &path, int w, int h, const float *rgb, bool pfm, const std::string &tonemap,
                float exposure, const std::string &format)
{
    bool ok = write8bit(path + "." + format, w, h, rgb, tonemap, exposure, format);
    if (pfm)
        ok = writePfm(path + ".pfm", w, h, rgb) && ok;
    return ok;
}

/* reading ---------------------------------------------------------------- */

namespace {

/* Lowercase extension of a file name, without the dot ("" if none). */
std::string extensionOf(const std::string &name)
{
    const size_t dot = name.find_last_of('.');
    if (dot == std::string::npos)
        return "";
    std::string ext = name.substr(dot + 1);
    for (char &ch : ext)
        ch = (char)((ch >= 'A' && ch <= 'Z') ? ch - 'A' + 'a' : ch);
    return ext;
}

/* The whole file in memory, or false. */
bool slurp(const std::string &path, std::vector<unsigned char> &bytes)
{
    std::FILE *f = std::fopen(path.c_str(), "rb");
    if (!f)
        return false;
    std::fseek(f, 0, SEEK_END);
    const long size = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    bytes.resize(size > 0 ? (size_t)size : 0);
    const bool ok = size >= 0 && std::fread(bytes.data(), 1, bytes.size(), f) == bytes.size();
    std::fclose(f);
    return ok;
}

/* Portable float map: "PF" (RGB) or "Pf" (grey), width height, a scale
 * whose sign gives the byte order (negative: little-endian), then the rows
 * bottom to top. */
bool readPfm(const std::string &path, Image &img)
{
    std::FILE *f = std::fopen(path.c_str(), "rb");
    if (!f)
        return false;
    char magic[3] = {0, 0, 0};
    int w = 0, h = 0;
    float scale = 0;
    bool ok = std::fscanf(f, "%2s %d %d %f", magic, &w, &h, &scale) == 4 && w > 0 && h > 0 && scale != 0 &&
              (std::strcmp(magic, "PF") == 0 || std::strcmp(magic, "Pf") == 0);
    if (ok)
        ok = std::fgetc(f) != EOF; /* the single whitespace byte before the data */
    const int channels = magic[1] == 'F' ? 3 : 1;
    std::vector<float> raw;
    if (ok) {
        raw.resize((size_t)w * h * channels);
        ok = std::fread(raw.data(), sizeof(float), raw.size(), f) == raw.size();
    }
    std::fclose(f);
    if (!ok)
        return false;
    if (scale > 0) /* big-endian: swap each float's bytes */
        for (float &v : raw) {
            unsigned char *b = (unsigned char *)&v;
            const unsigned char t0 = b[0], t1 = b[1];
            b[0] = b[3];
            b[1] = b[2];
            b[2] = t1;
            b[3] = t0;
        }
    img.width = w;
    img.height = h;
    img.channels = channels;
    img.texels.resize(raw.size());
    for (int y = 0; y < h; y++) /* the file's first row is the bottom one */
        std::memcpy(&img.texels[(size_t)y * w * channels], &raw[(size_t)(h - 1 - y) * w * channels],
                    sizeof(float) * (size_t)w * channels);
    return true;
}

/* Radiance HDR. A run-length file goes through stb_image's float loader.
 * A flat file (four RGBE bytes per pixel, no run-length coding, which some
 * writers produce) is decoded here instead: stb's path for it is
 * miscompiled by gcc 16 at -O2 and above (every other row comes out
 * black), and the decode is a few lines. The header is lines up to a
 * blank one, then "-Y h +X w"; the value of a byte b with exponent e is
 * b 2^(e - 136), zero when e is zero, as stb computes it. */
bool readHdr(const std::string &path, Image &img)
{
    std::vector<unsigned char> bytes;
    if (!slurp(path, bytes))
        return false;
    size_t pos = 0;
    bool blank = false;
    while (pos < bytes.size() && !blank) {
        size_t end = pos;
        while (end < bytes.size() && bytes[end] != '\n')
            end++;
        blank = end == pos;
        pos = end + 1;
    }
    int w = 0, h = 0;
    size_t end = pos;
    while (end < bytes.size() && bytes[end] != '\n')
        end++;
    const std::string res(bytes.begin() + (long)pos, bytes.begin() + (long)end);
    const bool flat = blank && std::sscanf(res.c_str(), "-Y %d +X %d", &h, &w) == 2 && w > 0 && h > 0 &&
                      bytes.size() - (end + 1) == (size_t)w * h * 4;
    if (flat) {
        img.width = w;
        img.height = h;
        img.channels = 3;
        img.texels.resize((size_t)w * h * 3);
        const unsigned char *p = bytes.data() + end + 1;
        for (size_t i = 0; i < (size_t)w * h; i++, p += 4) {
            const float f = p[3] ? std::ldexp(1.0f, (int)p[3] - (128 + 8)) : 0.0f;
            img.texels[3 * i + 0] = p[0] * f;
            img.texels[3 * i + 1] = p[1] * f;
            img.texels[3 * i + 2] = p[2] * f;
        }
        return true;
    }
    int n = 0;
    float *d = stbi_loadf_from_memory(bytes.data(), (int)bytes.size(), &w, &h, &n, 0);
    if (!d)
        return false;
    img.width = w;
    img.height = h;
    img.channels = n;
    img.texels.assign(d, d + (size_t)w * h * n);
    stbi_image_free(d);
    return true;
}

/* An 8-bit image through stb_image (PNG, JPEG, BMP, TGA, ...): bytes to
 * [0, 1], the color channels through the sRGB curve when asked, the alpha
 * channel (the last of 2 or 4) as it is. */
bool read8bit(const std::string &path, bool srgb, Image &img)
{
    int w = 0, h = 0, n = 0;
    unsigned char *d = stbi_load(path.c_str(), &w, &h, &n, 0);
    if (!d)
        return false;
    img.width = w;
    img.height = h;
    img.channels = n;
    img.texels.resize((size_t)w * h * n);
    const int alpha = (n == 2 || n == 4) ? n - 1 : -1;
    for (size_t i = 0; i < img.texels.size(); i++) {
        const float v = d[i] / 255.0f;
        img.texels[i] = (srgb && (int)(i % (size_t)n) != alpha) ? srgbToLinear(v) : v;
    }
    stbi_image_free(d);
    return true;
}

} // namespace

bool readImage(const std::string &path, bool srgb, Image &image)
{
    const std::string ext = extensionOf(path);
    bool ok;
    if (ext == "pfm")
        ok = readPfm(path, image);
    else if (ext == "hdr")
        ok = readHdr(path, image);
    else
        ok = read8bit(path, srgb, image);
    if (!ok)
        std::fprintf(stderr, "readImage: cannot read %s (%s)\n", path.c_str(),
                     ext == "pfm" ? "bad PFM header or short file" : ext == "hdr" ? "bad HDR" : stbi_failure_reason());
    return ok;
}
