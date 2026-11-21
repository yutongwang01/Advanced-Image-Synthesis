/*
 * tangents.h: the tangents of a triangle, given complete (HW4 handout;
 * Suggested Workflow, step 13). A normal map bends the shading normal
 * about the surface's two tangents, dP/du and dP/dv, which a scene's
 * shader reads from the shade context as dPdu and dPdv (rt.h). You do not
 * write the formula. Call triangleTangents() where you fill the context
 * at a triangle's hit, and store its two vectors there. JunkShop's normal
 * maps need them, and the test scene NormalMap checks them.
 */
#ifndef TANGENTS_H
#define TANGENTS_H

#include "vec.h"

#include <cmath>

/* The tangents of the triangle (p0, p1, p2), its vertices in world space
 * and in the order of its indices, from the texture coordinates st0, st1
 * and st2 of the same three vertices (two floats each; pass NULL for all
 * three when the mesh has none). With e1 = p1 - p0, e2 = p2 - p0,
 * (du1, dv1) = st1 - st0 and (du2, dv2) = st2 - st0:
 *   dPdu = (dv2 e1 - dv1 e2) / det, dPdv = (du1 e2 - du2 e1) / det,
 *   det = du1 dv2 - dv1 du2.
 * They are constant over the triangle, and they are not unit vectors: a
 * shader normalizes them. Without texture coordinates, or when det is 0
 * (a triangle that is a line in the texture), they are two unit vectors
 * perpendicular to the triangle's normal, so that a shader always gets a
 * frame. */
inline void triangleTangents(const Vec3 &p0, const Vec3 &p1, const Vec3 &p2, const float *st0, const float *st1,
                             const float *st2, Vec3 &dPdu, Vec3 &dPdv)
{
    const Vec3 e1 = p1 - p0, e2 = p2 - p0;
    if (st0 && st1 && st2) {
        const float du1 = st1[0] - st0[0], dv1 = st1[1] - st0[1];
        const float du2 = st2[0] - st0[0], dv2 = st2[1] - st0[1];
        const float det = du1 * dv2 - dv1 * du2;
        if (std::fabs(det) > 1e-12f) {
            dPdu = (e1 * dv2 - e2 * dv1) / det;
            dPdv = (e2 * du1 - e1 * du2) / det;
            return;
        }
    }
    /* no st to go by: the frame about the triangle's normal */
    const Vec3 n = normalize(cross(e1, e2));
    const Vec3 a = std::fabs(n.x) < 0.9f ? Vec3(1, 0, 0) : Vec3(0, 1, 0);
    dPdu = normalize(cross(a, n));
    dPdv = cross(n, dPdu);
}

#endif /* TANGENTS_H */
