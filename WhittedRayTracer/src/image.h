/*
 * image.h: writing the rendered image (HW2 handout: the interface) and
 * reading the images textures come from. Complete in both
 * kits; nothing to change.
 */
#ifndef IMAGE_H
#define IMAGE_H

#include <string>
#include <vector>

/* Write w x h pixels of linear radiance (three floats per pixel, top row
 * first) as <path>.png, or <path>.jpg with format "jpg" (--format):
 * scaled by 2^exposure (stops), through the tone map ("none": clamped to
 * [0, 1]; "reinhard", "aces" or "filmic": a curve with a shoulder, as
 * rt.h's rtTonemap says), then sRGB encoded; and with pfm as <path>.pfm with the
 * floats as they are, linear, before exposure and tone map. The folder
 * must exist (cmdline.cpp's imagePath makes it). True if every file was
 * written, else stderr says. */
bool writeImage(const std::string &path, int w, int h, const float *rgb, bool pfm,
                const std::string &tonemap = "none", float exposure = 0, const std::string &format = "png");

/* The sRGB transfer function on one channel in [0, 1]. */
float linearToSrgb(float v);

/* Its inverse, for 8-bit textures loaded with srgb = 1. */
float srgbToLinear(float v);

/* An image read from a file: linear floats, top row first, the channels
 * interleaved (1 grey, 2 grey and alpha, 3 RGB, 4 RGBA). */
struct Image {
    int width = 0, height = 0, channels = 0;
    std::vector<float> texels;
};

/* Read an image file into linear floats: PNG, JPEG, BMP, TGA and Radiance
 * HDR through stb_image, PFM ("PF" RGB or "Pf" grey) by hand. The color
 * channels of an 8-bit image are decoded from sRGB when srgb is set, its
 * alpha never; HDR and PFM are linear as stored (a PFM's scale gives only
 * the byte order). False, with a message on stderr, when the file cannot
 * be read. rtMakeTexture is built on this. */
bool readImage(const std::string &path, bool srgb, Image &image);

#endif /* IMAGE_H */
