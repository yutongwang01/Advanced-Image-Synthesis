/*
 * sampler.cpp: your samplers ("Samplers" handout; workflow steps 2, 4 and
 * 5). Write each sampler as a class derived from Sampler, built on the
 * given hashing.h, and make makeSampler return the one a name asks for.
 * sampler.h declares what you write here; keep its names.
 */
#include "sampler.h"

#include "hashing.h"

#include <algorithm>
#include <vector>

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

/* The stratified sampler ("Samplers" handout, Section 6). Every dimension
 * a sample draws is stratified on its own across the pixel's spp samples:
 * a 2D dimension pair into the m x n cells of gridOfCells(spp), a 1D
 * dimension into spp equal intervals. Sample `index` takes cell
 * p = permutationElement(index, spp, key), the key hashed from the pixel
 * and the dimension's number, so each dimension (film, lens, each light,
 * the lobe, AO) has its own random assignment of samples to cells and the
 * same sample lands in unrelated cells of each (Cook's trick: the domains'
 * cells paired at random). Inside its cell the point is jittered by the
 * pixel sample's PCG32 stream, the independent sampler's. Single precision
 * throughout, as the reference computes it. */
class StratifiedSampler : public Sampler {
public:
    StratifiedSampler(int spp, uint64_t seed) : spp_(std::max(1, spp)), seed_(seed)
    {
        gridOfCells(spp_, m_, n_); /* once, here */
    }

    void startPixelSample(int x, int y, int index) override
    {
        index_ = index;
        dimension_ = 0;
        pixelHash_ = hash3((uint64_t)(uint32_t)x, (uint64_t)(uint32_t)y, seed_);
        startStream(rng_, x, y, index, seed_); /* the jitter's stream */
    }

    /* Dimension d: interval p of spp, jittered. */
    float next1D() override
    {
        const uint32_t p = cell();
        const float r = rng_.uniform();
        dimension_ += 1;
        return std::min(((float)p + r) / (float)spp_, ONE_MINUS_EPSILON);
    }

    /* Dimensions d and d + 1 as one pair: cell p of the m x n grid,
     * column p mod m and row p / m, jittered by r1 then r2. */
    Sample2 next2D() override
    {
        const uint32_t p = cell();
        const float r1 = rng_.uniform(); /* two statements: r1 first */
        const float r2 = rng_.uniform();
        Sample2 s;
        s.u = std::min(((float)(p % (uint32_t)m_) + r1) / (float)m_, ONE_MINUS_EPSILON);
        s.v = std::min(((float)(p / (uint32_t)m_) + r2) / (float)n_, ONE_MINUS_EPSILON);
        dimension_ += 2;
        return s;
    }

private:
    /* The current sample's cell in the current dimension: element index_
     * of the permutation keyed by the pixel and the dimension's number. */
    uint32_t cell() const
    {
        const uint32_t key = low32(hashDimension(pixelHash_, dimension_));
        return permutationElement((uint32_t)index_, (uint32_t)spp_, key);
    }

    int spp_;
    uint64_t seed_;
    int m_ = 1, n_ = 1;     /* the 2D grid, m x n = spp */
    int index_ = 0;         /* the current sample */
    int dimension_ = 0;     /* the next dimension's number: +1 per next1D, +2 per next2D */
    uint64_t pixelHash_ = 0;
    Pcg32 rng_;
};

/* The Halton sampler ("Samplers" handout, Section 7) ------------------------ */

/* 5.1: the first 1024 primes, 2 to 8161 (the 100th is 541): dimension d of
 * the Halton sequence is in base primes[d % 1024]. Made once, by trial
 * division, the first time it is asked for (a function-local static, so
 * thread-safe), before any pixel is rendered. */
const int kHaltonPrimes = 1024;

const std::vector<uint32_t> &haltonPrimes()
{
    static const std::vector<uint32_t> primes = []() {
        std::vector<uint32_t> p;
        p.reserve(kHaltonPrimes);
        for (uint32_t c = 2; (int)p.size() < kHaltonPrimes; ++c) {
            bool prime = true;
            for (uint32_t q : p) {
                if (q * q > c)
                    break;
                if (c % q == 0) {
                    prime = false;
                    break;
                }
            }
            if (prime)
                p.push_back(c);
        }
        return p;
    }();
    return primes;
}

/* 5.3: the radical inverse of index in a prime base, its digits Owen
 * scrambled, exactly as the handout's Section 7.5 gives it, line for line.
 * Each digit d goes through the affine permutation (a d + c) mod base, the
 * pair (a, c) from a 64-bit state that starts as mix64(hash) and becomes
 * mix64(state ^ (scrambled + 1)) after each digit, so a digit's permutation
 * depends on the pixel, the dimension, its position and the scrambled
 * digits before it (Owen's tree, hashed). Three things the handout warns
 * about: the loop runs on past the index's highest digit while a digit can
 * still move a float (its zeros scrambled too); the state advances once per
 * digit whatever the digit; and the arithmetic is double until the one
 * final cast to float. */
float owenScrambledRadicalInverse(uint32_t base, uint32_t index, uint64_t hash)
{
    const double invBase = 1.0 / (double)base;
    double invBaseM = 1; // base^-m after m digits
    uint64_t reversed = 0;
    uint64_t state = mix64(hash);
    // ... as long as a digit at this position still moves a float
    while (1.0f - (float)((base - 1) * invBaseM) < 1.0f) {
        const uint32_t next = index / base;
        const uint32_t digit = index - next * base;
        const uint64_t hi = high32(state), lo = low32(state);
        const uint32_t a = 1 + (uint32_t)((hi * (base - 1)) >> 32);
        const uint32_t c = (uint32_t)((lo * base) >> 32);
        const uint32_t scrambled = (a * digit + c) % base;
        reversed = reversed * base + scrambled;
        invBaseM *= invBase;
        index = next;
        state = mix64(state ^ (scrambled + 1));
    }
    const float value = (float)(invBaseM * (double)reversed);
    return std::min(value, ONE_MINUS_EPSILON);
}

/* 5.4: the sampler. Sample `index` of a pixel is point `index` of the
 * Halton sequence, (Phi_2(i), Phi_3(i), Phi_5(i), ...), dimension d in
 * base primes[d % 1024] (past 1023 the bases start over), scrambled per
 * pixel and per dimension by h = hashDimension(hash3(x, y, seed), d). No
 * PCG32 stream: every value is a function of the pixel, the index, the
 * dimension and the seed. */
class HaltonSampler : public Sampler {
public:
    explicit HaltonSampler(uint64_t seed) : seed_(seed) { haltonPrimes(); /* the table, made now */ }

    void startPixelSample(int x, int y, int index) override
    {
        index_ = index;
        dimension_ = 0;
        pixelHash_ = hash3((uint64_t)(uint32_t)x, (uint64_t)(uint32_t)y, seed_);
    }

    float next1D() override
    {
        const float value = sample(dimension_);
        dimension_ += 1;
        return value;
    }

    Sample2 next2D() override
    {
        Sample2 s;
        s.u = sample(dimension_);     /* dimension d */
        s.v = sample(dimension_ + 1); /* dimension d + 1 */
        dimension_ += 2;
        return s;
    }

private:
    /* Dimension d of the current sample. 5.2: dimension 0, base 2, is the
     * bit-reversed index (Phi_2 as 32-bit fixed point) Owen scrambled with
     * the given owenScramble, fixedToFloat keeping it below 1. Only
     * dimension 0 takes this shortcut: its scrambling hash differs from the
     * loop's, and the reference's images depend on which each one uses, so
     * dimension 1024 (base 2 again) goes through the loop. */
    float sample(int d) const
    {
        const uint64_t h = hashDimension(pixelHash_, d);
        if (d == 0)
            return fixedToFloat(owenScramble(reverseBits32((uint32_t)index_), low32(h)));
        return owenScrambledRadicalInverse(haltonPrimes()[(size_t)(d % kHaltonPrimes)], (uint32_t)index_, h);
    }

    uint64_t seed_;
    int index_ = 0;
    int dimension_ = 0;
    uint64_t pixelHash_ = 0;
};

} // namespace

/* Return a new sampler of the kind the name asks for ("independent",
 * "stratified", "halton", "sobol"), for spp samples per pixel and the
 * seed. TODO (workflow step 2): until you write it, there is none. */
std::unique_ptr<Sampler> makeSampler(const std::string &name, int spp, uint64_t seed)
{
    if (name == "stratified")
        return std::make_unique<StratifiedSampler>(spp, seed);
    if (name == "halton")
        return std::make_unique<HaltonSampler>(seed);
    /* "independent", and sobol until it exists (extra credit). */
    return std::make_unique<IndependentSampler>(seed);
}

/* The m x n cells that count samples stratify into: m the largest divisor
 * of count not above its square root. TODO (workflow step 4). */
void gridOfCells(int count, int &m, int &n)
{
    if (count < 1) {
        m = n = 1;
        return;
    }
    m = 1;
    for (int d = 1; d * d <= count; ++d) /* integers only: no sqrt rounding */
        if (count % d == 0)
            m = d;
    n = count / m;
}
