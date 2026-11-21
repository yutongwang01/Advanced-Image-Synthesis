# ECE/CS 285 — HW3

This C++ project extends the HW2 **Whitted ray tracer** into the HW3 **Cook distributed ray tracer**. Both integrators remain available in one executable, `gauchoRT`, through `--integrator whitted|cook`. The renderer follows the course RenderMan-like interface and renders the provided scenes.

## Platform and dependencies

- **Tested platform:** macOS on Apple Silicon, AppleClang, CMake 4.3.4.
- **Build:** CMake 3.16+ and a C++17 compiler.
- **Study tools:** Python 3 with NumPy, Pillow and Matplotlib; SciPy for chi-square p-values when available.
- **Video / montage generation:** FFmpeg.

Run all commands from the project root (`DistributedRayTracer/`), alongside `scenes/`, `models/`, `textures/` and `renders/`.

## Build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release -j4
```

The executable is normally `./build/gauchoRT` on macOS. The provided `CMakeLists.txt` builds `.cpp` files under `src/` automatically.

HW3 upgrades the same HW2 project rather than replacing the HW2 renderer. With `RT_HOMEWORK` set to `3` in `src/options.h`, **Cook is the default**. Select Whitted explicitly to reproduce HW2 behavior.

## Running scenes

```bash
# HW2 (Whitted)
./build/gauchoRT Spheres --integrator whitted
./build/gauchoRT Bunny --integrator whitted --bvh sah

# HW3 (Cook)
./build/gauchoRT QuadLight --integrator cook
./build/gauchoRT SphereLight --integrator cook --pfm
./build/gauchoRT Skylight --integrator cook

# HW3 tiers: scenes with Cook references
./build/gauchoRT test
./build/gauchoRT study
./build/gauchoRT hero --progress
```

The default render folders are `renders/1_Whitted/` and `renders/2_Cook/`, matching the corresponding reference hierarchy. The `test`, `study`, `hero`, `all` and `stills` selectors use the active integrator's scene selection rules.

Useful overrides:

| Option | Meaning |
|---|---|
| `--integrator whitted\|cook` | Choose the rendering method |
| `--width W --height H` | Override image resolution |
| `--spp N` | Pixel samples per pixel (Cook) |
| `--sampler independent\|stratified\|halton` | Select the sampling sequence (Cook) |
| `--seed S` | Choose sampler seed |
| `--lens R` | Override thin-lens aperture radius |
| `--light-samples N` | Number of samples per area light and environment light at each shading point; delta lights (point, directional, and spot) are evaluated once |
| `--threads N` | Number of renderer worker threads |
| `--bvh sah\|midpoint`, `--no-accel` | Acceleration selection / brute-force traversal |
| `--time-limit S --progress` | Limited render / progress reports |
| `--out PATH --pfm` | Override output and optionally save linear PFM |
| `--aov NAME`, `--trace X Y` | Debug output and ray tracing for one pixel |


## HW2 — Whitted ray tracer

The HW2 renderer implements:

- Camera rays and sphere/triangle intersection.
- Direct lighting, hard shadows, recursive mirror reflection and refraction.
- Surface and cutout shaders, texture mapping and environment backgrounds.
- BVH acceleration using midpoint splitting and 12-bin surface area heuristic (SAH), with a brute-force option for comparisons.
- Multi-threaded rendering using 16×16 image tiles, deterministic results across thread counts, debugging AOVs and trace logs.

Example acceleration checks:

```bash
./build/gauchoRT Bunny --integrator whitted --no-accel
./build/gauchoRT Bunny --integrator whitted --bvh midpoint
./build/gauchoRT Bunny --integrator whitted --bvh sah
```

## HW3 — Cook distributed ray tracer

HW3 retains Whitted and introduces distributed sampling for effects that HW2 estimated with one ray or a simplified approximation:

- **Samplers:** independent PCG-based samples, stratified samples, and a scrambled Halton sequence.
- **Warps:** concentric disk, cosine hemisphere, uniform sphere, uniform cone, triangle and Phong lobe, each with its PDF.
- **Camera:** stochastic film samples for antialiasing and thin-lens depth of field.
- **Area lighting:** mesh and sphere emitters, shadow-ray visibility and next-event estimation (NEE), including multiple light samples.
- **Environment lighting:** importance sampling of the latitude–longitude environment map.
- **Shading:** glossy reflection and Fresnel/coat treatment, clear and frosted transmission, and ambient occlusion.
- **Determinism:** sample dimensions are drawn consistently so rendering results do not depend on worker thread count.

### Verification

```bash
# Six warp distributions (chi-square tests)
python tools/warptest.py all

# Exact area-light test and sampling study
python tools/area_light_test.py --rt ./build/gauchoRT
python tools/light_samples.py --rt ./build/gauchoRT

# Sampler convergence study
python tools/convergence.py --rt ./build/gauchoRT

# Final reference comparison and completeness check
python tools/compare.py \
  renders/2_Cook reference_renders/2_Cook \
  --require-all \
  --exclude "4_ExtraCreditScenes/*" \
  --exclude "1_TestScenes/Convergence_ref.png"
```

**Recorded HW3 reference comparison:** **49 passed, 0 failed, 0 required files missing** in the final comparison. All seven Cook Hero Scenes passed their reference-image comparisons.

**Warp tests:** 6 of 6 PASS (`disk`, `cosine_hemisphere`, `uniform_sphere`, `uniform_cone`, `triangle`, `phong`).

**Area-light statistical test:** all 4, 16, 64 and 256 spp configurations PASS, with RMSE 0.02006, 0.00999, 0.00503 and 0.00252 respectively. The near-twofold RMSE reduction for each fourfold sample increase agrees with Monte Carlo convergence.

**Convergence-study fitted slopes:** independent −0.51, stratified −0.61 and Halton −0.67. The study generated `convergence.csv`, `convergence.png` and `SamplerGallery.png` in `renders/2_Cook/2_StudyScenes/`.

The convergence analysis and additional images are discussed in [`WRITEUP_HW3.md`](WRITEUP_HW3.md).

## Study images and movies

The Cook study outputs include `FocusSweep` (eight individual frames plus a 4×2 montage), `LightSamples.png` and the sampler convergence figures. Create or update the FocusSweep montage with:

```bash
ffmpeg -y \
  -i renders/2_Cook/2_StudyScenes/FocusSweep/FocusSweep_%03d.png \
  -vf tile=4x2 \
  renders/2_Cook/2_StudyScenes/FocusSweep.png
```

For the required `FrostedEarthMovie`, generate the MP4 from its rendered numbered frames using FFmpeg, with the frame rate specified by the scene or course instructions. Check that the video plays correctly; image comparison checks only that the MP4 exists.

## Hero scenes

The Cook hero tier includes `Candy`, `ChopperTitan`, `MagicalClock`, `BarcelonaPavilion`, `Crown`, `Chess` and `FishyCat`. These scenes are expensive at their supplied sample counts. Test a partial render in a separate path before committing to a full run:

```bash
./build/gauchoRT Candy --time-limit 60 --out renders/try/Candy
python tools/compare.py --partial \
  renders/try/Candy.png \
  reference_renders/2_Cook/3_HeroScenes/Candy.png

./build/gauchoRT hero --progress
```

**Partial Candy validation:** An earlier partial render passed the reference comparison with an RMSE of 0.00116 (threshold 0.010). The subsequent full Candy render also passed, with an RMSE of 0.00113.

## Project layout

```text
DistributedRayTracer/
├── CMakeLists.txt
├── README.md             
├── WRITEUP_HW3.md             # HW3 report
├── include/                   # Course/scene-facing interfaces (do not edit)
├── src/                       # Whitted, Cook, samplers, warps, BVH, camera, lights
├── scenes/                    # Provided HW2 and HW3 scenes
├── tools/                     # Given tests, comparisons, and study scripts
├── renders/
│   ├── 1_Whitted/
│   └── 2_Cook/
├── reports/
├── models/                    # Provided assets; omit from submission archive
├── textures/                  # Provided assets; omit from submission archive
└── reference_renders/         # Provided references; omit from submission archive
```

## Extra Credit

None attempted.

## AI-use statement

ChatGPT was used to explain course concepts and assignment steps, discuss implementation approaches, diagnose compilation and rendering issues, interpret numerical test output, and assist with the organization and wording of documentation.
