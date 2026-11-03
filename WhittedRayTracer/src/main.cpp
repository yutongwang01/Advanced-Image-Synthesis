/*
 * main.cpp: render one scene by name (gauchoRT Spheres; gauchoRT alone lists
 * them) or a set of them (gauchoRT all; gauchoRT stills, without the movies;
 * gauchoRT test | benchmark | study | compare | hero | extra, one tier). The
 * command line is read in cmdline.cpp (usage() there lists every option)
 * into `options` (options.h), which the renderer reads at rtWorldEnd.
 * Complete in both kits; the one edit is the SIMPLE_MAIN switch below. Run
 * from the folder that holds textures/ and models/.
 */
#include "cmdline.h"
#include "options.h"

#include <cstdio>

using namespace std;

/* To make things easier, you have two main() functions. SIMPLE_MAIN 1 is
 * the one to start with: a scene is a function (one per scene under
 * scenes/) that draws through your rt* calls and writes its image, and
 * this main calls one. SIMPLE_MAIN 0 is the real one: the scene named on
 * the command line, found in the scene tables, with every option. Switch
 * once your first scene renders. */
#define SIMPLE_MAIN 0

#if SIMPLE_MAIN

void Spheres(void); /* scenes/1_Whitted/1_TestScenes/Spheres.cpp */

int main()
{
    /* The options are a struct (options.h) the renderer reads at rtWorldEnd;
     * the real main below fills it from the command line. Here they are set
     * by hand: uncomment what a step of the Suggested Workflow asks for. */
    // options.out = "renders/hello"; /* the image goes to renders/hello.png; rename it to keep the last one */
    // options.aov = "raydir";        /* output the ray direction for debugging eye ray calculation */
    // options.aov = "normal";        /* output the normals for debugging (or "depth", "uv", "albedo", "shadow") */
    // options.pfm = true;            /* also write the exact floats, renders/hello.pfm (tools/compare.py reads both) */
    // options.width = 240; options.height = 135; /* a quarter-size image while a scene is slow */
    // options.threads = 1;           /* one worker, to debug the pixel loop */
    // options.traceX = 480; options.traceY = 270; /* --trace: log what the renderer does for one pixel */
    // options.noAccel = true;        /* brute force, without the BVH: the check of the BVH */

    /* Render the scene */
    Spheres();
    return 0;
}

#else

int main(int argc, char **argv)
{
    /* The options go into `options`; what comes back is the scene argument,
     * one scene's name or a set of them (all, stills, or a tier). --help, a
     * missing scene and the tests --warptest and --bsdftest exit in there. */
    const char *scene = parseCommandLine(argc, argv);

    /* Run every scene the argument selects. A table is an array of {name,
     * function, tier} entries ending with a NULL name (scenes.h), one per
     * folder of scenes/ the build found. */
    int ran = 0;
    for (const SceneTable *table = SCENE_TABLES; table->scenes; table++)
        for (const RtSceneEntry *s = table->scenes; s->name; s++)
            if (selected(scene, s, table->hw)) {
                printf("== %s\n", s->name);
                fflush(stdout);
                options.scene = s->name; /* for the TIME line */
                /* Render the scene */
                s->function();
                ran++;
            }

    if (ran == 0) {
        fprintf(stderr, "unknown scene '%s'\n", scene);
        usage();
        return 1;
    }
    return 0;
}

#endif
