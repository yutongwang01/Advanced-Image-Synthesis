/*
 * transform.h: a 4 x 4 matrix and the current transformation (the CTM),
 * kept in transform.cpp and edited by the rt* transform calls. Complete in
 * both kits; nothing to change. RenderMan's and HW1's convention: a point
 * is a row vector, p' = p M, translation in the bottom row (M[3][0..2]),
 * "A then B" is A * B, and an RtMatrix copies straight in. Normals go
 * through the inverse transpose: n' = n (M^-1)^T.
 */
#ifndef TRANSFORM_H
#define TRANSFORM_H

#include "vec.h"

#include <vector>

struct Mat4 {
    float m[4][4];

    static Mat4 identity();
    static Mat4 translate(float dx, float dy, float dz);
    static Mat4 rotate(float angleDegrees, float x, float y, float z); /* right-handed, about the axis (x, y, z) */
    static Mat4 scale(float sx, float sy, float sz);
    static Mat4 fromRows(const float rows[4][4]); /* an RtMatrix */

    /* this, then b: p (this * b) = (p this) b. */
    Mat4 operator*(const Mat4 &b) const;

    Vec3 point(const Vec3 &p) const;  /* p' = p M with w = 1: a position */
    Vec3 vector(const Vec3 &v) const; /* the linear part only (w = 0): a direction or a tangent */
    Mat4 transposed() const;
    Mat4 inverse() const; /* the identity, with a message on stderr, if singular */
    float determinant3() const; /* of the upper-left 3 x 3 (negative: a mirroring transform) */
};

/* The current transformation. rtBegin resets it to the identity with an
 * empty stack; rtTransformBegin/End push and pop; every other transform
 * call left-multiplies (CTM = M * CTM). The renderer reads it when a
 * primitive or a light is drawn: geometry is baked then. With motion poses
 * at this level (rtMotionBegin) it is the earliest pose, so a renderer
 * without motion blur draws everything at time 0, as rt.h says; one with
 * it reads ctmKeys(). */
const Mat4 &ctm();
void ctmReset();

/* The poses of the CTM at the times of the rtMotionBegin blocks made at
 * this level, in the order opened (rt.h, rtMotionBegin). A transform call
 * inside a block multiplies that pose alone, outside one the CTM and every
 * pose; rtTransformBegin/End save and restore them with the CTM. */
struct MotionKey {
    float time;
    Mat4 m;
};
const std::vector<MotionKey> &ctmKeys();

/* The time of the block that is open, or -1 outside any block (rtCamera
 * keys the camera by it). */
float ctmMotionTime();

#endif /* TRANSFORM_H */
