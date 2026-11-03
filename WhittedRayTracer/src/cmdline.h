/*
 * cmdline.h: the command line of gauchoRT (the README's command line), read by
 * cmdline.cpp into `options` (options.h), and the scene tables main.cpp
 * renders from. Complete in both kits; nothing to change.
 */
#ifndef CMDLINE_H
#define CMDLINE_H

#include "scenes.h"

/* A scene table (scenes.h), what to call it, the homework it belongs to
 * (the hidden benchmark scene counts as HW2's) and its folder. */
struct SceneTable {
    const RtSceneEntry *scenes; /* {name, function, tier} entries ending with a NULL name */
    const char *label;          /* "HW2", "HW3", ... */
    int hw;
    const char *folder;         /* "scenes/1_Whitted", ... */
};

/* One table per folder of scenes/ the build found, ending with an entry
 * whose scenes pointer is NULL. */
extern const SceneTable SCENE_TABLES[];

/* Read the command line into `options` and return the scene argument: a
 * scene's name, a tier, "all" or "stills". Everything that does not end in
 * a render is settled in here and exits: a bad option, --help, no scene
 * argument, and the tests --warptest (HW3) and --bsdftest (HW4). */
const char *parseCommandLine(int argc, char **argv);

/* Whether the scene argument selects this entry of a table of homework hw:
 * by name always; by "all", "stills" or its tier only when the table is
 * this homework's or an earlier one's (RT_HOMEWORK, options.h). */
bool selected(const char *scene, const RtSceneEntry *s, int hw);

/* The usage text and the list of scenes, on stderr. */
void usage();

#endif /* CMDLINE_H */
