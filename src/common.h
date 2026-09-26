// common.h - shared definitions, globals, helpers for 《长空·1951》
#ifndef COMMON_H
#define COMMON_H

#include "raylib.h"
#include "raymath.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define APP_TITLE     "长空·1951  J-20 SKIES OVER KOREA"
#define WORLD_HALF    1300.0f           // terrain spans -HALF..HALF metres
#define SEA_Y         (-22.0f)
#define GRAVITY       18.0f
#define MAX_TARGETS   40
#define MAX_ACTORS    48
#define MAX_TRACERS   256
#define MAX_PARTS     700

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif
#define DEG2R (M_PI/180.0f)
#define R2D   (180.0f/M_PI)

// ---------- global font (embedded CJK) ----------
extern Font gFont;
Font GameFont(void);
void LoadGameFont(void);

// ---------- small helpers ----------
static inline float frand(float a,float b){ return a + (b-a)*((float)rand()/(float)RAND_MAX); }
static inline int   irand(int a,int b){ return a + rand()%(b-a+1); }
static inline float clampf(float v,float a,float b){ return v<a?a:(v>b?b:v); }
static inline Vector3 v3(float x,float y,float z){ return (Vector3){x,y,z}; }
static inline Vector3 vadd(Vector3 a,Vector3 b){ return (Vector3){a.x+b.x,a.y+b.y,a.z+b.z}; }
static inline Vector3 vsub(Vector3 a,Vector3 b){ return (Vector3){a.x-b.x,a.y-b.y,a.z-b.z}; }
static inline Vector3 vmul(Vector3 a,float s){ return (Vector3){a.x*s,a.y*s,a.z*s}; }
static inline float   vlen(Vector3 a){ return sqrtf(a.x*a.x+a.y*a.y+a.z*a.z); }
static inline Vector3 vnorm(Vector3 a){ float l=vlen(a); if(l<1e-6f) return (Vector3){0,1,0}; return vmul(a,1.0f/l); }
static inline Vector3 vcross(Vector3 a,Vector3 b){ return (Vector3){a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x}; }
static inline float   vdot(Vector3 a,Vector3 b){ return a.x*b.x+a.y*b.y+a.z*b.z; }
static inline Vector3 vlerp(Vector3 a,Vector3 b,float t){ return (Vector3){a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t,a.z+(b.z-a.z)*t}; }

// rotate an orientation so its forward(-Z) points toward a target with a max turn rate
Quaternion SteerForward(Quaternion cur, Vector3 desired, float maxStepRad, float dt, int bank);
void  AxisAngleLocal(Quaternion q, Vector3 worldAxisAngle, Quaternion* out);
float WrapPI(float a);

// build a TRS part matrix (translate * rotate(axis,ang) * scale)
Matrix MPart(Vector3 t, Vector3 axis, float angRad, Vector3 s);
Matrix MPartQuat(Quaternion q, Vector3 s);

// ---------- terrain ----------
void   Terrain_Init(void);
void   Terrain_ApplyShader(Shader s);
float  Terrain_Height(float x, float z);
Vector3 Terrain_Normal(float x, float z);
void   Terrain_Draw(Camera3D cam);
void   Sea_Draw(Camera3D cam);
extern Model gTerrain;
extern int   gTerrainReady;

// ---------- scene (shaders / models / particles / sfx) ----------
enum { P_BOX=0, P_CYL, P_SPHERE, P_CONE, P_OCT, P_SHAPE_COUNT };
enum {
    C_DARK=0,C_GREY,C_STEEL,C_BLACK,C_WHITE,C_RED,C_YELLOW,C_ORANGE,
    C_OLIVE,C_KHAKI,C_SKIN,C_WOOD,C_GREEN,C_BROWN,C_NAVY,C_SAND,
    C_HELMET,C_GI,C_PVA,C_DARKOLIVE,C_RUDDER,C_GLASS,C_MARK,C_JETSILVER,C_PAL_COUNT
};
void   Scene_Load(void);
void   Scene_Unload(void);
void   Scene_SetCamera(Camera3D cam);
void   DrawPart(int shape, int color, Matrix parent, Matrix local);
void   DrawParts(int shape, int color, Matrix parent, const Matrix* locals, int n);
void   DrawJ20(Vector3 pos, Quaternion q, float scale, int insignia);
void   DrawSabre(Vector3 pos, Quaternion q, float scale);
void   DrawSoldier(Vector3 feet, float yaw, int uniform, float scale, int rifleUp);
void   DrawVehicle(Vector3 pos, float yaw, int kind, float scale);
void   DrawMissile(Vector3 pos, Quaternion q);
void   DrawBomb(Vector3 pos, Quaternion q);
void   DrawRifleView(Camera3D cam, int type, float kick);
Vector3 RifleMuzzle(Camera3D cam, int type);

// particle / tracer fx
void FX_Init(void);
void FX_Update(float dt);
void FX_Draw3D(Camera3D cam);
void FX_Explosion(Vector3 p, float scale);
void FX_Fireball(Vector3 p, float scale);
void FX_Smoke(Vector3 p, float scale);
void FX_Muzzle(Vector3 p);
void FX_FlakBurst(Vector3 p);
void FX_Tracer(Vector3 a, Vector3 b, Color c, float life);
void FX_EngineSmoke(Vector3 p);
extern int gActiveParticles;

// procedural sound (tolerates missing audio device)
void Sfx_Load(void);
void Sfx_Unload(void);
void Sfx_Gun(void);
void Sfx_Boom(float vol);
void Sfx_Flak(void);
void Sfx_Engine(float throttle01, int on);
void Sfx_Bugle(void);

// clouds / sky decoration
void Env_Load(void);
void Env_Update(float dt);
void Env_Draw(Camera3D cam);
void Env_DrawSky2D(void);

// ---------- modes ----------
typedef struct {
    int win;              // 0 ongoing, 1 victory, 2 killed/defeat, 3 sacrifice
    int jetsKilled, groundKilled, wavesTotal;
    int missilesLeft;
    float hp, timeAlive, score;
    int endingId;
} AirResult;
void Air_Run(int *outMode, int *outEnding);   // runs until mode exits; fills ending

typedef struct {
    int win;
    int foesKilled, friendliesAlive, holdTime;
    float hp, timeAlive;
    int endingId;
} GroundResult;
void Ground_Run(int *outMode, int *outEnding);

void DrawEnding(int mode, int endingId, int fromAir, void* res);
void DrawHistoryScreen(void);

// HUD helper using CJK font
void CN(const char* text, int x, int y, int size, Color c);
int  CNWidth(const char* text, int size);
void CNC(const char* text, int cx, int y, int size, Color c); // centered by x

// on-screen touch controls (inert stubs on desktop)
void  Touch_Update(int mode);          // mode: 0 air, 1 ground
void  Touch_DrawHUD(void);
float Touch_AxisX(void);               // left stick, right +
float Touch_AxisY(void);               // left stick, up +
float Touch_LookDX(void);              // ground look delta (pixels)
float Touch_LookDY(void);
int   Touch_FireHeld(void);            // cannon / rifle (continuous)
int   Touch_ActPressed(void);          // missile (air) / jump (ground)
int   Touch_BPressed(void);            // bomb (air) / reload (ground)
int   Touch_SwitchPressed(void);       // ground weapon switch
int   Touch_PausePressed(void);        // on-screen pause/back
int   Touch_ADSHeld(void);             // ground aim-down-sights (hold)
int   Touch_PauseMenuSelect(void);     // pause overlay: 1 resume, 2 quit
void  Touch_DrawPauseMenu(void);
int   Touch_IsTouch(void);

// battle scenario: 0 = generic ridge assault, 1 = Chosin Reservoir / Ice Company
extern int gScenario;

// self test
extern int gSelfTest;
extern int gUncap;
extern char gShotDir[];
extern AirResult gAirResult;
extern GroundResult gGroundResult;

#endif
