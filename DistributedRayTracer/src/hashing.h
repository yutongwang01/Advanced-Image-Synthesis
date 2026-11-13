/*
 * hashing.h: the hash and random-number primitives every sampler is
 * built on (HW3 handout). Provided complete in the HW3 kit, so that a
 * sampler written to the HW3 handout gives the reference's
 * numbers bit for bit. Nothing here is specific to rendering.
 */
#ifndef HASHING_H
#define HASHING_H

#include <cstdint>

/* The largest float below 1, so a sample is always in [0, 1). */
const float ONE_MINUS_EPSILON = 0x1.fffffep-1f;

/* The SplitMix64 finalizer: a 64-bit hash of a 64-bit integer. */
inline uint64_t mix64(uint64_t z)
{
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
    return z ^ (z >> 31);
}

/* A hash of three integers: a pixel and a seed into a stream number. */
inline uint64_t hash3(uint64_t a, uint64_t b, uint64_t c)
{
    return mix64(a * 0x9e3779b97f4a7c15ull ^ mix64(b + 0x632be59bd9b4e019ull) ^ mix64(c + 0x85157af5ull));
}

/* The hash of one dimension of one pixel's samples, from the pixel's hash. */
inline uint64_t hashDimension(uint64_t pixelHash, int dimension)
{
    return mix64(pixelHash ^ mix64((uint64_t)(uint32_t)dimension * 0x9e3779b97f4a7c15ull + 0x7f4a7c15ull));
}

inline uint32_t low32(uint64_t h) { return (uint32_t)h; }
inline uint32_t high32(uint64_t h) { return (uint32_t)(h >> 32); }

/* A 32-bit fixed-point number (bit 31 the first binary digit) as a float
 * in [0, 1). */
inline float fixedToFloat(uint32_t x)
{
    const float f = (float)x * 0x1p-32f;
    return f < ONE_MINUS_EPSILON ? f : ONE_MINUS_EPSILON;
}

inline uint32_t reverseBits32(uint32_t v)
{
    v = (v << 16) | (v >> 16);
    v = ((v & 0x00ff00ffu) << 8) | ((v & 0xff00ff00u) >> 8);
    v = ((v & 0x0f0f0f0fu) << 4) | ((v & 0xf0f0f0f0u) >> 4);
    v = ((v & 0x33333333u) << 2) | ((v & 0xccccccccu) >> 2);
    v = ((v & 0x55555555u) << 1) | ((v & 0xaaaaaaaau) >> 1);
    return v;
}

/* Element i of a random permutation of 0 .. n - 1 chosen by `hash`
 * (Kensler 2013, "Correlated Multi-Jittered Sampling"): a hash that is a
 * bijection on the integers below the next power of two above n, cycled
 * until it lands below n. */
inline uint32_t permutationElement(uint32_t i, uint32_t n, uint32_t hash)
{
    uint32_t w = n - 1;
    w |= w >> 1;
    w |= w >> 2;
    w |= w >> 4;
    w |= w >> 8;
    w |= w >> 16;
    do {
        i ^= hash;
        i *= 0xe170893du;
        i ^= hash >> 16;
        i ^= (i & w) >> 4;
        i ^= hash >> 8;
        i *= 0x0929eb3fu;
        i ^= hash >> 23;
        i ^= (i & w) >> 1;
        i *= 1 | hash >> 27;
        i *= 0x6935fa69u;
        i ^= (i & w) >> 11;
        i *= 0x74dcb303u;
        i ^= (i & w) >> 2;
        i *= 0x9e501cc3u;
        i ^= (i & w) >> 2;
        i *= 0xc860a3dfu;
        i &= w;
        i ^= i >> 5;
    } while (i >= n);
    return (i + hash) % n;
}

/* Owen scrambling in base 2 of a 32-bit number whose bit 31 is the first
 * digit: every digit is flipped or not by a hash of the seed and the
 * digits above it (Owen 1995), as Laine and Karras's 2011 hash on the
 * bit-reversed number with Burley's 2020 constants. Applied to a sample
 * index instead, it is a nested uniform shuffle. */
inline uint32_t owenScramble(uint32_t v, uint32_t seed)
{
    v = reverseBits32(v);
    v ^= v * 0x3d20adeau;
    v += seed;
    v *= (seed >> 16) | 1;
    v ^= v * 0x05526c56u;
    v ^= v * 0x53a22864u;
    return reverseBits32(v);
}

/* PCG32 (O'Neill 2014), as in pbrt-v4: a 64-bit linear congruential
 * state with a permuted 32-bit output, one stream per sequence index, and
 * a jump ahead by any number of steps in O(log n). */
class Pcg32 {
public:
    void setSequence(uint64_t sequence, uint64_t seed)
    {
        state = 0;
        inc = (sequence << 1u) | 1u;
        next();
        state += seed;
        next();
    }

    uint32_t next()
    {
        const uint64_t old = state;
        state = old * MULT + inc;
        const uint32_t xorshifted = (uint32_t)(((old >> 18u) ^ old) >> 27u);
        const uint32_t rot = (uint32_t)(old >> 59u);
        return (xorshifted >> rot) | (xorshifted << ((~rot + 1u) & 31u));
    }

    float uniform() { return fixedToFloat(next()); }

    /* Move the state on by delta steps (Brown 1994, "Random number
     * generation with arbitrary strides"). */
    void advance(uint64_t delta)
    {
        uint64_t curMult = MULT, curPlus = inc, accMult = 1u, accPlus = 0u;
        while (delta > 0) {
            if (delta & 1) {
                accMult *= curMult;
                accPlus = accPlus * curMult + curPlus;
            }
            curPlus = (curMult + 1) * curPlus;
            curMult *= curMult;
            delta /= 2;
        }
        state = accMult * state + accPlus;
    }

private:
    static constexpr uint64_t MULT = 0x5851f42d4c957f2dull;
    uint64_t state = 0x853c49e6748fea9bull, inc = 0xda3e39cb94b95bdbull;
};

/* The pixel and the seed pick a PCG32 stream, and sample `index` starts
 * 65536 steps per index into it, so a sample can use up to 65536 random
 * numbers before it would run into the next one's. independent draws
 * every dimension from it, stratified only the jitter inside a cell. */
inline void startStream(Pcg32 &rng, int x, int y, int index, uint64_t seed)
{
    rng.setSequence(hash3((uint64_t)(uint32_t)x, (uint64_t)(uint32_t)y, seed), seed);
    rng.advance((uint64_t)index * 65536ull);
}

#endif
