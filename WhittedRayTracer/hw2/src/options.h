/*
 * options.h: the command line (the README lists it), read by cmdline.cpp
 * into `options` and by the renderer at rtWorldEnd, and the homework this
 * renderer is at. Every option overrides what the scene file set, so the benchmark
 * and the studies can be scripted without editing scenes; the "unset"
 * value means "as the scene said". Complete in both kits; the one line
 * that changes is RT_HOMEWORK. The scene files never see this file.
 */
#ifndef OPTIONS_H
#define OPTIONS_H

#include <cstdint>
#include <string>
#include <vector>

/* The homework this renderer is at: 2, 3 or 4. It picks the default
 * integrator (2 = whitted, 3 = cook, 4 = kajiya), which integrators
 * --integrator accepts (this homework's and earlier), and the scene tables
 * gauchoRT all and the tiers include (scenes/1_Whitted/ to scenes/<integrator>/);
 * gauchoRT <name> renders any scene the build has. */
#define RT_HOMEWORK 2

struct Options {
    std::string integrator = RT_HOMEWORK >= 4 ? "kajiya" : RT_HOMEWORK == 3 ? "cook" : "whitted";
                               /* --integrator whitted|cook|kajiya; this homework's unless given */
    int width = 0, height = 0; /* --width W --height H; 0: the scene's rtFormat */
    int spp = 0;               /* --spp N; 0: the scene's rtPixelSamples */
    int maxDepth = -1;         /* --max-depth N (0: no secondary rays); -1: the scene's rtMaxDepth */
    float ambient[3] = {0, 0, 0}; /* --ambient R G B: ka of the shading formula, overriding the scene's rtAmbient */
    bool ambientSet = false;   /*   whether --ambient was given; else the scene's */
    int threads = 0;           /* --threads N; 0: all cores */
    bool noAccel = false;      /* --no-accel: a linear scan over every primitive */
    double timeLimit = 0;      /* --time-limit S: stop handing out tiles after S seconds of rendering and print the
                                  LIMIT line (HW2 handout: timing); 0: render everything */
    std::string out;           /* --out PATH: write PATH.png (and .pfm); "": renders/<integrator>/<name> */
    bool pfm = false;          /* --pfm: also write the linear float image */
    std::string format = "png"; /* --format png|jpg: the image's file type; the PFM of --pfm is separate */
    std::string tonemap;       /* --tonemap none|reinhard|aces|filmic: the PNG's tone map; "": the scene's rtTonemap */
    float exposure = 0;        /* --exposure EV: stops added before the tone map */
    bool exposureSet = false;  /*   whether --exposure was given; else the scene's */
    std::string aov;           /* --aov NAME: a debug image instead of the beauty, by name; the renderer knows
                                  which it has (HW2: raydir, normal, ng, depth, uv, albedo, cost, shadow; HW3 adds ao; the
                                  reference adds badnormal) and refuses another; "": the beauty image */
    int traceX = -1, traceY = -1; /* --trace X Y: the pixel whose rays the renderer logs (debug.h); -1: none */
    bool progress = false;     /* --progress: a line on stderr every two seconds while rendering (timer.h) */
    int frameFirst = -1, frameLast = -1; /* --frames A-B: render frames A to B of a movie only (the worlds
                                            since rtBegin, from 0); -1: every frame */
    float lens = -1;           /* --lens R: the lens radius, overriding the scene's rtLens (0: a pinhole); -1: the scene's */
    std::string sampler;       /* --sampler independent|stratified|halton|sobol (HW3); "": the scene's rtSampler */
    uint64_t seed = 0;         /* --seed N (HW3): the same seed gives the same image */
    int lightSamples = 1;      /* --light-samples N (HW3): cook samples every light N times at a hit */
    std::string warp;          /* --warptest WARP N [PARAM] (HW3): the warp, "" when not testing one */
    int warpCount = 0;         /*   how many samples to print */
    float warpParam = 0;       /*   uniform_cone's cosThetaMax, phong's exponent or ggx's alpha; 0 for the others */
    bool bsdfTest = false;     /* --bsdftest [ARG ...] (HW4): check the BSDFs instead of rendering */
    std::vector<std::string> bsdfTestArgs; /* the arguments after it, up to the next option */
    std::vector<float> sceneArgs;  /* the numbers after the scene's name: rtSceneArg reads them */
    std::string envSampling = "importance"; /* --env-sampling importance|uniform (HW4): how kajiya samples the
                                               environment map as a light; whitted and cook ignore it */
    std::string isStrategy = "mis"; /* --ISstrategy mis|light|bsdf (HW4): kajiya's strategies for light that arrives from
                                  the lights, both weighted by MIS or one alone; whitted and cook ignore it */
    std::string bvh = "sah";   /* --bvh sah|midpoint: which builder; a renderer with one ignores it (Workflow, step 14) */
    bool noInstance = false;   /* --no-instance (reference only): bake every mesh, none drawn as an instance */
    bool accumulate = false;   /* --accumulate (reference only): keep <image>.acc, the sum of every sample and the
                                  count per pixel, and go on from it when it exists, so a reference is rendered to
                                  any count in several runs; the PNG and PFM are the mean so far. The same image
                                  as one long run under independent, halton and sobol, whose sample i depends on i
                                  alone; under stratified each run's strata are its own */
    int rebraid = 16;          /* --rebraid K (reference only): instancing opens each instance into the pieces of
                                  its BVH of at most K triangles, each a box of the scene's BVH (a huge K: one) */
    int cropX = 0, cropY = 0, cropW = 0, cropH = 0; /* --crop X Y W H (reference only): render only that
                                                       window of the full frame, as a W x H image; 0 0 0 0: all */
    std::string soloLight;     /* --solo-light N|A-B|none|ambient (reference only): one light's contribution
                                  alone (the N-th: the scene's light calls in order, then the emitters), the
                                  lights A to B together, none of them, or the ambient term alone; "": all */
    const char *scene = "";    /* the name of the scene being run, for the TIME line */
};

extern Options options; /* defined in cmdline.cpp, filled before any scene runs */

/* Where rtWorldEnd writes an image, without extension: --out as given,
 * else renders/<integrator>/<display>, <integrator> the folder 1_Whitted,
 * 2_Cook or 3_Kajiya (numbered to sort in homework order) and <display>
 * the rtDisplay name, with _<aov> appended for a debug image, _light or
 * _bsdf for --ISstrategy, and _uniform for --env-sampling uniform, so a
 * variant never overwrites the default. Makes the folder (cmdline.cpp). */
std::string imagePath(const std::string &display);

/* --warptest (HW3 handout): print options.warpCount samples of
 * the warp options.warp, one "x y z pdf" line each ("x y 0 pdf" for the
 * disk), and return 0, or 1 with a message. cmdline.cpp calls it instead of
 * rendering; until HW3 (HW4 for ggx) it only says the warp is not there. */
int warpTest(const Options &o);

/* --bsdftest (HW4 handout): check the BSDFs (each pdf integrates
 * to at most 1 and its samples follow it), reading options.bsdfTestArgs as
 * the HW4 handout says, and return 0, or 1 with a message. cmdline.cpp calls
 * it instead of rendering; until HW4 it says the BSDFs are not there. */
int bsdfTest(const Options &o);

#endif /* OPTIONS_H */
