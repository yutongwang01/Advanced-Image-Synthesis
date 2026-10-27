//! shaders.rs: the four required shaders (HW2 handout: shaders) and their
//! parameter structs, the twins of shaders.h; perlinNoise is given in
//! noise.rs. Given complete in both kits: HW1 had you write their like, and
//! here the renderer's side, running them at a hit, is yours (Workflow, step
//! 12). Scenes bind the shaders by name (rtSurface(CHECKER, &cp, sizeof
//! cp)), so the names, the signatures and the structs' layout stay as they
//! are (#[repr(C)], the same field order as shaders.h). A shader is an
//! ordinary function: the renderer fills the inputs of the RtShadeContext
//! (rt.rs) at a hit and calls it; a surface shader edits c.m, a cutout
//! shader returns 0 where the surface does not exist. A shader knows
//! nothing of lights and keeps no state (threads).

#![allow(non_snake_case)] // the shaders' names are the interface's

use crate::rt::{RtFloat, RtInt, RtMaterial, RtShadeContext};
use crate::texture;

/// TEXTURED: albedo from an image. albedo_tex is an rtMakeTexture handle
/// (-1: leave the albedo alone). alpha_tex is a second handle whose red
/// channel is the coverage mask, or -1 for the albedo image's own alpha;
/// only ALPHA_CUTOUT reads it. ObjDraw fills this from map_Kd and map_d.
#[repr(C)]
pub struct TexturedParams {
    pub albedo_tex: RtInt,
    pub alpha_tex: RtInt,
}

/// CHECKER: sizex checks across u and sizey across v, alternating two
/// whole materials, a (the check at u = v = 0) and b.
#[repr(C)]
pub struct CheckerParams {
    pub sizex: RtInt,
    pub sizey: RtInt,
    pub a: RtMaterial,
    pub b: RtMaterial,
}

/// ALPHA_CUTOUT: solid where the mask is >= threshold. Reads
/// TexturedParams: alpha_tex's red channel if alpha_tex >= 0, else
/// albedo_tex's alpha.
#[repr(C)]
pub struct CutoutParams {
    pub tex: TexturedParams,
    pub threshold: RtFloat, /* 0.5 is the usual value */
}

/* TEXTURED -------------------------------------------------------------- */

/// The albedo from an image: c.m.albedo = rtTexture(albedo_tex, u, v).rgb,
/// or left alone when albedo_tex is -1. Reads TexturedParams; ignores
/// alpha_tex (that is ALPHA_CUTOUT's).
/// # Safety
/// `c` is the context the renderer filled, its params a TexturedParams.
#[no_mangle]
pub unsafe extern "C" fn TEXTURED(c: *mut RtShadeContext) {
    let c = &mut *c;
    let p = &*(c.params as *const TexturedParams);
    if p.albedo_tex < 0 {
        return;
    }
    let rgba = texture::lookup(p.albedo_tex, c.u, c.v); /* what rtTexture returns */
    c.m.albedo = [rgba[0], rgba[1], rgba[2]];
}

/* CHECKER --------------------------------------------------------------- */

/* The check (u, v) falls in is (floor(u sizex), floor(v sizey)), with u = 1
 * and v = 1 counted in the last check. Checks where i + j is even get
 * material a, the others material b, whole (albedo, specular, roughness,
 * transmission, everything). Reads CheckerParams. */

/// Which of `size` checks x falls in; x = 1 counts in the last one, and
/// coordinates outside [0, 1] keep tiling.
fn check_index(x: f32, size: i32) -> i32 {
    let i = (x * size as f32).floor() as i32;
    if i == size && x <= 1.0 { size - 1 } else { i }
}

/// True on the a checks (i + j even), false on the b checks.
fn on_check_a(c: &RtShadeContext, p: &CheckerParams) -> bool {
    ((check_index(c.u, p.sizex) + check_index(c.v, p.sizey)) & 1) == 0
}

/// # Safety
/// `c` is the context the renderer filled, its params a CheckerParams.
#[no_mangle]
pub unsafe extern "C" fn CHECKER(c: *mut RtShadeContext) {
    let c = &mut *c;
    let p = &*(c.params as *const CheckerParams);
    c.m = if on_check_a(c, p) { p.a } else { p.b };
}

/* ALPHA_CUTOUT ---------------------------------------------------------- */

/// Solid (1) where the mask is >= threshold, a hole (0) elsewhere. The
/// mask is alpha_tex's red channel when alpha_tex >= 0, else albedo_tex's
/// alpha channel. Reads CutoutParams. Runs on every candidate hit of every
/// ray, shadow rays included, so the holes show in the shadows too.
/// # Safety
/// `c` is the context the renderer filled, its params a CutoutParams.
#[no_mangle]
pub unsafe extern "C" fn ALPHA_CUTOUT(c: *mut RtShadeContext) -> RtInt {
    let c = &*c;
    let p = &*(c.params as *const CutoutParams);
    let mask = if p.tex.alpha_tex >= 0 {
        texture::lookup(p.tex.alpha_tex, c.u, c.v)[0]
    } else if p.tex.albedo_tex >= 0 {
        texture::lookup(p.tex.albedo_tex, c.u, c.v)[3]
    } else {
        return 1; /* no mask at all: solid */
    };
    (mask >= p.threshold) as RtInt
}

/* CHECKER_CUTOUT -------------------------------------------------------- */

/// The same checks as CHECKER: solid on the a checks, a hole on the b
/// checks. Reads CheckerParams (the two materials are ignored).
/// # Safety
/// `c` is the context the renderer filled, its params a CheckerParams.
#[no_mangle]
pub unsafe extern "C" fn CHECKER_CUTOUT(c: *mut RtShadeContext) -> RtInt {
    let c = &*c;
    let p = &*(c.params as *const CheckerParams);
    on_check_a(c, p) as RtInt
}
