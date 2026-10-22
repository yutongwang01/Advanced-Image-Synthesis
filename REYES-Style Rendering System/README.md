# ECE/CS 285 HW1 – REYES-Style Rendering System

This project implements a REYES-style micropolygon renderer in C++. It supports geometric primitives, polygon meshes, transformations, texture mapping, surface and displacement shading, depth buffering, and transparency.

## Platform

Tested on: macOS (Apple Silicon), AppleClang, CMake 4.4.4

## Build

Enter the starter/ folder (the one containing `scenes/`, `textures/` and `renders/`) and run:

    cd starter
    cmake -S . -B build-mac -DCMAKE_BUILD_TYPE=Release
    cmake --build build-mac -j4

`CMakeLists.txt` selects a Release build by default.

## Run

Run the executable from the starter/ folder so it finds `textures/` and `models/`
and writes into `renders/`:

    ./build-mac/reyes <scene>        one scene by name (no argument lists them)
    ./build-mac/reyes test           all Tier 1 scenes, including SimpleAnimationMovie
    ./build-mac/reyes intermediate   all Tier 2 scenes
    ./build-mac/reyes cinematic      all Tier 3 scenes, including WallEMovie
    ./build-mac/reyes stills         every still image, skipping the two movies
    ./build-mac/reyes all            everything, including the original scene

Original scene:

    ./build-mac/reyes GipsyDanger

Output: `renders/4_OriginalScene/GipsyDanger.png` (960 × 600, 4 × 4 samples).

## Movies

After rendering the frames:

    ffmpeg -framerate 24 -i renders/1_TestScenes/SimpleAnimation/SimpleAnimation_%02d.png \
           -c:v libx264 -pix_fmt yuv420p -crf 18 renders/1_TestScenes/SimpleAnimation.mp4
    ffmpeg -framerate 24 -i renders/3_CinematicScenes/WallEMovie/WallEMovie_%02d.jpg \
           -c:v libx264 -pix_fmt yuv420p -crf 18 renders/3_CinematicScenes/WallEMovie.mp4

## Original scene: Gipsy Danger

`scenes/4_OriginalScene/GipsyDanger.cpp`, registered in
`scenes/0_scene_table.cpp` as `GipsyDanger` (tier `original`) and listed in
`CMakeLists.txt`.

A close, low-angle shot of the Jaeger Gipsy Danger. 

Renderer features it uses:

- **Polygon meshes**: the Gipsy Danger OBJ (vertex normals and texture
  coordinates), plus box/quad geometry for the hangar, door jamb, mountain
  ridge and sky.
- **Textures**: the model's seven material textures, loaded with
  `RiMakeTexture` into slots 0–6 and sampled with `RiTexture` in the hero
  shader.
- **Custom surface shaders** (scene-local, built on the renderer's
  `perlinNoise()`): dirty rain-streaked concrete, the hero armor (cool rim
  light, hemispheric ambient, emissive arc reactor), storm sky, mountain,
  drifting fog, light beams, radial glows and a lens vignette.
- **Transparency**: three fog layers, ~1,700 rain streaks, soft light beams,
  lamp/reactor glows, the backlit haze behind the robot and the vignette,
  all composited through the per-sample hit lists.
- **Depth output and shadows**: a depth pass from an off-screen floodlight
  (`RiDisplay(..., "zfile", "z")`), read back with `shadowmap.h`'s
  `zfile_load`/`zfile_visibility`; the Jaeger's shadow falls on the hangar
  facade on the left.

## Model sources

- **Gipsy Danger** (`models/GipsyDanger/gipsy_danger.obj` and
  `models/GipsyDanger/textures/Image_*.jpg`): "Gipsy Danger (No Rigged bone)
  Pacific rim" by **KenzoDkohno22T** on Sketchfab,
  <https://sketchfab.com/3d-models/gipsy-danger-no-rigged-bone-pacific-rim-22e127392df94315a1a7e5b29967cf36>,
  licensed under **Creative Commons Attribution (CC BY 4.0)**. The author's
  page further credits @EvesStudio2008, @William_3d and @Crimsonanimations.
  - *Conversion*: the downloaded model was opened in **Blender** and exported
    as a Wavefront OBJ (`gipsy_danger.obj`, with vertex normals and texture
    coordinates, one material group per armor part) together with its seven
    base-color textures (`Image_0/2/4/6/8/10/12.jpg`), which the scene loads
    into texture slots 0–6. No geometry was modified.
  - Gipsy Danger is a character from *Pacific Rim* (© Legendary Pictures /
    Warner Bros.); the model is used here only for this non-commercial
    course assignment.
- All other scene geometry (hangar, sky, mountain, rain, fog) is generated
  procedurally in the scene file.
- The supplied starter-kit models and textures are used as provided.

## Extra credit

None attempted.

## AI-use statement

I used ChatGPT to help understand REYES concepts, interpret debugging errors, and troubleshoot build and rendering issues.

I also used AI assistance for artistic scene development, including scene composition, procedural environment geometry, lighting ideas, and the C++ code for my original Gipsy Danger scene.