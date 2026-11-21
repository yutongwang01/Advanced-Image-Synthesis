# ECE/CS 285 HW4: a path tracer

This is the upgrade kit for HW4. Your job is to turn the distributed ray
tracer that you wrote in HW3 into a path tracer, the `kajiya`
integrator. Build on your own HW3 renderer, whatever state it is in.
There is no reference HW3 renderer to start from.

Follow the 15 steps of the Suggested Workflow, and take the formulas
from the "BSDFs" and "Path Tracing" handouts. If the HW4 handout and one
of those two seem to disagree, follow the technical handout.

## Installing the kit

1. Save your HW3 work first, by copying the folder or by committing it.
   The kit unzips over that folder and replaces some given files, so
   this copy is your way back.
2. If you wrote files named `bsdf`, `bsdftest`, or `roughdielectric`,
   give yours other names, because the kit adds files with those names.
   The C++ build finds the renamed files by itself. In Rust, change
   their `mod` lines to match.
3. Unzip the kit's two zips, `hw4.zip` and `hw4_junkshop.zip`, over your
   HW3 folder. The second holds only the model of the hero scene
   `JunkShop`. After step 2, the kit overwrites none
   of the files that you wrote. It replaces some given files with new
   versions, and it adds new files ("What the kit adds", below). This
   file is `README_HW4.md`, so your own `README.md` stays.
4. Make the edits of the next section.
5. Build from scratch. In C++, delete `build/`, then run:

        cmake -B build
        cmake --build build --config Release

    In Rust:

        cargo clean
        cargo build --release

    An incremental build is not enough. The files of the kit carry the
    date of the zip, which is older than your last build, so the build
    would not recompile the new `warptest` and `cmdline`.

## The edits to your files

In C++:

1. Set `RT_HOMEWORK` to 4 in `src/options.h`. This makes `kajiya` the
   default integrator. It also makes `gauchoRT all` and the tiers take
   the HW4 scenes and the earlier scenes that have a `kajiya` reference.
2. Turn on the given `--bsdftest`. Your `src/rt.cpp` has had a stub
   `bsdfTest()` since HW2. Add `#include "bsdf.h"` to `rt.cpp`, and make
   the stub `return runBsdfTest(o);`. Your `warpTest()` has returned
   `runWarpTest(o)` in the same way since HW3.

In Rust:

1. Add `mod bsdf;`, `mod bsdftest;`, and `mod roughdielectric;` to
   `rust/src/main.rs`, beside the other `mod` lines. Do this first,
   because the kit's new `warptest.rs` calls `bsdf.rs` and the crate
   does not build without these lines.
2. Set `RT_HOMEWORK` to 4 in `rust/src/options.rs`.
3. In `rust/src/rt.rs`, make the stub `bsdf_test` return
   `crate::bsdftest::run_bsdf_test(o)`.

In C++ and in Rust, open `scenes/1_Whitted/3_HeroScenes/Barbershop.cpp`
and change `rtPixelSamples(2048)` to `rtPixelSamples(512)`. The
reference of `BarbershopSunOnly` is rendered at 512 samples per pixel,
and your HW2 copy of the scene still asks for 2048.

After the build, expect three things:

- The C++ kit builds as unzipped, before you write anything.
- A command without `--integrator` now renders with `kajiya`. With
  `--integrator whitted` or `--integrator cook`, your renderer renders
  as it did.
- `gauchoRT --bsdftest` runs on the stub BSDF. The stub is black, so
  every row says FAIL except three rows whose right answer is 0. Section
  11 of the "BSDFs" handout says what each row expects.

## Your BSDF, and what is given

Write your BSDF in the kit's `src/bsdf.cpp` (Rust: `rust/src/bsdf.rs`).
Section 1 of the "BSDFs" handout shows the declarations of the given
`bsdf.h` and says what each one is for.

- **The public names in `bsdf.h` are fixed.** They are the `Bsdf` class
  with its constructor, `eval`, `pdf`, `sample`, `hasNonDelta`, and
  `deltaMasses`, the three GGX functions `ggxD`, `ggxLambda`, and
  `sampleGgxVisibleNormal`, and the constant `MIRROR_ALPHA`. Three given
  files call them, so keep them as they are.
- **The private part of `Bsdf` is yours.** It starts with the given
  conversions `toLocal()` and `toWorld()`. Add the members and the
  helper functions that your models need.
- **In Rust**, the same names are fixed in `bsdf.rs`, in snake_case, and
  the fields of the struct are yours.

Frosted glass is a transmissive material with roughness above 0. The kit
gives it complete, as the class `RoughDielectric` [Walter et al. 2007].
You do not write that class, but it uses three things of yours:

- The local frame, which is the three vectors `t`, `b`, and `n` that
  your constructor builds (Section 2 of the "BSDFs" handout).
- Your three GGX functions.
- Your `sampleCosineHemisphere` from HW3.

The rows of frosted glass in the BSDF test fail until all three are
right. The stub already holds the lines that hand a frosted material to
the class (Section 9 of the "BSDFs" handout). In the constructor, write
the code that builds `t` and `b` above those lines, and keep the lines.

## The tangents, given

A normal map tilts the shading normal about the two tangents of the
surface. A shader reads them as `dPdu` and `dPdv` in the shade context.
Until HW4 your renderer could leave them at zero. The hero scene
`JunkShop` needs them, because it has a normal map on almost every
surface. The kit gives the function that computes them for a triangle,
`triangleTangents()` in `src/tangents.h` (Rust: `triangle_tangents` in
`rust/src/tangents.rs`). You call it where you fill the shade context
at a triangle's hit, and you store its two vectors there. Step 13 of
the Suggested Workflow says how, and the test scene `NormalMap` checks
it in a few seconds. In Rust, add `mod tangents;` to `main.rs` in that
step and not before, so that the unused module raises no warning.

## What the kit adds

Do not change a file marked "given" or "new version". "What stays
frozen", below, gives the exceptions.

    src/bsdf.h                            yours        the header of your BSDF (public names fixed)
    src/bsdf.cpp                          yours        a stub, for the GGX functions and your BSDF
    rust/src/bsdf.rs                      yours        the Rust twin of both, a stub
    src/bsdftest.cpp                      given        --bsdftest, which your bsdfTest() calls
    rust/src/bsdftest.rs                  given        its Rust twin
    src/roughdielectric.h, .cpp           given        frosted glass
    src/dielectric_albedo.h               given        the grid of the table that frosted glass reads
    src/rough_dielectric_albedo_data.h    given        that table
    rust/src/roughdielectric.rs           given        the Rust twin of frosted glass
    rust/src/rough_dielectric_albedo.bin  given        its table, read when the crate is compiled
    src/tangents.h                        given        the tangents of a triangle, which your hit code calls
    rust/src/tangents.rs                  given        its Rust twin
    src/warptest.cpp                      new version  --warptest, with one more warp, ggx
    rust/src/warptest.rs                  new version  its Rust twin
    src/cmdline.cpp, .h                   new version  the code that reads the command line. It lists the
                                                       scenes that all renders under kajiya, and it takes
                                                       --integrator jensen
    rust/src/cmdline.rs                   new version  its Rust twin
    tools/compare.py                      new version  with the thresholds for HW4
    scenes/3_Kajiya/                      new          the HW4 scenes, in three folders (below)
    models/, textures/                    new          what the new scenes load
    reference_renders/3_Kajiya/           new          the kajiya references of every HW4 scene, and
                                                       of the five HW2 scenes that kajiya renders again
    README_HW4.md                         new          this file

`reference_renders/1_Whitted/` and `2_Cook/` stay where they are.

## The command line

The given `cmdline.cpp` parses the new options into `options`. Read them
from there in your renderer. Every option from HW2 and HW3 still works.

    --integrator whitted|cook|kajiya|jensen
                                which integrator renders. kajiya is the default once
                                RT_HOMEWORK is 4. jensen is the photon map (extra credit).
    --max-depth N               the longest path. It overrides the scene's rtMaxDepth().
    --ISstrategy mis|light|bsdf the importance-sampling strategy, which is how kajiya finds
                                the light of the lights. mis uses light samples and BSDF
                                samples, weighted by MIS (the default). light and bsdf each
                                use one of the two alone.
    --env-sampling importance|uniform
                                how kajiya samples an environment map as a light. importance
                                samples it by its brightness (the default). uniform samples
                                it uniformly over the sphere.
    --bsdftest [N]              run the BSDF test and exit, with N samples a row (default
                                400,000).
    --warptest ggx N ALPHA      print N samples of your sampleGgxVisibleNormal and exit. You
                                do not run it yourself, because tools/warptest.py runs it.

## The scenes

`scenes/3_Kajiya/` holds the new scenes, in three folders. Section 9 of
the handout describes them, the comment at the top of each scene file
says what the scene tests, and `reference_renders/3_Kajiya/` shows each
picture.

- `1_TestScenes/` holds `Furnace`, `VeachMIS`, `MaterialBalls`,
  `EnvLit`, `Caustics`, and `NormalMap`. Render `VeachMIS` plain, with `--ISstrategy
  light`, and with `--ISstrategy bsdf`. Render `EnvLit` plain and with
  `--env-sampling uniform`. The reference of `Caustics` shows how little
  of the light through the glass a path tracer finds, and finding the
  rest is extra credit (below). `NormalMap` is a flat wall whose shader
  tilts the normal into round studs, which tests the tangents.
- `2_CompareScenes/` holds `SponzaGI`. Compare it with your HW2 picture
  of `Sponza`.
- `3_HeroScenes/` holds `Classroom`, `Colonnato`, `HallBench`,
  `LoneMonk`, and `JunkShop`. Each can take hours on eight cores.

Of the earlier scenes, `kajiya` renders again only five, all from HW2.
They are the scenes in which a path tracer shows something that
`whitted` and `cook` cannot. Their references are in
`reference_renders/3_Kajiya/` with the references of the new scenes.

- Two are test scenes, `CornellBox` (color bleeding) and
  `CornellBoxSpheres` (the caustic under the glass ball).
- Three are hero scenes, `BarcelonaPavilionSunOnly`,
  `BarbershopSunOnly`, and `SanMiguel`. The first two are lit by the sun
  and the sky alone.

No other HW2 scene and no HW3 scene has a `kajiya` reference, so
`gauchoRT all` and the tiers leave them out under `kajiya`. Running
`gauchoRT` with no argument marks them "by name only under kajiya". A
name still renders any scene.

## The photon map (extra credit)

Section 12 of the handout says what the caustic photon map
[Jensen 1996] must do, and the last section of the Suggested Workflow
says where to start. Three things concern your files:

- The given command line accepts `--integrator jensen` and writes its
  images to `renders/4_Jensen/`. Add `jensen` to your integrator switch,
  where you added `kajiya`. It is your path tracer with the photon map.
- The kit has no scene and no reference image for the photon map. Write
  scenes of your own that show it working. Make the folder
  `scenes/3_Kajiya/4_ExtraCreditScenes/`, put each scene in a new file
  there, and add its two lines to `scenes/3_Kajiya/0_scene_table.cpp`
  (the declaration and the table entry, as `Caustics` has them). Render each scene by name under
  `kajiya` and under `jensen`, because `gauchoRT all` and the tiers
  render nothing under `jensen`. Hand in the scene files and both images
  of each scene, and say in your README what each pair shows.
- The command line has no option for the settings of a photon map, such
  as the number of photons. Keep them as constants, or add options of
  your own to `cmdline.cpp` and `options.h` (Rust: `cmdline.rs` and
  `options.rs`).

## Checking your work

    gauchoRT --bsdftest                           every row must say PASS, and the last
                                                  line must say "all pass"
    python tools/warptest.py ggx 0.25             the chi-square test of your
                                                  sampleGgxVisibleNormal
    python tools/compare.py renders/3_Kajiya reference_renders/3_Kajiya
                                                  your images against the references

Every image in `reference_renders/3_Kajiya/` is one that your renderer
must match to within floating-point error. This holds for every scene,
because every sampler gives the same numbers for the same pixel, sample,
and seed. The kit's `compare.py` prints PASS for a `kajiya` image when
its RMSE against the reference is at most these thresholds:

- 0.002 for a test scene or a compare scene. A correct renderer is
  usually at 0.0002 or below.
- 0.01 for a hero scene. The reference renderer instances the meshes
  that a scene draws many times, and a renderer that bakes them rounds
  some hits differently.

An image that is a correct picture with different noise fails these
thresholds by a wide margin, with an RMSE of 0.01 to 0.08. That failure
means that your renderer draws its random numbers in another order, or
builds another frame, than the two technical handouts specify.

`compare.py --noise` is for another question. It compares two noisy
renders of your own that should agree on average, such as the same scene
under `--ISstrategy light` and `--ISstrategy bsdf` (step 11 of the
Suggested Workflow).

The Python tools need Python 3 with NumPy, Matplotlib, and Pillow, as in
HW3:

    python -m pip install numpy matplotlib pillow

The reference kit for HW4 is a separate download, as before. Use it to
render a scene of your own, to compare `--trace X Y` logs, and to time
your renderer against the reference on your own machine.

## What to hand in

Section 15 of the handout lists what to hand in, and Section 16 gives
the AI policy. You hand in one image for every image in
`reference_renders/3_Kajiya/`, the hero scenes among them. Before you
submit, run the final check:

    python tools/compare.py --require-all renders/3_Kajiya reference_renders/3_Kajiya

It names every reference image that you have not rendered.

## What stays frozen

Do not change these files:

- `include/rt.h` and `include/shaders.h`.
- The scene files of HW2, HW3, and HW4. There is one exception. Add the
  two lines of a scene of your own to
  `scenes/3_Kajiya/0_scene_table.cpp`.
- The files that "What the kit adds" marks as given or as a new version,
  and the given files of HW2 and HW3, which `README_HW3.md` lists. There
  are three exceptions. Set `RT_HOMEWORK` in `options.h`. Add the `mod`
  lines to `main.rs`. Add the options of an extra credit of your own to
  `cmdline.*` and `options.h`.

Keep your `whitted` and `cook` integrators rendering the images of HW2
and HW3 as before.
