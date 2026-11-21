/*
 * sampler.h: the samplers' interface ("Samplers" handout), given
 * complete. A sampler hands out one pixel sample's random numbers in
 * [0, 1), dimension by dimension, in the handout's order. Every value
 * depends only on the pixel, the sample's index, the dimension and the
 * seed, never on the thread. You write the samplers and makeSampler in
 * sampler.cpp; keep these names, since the given warptest.cpp calls them.
 */
#ifndef SAMPLER_H
#define SAMPLER_H

#include <cstdint>
#include <memory>
#include <string>

/* Two sample values, each in [0, 1). */
struct Sample2 {
    float u, v;
};

class Sampler {
public:
    virtual ~Sampler() = default;

    /* Start sample `index` of the pixel at (x, y), in the full frame's
     * coordinates: from here next1D and next2D give that sample's
     * dimensions in order. */
    virtual void startPixelSample(int x, int y, int index) = 0;

    virtual float next1D() = 0;
    virtual Sample2 next2D() = 0;
};

/* The sampler called `name` ("independent", "stratified", "halton",
 * "sobol") for spp samples per pixel and the given seed. */
std::unique_ptr<Sampler> makeSampler(const std::string &name, int spp, uint64_t seed);

/* The m x n cells that count samples stratify into: m the largest divisor
 * of count not above its square root, so 16 is 4 x 4, 8 is 2 x 4, and a
 * prime is a strip. The stratified sampler's cells, and cook's light
 * samples'. */
void gridOfCells(int count, int &m, int &n);

#endif /* SAMPLER_H */
