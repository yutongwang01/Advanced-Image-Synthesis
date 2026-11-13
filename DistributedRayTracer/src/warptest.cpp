/*
 * warptest.cpp: --warptest WARP N [PARAM] ("Warps" handout, Section 4),
 * given complete: N samples of a warp, one "x y z pdf" line each ("x y 0
 * pdf" for the disk, "b0 b1 b2 pdf" for the triangle), each pdf computed
 * from its point, for tools/warptest.py's chi-square test and pictures.
 * The 2D samples are the N samples of pixel (0, 0) under --sampler,
 * independent unless given. It calls your sampler and your warps by the
 * names sampler.h and warp.h fix; your warpTest() in rt.cpp calls it.
 */
#include "warp.h"

#include "options.h"

#include <cstdio>
#include <memory>
#include <string>

int runWarpTest(const Options &o)
{
    const std::string &w = o.warp;
    const float param = o.warpParam;
    if (w == "uniform_cone" && !(param >= -1 && param < 1)) {
        std::fprintf(stderr, "--warptest uniform_cone: cosThetaMax must be in [-1, 1), not %g\n", param);
        return 1;
    }
    if (w == "phong" && !(param >= 0)) {
        std::fprintf(stderr, "--warptest phong: the exponent must be at least 0, not %g\n", param);
        return 1;
    }

    const std::string name = o.sampler.empty() ? "independent" : o.sampler;
    std::unique_ptr<Sampler> sampler = makeSampler(name, o.warpCount, o.seed);
    if (!sampler) {
        std::fprintf(stderr, "--warptest: makeSampler(\"%s\") returned no sampler; write it first (workflow step 2)\n",
                     name.c_str());
        return 1;
    }
    static char buffer[1 << 16];
    std::setvbuf(stdout, buffer, _IOFBF, sizeof buffer);
    for (int i = 0; i < o.warpCount; i++) {
        sampler->startPixelSample(0, 0, i);
        const Sample2 u = sampler->next2D();
        Vec3 p;
        float pdf;
        if (w == "disk") {
            p = sampleConcentricDisk(u);
            pdf = concentricDiskPdf();
        } else if (w == "cosine_hemisphere") {
            p = sampleCosineHemisphere(u);
            pdf = cosineHemispherePdf(p.z);
        } else if (w == "uniform_sphere") {
            p = sampleUniformSphere(u);
            pdf = uniformSpherePdf();
        } else if (w == "uniform_cone") {
            p = sampleUniformCone(u, param);
            pdf = p.z >= param ? uniformConePdf(param) : 0;
        } else if (w == "triangle") {
            p = sampleUniformTriangle(u);
            pdf = uniformTrianglePdf();
        } else if (w == "phong") {
            p = samplePhongLobe(u, param);
            pdf = phongLobePdf(p.z, param);
        } else {
            std::fprintf(stderr, "--warptest: no warp '%s'\n", w.c_str());
            return 1;
        }
        std::printf("%.9g %.9g %.9g %.9g\n", p.x, p.y, p.z, pdf);
    }
    std::fflush(stdout);
    return 0;
}
