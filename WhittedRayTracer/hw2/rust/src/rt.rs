//! rt.rs: the ECE/CS 285 ray tracing interface (include/rt.h) in Rust, and
//! the renderer behind it: the graphics state, the rt* entry points and
//! the pixel loop.
//!
//! The scene files are C++ and compile against rt.h; this file declares the
//! same types with the same memory layout (#[repr(C)]) and exports the same
//! functions with C linkage (#[no_mangle] extern "C"), so the unchanged
//! scene files link against your renderer. Keep every signature as it is.
//! The transforms (rtTranslate and friends) are exported by transform.rs,
//! perlinNoise by noise.rs, both given complete.
//!
//! This is a stub. Every function the scenes call is here, the ones that
//! only store something already work, and rtWorldEnd writes a black
//! image of the right size and name and prints the TIME line, so the whole
//! kit builds and runs before you have written a line. Replace the stubs
//! with your ray tracer, following the Suggested Workflow: the camera and
//! the pixel loop first (Spheres --aov raydir is the first picture, then --aov normal), then
//! spheres and triangles, lights and shadows, the Whitted model with its
//! reflection and refraction, the shaders' binding, the BVH, threads. The
//! comment on each function says what the finished version must do and
//! where the handout describes it. Split the code into more modules as it
//! grows.
//!
//! What is given complete, and not here: the transforms (transform.rs, the
//! CTM behind rtTranslate and friends, which you read with ctm()),
//! write_image and read_image (image.rs), perlinNoise (noise.rs), the
//! command line and the scene tables (main.rs, cmdline.rs, options.rs: the
//! options land in OPTIONS, which you read at rtWorldEnd), the timer and
//! the time budget (timer.rs), the trace log and the AOV encoders
//! (debug.rs), the texture table and its lookup (texture.rs, behind
//! rtMakeTexture and rtTexture below) and the four shaders (shaders.rs; you
//! wrote their like in HW1).
//!
//! The calls marked [HW3] exist now so that no header changes later:
//! store what they say; nothing reads it until HW3.

#![allow(non_snake_case)] // the rt* names and the context's fields are the interface's
#![allow(dead_code)] // the parts of the state the stub does not read yet

use std::ffi::{c_char, c_void, CStr};
use std::sync::Mutex;

use crate::image::write_image;
use crate::options::{options, Options};
use crate::texture;
use crate::timer::{RenderBudget, Timer};
use crate::transform::{ctm, ctm_reset, Vec3};

/* types ------------------------------------------------------------------ */

pub type RtVoid = ();
pub type RtFloat = f32;
pub type RtInt = i32;
pub type RtColor = [RtFloat; 3];
pub type RtPoint = [RtFloat; 3];
pub type RtVector = [RtFloat; 3];
pub type RtNormal = [RtFloat; 3];
pub type RtMatrix = [[RtFloat; 4]; 4];

/// What a surface is made of, at one point (rt.h's RtMaterial, same
/// layout). Scenes set it with rtMaterial; a surface shader may rewrite any
/// field at the hit point. Each field says who reads it and its symbol in
/// the HW2 shading formula; ka, the ambient coefficient, is rtAmbient's,
/// for the whole scene.
#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct RtMaterial {
    pub albedo: RtColor,       /* kd: diffuse reflectance per channel, 0 to 1 */
    pub specular: RtColor,     /* ks: the reflectance head on: it weights the mirror ray and the highlight (HW2),
                                  the glossy lobe (HW3), and is Schlick's F0, metal or not (HW4) */
    pub roughness: RtFloat,    /* 0 = mirror, 1 = fully rough; the lobe's width is alpha = roughness^2 in every
                                  homework. HW2: the Phong exponent n = 2 / alpha^2 - 2 (alpha clamped to
                                  [0.05, 1]) and how much of the mirror ray survives; HW3: the glossy lobe's
                                  width; HW4: GGX's alpha */
    pub metallic: RtFloat,     /* 0 = dielectric, 1 = metal. HW4 only; HW2 and HW3 ignore it */
    pub transmission: RtFloat, /* kt: 0 = opaque, 1 = clear glass */
    pub ior: RtFloat,          /* eta: index of refraction of the inside (1.5 = glass) */
    pub emission: RtColor,     /* radiance emitted (seen directly, this bright). Nonzero makes the draw call a
                                  light: a point light at its center in HW2, its surface sampled in HW3, with
                                  MIS in HW4 */
}

impl RtMaterial {
    /// The default material: albedo 0.8, no specular, roughness 0.5, opaque, ior 1.5, no emission.
    pub const fn default_material() -> RtMaterial {
        RtMaterial {
            albedo: [0.8, 0.8, 0.8],
            specular: [0.0, 0.0, 0.0],
            roughness: 0.5,
            metallic: 0.0,
            transmission: 0.0,
            ior: 1.5,
            emission: [0.0, 0.0, 0.0],
        }
    }
}

/// What a shader sees at one hit point (rt.h's RtShadeContext, same
/// layout). The renderer fills the inputs; a surface shader edits m (and N,
/// for bump or normal mapping); a cutout shader only reads.
#[repr(C)]
pub struct RtShadeContext {
    /* inputs */
    pub P: RtPoint,          /* the hit point, world space */
    pub Po: RtPoint,         /* the same point in the primitive's object space (before the CTM), for solid
                                procedural patterns that stick to the object */
    pub Ng: RtNormal,        /* unit geometric normal, world space, on the vertex normals' side, turned to face the
                                incoming ray */
    pub N: RtNormal,         /* unit shading normal: the interpolated vertex normal, or Ng; on Ng's side */
    pub dPdu: RtVector,      /* world-space tangents dP/du and dP/dv, for normal mapping; a renderer may */
    pub dPdv: RtVector,      /*   leave them zero until it needs them */
    pub u: RtFloat,          /* texture coordinates: a mesh's st (t = 0 at the top row of a texture), a */
    pub v: RtFloat,          /*   sphere's longitude and latitude */
    pub I: RtVector,         /* unit direction of the ray that hit, toward P */
    pub time: RtFloat,       /* the ray's time in [0, 1): the pixel sample's (rtMotionBegin); 0 when nothing
                                moves, and always under whitted */
    pub depth: RtInt,        /* 0 for camera rays, +1 per bounce, -1 for a shadow ray */
    pub params: *const c_void, /* this shader's parameter block, or null */
    /* in and out */
    pub m: RtMaterial,       /* in: the material bound when the primitive was drawn; out: what the surface
                                shader leaves */
}

/// A surface shader edits c.m (and may replace c.N); None where a scene
/// passes NULL (the bound material, unchanged).
pub type RtShaderFn = Option<unsafe extern "C" fn(*mut RtShadeContext)>;

/// A cutout shader returns 0 where the surface does not exist: the ray goes
/// on as if nothing were there. Runs on every candidate hit of every ray,
/// shadow rays included (depth -1), with P, Po, Ng, N, u, v, time, depth and
/// params filled; both roots of a sphere are candidates, so a hole shows its
/// inside. None where a scene passes NULL: solid everywhere.
pub type RtCutoutFn = Option<unsafe extern "C" fn(*mut RtShadeContext) -> RtInt>;

/* the graphics state ----------------------------------------------------- */

/// A bound shader: the function and its own copy of the parameter block,
/// made at bind time (HW2 handout: shaders). The copy is shallow: what the
/// block points to stays the scene's. Stored 8-byte aligned, since the
/// shader casts it back to its struct.
pub struct Binding<F: Copy> {
    pub f: Option<F>,
    params: Vec<u64>,
}

impl<F: Copy> Binding<F> {
    pub const fn none() -> Binding<F> {
        Binding { f: None, params: Vec::new() }
    }

    /// # Safety
    /// `p` points at `nbytes` readable bytes, or is null.
    pub unsafe fn bind(&mut self, f: Option<F>, p: *const c_void, nbytes: usize) {
        self.f = f;
        self.params.clear();
        if f.is_some() && !p.is_null() && nbytes > 0 {
            self.params.resize(nbytes.div_ceil(8), 0);
            std::ptr::copy_nonoverlapping(p as *const u8, self.params.as_mut_ptr() as *mut u8, nbytes);
        }
    }

    /// The copied block, for the context's params (null when there is none).
    pub fn params(&self) -> *const c_void {
        if self.params.is_empty() { std::ptr::null() } else { self.params.as_ptr() as *const c_void }
    }
}

/// The graphics state. rtBegin resets it to the defaults listed at
/// rtBegin. Options and attributes persist across rtWorldBegin/End pairs;
/// the world's contents (lights, primitives) are cleared at rtWorldBegin, so a
/// scene that renders several images builds each world afresh. The
/// finished renderer keeps more here: the lights, the primitives, the
/// textures, the BVH.
pub struct State {
    pub xres: i32,
    pub yres: i32,
    pub spp: i32,
    pub max_depth: i32,
    pub ambient: [f32; 3],
    pub background: [f32; 3],
    pub environment: i32, /* rtEnvironment's map, a texture handle (-1: none, the background shows) */
    pub environment_scale: f32,
    pub environment_rotate_y: f32,
    pub eye: [f32; 3],
    pub lookat: [f32; 3],
    pub up: [f32; 3],
    pub fovy: f32,
    pub display: String, /* rtDisplay's name, without extension */
    pub tonemap: String, /* rtTonemap: the PNG's tone map and exposure (stops) */
    pub exposure: f32,

    /* The [HW3] calls, stored now and read by cook (HW3): whitted renders
     * through a pinhole, one ray per pixel center, with plain ambient. */
    pub lens_radius: f32,
    pub focus_distance: f32,
    pub sampler: String,
    pub ao_distance: f32,
    pub has_portal: bool, /* rtEnvironmentPortal's quad, world space; sampling the environment through it is
                             HW3 extra credit, and nothing reads it until then */
    pub portal: [[f32; 3]; 4],

    pub material: RtMaterial,
    pub surface: Binding<unsafe extern "C" fn(*mut RtShadeContext)>,
    pub cutout: Binding<unsafe extern "C" fn(*mut RtShadeContext) -> RtInt>,
    /* TODO: the scene data structure (Workflow, step 4): a list of draw
     * calls (what the primitives of one rtSphere or rtTriangleMesh call
     * share), a list of spheres, a list of meshes and one of their
     * triangles, a list of lights, and from step 14 the BVH. */
}

impl State {
    pub const fn new() -> State {
        State {
            xres: 640,
            yres: 480,
            spp: 1,
            max_depth: 8,
            ambient: [0.0; 3],
            background: [0.0; 3],
            environment: -1,
            environment_scale: 1.0,
            environment_rotate_y: 0.0,
            eye: [0.0, 0.0, 0.0],
            lookat: [0.0, 0.0, -1.0],
            up: [0.0, 1.0, 0.0],
            fovy: 45.0,
            display: String::new(),
            tonemap: String::new(),
            exposure: 0.0,
            lens_radius: 0.0,
            focus_distance: 1.0,
            sampler: String::new(),
            ao_distance: 0.0,
            has_portal: false,
            portal: [[0.0; 3]; 4],
            material: RtMaterial::default_material(),
            surface: Binding::none(),
            cutout: Binding::none(),
        }
    }
}

/// Only the main thread touches the state through the rt* calls; the pixel
/// loop's workers read what rtWorldEnd hands them.
pub static STATE: Mutex<State> = Mutex::new(State::new());

fn state() -> std::sync::MutexGuard<'static, State> {
    STATE.lock().expect("state")
}

/// The world's contents gone (rtWorldBegin, and rtWorldEnd when it is done).
fn clear_world() {
    /* TODO: empty the lists of draw calls, spheres, meshes, triangles and
     * lights (Workflow, step 4) */
}

/// A C string argument as a Rust String ("" for a null pointer).
/// # Safety
/// `s` is null or a NUL-terminated string.
unsafe fn text(s: *const c_char) -> String {
    if s.is_null() { String::new() } else { CStr::from_ptr(s).to_string_lossy().into_owned() }
}

/// Three floats at a C pointer (an RtColor, RtPoint or RtVector argument).
/// # Safety
/// `p` points at three floats.
unsafe fn three(p: *const f32) -> [f32; 3] {
    [*p, *p.add(1), *p.add(2)]
}

/// The worker count: --threads, or every core.
pub fn thread_count(o: &Options) -> i32 {
    if o.threads > 0 {
        return o.threads;
    }
    std::thread::available_parallelism().map(|n| n.get() as i32).unwrap_or(1)
}

/* session, options ------------------------------------------------------ */

/// Start a session and reset everything to these defaults:
/// 640 x 480, 1 spp, max depth 8, no ambient, a black background and no
/// environment, the camera at the origin looking down -z with +y up and
/// fovy 45, a pinhole, the independent sampler, no ambient occlusion, the
/// default material, no shaders, the CTM the identity with an empty stack.
/// rtEnd frees the textures and the geometry.
#[no_mangle]
pub extern "C" fn rtBegin() {
    let mut s = state();
    *s = State::new();
    s.display = "out".to_string();
    s.tonemap = "none".to_string();
    s.sampler = "independent".to_string();
    ctm_reset();
    texture::clear();
}

#[no_mangle]
pub extern "C" fn rtEnd() {
    *state() = State::new();
    texture::clear();
}

/// The image size in pixels (--width and --height override it).
#[no_mangle]
pub extern "C" fn rtFormat(xres: RtInt, yres: RtInt) {
    let mut s = state();
    s.xres = xres.max(1);
    s.yres = yres.max(1);
}

/// The output name without extension: rtWorldEnd writes
/// renders/<integrator>/<name>.png, plus the .pfm with --pfm (image_path in
/// cmdline.rs; --out replaces the whole path).
/// # Safety
/// `name` is null or a NUL-terminated string.
#[no_mangle]
pub unsafe extern "C" fn rtDisplay(name: *const c_char) {
    let n = text(name);
    state().display = if n.is_empty() { "out".to_string() } else { n };
}

/// The scene's own arguments: the numbers after its name on the command
/// line (cmdline.rs), or the default where none was given.
#[no_mangle]
pub extern "C" fn rtSceneArg(index: RtInt, default_value: RtFloat) -> RtFloat {
    let o = crate::options::OPTIONS.read().expect("options");
    if index >= 0 && (index as usize) < o.scene_args.len() { o.scene_args[index as usize] } else { default_value }
}

/// The PNG's look: stored here, applied by write_image (image.rs, given
/// complete); --tonemap and --exposure override it.
/// # Safety
/// `op` is null or a NUL-terminated string.
#[no_mangle]
pub unsafe extern "C" fn rtTonemap(op: *const c_char, exposure: RtFloat) {
    let mut s = state();
    let t = text(op);
    s.tonemap = if t.is_empty() { "none".to_string() } else { t };
    s.exposure = exposure;
}

/// Samples per pixel. whitted renders one whatever it says; cook and
/// kajiya (HW3, HW4) use it.
#[no_mangle]
pub extern "C" fn rtPixelSamples(spp: RtInt) {
    state().spp = spp.max(1);
}

/// The deepest secondary ray: a ray at depth d spawns children only if
/// d < max_depth (Whitted Illumination handout).
#[no_mangle]
pub extern "C" fn rtMaxDepth(depth: RtInt) {
    state().max_depth = depth.max(0);
}

/// The ambient term: ka * albedo at every lit point (Whitted Illumination
/// handout); cook scales it by occlusion, kajiya ignores it.
/// # Safety
/// `ka` points at three floats.
#[no_mangle]
pub unsafe extern "C" fn rtAmbient(ka: *const RtFloat) {
    state().ambient = three(ka);
}

/// The radiance a ray that leaves the scene returns, at any depth, when no
/// environment map is set.
/// # Safety
/// `radiance` points at three floats.
#[no_mangle]
pub unsafe extern "C" fn rtBackground(radiance: *const RtFloat) {
    state().background = three(radiance);
}

/// A latitude-longitude map in place of the background (Workflow, step 13):
/// loaded now as a texture (PFM or HDR, linear; an 8-bit image is decoded
/// from sRGB), looked up at render time by the direction turned by -rotateY
/// about +y (the map turns by +rotateY, as rtRotate turns a textured
/// sphere), at the sphere's u and v of that direction. NULL removes it; a
/// file that does not load leaves the background showing.
/// # Safety
/// `file` is null or a NUL-terminated string.
#[no_mangle]
pub unsafe extern "C" fn rtEnvironment(file: *const c_char, scale: RtFloat, rotate_y: RtFloat) {
    let _ = text(file);
    let mut s = state();
    s.environment = -1; /* TODO (step 13): if the file is given, rtMakeTexture(file, 1), else -1 */
    s.environment_scale = scale;
    s.environment_rotate_y = rotate_y;
}

/// The camera in the world: eye, look-at, up and the full vertical field of
/// view in degrees (Viewing Geometry handout); the CTM does not apply. Stored
/// here; rtWorldEnd builds the camera's frame from it (Workflow, step 5).
/// ([HW3, extra credit]: inside a motion block the call keys the camera at the block's
/// time, ctm_motion_time(); store that too. A renderer with motion blur
/// reads it; nothing does until then.)
/// # Safety
/// The three pointers point at three floats each.
#[no_mangle]
pub unsafe extern "C" fn rtCamera(eye: *const RtFloat, lookat: *const RtFloat, up: *const RtFloat, fovy: RtFloat) {
    let mut s = state();
    s.eye = three(eye);
    s.lookat = three(lookat);
    s.up = three(up);
    s.fovy = fovy;
}

/* lights ----------------------------------------------------------------- */

/// All three lights go through the CTM at the time of the call: the position
/// as a point, the direction as a vector (ctm().point and ctm().vector).
/// Intensities are radiometric (HW2 handout: the interface). Each adds a
/// light to the list in State (Workflow, step 9); the emissive draw calls
/// join them as stand-in point lights at rtWorldEnd (Whitted Illumination
/// handout).
/// # Safety
/// The pointers point at three floats each.
#[no_mangle]
pub unsafe extern "C" fn rtPointLight(position: *const RtFloat, intensity: *const RtFloat) {
    let (_p, _i) = (three(position), three(intensity));
    /* TODO: add the light to the list, its position through the CTM */
}

/// # Safety
/// The pointers point at three floats each.
#[no_mangle]
pub unsafe extern "C" fn rtDirectionalLight(direction: *const RtFloat, irradiance: *const RtFloat) {
    let (_d, _e) = (three(direction), three(irradiance));
    /* TODO: add the light to the list, its direction through the CTM, normalized */
}

/// The cone is stored as cosines; the penumbra is clamped into the cone.
/// # Safety
/// The pointers point at three floats each.
#[no_mangle]
pub unsafe extern "C" fn rtSpotLight(
    position: *const RtFloat,
    direction: *const RtFloat,
    intensity: *const RtFloat,
    _cone_angle: RtFloat,
    _penumbra_angle: RtFloat,
    _max_distance: RtFloat,
) {
    let (_p, _d, _i) = (three(position), three(direction), three(intensity));
    /* TODO: add the light to the list: position and direction through the
     * CTM, the cosines of the outer angle and of outer - penumbra, the reach */
}

/* attributes ------------------------------------------------------------- */

/// Attributes stay set until changed (rtTransformEnd does not restore them).
/// The material is copied now and becomes c.m at every hit on the
/// primitives drawn after it.
/// # Safety
/// `m` is null or points at an RtMaterial.
#[no_mangle]
pub unsafe extern "C" fn rtMaterial(m: *const RtMaterial) {
    if !m.is_null() {
        state().material = *m;
    }
}

/// Bind a shader to the primitives that follow, copying nbytes of params
/// now: the scene's struct may be gone by the time the shader runs. A
/// binding stays until the next call, so rtSurface(NULL, NULL, 0) unbinds
/// (None here): the bound material then reaches a hit unchanged;
/// rtCutout(NULL, NULL, 0) unbinds too, and the surface is solid
/// everywhere.
/// # Safety
/// `params` is null or points at `nbytes` bytes.
#[no_mangle]
pub unsafe extern "C" fn rtSurface(f: RtShaderFn, params: *const c_void, nbytes: usize) {
    state().surface.bind(f, params, nbytes);
}

/// # Safety
/// `params` is null or points at `nbytes` bytes.
#[no_mangle]
pub unsafe extern "C" fn rtCutout(f: RtCutoutFn, params: *const c_void, nbytes: usize) {
    state().cutout.bind(f, params, nbytes);
}

/* primitives ------------------------------------------------------------- */

/// The CTM is a value: ctm() is the transformation as it stands at this
/// call, and a primitive keeps its own copy, so later transform calls
/// change the CTM but not what was already drawn. A sphere keeps the copy
/// (object to world), its inverse (world to object) and its radius: a ray
/// is carried into the sphere's object space through the inverse, where
/// the sphere is the one of that radius at the origin (so an ellipsoid or
/// a sheared sphere works), and its normal comes back through the inverse
/// transpose. A mesh is baked instead: its vertices go through the CTM
/// now, its normals through the inverse transpose, and only the inverse is
/// kept, for Po (the hit point in object space, which solid shaders read)
/// and the tangents. What the primitives of one call share (the copy, its
/// inverse, the material and the shaders bound at the call) is one
/// draw-call record they name by index (Intersection handout).
///
/// A sphere of the given radius at the CTM's origin.
#[no_mangle]
pub extern "C" fn rtSphere(radius: RtFloat) {
    if !(radius > 0.0) {
        eprintln!("{}: rtSphere with radius {}, skipped", options().scene, radius);
        return;
    }
    let _m = ctm();
    /* TODO: add a draw call (a copy of ctm(), object to world; its inverse,
     * world to object; the material and the shaders bound now) and the
     * sphere, its radius and its draw call, to their lists (Workflow, step 6) */
}

/// ntris triangles over nverts vertices, counterclockwise from outside; N,
/// st and Cs may be null. Vertices go through the CTM, normals through its
/// inverse transpose; each triangle keeps its first vertex, two edges and
/// constant tangents (Intersection handout). A mirroring CTM (a negative
/// determinant, ctm().determinant3()) swaps the winding so the outside
/// stays outside; a bad index skips the triangle.
/// # Safety
/// The arrays are as long as nverts and 3 ntris say; N, st and Cs may be null.
#[no_mangle]
pub unsafe extern "C" fn rtTriangleMesh(
    nverts: RtInt,
    p: *const RtPoint,
    n: *const RtNormal,
    st: *const RtFloat,
    cs: *const RtColor,
    ntris: RtInt,
    indices: *const RtInt,
) {
    if nverts <= 0 || ntris <= 0 || p.is_null() || indices.is_null() {
        return;
    }
    let _ = (n, st, cs);
    /* TODO: add a draw call, then bake the mesh to world space and add it and
     * its triangles to their lists (Workflow, step 7) */
}

/* textures --------------------------------------------------------------- */

/// The texture table is given (texture.rs): texture::load reads the file
/// through read_image (image.rs: PNG, JPEG, BMP, TGA, HDR and PFM as linear
/// floats, the 8-bit color channels decoded from sRGB when asked), trying
/// the path as given and then under textures/, and returns a handle (-1
/// with a message when it cannot); handles live until rtEnd.
/// # Safety
/// `file` is null or a NUL-terminated string.
#[no_mangle]
pub unsafe extern "C" fn rtMakeTexture(file: *const c_char, srgb: RtInt) -> RtInt {
    texture::load(&text(file), srgb != 0)
}

/// Bilinear lookup with repeat wrap; s left to right, t top to bottom (t = 0
/// is the top row); alpha 1 when the image has none. Given (texture.rs).
/// # Safety
/// `rgba` points at four floats.
#[no_mangle]
pub unsafe extern "C" fn rtTexture(handle: RtInt, s: RtFloat, t: RtFloat, rgba: *mut RtFloat) {
    let v = texture::lookup(handle, s, t);
    std::ptr::copy_nonoverlapping(v.as_ptr(), rgba, 4);
}

/* [HW3] calls ------------------------------------------------------------- */

/// Stored in the graphics state; whitted ignores all four. cook (HW3)
/// reads the lens, the sampler and the AO distance; the portal is HW3 extra
/// credit, read by no integrator yet.
#[no_mangle]
pub extern "C" fn rtLens(radius: RtFloat, focus_distance: RtFloat) {
    let mut s = state();
    s.lens_radius = radius.max(0.0);
    s.focus_distance = if focus_distance > 0.0 { focus_distance } else { 1.0 };
}

/// # Safety
/// `name` is null or a NUL-terminated string.
#[no_mangle]
pub unsafe extern "C" fn rtSampler(name: *const c_char) {
    let n = text(name);
    let mut s = state();
    if ["independent", "stratified", "halton", "sobol"].contains(&n.as_str()) {
        s.sampler = n;
    } else {
        eprintln!("{}: rtSampler(\"{}\"): not a sampler, keeping {}", options().scene, n, s.sampler);
    }
}

#[no_mangle]
pub extern "C" fn rtAmbientOcclusion(max_distance: RtFloat) {
    state().ao_distance = max_distance.max(0.0);
}

/// # Safety
/// `corners` is null or points at four RtPoints.
#[no_mangle]
pub unsafe extern "C" fn rtEnvironmentPortal(corners: *const RtPoint) {
    let mut s = state();
    s.has_portal = !corners.is_null();
    if !corners.is_null() {
        let m = ctm();
        for i in 0..4 {
            s.portal[i] = m.point(Vec3::from_array(*corners.add(i))).to_array();
        }
    }
}

/// --warptest (HW3) and --bsdftest (HW4): cmdline.rs calls these instead of
/// rendering. Until those homeworks they only say so.
pub fn warp_test(o: &Options) -> i32 {
    eprintln!("--warptest {}: the warps are HW3's; this renderer has none yet", o.warp);
    1
}

pub fn bsdf_test(_o: &Options) -> i32 {
    eprintln!("--bsdftest: the BSDFs are HW4's; this renderer has none yet");
    1
}

/* world ------------------------------------------------------------------ */

/// rtWorldBegin starts a world: the lights and primitives drawn from here to
/// rtWorldEnd are what the image shows. The options and attributes set so
/// far carry over. Returns whether --frames wants this frame; a movie skips
/// the draw calls of one it does not (rt.h).
#[no_mangle]
pub extern "C" fn rtWorldBegin(frame: RtInt) -> RtInt {
    clear_world();
    let o = options();
    (o.frame_first < 0 || (frame >= o.frame_first && frame <= o.frame_last)) as RtInt
}

/// Build the acceleration structure, render, write the image and print the
/// TIME line (HW2 handout: timing):
///     TIME <scene> build=<s> render=<s> total=<s> threads=<n>
/// build is the light list and the BVH, render the pixel loop, total
/// includes writing the image; all three come from timer.rs's Timer. The
/// command line overrides the scene's format, samples and depth here
/// (options().width, .height, .spp, .max_depth, .ambient; .aov picks a
/// debug image, .lens the lens).
///
/// The finished pixel loop (Workflow, steps 5 and 15): 16 x 16 tiles handed out from an atomic
/// counter to a pool of workers (rayon or std::thread), each writing only
/// its tile, so the image is the same at any thread count; trace_begin(px,
/// py) before a pixel and trace_end() after it (debug.rs); the time budget
/// of timer.rs (RenderBudget: its tile_order, expired() before each tile
/// and row, done() per row, report() after the TIME line). Under whitted
/// one ray through each pixel's center, time 0.
/// The debug images this renderer writes (--aov; debug.rs has the
/// encoders). HW3 adds ao.
fn is_aov(name: &str) -> bool {
    ["raydir", "normal", "ng", "depth", "uv", "albedo", "cost", "shadow"].contains(&name)
}

#[no_mangle]
pub extern "C" fn rtWorldEnd() {
    let total = Timer::new();
    let o = options();

    let (w, h, display, tonemap, exposure) = {
        let s = state();
        (
            if o.width > 0 { o.width } else { s.xres },
            if o.height > 0 { o.height } else { s.yres },
            s.display.clone(),
            s.tonemap.clone(),
            s.exposure,
        )
    };
    let threads = thread_count(&o);
    if !o.aov.is_empty() && !is_aov(&o.aov) {
        eprintln!(
            "{}: --aov {}: not an image this renderer writes; it has raydir, normal, ng, depth, uv, albedo, cost and shadow",
            o.scene, o.aov
        );
        std::process::exit(1);
    }

    /* TODO: build (Workflow, steps 9 and 14): the light list (the delta
     * lights and the emissive draw calls' stand-ins) and the BVH over every
     * primitive. */
    let build = total.seconds();

    /* TODO: render (Workflow, step 5 on): the pixel loop, one ray through
     * each pixel's center, shaded by the Whitted model. For now, a black
     * image. */
    let render = Timer::new();
    let rgb = vec![0.0f32; (w * h * 3) as usize];
    let mut budget = RenderBudget::new(o.time_limit); /* --time-limit: the clock starts here (timer.rs) */
    if o.progress {
        budget.progress((w * h) as i64); /* --progress: a line every two seconds */
    }
    budget.done((w * h) as i64);
    budget.finish();
    let render_time = render.seconds();

    /* The scene's tone map and exposure, unless the command line says
     * otherwise; a debug image (--aov) is written as it is. */
    let aov = !o.aov.is_empty();
    let tonemap = if aov { "none".to_string() } else if o.tonemap.is_empty() { tonemap } else { o.tonemap.clone() };
    let exposure = if aov { 0.0 } else if o.exposure_set { o.exposure } else { exposure };
    write_image(&crate::cmdline::image_path(&display), w, h, &rgb, o.pfm, &tonemap, exposure, &o.format);

    let scene_name = if o.scene.is_empty() { &display } else { &o.scene }; /* the simple main sets none */
    println!("TIME {} build={:.3} render={:.3} total={:.3} threads={}", scene_name, build, render_time, total.seconds(), threads);
    budget.report(scene_name, render_time, (w * h) as i64, 1);

    clear_world();
}
