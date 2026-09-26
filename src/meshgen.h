// meshgen.h - hand-built indexed primitives (positions/normals/uv/white colors),
// uploaded exactly once. Avoids raylib GenMesh* GPU-upload quirks on 5.5.
#ifndef MESHGEN_H
#define MESHGEN_H
#include "raylib.h"
Mesh MG_Box(float w, float h, float d);
Mesh MG_Cylinder(float r, float h, int seg);
Mesh MG_Cone(float r, float h, int seg);
Mesh MG_Sphere(float r, int rings, int slices);
#endif
