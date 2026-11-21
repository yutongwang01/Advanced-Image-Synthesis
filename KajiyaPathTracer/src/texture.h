/*
 * texture.h: the texture table behind rtMakeTexture and rtTexture (HW2
 * handout: shaders and textures). Given complete in both kits; rt.cpp's two calls
 * hand it the file and the lookup, and the environment map (Workflow,
 * step 13) reads it through textureLookupLatLong.
 */
#ifndef TEXTURE_H
#define TEXTURE_H

#include <string>

/* Load an image and return its handle (>= 0), or -1 with a message on
 * stderr. Tries the path as given, then under textures/. 8-bit formats
 * (PNG, JPEG, BMP, TGA) are decoded to linear when srgb is set, the alpha
 * channel never; PFM and Radiance HDR are linear floats already. */
int textureLoad(const std::string &file, bool srgb);

/* Bilinear lookup with repeat wrap; s runs left to right, t top to
 * bottom. rgba gets alpha 1 when the image has none. An invalid handle
 * reads opaque black. */
void textureLookup(int handle, float s, float t, float rgba[4]);

/* The same lookup for a latitude-longitude map (an environment): s wraps
 * around, t clamps at the top and bottom rows, which are the poles. */
void textureLookupLatLong(int handle, float s, float t, float rgba[4]);

/* A texture's size in texels, false for an invalid handle; and one
 * texel's linear RGBA, column x of row y (top row first), for building
 * the environment's sampling distribution (HW4). */
bool textureSize(int handle, int &w, int &h);
void textureTexel(int handle, int x, int y, float rgba[4]);

/* Free every texture (rtBegin, rtEnd). */
void texturesClear();

#endif /* TEXTURE_H */
