# ECE/CS 285 HW2 – Whitted Ray Tracer

This project implements a Whitted-style ray tracer in C++. It supports
ray-sphere and ray-triangle intersections, direct illumination, hard
shadows, recursive reflection and refraction, surface and cutout shaders,
texture mapping, and environment mapping. The renderer uses BVH
acceleration and multithreaded tile rendering.

It is built on the HW2 starter kit: the renderer lives in `src/rt.cpp`
and implements the interface in `include/rt.h`, which is complete and
never changes. `reference_renders/` has every scene as the reference
renderer draws it. The same renderer continues through HW3 and HW4.

## Platform

Tested on: macOS (Apple Silicon), AppleClang, CMake 4.4.4.

## The three downloads

The kit is three zips that unzip into the same folder: `hw2.zip`, the
core kit (the code, the test and benchmark scenes, their models and
textures, their reference renders, about 100 MB), and `hw2_heroA.zip`
and `hw2_heroB.zip`, the hero scenes' models, textures and reference
renders (350 to 450 MB each). The hero scenes' source files are in the
core kit, so they build without the packs and fail at load time until the
pack they need is unzipped over the kit. Everything else works with the
core kit alone.

## Build

You need CMake (3.16 or newer) and a C++17 compiler (gcc, clang or
Visual Studio). Enter the `WhittedRayTracer/` folder (the one containing
`scenes/`, `models/`, `textures/` and `renders/`) and run:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j4
```

`CMakeLists.txt` picks a Release (optimized) build when no build type is
given. `-ffp-contract=off` is added to the compiler options for GCC and
Clang: it disables floating-point contraction, which made Dragon pass the
reference comparison.

On Windows, Visual Studio builds every configuration from one project, so
name the configuration when building and running:

    cmake -B build
    cmake --build build --config Release
    .\build\Release\gauchoRT.exe Spheres

CMake takes the first toolchain it finds. When that is not Visual Studio
(MSYS2's gcc with Ninja, say, on a machine that has both), its first
lines say so (`-- Building for: Ninja`), the configuration flag is
ignored, the executable is `.\build\gauchoRT.exe`, and it runs from the
shell whose PATH found that toolchain, where its runtime DLLs are.

The build takes every `.cpp` under `src/`; nothing in `CMakeLists.txt`
needs editing when files are added.

## Run

Run the executable from the `WhittedRayTracer/` folder: that is where it
looks for models and textures and where it writes the images.

```bash
./build/gauchoRT Spheres       # One scene
./build/gauchoRT benchmark     # Benchmark scenes
./build/gauchoRT hero          # Hero scenes
./build/gauchoRT stills        # All scenes except movies
./build/gauchoRT all           # All scenes
```

Output images are saved under `renders/1_Whitted/`.

### The command line

    gauchoRT <scene> [numbers]   one scene by name (gauchoRT alone lists them); the numbers
                                 after the name are the scene's own arguments (rtSceneArg)
    gauchoRT all                 every scene
    gauchoRT stills              every scene but the movies
    gauchoRT test | benchmark | hero
                                 one tier
    gauchoRT --help              every option

The options override what a scene asks for, so nothing in a scene file
ever needs editing: `--width W --height H`, `--threads N`, `--no-accel`
(brute force, for the timing table), `--bvh sah|midpoint`,
`--time-limit S` (stop after S seconds and estimate the whole frame),
`--progress`, `--frames A-B` (a movie's frames A to B only),
`--out PATH`, `--pfm` (the linear image beside the PNG), `--format jpg`,
`--tonemap` and `--exposure`, `--ambient R G B`, `--max-depth N`,
`--aov NAME` (a debug image instead of the picture: `raydir`, `normal`,
`ng`, `depth`, `uv`, `albedo`, `cost`, `shadow`) and `--trace X Y` (a log
of everything the renderer does for one pixel). The options of HW3 and
HW4 (`--spp`, `--sampler`, `--seed`, `--lens`, `--light-samples`,
`--warptest`, `--bsdftest`, `--env-sampling`, `--ISstrategy`) are parsed
now and ignored by a HW2 renderer.

## BVH Acceleration

Two BVH construction methods are implemented: midpoint splitting and
12-bin Surface Area Heuristic (SAH).

```bash
./build/gauchoRT Bunny --no-accel
./build/gauchoRT Bunny --bvh midpoint
./build/gauchoRT Bunny --bvh sah
```

The `--no-accel` option uses brute-force ray intersection.

## Multithreading

The renderer uses 16×16 image tiles distributed among worker threads.

```bash
./build/gauchoRT Dragon --threads 1
./build/gauchoRT Dragon --threads 4
```

The rendering results were checked for consistency across different
thread counts.

## The scenes

`scenes/1_Whitted/1_TestScenes/` holds the small test scenes, one per
file; each one's comment says what it shows and tests, and
`reference_renders/` shows the picture. `2_BenchmarkScenes/` holds the
three scenes the leaderboard times (`Dragon`, `Stadium`, `Forest`), and
`3_HeroScenes/` the big ones: `Sponza`, `WhiteLands`,
`BarcelonaPavilion`, `SanMiguel`, `Crown`, `Barbershop`, `Chess`,
`FishyCat` and `AtticCloseup`, plus `BarcelonaPavilionSunOnly` and
`BarbershopSunOnly`. The scene files are the inputs to the renderer and
must not be edited. To add a scene of your own, put it in a new file
under `scenes/1_Whitted/` and add one line to
`scenes/1_Whitted/0_scene_table.cpp`.

Every scene also carries the settings of HW3 and HW4 (`rtPixelSamples`,
`rtSampler`, `rtLens`, `rtMotionBegin`, `rtAmbientOcclusion`,
`rtEnvironmentPortal`): a HW2 renderer records them and ignores them, and
renders one ray through each pixel's center.

## Movies

The two movie scenes write numbered frames (`--frames 0-3` renders a few
of them while you work):

```bash
./build/gauchoRT MoleculeMovie
./build/gauchoRT FunWithRefractionMovie
```

After rendering the frames, the image sequences can be converted to MP4
using FFmpeg:

```bash
ffmpeg -framerate 24 \
  -i renders/1_Whitted/1_TestScenes/MoleculeMovie/MoleculeMovie_%03d.png \
  -c:v libx264 -pix_fmt yuv420p -crf 18 \
  renders/1_Whitted/1_TestScenes/MoleculeMovie.mp4
```

```bash
ffmpeg -framerate 24 \
  -i renders/1_Whitted/1_TestScenes/FunWithRefractionMovie/FunWithRefractionMovie_%03d.png \
  -c:v libx264 -pix_fmt yuv420p -crf 18 \
  renders/1_Whitted/1_TestScenes/FunWithRefractionMovie.mp4
```

## The TIME line and the benchmark

`rtWorldEnd` prints one line per image:

    TIME Sponza build=0.412 render=3.871 total=4.283 threads=8

`build` is the acceleration structure, `render` the pixel loop, `total`
includes writing the image. The leaderboard runs the benchmark and hero
scenes at 1920 x 1080 on 8 threads, three times each, and ranks the
median of `build + render` by its geometric mean over the scenes, after
checking each image against the reference with `tools/compare.py`.
`--no-accel` renders without the acceleration structure, for the timing
table of the write-up; `--time-limit S` makes that affordable by
rendering for S seconds and printing a `LIMIT` line that estimates the
whole frame. `python tools/timing_table.py` produces that table: every
benchmark and hero scene with the SAH builder, the midpoint builder, and
brute force for five minutes (`--runs 3` for the benchmark's median).

## Verification

Rendered images were compared with the provided reference images using:

```bash
python tools/compare.py renders/1_Whitted reference_renders/1_Whitted
```

It reports the RMSE of the pixel values, pass or fail against the
scene's threshold, and a difference image beside each failing one.
`--aov normal` and the other debug images, and `--trace X Y`, are the
tools for finding out why an image differs.

All 231 images with available references passed the RMSE threshold of
0.020.

## What is where

    include/rt.h          the interface the renderer implements; do not change it
    include/shaders.h     the four shaders and their parameter structs; do not change it
    include/scenes.h      the table of scenes (filled in by scenes/*/0_scene_table.cpp)
    include/debug.h       the trace log and the debug images' encoders
    include/timer.h       the stopwatch and the time budget of --time-limit
    include/*.h (others)  helpers the scene files use: obj.h loads the models,
                          sceneutil.h is shared scene code, vec.h a small vector type
    src/main.cpp          main(): runs the scenes the command line names
    src/cmdline.*         the command line (every option) and the scene tables
    src/options.h         the options struct main fills and the renderer reads, and
                          RT_HOMEWORK, the one line that changes when HW3 starts
    src/transform.*       the current transformation (rtTranslate and friends)
    src/image.*           writeImage (PNG with tone map, PFM); readImage (PNG, JPEG, HDR, PFM)
    src/noise.cpp         perlinNoise
    src/debug.cpp         --trace
    src/rt.cpp            the renderer
    src/shaders.cpp       TEXTURED, CHECKER, ALPHA_CUTOUT, CHECKER_CUTOUT
    src/texture.*         the texture table and its lookup behind rtMakeTexture and rtTexture
    rust/                 the same kit for Rust
    scenes/1_Whitted/     the scene files, in three tiers
    renders/              where the rendered images go, in the same folders
    reference_renders/    the same images from the reference renderer
    textures/, models/    what the scenes load
    third_party/          stb_image.h and stb_image_write.h
    tools/compare.py      RMSE and a difference image between two PNGs or two folders
    tools/timing_table.py the write-up's timing table over the benchmark and hero scenes

## Extra Credit

None attempted.

## AI-use statement

I used ChatGPT to help understand ray tracing concepts, interpret
assignment requirements, troubleshoot compiler and rendering issues, and
analyze differences between my renders and the reference images.
Meanwhile, I used GPT to discuss implementation approaches, debug
numerical precision issues, and analyze BVH and multithreading
performance.
