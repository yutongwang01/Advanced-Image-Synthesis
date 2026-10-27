//! main.rs: render one scene by name (gauchoRT Spheres; gauchoRT alone lists
//! them) or a set of them (gauchoRT all; gauchoRT stills, without the
//! movies; gauchoRT test | benchmark | study | compare | hero | extra, one
//! tier). The command line is read in cmdline.rs (usage() there lists every
//! option) into OPTIONS (options.rs), which the renderer reads at
//! rtWorldEnd. Complete in both kits; the one edit is the SIMPLE_MAIN switch
//! below; the twin of the C++ kit's main.cpp. The executable changes to the
//! kit root by itself (textures/, models/, renders/).

mod cmdline;
mod debug;
mod image;
mod noise;
mod options;
mod rt;
mod shaders;
mod texture;
mod timer;
mod transform;

use std::path::Path;

use options::OPTIONS;

/// To make things easier, you have two main() functions. SIMPLE_MAIN true
/// is the one to start with: a scene is a function (one per scene under
/// scenes/) that draws through your rt* calls and writes its image, and
/// simple_main calls one. SIMPLE_MAIN false is the real one: the scene
/// named on the command line, found in the scene tables, with every option.
/// Switch once your first scene renders.
const SIMPLE_MAIN: bool = false;

fn simple_main(tables: &[cmdline::SceneTable]) {
    // The options are a struct (options.rs) the renderer reads at rtWorldEnd;
    // the real main below fills it from the command line. Here they are set
    // by hand: uncomment what a step of the Suggested Workflow asks for.
    {
        #[allow(unused_mut, unused_variables)]
        let mut o = OPTIONS.write().expect("options");
        // o.out = "renders/hello".to_string(); /* the image goes to renders/hello.png; rename it to keep the last one */
        // o.aov = "raydir".to_string();        /* output the ray direction for debugging eye ray calculation */
        // o.aov = "normal".to_string();        /* output the normals for debugging (or "depth", "uv", "albedo", "shadow") */
        // o.pfm = true;                        /* also write the exact floats, renders/hello.pfm (tools/compare.py reads both) */
        // o.width = 240; o.height = 135;       /* a quarter-size image while a scene is slow */
        // o.threads = 1;                       /* one worker, to debug the pixel loop */
        // o.trace_x = 480; o.trace_y = 270;    /* --trace: log what the renderer does for one pixel */
        // o.no_accel = true;                   /* brute force, without the BVH: the check of the BVH */
    }

    // Render the scene. The scene functions are C++ (scenes/), so Rust calls
    // one through the scene table, by its name: change "Spheres" to render
    // another.
    let name = "Spheres";
    match tables.iter().flat_map(|t| &t.scenes).find(|s| s.name == name) {
        // SAFETY: the C++ scene functions take no arguments and return nothing.
        Some(s) => unsafe { (s.function)() },
        None => eprintln!("no scene named {} in the scene tables", name),
    }
}

fn main() {
    // The scenes look for textures/ and models/ and write into renders/
    // relative to the kit root, the folder above this Cargo project. Go
    // there, so that `cargo run` works from rust/ or from the kit root.
    if let Some(kit) = Path::new(env!("CARGO_MANIFEST_DIR")).parent() {
        if kit.join("scenes").is_dir() {
            let _ = std::env::set_current_dir(kit);
        }
    }
    if !Path::new("scenes").is_dir() {
        eprintln!("run gauchoRT from the kit root, the folder that contains scenes/");
    }

    let tables = cmdline::tables();
    if SIMPLE_MAIN {
        simple_main(&tables);
        return;
    }

    // The options go into OPTIONS; what comes back is the scene argument,
    // one scene's name or a set of them (all, stills, or a tier). --help, a
    // missing scene and the tests --warptest and --bsdftest exit in there.
    let args: Vec<String> = std::env::args().collect();
    let scene = cmdline::parse_command_line(&args, &tables);

    // Run every scene the argument selects: one table per folder of scenes/
    // the build found, each an array of {name, function, tier} entries.
    let mut ran = 0;
    for table in &tables {
        for s in &table.scenes {
            if cmdline::selected(&scene, s, table.hw) {
                println!("== {}", s.name);
                OPTIONS.write().expect("options").scene = s.name.to_string(); /* for the TIME line */
                // Render the scene
                // SAFETY: the C++ scene functions take no arguments and return nothing.
                unsafe { (s.function)() };
                ran += 1;
            }
        }
    }

    if ran == 0 {
        eprintln!("unknown scene '{}'", scene);
        cmdline::usage(&tables);
        std::process::exit(1);
    }
}
