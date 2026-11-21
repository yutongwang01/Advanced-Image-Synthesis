/*
 * 0_scene_table.cpp: every HW4 scene gauchoRT can render, by name
 * and tier: the test scenes (1_TestScenes/, a file per scene, in the
 * Suggested Workflow's order), then 2_CompareScenes/, then 3_HeroScenes/.
 * A scene of your own for an extra credit goes in 4_ExtraCreditScenes/,
 * with tier "extra".
 * Not a scene file: this table is how main.cpp (or main.rs) reaches the
 * scene functions. To add a scene, declare its function here and add one
 * line to RT_SCENES_HW4. The HW2 and HW3 scenes keep their own folders
 * and tables (scenes.h).
 */
#include "scenes.h"

/* 1_TestScenes/ */
void Furnace(void);
void VeachMIS(void); /* also with --ISstrategy light and --ISstrategy bsdf */
void MaterialBalls(void);
void EnvLit(void); /* also with --env-sampling uniform */
void Caustics(void); /* the light through glass that kajiya cannot find */
void NormalMap(void); /* a normal map, which needs the hit's tangents */

/* 2_CompareScenes/ */
void SponzaGI(void);

/* 3_HeroScenes/ (interiors lit by bounce light: for kajiya) */
void Classroom(void);
void Colonnato(void);
void HallBench(void);
void LoneMonk(void);
void JunkShop(void);

extern "C" const RtSceneEntry RT_SCENES_HW4[] = {
    /* 1_TestScenes/ */
    {"Furnace", Furnace, "test"},
    {"VeachMIS", VeachMIS, "test"},
    {"MaterialBalls", MaterialBalls, "test"},
    {"EnvLit", EnvLit, "test"},
    {"Caustics", Caustics, "test"},
    {"NormalMap", NormalMap, "test"},
    /* 2_CompareScenes/ */
    {"SponzaGI", SponzaGI, "compare"},
    /* 3_HeroScenes/ */
    {"Classroom", Classroom, "hero"},
    {"Colonnato", Colonnato, "hero"},
    {"HallBench", HallBench, "hero"},
    {"LoneMonk", LoneMonk, "hero"},
    {"JunkShop", JunkShop, "hero"},
    {NULL, NULL, NULL}
};
