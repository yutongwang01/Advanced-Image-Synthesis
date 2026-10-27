/*
 * transform.cpp: the current transformation (the CTM), its stack, and the
 * rt* calls that edit it (rt.h). Complete in both
 * kits; nothing to change. Row vectors, as in HW1: p' = p M, translation
 * in the bottom row, and each call does CTM = M * CTM, so the call made
 * last is applied to points first. Angles are degrees. The motion blocks
 * (rtMotionBegin/End) keep a pose per time beside the CTM; a renderer
 * with motion blur (HW3 extra credit) interpolates between them.
 */
#include "transform.h"

#include "rt.h"

#include <cmath>
#include <cstdio>
#include <vector>

/* Mat4 ----------------------------------------------------------------- */

Mat4 Mat4::identity()
{
    Mat4 r;
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++)
            r.m[i][j] = i == j ? 1.0f : 0.0f;
    return r;
}

Mat4 Mat4::translate(float dx, float dy, float dz)
{
    Mat4 r = identity();
    r.m[3][0] = dx;
    r.m[3][1] = dy;
    r.m[3][2] = dz;
    return r;
}

Mat4 Mat4::scale(float sx, float sy, float sz)
{
    Mat4 r = identity();
    r.m[0][0] = sx;
    r.m[1][1] = sy;
    r.m[2][2] = sz;
    return r;
}

/* Rodrigues' rotation about the unit axis (x, y, z), transposed into row
 * form. A zero axis is left as the identity. */
Mat4 Mat4::rotate(float angleDegrees, float x, float y, float z)
{
    Mat4 r = identity();
    float len = std::sqrt(x * x + y * y + z * z);
    if (len == 0)
        return r;
    x /= len;
    y /= len;
    z /= len;
    const float a = angleDegrees * 3.14159265358979323846f / 180.0f;
    const float c = std::cos(a), s = std::sin(a), t = 1 - c;
    r.m[0][0] = t * x * x + c;
    r.m[0][1] = t * x * y + s * z;
    r.m[0][2] = t * x * z - s * y;
    r.m[1][0] = t * x * y - s * z;
    r.m[1][1] = t * y * y + c;
    r.m[1][2] = t * y * z + s * x;
    r.m[2][0] = t * x * z + s * y;
    r.m[2][1] = t * y * z - s * x;
    r.m[2][2] = t * z * z + c;
    return r;
}

Mat4 Mat4::fromRows(const float rows[4][4])
{
    Mat4 r;
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++)
            r.m[i][j] = rows[i][j];
    return r;
}

Mat4 Mat4::operator*(const Mat4 &b) const
{
    Mat4 r;
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++)
            r.m[i][j] = m[i][0] * b.m[0][j] + m[i][1] * b.m[1][j] + m[i][2] * b.m[2][j] + m[i][3] * b.m[3][j];
    return r;
}

Vec3 Mat4::point(const Vec3 &p) const
{
    Vec3 r(p.x * m[0][0] + p.y * m[1][0] + p.z * m[2][0] + m[3][0],
           p.x * m[0][1] + p.y * m[1][1] + p.z * m[2][1] + m[3][1],
           p.x * m[0][2] + p.y * m[1][2] + p.z * m[2][2] + m[3][2]);
    float w = p.x * m[0][3] + p.y * m[1][3] + p.z * m[2][3] + m[3][3];
    return w != 0 && w != 1 ? r / w : r; /* affine in practice; divide if not */
}

Vec3 Mat4::vector(const Vec3 &v) const
{
    return Vec3(v.x * m[0][0] + v.y * m[1][0] + v.z * m[2][0], v.x * m[0][1] + v.y * m[1][1] + v.z * m[2][1],
                v.x * m[0][2] + v.y * m[1][2] + v.z * m[2][2]);
}

Mat4 Mat4::transposed() const
{
    Mat4 r;
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++)
            r.m[i][j] = m[j][i];
    return r;
}

float Mat4::determinant3() const
{
    return m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1]) - m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0]) +
           m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
}

/* The general 4 x 4 inverse by cofactors, in double so a chain of scenes'
 * transforms (Sponza's 0.01 scale, a bunny scaled by 20) inverts cleanly. */
Mat4 Mat4::inverse() const
{
    double a[16];
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++)
            a[4 * i + j] = m[i][j];

    double inv[16];
    inv[0] = a[5] * a[10] * a[15] - a[5] * a[11] * a[14] - a[9] * a[6] * a[15] + a[9] * a[7] * a[14] +
             a[13] * a[6] * a[11] - a[13] * a[7] * a[10];
    inv[4] = -a[4] * a[10] * a[15] + a[4] * a[11] * a[14] + a[8] * a[6] * a[15] - a[8] * a[7] * a[14] -
             a[12] * a[6] * a[11] + a[12] * a[7] * a[10];
    inv[8] = a[4] * a[9] * a[15] - a[4] * a[11] * a[13] - a[8] * a[5] * a[15] + a[8] * a[7] * a[13] +
             a[12] * a[5] * a[11] - a[12] * a[7] * a[9];
    inv[12] = -a[4] * a[9] * a[14] + a[4] * a[10] * a[13] + a[8] * a[5] * a[14] - a[8] * a[6] * a[13] -
              a[12] * a[5] * a[10] + a[12] * a[6] * a[9];
    inv[1] = -a[1] * a[10] * a[15] + a[1] * a[11] * a[14] + a[9] * a[2] * a[15] - a[9] * a[3] * a[14] -
             a[13] * a[2] * a[11] + a[13] * a[3] * a[10];
    inv[5] = a[0] * a[10] * a[15] - a[0] * a[11] * a[14] - a[8] * a[2] * a[15] + a[8] * a[3] * a[14] +
             a[12] * a[2] * a[11] - a[12] * a[3] * a[10];
    inv[9] = -a[0] * a[9] * a[15] + a[0] * a[11] * a[13] + a[8] * a[1] * a[15] - a[8] * a[3] * a[13] -
             a[12] * a[1] * a[11] + a[12] * a[3] * a[9];
    inv[13] = a[0] * a[9] * a[14] - a[0] * a[10] * a[13] - a[8] * a[1] * a[14] + a[8] * a[2] * a[13] +
              a[12] * a[1] * a[10] - a[12] * a[2] * a[9];
    inv[2] = a[1] * a[6] * a[15] - a[1] * a[7] * a[14] - a[5] * a[2] * a[15] + a[5] * a[3] * a[14] +
             a[13] * a[2] * a[7] - a[13] * a[3] * a[6];
    inv[6] = -a[0] * a[6] * a[15] + a[0] * a[7] * a[14] + a[4] * a[2] * a[15] - a[4] * a[3] * a[14] -
             a[12] * a[2] * a[7] + a[12] * a[3] * a[6];
    inv[10] = a[0] * a[5] * a[15] - a[0] * a[7] * a[13] - a[4] * a[1] * a[15] + a[4] * a[3] * a[13] +
              a[12] * a[1] * a[7] - a[12] * a[3] * a[5];
    inv[14] = -a[0] * a[5] * a[14] + a[0] * a[6] * a[13] + a[4] * a[1] * a[14] - a[4] * a[2] * a[13] -
              a[12] * a[1] * a[6] + a[12] * a[2] * a[5];
    inv[3] = -a[1] * a[6] * a[11] + a[1] * a[7] * a[10] + a[5] * a[2] * a[11] - a[5] * a[3] * a[10] -
             a[9] * a[2] * a[7] + a[9] * a[3] * a[6];
    inv[7] = a[0] * a[6] * a[11] - a[0] * a[7] * a[10] - a[4] * a[2] * a[11] + a[4] * a[3] * a[10] +
             a[8] * a[2] * a[7] - a[8] * a[3] * a[6];
    inv[11] = -a[0] * a[5] * a[11] + a[0] * a[7] * a[9] + a[4] * a[1] * a[11] - a[4] * a[3] * a[9] -
              a[8] * a[1] * a[7] + a[8] * a[3] * a[5];
    inv[15] = a[0] * a[5] * a[10] - a[0] * a[6] * a[9] - a[4] * a[1] * a[10] + a[4] * a[2] * a[9] +
              a[8] * a[1] * a[6] - a[8] * a[2] * a[5];

    double det = a[0] * inv[0] + a[1] * inv[4] + a[2] * inv[8] + a[3] * inv[12];
    if (det == 0) {
        std::fprintf(stderr, "transform: singular matrix (a zero scale?), using the identity\n");
        return identity();
    }
    Mat4 r;
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++)
            r.m[i][j] = (float)(inv[4 * i + j] / det);
    return r;
}

/* the CTM ------------------------------------------------------------- */

/* The CTM is one matrix plus, for motion blur, a pose per key time (rt.h,
 * rtMotionBegin): a transform call inside a block multiplies that pose
 * alone, outside a block the CTM and every pose. rtTransformBegin/End
 * save and restore the matrix and the poses together. */
namespace {

struct Level {
    Mat4 m;
    std::vector<MotionKey> keys;
};

Level current = {Mat4::identity(), {}};
int editing = -1; /* the key of the open block, or -1 */
std::vector<Level> stack;

void apply(const Mat4 &M)
{
    if (editing >= 0) {
        current.keys[(size_t)editing].m = M * current.keys[(size_t)editing].m;
        return;
    }
    current.m = M * current.m;
    for (MotionKey &k : current.keys)
        k.m = M * k.m;
}

} // namespace

/* The matrix, or with motion poses the earliest of them (rt.h: a renderer
 * without motion blur draws everything at time 0). */
const Mat4 &ctm()
{
    if (current.keys.empty())
        return current.m;
    const MotionKey *first = &current.keys[0];
    for (const MotionKey &k : current.keys)
        if (k.time < first->time)
            first = &k;
    return first->m;
}

const std::vector<MotionKey> &ctmKeys() { return current.keys; }

float ctmMotionTime() { return editing >= 0 ? current.keys[(size_t)editing].time : -1.0f; }

void ctmReset()
{
    current = {Mat4::identity(), {}};
    editing = -1;
    stack.clear();
}

RtVoid rtTransformBegin(void)
{
    if (editing >= 0)
        std::fprintf(stderr, "rtTransformBegin inside a motion block\n");
    stack.push_back(current);
}

RtVoid rtTransformEnd(void)
{
    if (stack.empty()) {
        std::fprintf(stderr, "rtTransformEnd without rtTransformBegin\n");
        return;
    }
    current = stack.back();
    stack.pop_back();
    editing = -1;
}

/* A block at a time that already has a pose at this level edits that
 * pose further; a new time starts from the CTM as it is now. */
RtVoid rtMotionBegin(RtFloat time)
{
    if (editing >= 0) {
        std::fprintf(stderr, "rtMotionBegin inside a motion block, ignored\n");
        return;
    }
    if (!(time >= 0 && time <= 1))
        std::fprintf(stderr, "rtMotionBegin(%g): the shutter is open from 0 to 1\n", time);
    for (size_t i = 0; i < current.keys.size(); i++)
        if (current.keys[i].time == time) {
            editing = (int)i;
            return;
        }
    MotionKey k;
    k.time = time;
    k.m = current.m;
    current.keys.push_back(k);
    editing = (int)current.keys.size() - 1;
}

RtVoid rtMotionEnd(void)
{
    if (editing < 0)
        std::fprintf(stderr, "rtMotionEnd without rtMotionBegin\n");
    editing = -1;
}

RtVoid rtTranslate(RtFloat dx, RtFloat dy, RtFloat dz) { apply(Mat4::translate(dx, dy, dz)); }

RtVoid rtRotate(RtFloat angle, RtFloat x, RtFloat y, RtFloat z) { apply(Mat4::rotate(angle, x, y, z)); }

RtVoid rtScale(RtFloat sx, RtFloat sy, RtFloat sz) { apply(Mat4::scale(sx, sy, sz)); }

RtVoid rtConcatTransform(RtMatrix m) { apply(Mat4::fromRows(m)); }
