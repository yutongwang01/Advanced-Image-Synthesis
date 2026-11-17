/*
 * sampler.cpp: your samplers ("Samplers" handout; workflow steps 2, 4 and
 * 5). Write each sampler as a class derived from Sampler, built on the
 * given hashing.h, and make makeSampler return the one a name asks for.
 * sampler.h declares what you write here; keep its names.
 */
#include "sampler.h"

#include "hashing.h"

namespace {

/* The independent sampler ("Samplers" handout, Section 5): plain random
 * numbers, one PCG32 stream per pixel sample. The stream depends only on
 * the pixel and the seed, where sample `index` starts in it only on the
 * index (65536 numbers per sample, startStream), and each value only on
 * how many the sample has drawn before it: so a value is a function of
 * (x, y, index, dimension, seed) alone, never of the thread. */
class IndependentSampler : public Sampler {
public:
    explicit IndependentSampler(uint64_t seed) : seed_(seed) {}

    void startPixelSample(int x, int y, int index) override { startStream(rng_, x, y, index, seed_); }

    /* fixedToFloat already keeps the value below 1 (at most ONE_MINUS_EPSILON). */
    float next1D() override { return rng_.uniform(); }

    Sample2 next2D() override
    {
        /* Two statements, u first: the order of evaluation of a function
         * call's arguments is unspecified in C++. */
        Sample2 s;
        s.u = rng_.uniform();
        s.v = rng_.uniform();
        return s;
    }

private:
    uint64_t seed_;
    Pcg32 rng_;
};

} // namespace

/* Return a new sampler of the kind the name asks for ("independent",
 * "stratified", "halton", "sobol"), for spp samples per pixel and the
 * seed. TODO (workflow step 2): until you write it, there is none. */
std::unique_ptr<Sampler> makeSampler(const std::string &name, int spp, uint64_t seed)
{
    (void)name;
    (void)spp;
    return std::make_unique<IndependentSampler>(seed);
}

/* The m x n cells that count samples stratify into: m the largest divisor
 * of count not above its square root. TODO (workflow step 4). */
void gridOfCells(int count, int &m, int &n)
{
    m = 1;
    n = count;
}
