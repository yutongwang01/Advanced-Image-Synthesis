/*
 * sampler.cpp: your samplers ("Samplers" handout; workflow steps 2, 4 and
 * 5). Write each sampler as a class derived from Sampler, built on the
 * given hashing.h, and make makeSampler return the one a name asks for.
 * sampler.h declares what you write here; keep its names.
 */
#include "sampler.h"

#include "hashing.h"

/* Return a new sampler of the kind the name asks for ("independent",
 * "stratified", "halton", "sobol"), for spp samples per pixel and the
 * seed. TODO (workflow step 2): until you write it, there is none. */
std::unique_ptr<Sampler> makeSampler(const std::string &name, int spp, uint64_t seed)
{
    (void)name;
    (void)spp;
    (void)seed;
    return nullptr;
}

/* The m x n cells that count samples stratify into: m the largest divisor
 * of count not above its square root. TODO (workflow step 4). */
void gridOfCells(int count, int &m, int &n)
{
    m = 1;
    n = count;
}
