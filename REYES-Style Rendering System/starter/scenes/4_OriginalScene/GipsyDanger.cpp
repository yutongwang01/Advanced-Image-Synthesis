
#include "Ri.h"
#include "obj.h"
#include "sceneutil.h"

#include <cstring>
#include <cstdio>

namespace {

const double EYE[3] = {1.8, 1.7, -4.3};
const double TARGET[3] = {0.0, 1.65, 0.0};

int materialSlot(const char *name) {
    const char *materials[] = {
        "GypsyDanger_UpperLeg",
        "GypsyDanger_LowerLeg",
        "GypsyDanger_Body",
        "GypsyDanger_Head",
        "GypsyDanger_Shoulder",
        "GypsyDanger_ArmUpper",
        "GypsyDanger_ArmLower"
    };

    for (int i = 0; i < 7; ++i) {
        if (std::strcmp(name, materials[i]) == 0)
            return i;
    }
    return -1;
}

void DrawGipsyDanger(const ObjMesh *mesh) {
    RtColor white = {1, 1, 1};
    RtColor opaque = {1, 1, 1};

    RiColor(white);
    RiOpacity(opaque);

    for (int k = 0; mesh && k < mesh->ngroups; ++k) {
        const ObjGroup &g = mesh->groups[k];

        TextureParams tp = {materialSlot(g.material)};

        if (tp.slot >= 0) {
            RiSurface(TexturedStandard, &tp, sizeof tp);
        } else {
            RiSurface(NULL, NULL, 0);
        }

        RiPointsPolygons(
            g.npolys, g.nverts, g.verts,
            g.P, g.N, g.st
        );
    }
}

}

void GipsyDanger(void) {
    float fov = 42.0f;
    RtMatrix camera;
    LookAt(EYE, TARGET, camera);

    RiBegin(RI_NULL);
        RiFormat(480, 270);
        RiPixelSamples(2, 2);
        RiDisplay(
            "4_OriginalScene/GipsyDanger.png",
            "", ""
        );

        RiFrameBegin(0);
            RiProjection(RI_PERSPECTIVE, "fov", &fov);
            RiConcatTransform(camera);

            const char *files[7] = {
                "Image_0.jpg", "Image_2.jpg",
                "Image_4.jpg", "Image_6.jpg",
                "Image_8.jpg", "Image_10.jpg",
                "Image_12.jpg"
            };

            for (int i = 0; i < 7; ++i) {
                char path[256];
                std::snprintf(
                    path, sizeof path,
                    "models/GipsyDanger/textures/%s",
                    files[i]
                );
                RiMakeTexture(path, i);
            }

            RiWorldBegin();
                ObjMesh *mesh = obj_load(
                    "models/GipsyDanger/gipsy_danger.obj"
                );

                // DrawGipsyDanger(mesh);
                RiTransformBegin();
                    RiRotate(150.0f, 0.0f, 1.0f, 0.0f);
                    DrawGipsyDanger(mesh);
                RiTransformEnd();
                obj_free(mesh);
            RiWorldEnd();
        RiFrameEnd();
    RiEnd();
}
