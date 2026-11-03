/*
 * cmdline.cpp: the command line of gauchoRT (the README lists it): the
 * options, each overriding what the scene file set, read into `options`
 * (options.h); usage(), which lists them and the scenes; the scene tables
 * main.cpp renders from, one per folder of scenes/ the build found
 * (scenes.h; all and the tiers take the tables up to RT_HOMEWORK, a name
 * runs anywhere); and imagePath, where rtWorldEnd writes. Complete in both kits; nothing
 * to change.
 */
#include "cmdline.h"
#include "options.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

using namespace std;

Options options; /* filled by parseCommandLine before any scene runs */

/* scenes --------------------------------------------------------------- */

const SceneTable SCENE_TABLES[] = {
    {RT_SCENES_HW2, "HW2", 2, "scenes/1_Whitted"},
#ifdef RT_HAVE_SCENES_HIDDEN
    {RT_SCENES_HIDDEN, "Hidden", 2, "scenes/hidden"},
#endif
#ifdef RT_HAVE_SCENES_HW3
    {RT_SCENES_HW3, "HW3", 3, "scenes/2_Cook"},
#endif
#ifdef RT_HAVE_SCENES_HW4
    {RT_SCENES_HW4, "HW4", 4, "scenes/3_Kajiya"},
#endif
    {NULL, NULL, 0, NULL},
};

namespace {

/* Whether a scene renders a movie's numbered frames: its name ends in
 * "Movie" (scenes.h). */
bool isMovie(const RtSceneEntry *s)
{
    const size_t n = strlen(s->name);
    return n >= 5 && strcmp(s->name + n - 5, "Movie") == 0;
}

/* Scene names are what you type, so no two entries may share one, in one
 * table or across two. */
void checkUniqueNames()
{
    vector<pair<string, const char *>> names; /* (name, folder) */
    for (const SceneTable *table = SCENE_TABLES; table->scenes; table++)
        for (const RtSceneEntry *s = table->scenes; s->name; s++)
            names.emplace_back(s->name, table->folder);
    sort(names.begin(), names.end());
    for (size_t i = 1; i < names.size(); i++)
        if (names[i].first == names[i - 1].first) {
            fprintf(stderr, "scene '%s' is in %s/ and %s/: scene names must be unique\n",
                    names[i].first.c_str(), names[i - 1].second, names[i].second);
            exit(1);
        }
}

} // namespace

bool selected(const char *scene, const RtSceneEntry *s, int hw)
{
    if (strcmp(scene, s->name) == 0)
        return true;
    if (hw > RT_HOMEWORK)
        return false;
    if (strcmp(scene, "stills") == 0)
        return !isMovie(s);
    return strcmp(scene, "all") == 0 || strcmp(scene, s->tier) == 0;
}

/* integrators and warps ------------------------------------------------ */

namespace {

/* The integrators, the homework each arrives in, and the folder of
 * renders/ and reference_renders/ its images go to (numbered so the
 * folders sort in homework order). Each named for the paper it
 * implements: Whitted 1980; Cook, Porter and Carpenter 1984 (distribution
 * ray tracing); Kajiya 1986 (path tracing). */
struct IntegratorName {
    const char *name;
    int hw;
    const char *folder;
};

const IntegratorName INTEGRATORS[] = {{"whitted", 2, "1_Whitted"}, {"cook", 3, "2_Cook"}, {"kajiya", 4, "3_Kajiya"}};

/* The homework an integrator arrives in, or 0 if there is no such name. */
int integratorHomework(const string &name)
{
    for (const IntegratorName &in : INTEGRATORS)
        if (name == in.name)
            return in.hw;
    return 0;
}

/* The warps of --warptest, and whether each takes a PARAM. The first six
 * are HW3's; ggx, the GGX distribution of visible normals, is HW4's. */
struct WarpName {
    const char *name;
    bool param;
};

const WarpName WARPS[] = {{"disk", false},        {"cosine_hemisphere", false}, {"uniform_sphere", false},
                          {"uniform_cone", true}, {"triangle", false},          {"phong", true},
                          {"ggx", true}};

} // namespace

/* usage ---------------------------------------------------------------- */

void usage()
{
    fprintf(stderr,
            "usage: gauchoRT <scene> [number ...] [options]\n"
            "                          (the numbers are the scene's own arguments, rtSceneArg)\n"
            "       gauchoRT all | stills | test | benchmark | study | compare | hero | extra [options]\n"
            "       gauchoRT --help       this text\n"
            "                          (stills: all but the movies, the scenes named ...Movie)\n"
            "       gauchoRT --warptest WARP N [PARAM]   (HW3) print N samples of a warp and exit;\n"
            "                          WARP: disk, cosine_hemisphere, uniform_sphere, triangle,\n"
            "                          uniform_cone COSTHETAMAX, phong EXPONENT, ggx ALPHA (HW4)\n"
            "       gauchoRT --bsdftest [ARG ...]        (HW4) check the BSDFs and exit; the arguments,\n"
            "                          up to the next option, are the renderer's to read\n"
            "options, each overriding what the scene file set:\n"
            "  --integrator whitted|cook|kajiya\n"
            "                          which integrator renders (default: this homework's)\n"
            "  --width W --height H    image size\n"
            "  --spp N                 samples per pixel (whitted renders one whatever it says)\n"
            "  --max-depth N           ray depth\n"
            "  --ambient R G B         ka of the shading formula, overriding the scene's rtAmbient\n"
            "                          (0 0 0: the lights alone; kajiya ignores it, as it does rtAmbient)\n"
            "  --threads N             worker threads (default: all cores)\n"
            "  --no-accel              brute force over every primitive (the timing table)\n"
            "  --time-limit S          stop after S seconds of rendering; the LIMIT line says how\n"
            "                          much was done and what the whole frame would take\n"
            "  --progress              a line on stderr every two seconds while rendering: the\n"
            "                          pixels done, the time, about how long is left\n"
            "  --frames A-B            of a movie, render frames A to B only (from 0; N alone: one)\n"
            "  --trace X Y             log what the renderer does for that pixel (debug.h): its rays,\n"
            "                          hits, lights and weights, on stderr\n"
            "  --out PATH              write PATH.png (and PATH.pfm) instead of\n"
            "                          renders/<integrator>/<name>\n"
            "  --pfm                   also write the linear float image\n"
            "  --format png|jpg        the image's file type (default png; jpg for sharing a picture)\n"
            "  --tonemap none|reinhard|aces|filmic\n"
            "                          the PNG's tone map (default: the scene's rtTonemap)\n"
            "  --exposure EV           stops added before the tone map (default: the scene's)\n"
            "  --lens R                the lens radius in meters, overriding the scene's rtLens\n"
            "                          (0: a pinhole); the focus distance stays the scene's\n"
            "  --aov NAME              a debug image instead of the beauty: raydir (the camera ray's\n"
            "                          direction, before any hit: the check of the viewing geometry),\n"
            "                          normal, ng, depth, uv, albedo, cost (the camera ray's BVH work\n"
            "                          over 512), shadow (the fraction of the lights unblocked), ao\n"
            "                          (HW3); the renderer refuses a name it does not have. Written as\n"
            "                          renders/<integrator>/<name>_<aov> unless --out says otherwise\n"
            "  --sampler independent|stratified|halton|sobol\n"
            "                          (HW3) the sample sequence\n"
            "  --seed N                (HW3) random seed; same seed, same image\n"
            "  --light-samples N       (HW3) cook samples every light N times at each hit,\n"
            "                          averaged (default 1)\n"
            "  --env-sampling importance|uniform\n"
            "                          (HW4) how kajiya samples the environment as a light: by its\n"
            "                          brightness (default) or uniformly over the sphere\n"
            "  --ISstrategy mis|light|bsdf\n"
            "                          (HW4) kajiya's strategies for the light from the lights:\n"
            "                          both weighted by MIS (mis, the default), or one alone\n"
            "  --bvh sah|midpoint      which BVH builder (a renderer with one builder ignores it)\n"
    );
    for (const SceneTable *table = SCENE_TABLES; table->scenes; table++) {
        fprintf(stderr, "%s scenes (%s/)%s:\n", table->label, table->folder,
                table->hw > RT_HOMEWORK ? ", by name only until RT_HOMEWORK reaches this homework" : "");
        const char *tier = "";
        for (const RtSceneEntry *s = table->scenes; s->name; s++) {
            if (strcmp(tier, s->tier) != 0) {
                tier = s->tier;
                fprintf(stderr, "  %s:\n", tier);
            }
            fprintf(stderr, "    %s%s\n", s->name, isMovie(s) ? " (movie: numbered frames)" : "");
        }
    }
}

/* the options ---------------------------------------------------------- */

namespace {

/* An integer option value of at least `least` (1 for a count, 0 for a
 * depth), or exit with a message. */
int integerAtLeast(const char *option, const char *value, long least)
{
    char *end = nullptr;
    long n = value ? strtol(value, &end, 10) : 0;
    if (!value || *value == '\0' || *end != '\0' || n < least) {
        fprintf(stderr, "%s needs %s, not '%s'\n", option, least > 0 ? "a positive integer" : "a non-negative integer",
                value ? value : "");
        exit(1);
    }
    return (int)n;
}

/* A positive integer option value, or exit with a message. */
int positiveInt(const char *option, const char *value) { return integerAtLeast(option, value, 1); }

/* A number option value, or exit with a message. */
float number(const char *option, const char *value)
{
    char *end = nullptr;
    double x = value ? strtod(value, &end) : 0;
    if (!value || *value == '\0' || *end != '\0') {
        fprintf(stderr, "%s needs a number, not '%s'\n", option, value ? value : "");
        exit(1);
    }
    return (float)x;
}

/* The one argument that follows an option, or exit with a message. An
 * option that takes several values names itself in `option`, since
 * argv[i] is then its previous value. */
const char *valueOf(int argc, char **argv, int &i, const char *option = nullptr)
{
    if (i + 1 >= argc) {
        fprintf(stderr, "%s needs a value\n", option ? option : argv[i]);
        exit(1);
    }
    i++;
    return argv[i];
}

/* Fill `options` from the command line and return the scene argument
 * (NULL if there was none). */
const char *readOptions(int argc, char **argv)
{
    const char *scene = nullptr;
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (strcmp(a, "--integrator") == 0) {
            options.integrator = valueOf(argc, argv, i);
            const int hw = integratorHomework(options.integrator);
            if (hw == 0) {
                fprintf(stderr, "--integrator: whitted, cook or kajiya, not '%s'\n", options.integrator.c_str());
                exit(1);
            }
            if (hw > RT_HOMEWORK) {
                fprintf(stderr,
                        "--integrator %s is HW%d's integrator, and this renderer is at HW%d.\n"
                        "When HW%d starts, set RT_HOMEWORK to %d in src/options.h.\n",
                        options.integrator.c_str(), hw, RT_HOMEWORK, hw, hw);
                exit(1);
            }
        } else if (strcmp(a, "--width") == 0)
            options.width = positiveInt(a, valueOf(argc, argv, i));
        else if (strcmp(a, "--height") == 0)
            options.height = positiveInt(a, valueOf(argc, argv, i));
        else if (strcmp(a, "--spp") == 0)
            options.spp = positiveInt(a, valueOf(argc, argv, i));
        else if (strcmp(a, "--max-depth") == 0)
            options.maxDepth = integerAtLeast(a, valueOf(argc, argv, i), 0); /* 0: no secondary rays at all */
        else if (strcmp(a, "--threads") == 0)
            options.threads = positiveInt(a, valueOf(argc, argv, i));
        else if (strcmp(a, "--no-accel") == 0)
            options.noAccel = true;
        else if (strcmp(a, "--ambient") == 0) {
            for (int k = 0; k < 3; k++) {
                const char *value = valueOf(argc, argv, i, a);
                char *end = nullptr;
                options.ambient[k] = (float)strtod(value, &end);
                if (!end || *end != '\0' || options.ambient[k] < 0) {
                    fprintf(stderr, "--ambient: three numbers, 0 or more, not '%s'\n", value);
                    exit(1);
                }
            }
            options.ambientSet = true;
        } else if (strcmp(a, "--time-limit") == 0) {
            const char *value = valueOf(argc, argv, i);
            char *end = nullptr;
            options.timeLimit = strtod(value, &end);
            if (!end || *end != '\0' || !(options.timeLimit > 0)) {
                fprintf(stderr, "--time-limit: seconds, more than 0, not '%s'\n", value);
                exit(1);
            }
        } else if (strcmp(a, "--out") == 0)
            options.out = valueOf(argc, argv, i);
        else if (strcmp(a, "--pfm") == 0)
            options.pfm = true;
        else if (strcmp(a, "--format") == 0) {
            options.format = valueOf(argc, argv, i);
            if (options.format != "png" && options.format != "jpg") {
                fprintf(stderr, "--format: png or jpg, not '%s'\n", options.format.c_str());
                exit(1);
            }
        }
        else if (strcmp(a, "--tonemap") == 0) {
            options.tonemap = valueOf(argc, argv, i);
            if (options.tonemap != "none" && options.tonemap != "reinhard" && options.tonemap != "aces" &&
                options.tonemap != "filmic") {
                fprintf(stderr, "--tonemap: none, reinhard, aces or filmic, not '%s'\n", options.tonemap.c_str());
                exit(1);
            }
        } else if (strcmp(a, "--exposure") == 0) {
            options.exposure = (float)atof(valueOf(argc, argv, i));
            options.exposureSet = true;
        } else if (strcmp(a, "--aov") == 0) {
            /* any name: the renderer knows which images it has and refuses
             * another, so a later homework can add one without a change here */
            options.aov = valueOf(argc, argv, i);
            if (options.aov.empty() || options.aov[0] == '-') {
                fprintf(stderr, "--aov needs an image's name\n");
                exit(1);
            }
        } else if (strcmp(a, "--sampler") == 0) {
            options.sampler = valueOf(argc, argv, i);
            const char *const samplers[] = {"independent", "stratified", "halton", "sobol"};
            bool known = false;
            for (const char *name : samplers)
                known = known || options.sampler == name;
            if (!known) {
                fprintf(stderr, "--sampler: independent, stratified, halton or sobol, not '%s'\n",
                        options.sampler.c_str());
                exit(1);
            }
        } else if (strcmp(a, "--seed") == 0) {
            const char *value = valueOf(argc, argv, i);
            char *end = nullptr;
            const unsigned long long n = strtoull(value, &end, 10);
            if (*value == '\0' || *value == '-' || *end != '\0') {
                fprintf(stderr, "--seed needs a non-negative integer, not '%s'\n", value);
                exit(1);
            }
            options.seed = (uint64_t)n;
        } else if (strcmp(a, "--light-samples") == 0) {
            options.lightSamples = atoi(valueOf(argc, argv, i));
            if (options.lightSamples < 1) {
                fprintf(stderr, "--light-samples needs a positive integer\n");
                exit(1);
            }
        } else if (strcmp(a, "--warptest") == 0) {
            options.warp = valueOf(argc, argv, i);
            const WarpName *warp = nullptr;
            for (const WarpName &w : WARPS)
                if (options.warp == w.name)
                    warp = &w;
            if (!warp) {
                fprintf(stderr,
                        "--warptest: disk, cosine_hemisphere, uniform_sphere, uniform_cone, triangle, "
                        "phong or ggx, not '%s'\n",
                        options.warp.c_str());
                exit(1);
            }
            options.warpCount = positiveInt(a, valueOf(argc, argv, i, a));
            if (warp->param)
                options.warpParam = number(a, valueOf(argc, argv, i, a));
        } else if (strcmp(a, "--bsdftest") == 0) {
            /* Everything up to the next option is the renderer's to read,
             * so the BSDF tests can take what HW4 needs without a change
             * here. */
            options.bsdfTest = true;
            while (i + 1 < argc && strncmp(argv[i + 1], "--", 2) != 0) {
                i++;
                options.bsdfTestArgs.push_back(argv[i]);
            }
        } else if (strcmp(a, "--ISstrategy") == 0) {
            options.isStrategy = valueOf(argc, argv, i);
            if (options.isStrategy != "mis" && options.isStrategy != "light" && options.isStrategy != "bsdf") {
                fprintf(stderr, "--ISstrategy: mis, light or bsdf, not '%s'\n", options.isStrategy.c_str());
                exit(1);
            }
        } else if (strcmp(a, "--env-sampling") == 0) {
            options.envSampling = valueOf(argc, argv, i);
            if (options.envSampling != "importance" && options.envSampling != "uniform") {
                fprintf(stderr, "--env-sampling: importance or uniform, not '%s'\n", options.envSampling.c_str());
                exit(1);
            }
        } else if (strcmp(a, "--help") == 0 || strcmp(a, "-h") == 0) {
            usage();
            exit(0);
        } else if (strcmp(a, "--progress") == 0)
            options.progress = true;
        else if (strcmp(a, "--frames") == 0) {
            const char *value = valueOf(argc, argv, i);
            char *end = nullptr;
            options.frameFirst = (int)strtol(value, &end, 10);
            options.frameLast = *end == '-' ? (int)strtol(end + 1, &end, 10) : options.frameFirst;
            if (*value == '\0' || *end != '\0' || options.frameFirst < 0 || options.frameLast < options.frameFirst) {
                fprintf(stderr, "--frames: a frame number N or a range A-B, from 0, not '%s'\n", value);
                exit(1);
            }
        } else if (strcmp(a, "--trace") == 0) {
            options.traceX = integerAtLeast(a, valueOf(argc, argv, i, a), 0);
            options.traceY = integerAtLeast(a, valueOf(argc, argv, i, a), 0);
        } else if (strcmp(a, "--lens") == 0) {
            options.lens = number(a, valueOf(argc, argv, i));
            if (options.lens < 0) {
                fprintf(stderr, "--lens needs a radius of 0 or more\n");
                exit(1);
            }
        } else if (strcmp(a, "--bvh") == 0) {
            options.bvh = valueOf(argc, argv, i);
            if (options.bvh != "sah" && options.bvh != "midpoint") {
                fprintf(stderr, "--bvh: sah or midpoint, not '%s'\n", options.bvh.c_str());
                exit(1);
            }
        }
        else if (a[0] == '-' && a[1] == '-') {
            fprintf(stderr, "unknown option '%s'\n", a);
            usage();
            exit(1);
        } else if (scene) {
            /* a number after the scene's name is the scene's own argument (rtSceneArg) */
            char *end = nullptr;
            const double v = strtod(a, &end);
            if (end == a || *end) {
                fprintf(stderr, "one scene (or tier) at a time: '%s' and '%s'\n", scene, a);
                exit(1);
            }
            options.sceneArgs.push_back((float)v);
        } else
            scene = a;
    }
    return scene;
}

} // namespace

const char *parseCommandLine(int argc, char **argv)
{
    checkUniqueNames();
    const char *scene = readOptions(argc, argv);

    /* A test instead of a render: --warptest WARP N [PARAM] (HW3) or
     * --bsdftest [ARG ...] (HW4), one at a time and with no scene. The
     * renderer defines both (options.h); its exit code is ours. */
    if (!options.warp.empty() || options.bsdfTest) {
        const char *test = options.bsdfTest ? "--bsdftest" : "--warptest";
        if (!options.warp.empty() && options.bsdfTest) {
            fprintf(stderr, "--warptest and --bsdftest: one test at a time\n");
            exit(1);
        }
        if (scene) {
            fprintf(stderr, "%s renders nothing: drop '%s'\n", test, scene);
            exit(1);
        }
        exit(options.bsdfTest ? bsdfTest(options) : warpTest(options));
    }

    if (!scene) {
        usage();
        exit(1);
    }
    return scene;
}

/* where an image goes -------------------------------------------------- */

string imagePath(const string &display)
{
    string path = options.out;
    if (path.empty()) {
        string folder = options.integrator;
        for (const IntegratorName &in : INTEGRATORS)
            if (options.integrator == in.name)
                folder = in.folder;
        path = "renders/" + folder + "/" + display;
        if (!options.aov.empty())
            path += "_" + options.aov;
        if (options.isStrategy != "mis")
            path += "_" + options.isStrategy;
        if (options.envSampling == "uniform")
            path += "_uniform";
    }
    const filesystem::path parent = filesystem::path(path).parent_path();
    error_code ignored;
    if (!parent.empty())
        filesystem::create_directories(parent, ignored);
    return path;
}
