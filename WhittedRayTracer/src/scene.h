/*
 * scene.h: the renderer's scene data structure (Workflow, step 4)
 */

#ifndef SCENE_H
#define SCENE_H

#include "rt.h"
#include "transform.h"
#include "vec.h"

#include <cstddef>
#include <limits>
#include <vector>


/* A ray o + t d, valid for t in [tMin, tMax]. d is a unit vector for camera
 * and secondary rays, so t is a distance; tMin is the offset that keeps a
 * secondary ray off the surface it starts on. */
struct Ray {
    Vec3 o;
    Vec3 d;
    float tMin = 0.0f;
    float tMax = std::numeric_limits<float>::infinity();
    float time = 0.0f; /* in [0, 1); always 0 under whitted */
    int depth = 0;     /* 0 for camera rays, +1 per bounce, -1 for a shadow ray (the cutout shader's convention) */

    Ray() {}
    Ray(const Vec3 &o_, const Vec3 &d_, float tMin_ = 0.0f,
        float tMax_ = std::numeric_limits<float>::infinity(), int depth_ = 0)
        : o(o_), d(d_), tMin(tMin_), tMax(tMax_), depth(depth_)
    {
    }

    Vec3 at(float t) const { return o + d * t; }
};

/* A shader bound with rtSurface or rtCutout: the function and its own copy
 * of the parameter block, made at bind time. */
template <typename Fn> struct ShaderBinding {
    Fn fn = nullptr;
    std::vector<unsigned char> params;

    /* Bind f with a copy of nbytes of p now (a NULL f or p binds no parameters). */
    void bind(Fn f, const void *p, size_t nbytes)
    {
        fn = f;
        params.clear();
        if (f && p && nbytes > 0)
            params.assign((const unsigned char *)p, (const unsigned char *)p + nbytes);
    }

    const void *data() const { return params.empty() ? nullptr : params.data(); }
};

/* the CTM at the call (object to world) and its inverse (world to object), the
 * material, and the shaders bound then. A sphere is intersected in object
 * space through worldToObject and its normal comes back through the inverse
 * transpose; a mesh is baked to world space at the call, and keeps the
 * inverse for Po, the hit point in object space, and for the tangents. */
struct DrawCall {
    Mat4 objectToWorld = Mat4::identity();
    Mat4 worldToObject = Mat4::identity();
    RtMaterial material;
    ShaderBinding<RtShaderFn> surface;
    ShaderBinding<RtCutoutFn> cutout;
    bool emissive = false; /* the material's emission is nonzero: the draw call is a light */
};

/* A sphere of the given radius at the origin of its object space. Under a
 * non-uniform CTM it is an ellipsoid; the draw call holds the transforms. */
struct Sphere {
    float radius = 1.0f;
    int drawCall = -1; /* index into Scene::drawCalls */
};

/* N, st and Cs are empty when the call passed NULL for them; 
 * otherwise they hold one entry per vertex (two floats
 * per vertex for st). A triangle's vertex indices point into these. */
struct Mesh {
    int drawCall = -1;         
    int firstTriangle = 0;   
    int triangleCount = 0;
    std::vector<Vec3> P;       /* world-space vertex positions */
    std::vector<Vec3> N;       /* world-space unit vertex normals, or empty */
    std::vector<float> st;     /* texture coordinates, 2 per vertex, or empty */
    std::vector<Vec3> Cs;      /* per-vertex colors replacing the albedo, or empty */

    bool hasNormals() const { return !N.empty(); }
    bool hasTexCoords() const { return !st.empty(); }
    bool hasColors() const { return !Cs.empty(); }
};

/* One triangle of a mesh, in world space: its first vertex, its two edges
 * (p0 + u e1 + v e2) and the constant tangents dP/du and dP/dv that the
 * texture coordinates make over it. The vertex indices find the normals,
 * texture coordinates and colors in the mesh to interpolate. */
struct Triangle {
    Vec3 p0;
    Vec3 e1, e2;        /* p1 - p0, p2 - p0 */
    Vec3 dPdu, dPdv;    /* world-space tangents; zero when the mesh has no st or the st are degenerate */
    int v[3] = {0, 0, 0}; /* indices into the mesh's vertex arrays */
    int mesh = -1;      /* index into Scene::meshes */

    /* The unnormalized geometric normal, e1 x e2: counterclockwise, so it
     * points outside. */
    Vec3 normal() const { return cross(e1, e2); }
};

/* The result of intersecting a ray with the scene: whether it hit, and
 * where. No lighting here; shading reads the draw call's material. */
struct Hit {
    float t = std::numeric_limits<float>::infinity(); /* distance along the ray to the hit point */
    Vec3 P;                                           /* the hit point, world space */
    Vec3 Ng;                                          /* the geometric normal */
    Vec3 N;                                           /* the shading normal */
    int drawCall = -1;                                /* index into Scene::drawCalls: material and transforms */
    int prim = -1;                                    /* the primitive hit: a sphere's index, or spheres.size() plus
                                                         a triangle's index (the renderer's own numbers, for --trace) */
    bool entering = false;                            /* the ray met the front: Ng . I < 0 with Ng as stored, before
                                                         any flip (Intersection handout, section 4) */
    float b1 = 0, b2 = 0;                             /* a triangle's barycentric weights of P1 and P2 (P0's is
                                                         1 - b1 - b2); 0 for a sphere */
    bool hit = false;                                 /* whether a valid intersection was found */
};

/* Emissive draw calls join the list at rtWorldEnd
 * as stand-in point lights at their center (Whitted Illumination handout). */
enum class LightType {
    Point,       /* rtPointLight */
    Directional, /* rtDirectionalLight */
    Spot,        /* rtSpotLight */
    Emissive     /* a sphere or mesh with nonzero emission */
};

struct Light {
    LightType type = LightType::Point;
    Vec3 position;             /* Point, Spot, Emissive: world space */
    Vec3 direction;            /* Directional, Spot: the unit direction the light travels */
    Vec3 intensity;            /* Point, Spot, Emissive: W/sr; Directional: irradiance, W/m^2 */
    float cosOuter = -1.0f;    /* Spot: cosine of the cone angle; the light is gone at this angle */
    float cosInner = -1.0f;    /* Spot: cosine of (cone - penumbra); full strength inside this angle */
    float maxDistance = 0.0f;  /* Spot: the reach, fading to nothing there; 0 for no limit */
    Vec3 normal;               /* Emissive, one-sided: the flat mesh's unit normal, the side it shines toward */
    bool oneSided = false;     /* Emissive: a flat mesh, I(w) = intensity * max(normal . w, 0) toward w */
    int drawCall = -1;         /* Emissive: index into Scene::drawCalls; -1 for the others */
};

/* An axis-aligned bounding box; empty (min > max) until something is added. */
struct AABB {
    Vec3 min = Vec3(std::numeric_limits<float>::infinity());
    Vec3 max = Vec3(-std::numeric_limits<float>::infinity());

    void expand(const Vec3 &p)
    {
        min = vmin(min, p);
        max = vmax(max, p);
    }
    void expand(const AABB &b)
    {
        min = vmin(min, b.min);
        max = vmax(max, b.max);
    }
    bool empty() const { return min.x > max.x || min.y > max.y || min.z > max.z; }
    Vec3 centroid() const { return (min + max) * 0.5f; }
    Vec3 extent() const { return max - min; }
    /* 2 (xy + yz + zx); 0 for an empty box */
    float surfaceArea() const
    {
        if (empty())
            return 0.0f;
        const Vec3 e = extent();
        return 2.0f * (e.x * e.y + e.y * e.z + e.z * e.x);
    }
};

/* One node of the BVH (Workflow, step 14), stored depth first: an interior
 * node's first child is the next node and `second` its second child; a
 * leaf names its primitives Scene::bvhPrims[first, first + count), each
 * a primitive number as in Hit::prim (a sphere's index, or spheres.size()
 * plus a triangle's index). */
struct BVHNode {
    AABB bounds;
    int first = 0;  /* leaf: the first of its entries in Scene::bvhPrims */
    int count = 0;  /* leaf: how many primitives; 0 for an interior node */
    int second = -1; /* interior: the index of the second child */

    bool isLeaf() const { return count > 0; }
};

/* The world between rtWorldBegin and rtWorldEnd. */
struct Scene {
    std::vector<DrawCall> drawCalls;
    std::vector<Sphere> spheres;
    std::vector<Mesh> meshes;
    std::vector<Triangle> triangles; /* every mesh's triangles, one mesh after another */
    std::vector<Light> lights;
    std::vector<BVHNode> bvh;  /* over every sphere and triangle, built at rtWorldEnd; empty: brute force */
    std::vector<int> bvhPrims; /* the primitive numbers, in the leaves' order */

    void clear()
    {
        drawCalls.clear();
        spheres.clear();
        meshes.clear();
        triangles.clear();
        lights.clear();
        bvh.clear();
        bvhPrims.clear();
    }
};

#endif
