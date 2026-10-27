//! transform.rs: a 4 x 4 matrix, a small vector type, and the current
//! transformation (the CTM) with its stack and the rt* calls that edit it.
//! Complete in both kits; nothing to change; the twin of the C++ kit's
//! transform.h, transform.cpp and vec.h. RenderMan's and HW1's convention:
//! a point is a row vector, p' = p M, translation in the bottom row
//! (m[3][0..2]), "A then B" is A * B, and an RtMatrix copies straight in.
//! Normals go through the inverse transpose: n' = n (M^-1)^T. Each call
//! does CTM = M * CTM, so the call made last is applied to points first.
//! Angles are degrees. The motion blocks (rtMotionBegin/End) keep a pose
//! per time beside the CTM; a renderer with motion blur (HW3 extra credit)
//! interpolates between them.

#![allow(dead_code)] // given complete: your renderer uses the rest as it grows

#![allow(non_snake_case)] // the rt* names are the interface's

use std::ops::{Add, Div, Mul, Neg, Sub};
use std::sync::Mutex;

/* Vec3 ------------------------------------------------------------------ */

/// A 3-vector of f32, the minimum a ray tracer needs; use it or any vector
/// library you like (glam, nalgebra); the interface never requires it.
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct Vec3 {
    pub x: f32,
    pub y: f32,
    pub z: f32,
}

impl Vec3 {
    pub const fn new(x: f32, y: f32, z: f32) -> Vec3 {
        Vec3 { x, y, z }
    }
    pub fn from_array(a: [f32; 3]) -> Vec3 {
        Vec3::new(a[0], a[1], a[2])
    }
    pub fn to_array(self) -> [f32; 3] {
        [self.x, self.y, self.z]
    }
    pub fn dot(self, b: Vec3) -> f32 {
        self.x * b.x + self.y * b.y + self.z * b.z
    }
    pub fn cross(self, b: Vec3) -> Vec3 {
        Vec3::new(self.y * b.z - self.z * b.y, self.z * b.x - self.x * b.z, self.x * b.y - self.y * b.x)
    }
    pub fn length(self) -> f32 {
        self.dot(self).sqrt()
    }
    pub fn normalized(self) -> Vec3 {
        let l = self.length();
        if l > 0.0 { self / l } else { self }
    }
}

impl Add for Vec3 {
    type Output = Vec3;
    fn add(self, b: Vec3) -> Vec3 {
        Vec3::new(self.x + b.x, self.y + b.y, self.z + b.z)
    }
}
impl Sub for Vec3 {
    type Output = Vec3;
    fn sub(self, b: Vec3) -> Vec3 {
        Vec3::new(self.x - b.x, self.y - b.y, self.z - b.z)
    }
}
impl Mul<f32> for Vec3 {
    type Output = Vec3;
    fn mul(self, s: f32) -> Vec3 {
        Vec3::new(self.x * s, self.y * s, self.z * s)
    }
}
impl Mul for Vec3 {
    type Output = Vec3;
    fn mul(self, b: Vec3) -> Vec3 {
        Vec3::new(self.x * b.x, self.y * b.y, self.z * b.z)
    }
}
impl Div<f32> for Vec3 {
    type Output = Vec3;
    fn div(self, s: f32) -> Vec3 {
        Vec3::new(self.x / s, self.y / s, self.z / s)
    }
}
impl Neg for Vec3 {
    type Output = Vec3;
    fn neg(self) -> Vec3 {
        Vec3::new(-self.x, -self.y, -self.z)
    }
}

/* Mat4 ------------------------------------------------------------------ */

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Mat4 {
    pub m: [[f32; 4]; 4],
}

impl Mat4 {
    pub const fn identity() -> Mat4 {
        Mat4 { m: [[1.0, 0.0, 0.0, 0.0], [0.0, 1.0, 0.0, 0.0], [0.0, 0.0, 1.0, 0.0], [0.0, 0.0, 0.0, 1.0]] }
    }

    pub fn translate(dx: f32, dy: f32, dz: f32) -> Mat4 {
        let mut r = Mat4::identity();
        r.m[3][0] = dx;
        r.m[3][1] = dy;
        r.m[3][2] = dz;
        r
    }

    pub fn scale(sx: f32, sy: f32, sz: f32) -> Mat4 {
        let mut r = Mat4::identity();
        r.m[0][0] = sx;
        r.m[1][1] = sy;
        r.m[2][2] = sz;
        r
    }

    /// Rodrigues' rotation about the unit axis (x, y, z), transposed into
    /// row form; right-handed. A zero axis is left as the identity.
    pub fn rotate(angle_degrees: f32, x: f32, y: f32, z: f32) -> Mat4 {
        let mut r = Mat4::identity();
        let len = (x * x + y * y + z * z).sqrt();
        if len == 0.0 {
            return r;
        }
        let (x, y, z) = (x / len, y / len, z / len);
        let a = angle_degrees * std::f32::consts::PI / 180.0;
        let (c, s) = (a.cos(), a.sin());
        let t = 1.0 - c;
        r.m[0][0] = t * x * x + c;
        r.m[0][1] = t * x * y + s * z;
        r.m[0][2] = t * x * z - s * y;
        r.m[1][0] = t * x * y - s * z;
        r.m[1][1] = t * y * y + c;
        r.m[1][2] = t * y * z + s * x;
        r.m[2][0] = t * x * z + s * y;
        r.m[2][1] = t * y * z - s * x;
        r.m[2][2] = t * z * z + c;
        r
    }

    /// An RtMatrix, rows as they are.
    pub fn from_rows(rows: &[[f32; 4]; 4]) -> Mat4 {
        Mat4 { m: *rows }
    }

    /// p' = p M with w = 1: a position.
    pub fn point(&self, p: Vec3) -> Vec3 {
        let m = &self.m;
        let r = Vec3::new(
            p.x * m[0][0] + p.y * m[1][0] + p.z * m[2][0] + m[3][0],
            p.x * m[0][1] + p.y * m[1][1] + p.z * m[2][1] + m[3][1],
            p.x * m[0][2] + p.y * m[1][2] + p.z * m[2][2] + m[3][2],
        );
        let w = p.x * m[0][3] + p.y * m[1][3] + p.z * m[2][3] + m[3][3];
        if w != 0.0 && w != 1.0 { r / w } else { r } /* affine in practice; divide if not */
    }

    /// The linear part only (w = 0): a direction or a tangent.
    pub fn vector(&self, v: Vec3) -> Vec3 {
        let m = &self.m;
        Vec3::new(
            v.x * m[0][0] + v.y * m[1][0] + v.z * m[2][0],
            v.x * m[0][1] + v.y * m[1][1] + v.z * m[2][1],
            v.x * m[0][2] + v.y * m[1][2] + v.z * m[2][2],
        )
    }

    pub fn transposed(&self) -> Mat4 {
        let mut r = Mat4::identity();
        for i in 0..4 {
            for j in 0..4 {
                r.m[i][j] = self.m[j][i];
            }
        }
        r
    }

    /// Of the upper-left 3 x 3 (negative: a mirroring transform).
    pub fn determinant3(&self) -> f32 {
        let m = &self.m;
        m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1]) - m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0])
            + m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0])
    }

    /// The general 4 x 4 inverse by cofactors, in f64 so a chain of scenes'
    /// transforms (Sponza's 0.01 scale, a bunny scaled by 20) inverts
    /// cleanly. The identity, with a message on stderr, if singular.
    pub fn inverse(&self) -> Mat4 {
        let mut a = [0.0f64; 16];
        for i in 0..4 {
            for j in 0..4 {
                a[4 * i + j] = self.m[i][j] as f64;
            }
        }
        let mut inv = [0.0f64; 16];
        inv[0] = a[5] * a[10] * a[15] - a[5] * a[11] * a[14] - a[9] * a[6] * a[15] + a[9] * a[7] * a[14] + a[13] * a[6] * a[11] - a[13] * a[7] * a[10];
        inv[4] = -a[4] * a[10] * a[15] + a[4] * a[11] * a[14] + a[8] * a[6] * a[15] - a[8] * a[7] * a[14] - a[12] * a[6] * a[11] + a[12] * a[7] * a[10];
        inv[8] = a[4] * a[9] * a[15] - a[4] * a[11] * a[13] - a[8] * a[5] * a[15] + a[8] * a[7] * a[13] + a[12] * a[5] * a[11] - a[12] * a[7] * a[9];
        inv[12] = -a[4] * a[9] * a[14] + a[4] * a[10] * a[13] + a[8] * a[5] * a[14] - a[8] * a[6] * a[13] - a[12] * a[5] * a[10] + a[12] * a[6] * a[9];
        inv[1] = -a[1] * a[10] * a[15] + a[1] * a[11] * a[14] + a[9] * a[2] * a[15] - a[9] * a[3] * a[14] - a[13] * a[2] * a[11] + a[13] * a[3] * a[10];
        inv[5] = a[0] * a[10] * a[15] - a[0] * a[11] * a[14] - a[8] * a[2] * a[15] + a[8] * a[3] * a[14] + a[12] * a[2] * a[11] - a[12] * a[3] * a[10];
        inv[9] = -a[0] * a[9] * a[15] + a[0] * a[11] * a[13] + a[8] * a[1] * a[15] - a[8] * a[3] * a[13] - a[12] * a[1] * a[11] + a[12] * a[3] * a[9];
        inv[13] = a[0] * a[9] * a[14] - a[0] * a[10] * a[13] - a[8] * a[1] * a[14] + a[8] * a[2] * a[13] + a[12] * a[1] * a[10] - a[12] * a[2] * a[9];
        inv[2] = a[1] * a[6] * a[15] - a[1] * a[7] * a[14] - a[5] * a[2] * a[15] + a[5] * a[3] * a[14] + a[13] * a[2] * a[7] - a[13] * a[3] * a[6];
        inv[6] = -a[0] * a[6] * a[15] + a[0] * a[7] * a[14] + a[4] * a[2] * a[15] - a[4] * a[3] * a[14] - a[12] * a[2] * a[7] + a[12] * a[3] * a[6];
        inv[10] = a[0] * a[5] * a[15] - a[0] * a[7] * a[13] - a[4] * a[1] * a[15] + a[4] * a[3] * a[13] + a[12] * a[1] * a[7] - a[12] * a[3] * a[5];
        inv[14] = -a[0] * a[5] * a[14] + a[0] * a[6] * a[13] + a[4] * a[1] * a[14] - a[4] * a[2] * a[13] - a[12] * a[1] * a[6] + a[12] * a[2] * a[5];
        inv[3] = -a[1] * a[6] * a[11] + a[1] * a[7] * a[10] + a[5] * a[2] * a[11] - a[5] * a[3] * a[10] - a[9] * a[2] * a[7] + a[9] * a[3] * a[6];
        inv[7] = a[0] * a[6] * a[11] - a[0] * a[7] * a[10] - a[4] * a[2] * a[11] + a[4] * a[3] * a[10] + a[8] * a[2] * a[7] - a[8] * a[3] * a[6];
        inv[11] = -a[0] * a[5] * a[11] + a[0] * a[7] * a[9] + a[4] * a[1] * a[11] - a[4] * a[3] * a[9] - a[8] * a[1] * a[7] + a[8] * a[3] * a[5];
        inv[15] = a[0] * a[5] * a[10] - a[0] * a[6] * a[9] - a[4] * a[1] * a[10] + a[4] * a[2] * a[9] + a[8] * a[1] * a[6] - a[8] * a[2] * a[5];
        let det = a[0] * inv[0] + a[1] * inv[4] + a[2] * inv[8] + a[3] * inv[12];
        if det == 0.0 {
            eprintln!("transform: singular matrix (a zero scale?), using the identity");
            return Mat4::identity();
        }
        let mut r = Mat4::identity();
        for i in 0..4 {
            for j in 0..4 {
                r.m[i][j] = (inv[4 * i + j] / det) as f32;
            }
        }
        r
    }
}

impl Mul for Mat4 {
    type Output = Mat4;
    /// this, then b: p (this * b) = (p this) b.
    fn mul(self, b: Mat4) -> Mat4 {
        let mut r = Mat4::identity();
        for i in 0..4 {
            for j in 0..4 {
                r.m[i][j] = self.m[i][0] * b.m[0][j] + self.m[i][1] * b.m[1][j] + self.m[i][2] * b.m[2][j] + self.m[i][3] * b.m[3][j];
            }
        }
        r
    }
}

/* the CTM ------------------------------------------------------------- */

/// The pose of the CTM at the time of an rtMotionBegin block made at this
/// level (rt.h, rtMotionBegin). A transform call inside a block multiplies
/// that pose alone, outside one the CTM and every pose; rtTransformBegin/End
/// save and restore them with the CTM.
#[derive(Clone, Copy, Debug)]
pub struct MotionKey {
    pub time: f32,
    pub m: Mat4,
}

#[derive(Clone)]
struct Level {
    m: Mat4,
    keys: Vec<MotionKey>,
}

struct Ctm {
    current: Level,
    editing: i32, /* the key of the open block, or -1 */
    stack: Vec<Level>,
}

static CTM: Mutex<Ctm> = Mutex::new(Ctm { current: Level { m: Mat4::identity(), keys: Vec::new() }, editing: -1, stack: Vec::new() });

fn apply(ctm: &mut Ctm, m: Mat4) {
    if ctm.editing >= 0 {
        let k = &mut ctm.current.keys[ctm.editing as usize];
        k.m = m * k.m;
        return;
    }
    ctm.current.m = m * ctm.current.m;
    for k in ctm.current.keys.iter_mut() {
        k.m = m * k.m;
    }
}

/// The current transformation. rtBegin resets it to the identity with an
/// empty stack; rtTransformBegin/End push and pop; every other transform
/// call left-multiplies (CTM = M * CTM). The renderer reads it when a
/// primitive or a light is drawn: geometry is baked then. With motion poses
/// at this level (rtMotionBegin) it is the earliest pose, so a renderer
/// without motion blur draws everything at time 0, as rt.h says; one with
/// it reads ctm_keys().
pub fn ctm() -> Mat4 {
    let c = CTM.lock().expect("ctm");
    let mut first: Option<&MotionKey> = None;
    for k in &c.current.keys {
        if first.map_or(true, |f| k.time < f.time) {
            first = Some(k);
        }
    }
    first.map_or(c.current.m, |k| k.m)
}

/// The poses of the CTM at the times of the motion blocks at this level,
/// in the order opened.
pub fn ctm_keys() -> Vec<MotionKey> {
    CTM.lock().expect("ctm").current.keys.clone()
}

/// The time of the block that is open, or -1 outside any block (rtCamera
/// keys the camera by it).
pub fn ctm_motion_time() -> f32 {
    let c = CTM.lock().expect("ctm");
    if c.editing >= 0 { c.current.keys[c.editing as usize].time } else { -1.0 }
}

pub fn ctm_reset() {
    let mut c = CTM.lock().expect("ctm");
    c.current = Level { m: Mat4::identity(), keys: Vec::new() };
    c.editing = -1;
    c.stack.clear();
}

#[no_mangle]
pub extern "C" fn rtTransformBegin() {
    let mut c = CTM.lock().expect("ctm");
    if c.editing >= 0 {
        eprintln!("rtTransformBegin inside a motion block");
    }
    let level = c.current.clone();
    c.stack.push(level);
}

#[no_mangle]
pub extern "C" fn rtTransformEnd() {
    let mut c = CTM.lock().expect("ctm");
    match c.stack.pop() {
        Some(level) => c.current = level,
        None => eprintln!("rtTransformEnd without rtTransformBegin"),
    }
    c.editing = -1;
}

/// A block at a time that already has a pose at this level edits that
/// pose further; a new time starts from the CTM as it is now.
#[no_mangle]
pub extern "C" fn rtMotionBegin(time: f32) {
    let mut c = CTM.lock().expect("ctm");
    if c.editing >= 0 {
        eprintln!("rtMotionBegin inside a motion block, ignored");
        return;
    }
    if !(0.0..=1.0).contains(&time) {
        eprintln!("rtMotionBegin({}): the shutter is open from 0 to 1", time);
    }
    if let Some(i) = c.current.keys.iter().position(|k| k.time == time) {
        c.editing = i as i32;
        return;
    }
    let m = c.current.m;
    c.current.keys.push(MotionKey { time, m });
    c.editing = c.current.keys.len() as i32 - 1;
}

#[no_mangle]
pub extern "C" fn rtMotionEnd() {
    let mut c = CTM.lock().expect("ctm");
    if c.editing < 0 {
        eprintln!("rtMotionEnd without rtMotionBegin");
    }
    c.editing = -1;
}

#[no_mangle]
pub extern "C" fn rtTranslate(dx: f32, dy: f32, dz: f32) {
    apply(&mut CTM.lock().expect("ctm"), Mat4::translate(dx, dy, dz));
}

#[no_mangle]
pub extern "C" fn rtRotate(angle: f32, x: f32, y: f32, z: f32) {
    apply(&mut CTM.lock().expect("ctm"), Mat4::rotate(angle, x, y, z));
}

#[no_mangle]
pub extern "C" fn rtScale(sx: f32, sy: f32, sz: f32) {
    apply(&mut CTM.lock().expect("ctm"), Mat4::scale(sx, sy, sz));
}

/// RtMatrix m: C passes the array as a pointer to its first row.
/// # Safety
/// `m` points at 16 floats, as every scene's RtMatrix does.
#[no_mangle]
pub unsafe extern "C" fn rtConcatTransform(m: *const [f32; 4]) {
    if m.is_null() {
        return;
    }
    let rows: &[[f32; 4]; 4] = &*(m as *const [[f32; 4]; 4]);
    apply(&mut CTM.lock().expect("ctm"), Mat4::from_rows(rows));
}
