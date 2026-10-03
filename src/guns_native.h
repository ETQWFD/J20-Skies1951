// guns_native.h - real PBR small-arms in native builds (embedded GLB data)
#ifndef GUNS_NATIVE_H
#define GUNS_NATIVE_H
#include "raylib.h"

void GunsNative_Load(void);      // after Scene_Load (uses the lit world shader)
void GunsNative_Unload(void);
int  GunsNative_Ready(void);

// first-person held weapon. type: 0 Mosin/98k, 1 AKM, 2 bayonet
void GunsNative_DrawView(Camera3D cam, int type, float kick, float reload01);
// world-space embedded T-34: world = RotY(-heading) * Translate(pos) (metres, baked)
void GunsNative_DrawT34(Matrix world);
Vector3 GunsNative_Muzzle(void);
Vector3 GunsNative_MuzzlePoint(Camera3D cam, int type);
void Weapon_SwingTickNative(void);
void Weapon_AnimUpdateNative(float dt);

#endif
