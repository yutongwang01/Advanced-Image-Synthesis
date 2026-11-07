# ECE/CS 285 HW2 – Whitted Ray Tracer

This project implements a Whitted-style ray tracer in C++. It supports ray-sphere and ray-triangle intersections, direct illumination, hard shadows, recursive reflection and refraction, surface and cutout shaders, texture mapping, and environment mapping. The renderer uses BVH acceleration and multithreaded tile rendering.

## Platform

Tested on: macOS (Apple Silicon), AppleClang, CMake 4.4.4.

## Build

Enter the `WhittedRayTracer/` folder (the one containing `scenes/`, `models/`, `textures/`, and `renders/`) and run:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j4
```

Adding -ffp-contract=off to the CMake compiler options for GCC and Clang. This disables floating-point contraction and made Dragon pass the reference comparison.

## Run

Run the executable from the `WhittedRayTracer/` folder:

```bash
./build/gauchoRT Spheres       # One scene
./build/gauchoRT benchmark     # Benchmark scenes
./build/gauchoRT hero          # Hero scenes
./build/gauchoRT stills        # All scenes except movies
./build/gauchoRT all           # All scenes
```

Output images are saved under `renders/1_Whitted/`.

## BVH Acceleration

Two BVH construction methods are implemented: midpoint splitting and 12-bin Surface Area Heuristic (SAH).

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

The rendering results were checked for consistency across different thread counts.

## Movies

The two movie scenes are:

```bash
./build/gauchoRT MoleculeMovie
./build/gauchoRT FunWithRefractionMovie
```

After rendering the frames, the image sequences can be converted to MP4 using FFmpeg:

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

## Verification

Rendered images were compared with the provided reference images using:

```bash
python tools/compare.py renders/1_Whitted reference_renders/1_Whitted
```

All 231 images with available references passed the RMSE threshold of 0.020.


## Extra Credit

None attempted.

## AI-use statement

I used ChatGPT to help understand ray tracing concepts, interpret assignment requirements, troubleshoot compiler and rendering issues, and analyze differences between my renders and the reference images. Meanwhile, I used GPT to discuss implementation approaches, debug numerical precision issues, and analyze BVH and multithreading performance.

