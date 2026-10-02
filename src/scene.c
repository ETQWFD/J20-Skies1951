// scene.c - shaders (compiled at runtime), procedural models, particles, sfx
#include "common.h"
#include "noise.h"
#include "rlgl.h"
#include "meshgen.h"
#include "guns_native.h"

// ----------------------------------------------------------------------------
// Embedded GLSL 330 lit shader: Lambert + hemisphere ambient + distance fog.
// Compiled fresh on every launch via LoadShaderFromMemory().
// ----------------------------------------------------------------------------
// ----------------------------------------------------------------------------
// Embedded lit shader: Lambert + hemisphere ambient + distance fog.
// Desktop builds use GLSL 330; Android uses OpenGL ES 2.0 (GLSL 100).
// Compiled fresh on every launch via LoadShaderFromMemory().
// ----------------------------------------------------------------------------
#if defined(PLATFORM_ANDROID)
static const char *VS =
"attribute vec3 vertexPosition;\n"
"attribute vec2 vertexTexCoord;\n"
"attribute vec3 vertexNormal;\n"
"attribute vec4 vertexColor;\n"
"uniform mat4 mvp;\n"
"uniform mat4 matModel;\n"
"varying vec3 vW; varying vec3 vN; varying vec4 vC; varying vec2 vUV;\n"
"void main(){\n"
"  vW=(matModel*vec4(vertexPosition,1.0)).xyz;\n"
"  vN=mat3(matModel)*vertexNormal;\n"
"  vC=vertexColor; vUV=vertexTexCoord;\n"
"  gl_Position=mvp*vec4(vertexPosition,1.0);\n"
"}\n";

static const char *FS =
"precision mediump float;\n"
"varying vec3 vW; varying vec3 vN; varying vec4 vC; varying vec2 vUV;\n"
"uniform sampler2D texture0;\n"
"uniform vec4 colDiffuse;\n"
"uniform vec3 sunDir; uniform vec3 sunCol; uniform vec3 amb; uniform vec3 camPos;\n"
"uniform vec3 fogCol; uniform float fogNear; uniform float fogFar;\n"
"void main(){\n"
"  vec3 N=normalize(vN);\n"
"  float ndl=max(dot(N,normalize(sunDir)),0.0);\n"
"  float wrap=max(dot(N,normalize(sunDir))*0.5+0.5,0.0);\n"
"  float hemi=0.5+0.5*N.y;\n"
"  vec4 tex=texture2D(texture0,vUV);\n"
"  vec3 base=tex.rgb*colDiffuse.rgb*vC.rgb;\n"
"  vec3 skyTint=vec3(0.60,0.70,0.92);\n"
"  vec3 gndTint=vec3(0.40,0.35,0.29);\n"
"  vec3 hemiC=mix(gndTint,skyTint,hemi);\n"
"  vec3 lit=base*(amb + hemiC*0.26 + pow(ndl,0.85)*sunCol*1.35);\n"
"  float d=distance(vW,camPos);\n"
"  float f=clamp((d-fogNear)/(fogFar-fogNear),0.0,1.0); f=f*f;\n"
"  lit=mix(lit,fogCol,f);\n"
"  gl_FragColor=vec4(lit, tex.a*colDiffuse.a*vC.a);\n"
"}\n";
#else
static const char *VS =
"#version 330\n"
"layout(location=0) in vec3 vertexPosition;\n"
"layout(location=1) in vec2 vertexTexCoord;\n"
"layout(location=2) in vec3 vertexNormal;\n"
"layout(location=3) in vec4 vertexColor;\n"
"uniform mat4 mvp;\n"
"uniform mat4 matModel;\n"
"out vec3 vW; out vec3 vN; out vec4 vC; out vec2 vUV;\n"
"void main(){\n"
"  vW=(matModel*vec4(vertexPosition,1.0)).xyz;\n"
"  vN=mat3(matModel)*vertexNormal;\n"
"  vC=vertexColor; vUV=vertexTexCoord;\n"
"  gl_Position=mvp*vec4(vertexPosition,1.0);\n"
"}\n";

static const char *FS =
"#version 330\n"
"in vec3 vW; in vec3 vN; in vec4 vC; in vec2 vUV;\n"
"uniform sampler2D texture0;\n"
"uniform vec4 colDiffuse;\n"
"uniform vec3 sunDir; uniform vec3 sunCol; uniform vec3 amb; uniform vec3 camPos;\n"
"uniform vec3 fogCol; uniform float fogNear; uniform float fogFar;\n"
"out vec4 frag;\n"
"void main(){\n"
"  vec3 N=normalize(vN);\n"
"  float ndl=max(dot(N,normalize(sunDir)),0.0);\n"
"  float wrap=max(dot(N,normalize(sunDir))*0.5+0.5,0.0);\n"
"  float hemi=0.5+0.5*N.y;\n"
"  vec4 tex=texture2D(texture0,vUV);\n"
"  vec3 base=tex.rgb*colDiffuse.rgb*vC.rgb;\n"
"  vec3 skyTint=vec3(0.60,0.70,0.92);\n"
"  vec3 gndTint=vec3(0.40,0.35,0.29);\n"
"  vec3 hemiC=mix(gndTint,skyTint,hemi);\n"
"  vec3 lit=base*(amb + hemiC*0.26 + pow(ndl,0.85)*sunCol*1.35);\n"
"  float d=distance(vW,camPos);\n"
"  float f=clamp((d-fogNear)/(fogFar-fogNear),0.0,1.0); f=f*f;\n"
"  lit=mix(lit,fogCol,f);\n"
"  frag=vec4(lit, tex.a*colDiffuse.a*vC.a);\n"
"}\n";
#endif

Shader gLit = (Shader){0};
static int locSun,locSunCol,locAmb,locCam,locFogCol,locFogNear,locFogFar;
static Vector3 SUN_DIR = {-0.42f,-0.82f,-0.28f};

static Mesh    sMesh[P_SHAPE_COUNT];
static Material sMat[C_PAL_COUNT];
static bool sReady=false;

static const Color PAL[C_PAL_COUNT] = {
    [C_DARK]     = {42,45,50,255},
    [C_GREY]     = {158,164,172,255},
    [C_STEEL]    = {112,120,130,255},
    [C_BLACK]    = {20,20,22,255},
    [C_WHITE]    = {232,232,228,255},
    [C_RED]      = {190,34,30,255},
    [C_YELLOW]   = {222,190,70,255},
    [C_ORANGE]   = {224,122,38,255},
    [C_OLIVE]    = {102,110,62,255},
    [C_KHAKI]    = {168,158,116,255},
    [C_SKIN]     = {214,178,146,255},
    [C_WOOD]     = {120,82,46,255},
    [C_GREEN]    = {58,92,48,255},
    [C_BROWN]    = {96,70,48,255},
    [C_NAVY]     = {40,52,72,255},
    [C_SAND]     = {196,184,150,255},
    [C_HELMET]   = {78,92,58,255},
    [C_GI]       = {96,104,70,255},
    [C_PVA]      = {150,142,104,255},
    [C_DARKOLIVE]= {64,70,44,255},
    [C_RUDDER]   = {120,126,134,255},
    [C_GLASS]    = {40,62,88,255},
    [C_MARK]     = {200,40,36,255},
    [C_JETSILVER]= {96,104,118,255},
    [C_BLOOD]    = {112,14,14,210},
    [C_SHADOW]   = {0,0,0,120},
};

// ---------------------------------------------------------------- math helpers
Matrix MPartQ(Vector3 t, Quaternion q, Vector3 s)
{
    // raylib: MatrixMultiply(A,B) applies A then B; unit vertex is scaled,
    // rotated, then translated -> S * R * T (matches DrawModelEx).
    Matrix T=MatrixTranslate(t.x,t.y,t.z);
    Matrix R=QuaternionToMatrix(q);
    Matrix S=MatrixScale(s.x,s.y,s.z);
    return MatrixMultiply(MatrixMultiply(S,R),T);
}
Matrix MPart(Vector3 t, Vector3 axis, float angRad, Vector3 s)
{
    Quaternion q = QuaternionFromAxisAngle(axis, angRad);
    return MPartQ(t,q,s);
}
float WrapPI(float a){ while(a>M_PI) a-=2*M_PI; while(a<-M_PI) a+=2*M_PI; return a; }

Quaternion SteerForward(Quaternion cur, Vector3 desired, float maxStepRad, float dt, int bank)
{
    (void)bank;
    Vector3 fwd = Vector3RotateByQuaternion((Vector3){0,0,-1}, cur);
    desired = vnorm(desired);
    float d = clampf(vdot(fwd,desired), -1.0f, 1.0f);
    float ang = acosf(d);
    if (ang < 0.0005f) return cur;
    Vector3 axis = vnorm(vcross(fwd,desired));
    float step = fminf(ang, maxStepRad*dt);
    Quaternion dq = QuaternionFromAxisAngle(axis, step);
    return QuaternionMultiply(dq, cur);
}
void AxisAngleLocal(Quaternion q, Vector3 worldAxisAngle, Quaternion* out)
{
    // rotate a world rotation into local space (utility, currently unused externally)
    Quaternion inv = QuaternionIdentity();
    Vector3 axis = vnorm(worldAxisAngle);
    (void)q;(void)inv;(void)axis;(void)out;
}

void MeshEnsureColors(Mesh *m) { (void)m; }

// ---------------------------------------------------------------- textures
static Texture2D texGlow, texFire;
static Texture2D makeGlow(int fire)
{
    int S=64; Image im=GenImageColor(S,S,BLANK);
    for (int y=0;y<S;y++)for(int x=0;x<S;x++)
    {
        float dx=(x-S*0.5f)/(S*0.5f), dy=(y-S*0.5f)/(S*0.5f);
        float d=sqrtf(dx*dx+dy*dy);
        float a=clampf(1.0f-d,0,1); a=a*a;
        Color c;
        if (fire)
        {
            // hot core -> orange -> red edge
            if (d<0.35f) c=(Color){255,246,200,255};
            else if (d<0.62f) c=(Color){255,176,60,255};
            else c=(Color){210,70,20,255};
        }
        else c=(Color){255,255,255,255};
        c.a=(unsigned char)(a*255);
        ImageDrawPixel(&im,x,y,c);
    }
    Texture2D t=LoadTextureFromImage(im); UnloadImage(im); return t;
}

// ---------------------------------------------------------------- scene setup
void Scene_Load(void)
{
    gLit = LoadShaderFromMemory(VS, FS);
    if (gLit.id > 0)
    {
        locSun=GetShaderLocation(gLit,"sunDir");
        locSunCol=GetShaderLocation(gLit,"sunCol");
        locAmb=GetShaderLocation(gLit,"amb");
        locCam=GetShaderLocation(gLit,"camPos");
        locFogCol=GetShaderLocation(gLit,"fogCol");
        locFogNear=GetShaderLocation(gLit,"fogNear");
        locFogFar=GetShaderLocation(gLit,"fogFar");
        Terrain_ApplyShader(gLit);
    }

    sMesh[P_BOX]=MG_Box(1,1,1);
    sMesh[P_CYL]=MG_Cylinder(0.5f,1.0f,12);
    sMesh[P_SPHERE]=MG_Sphere(0.5f,16,18);
    sMesh[P_CONE]=MG_Cone(0.5f,1.0f,14);
    sMesh[P_OCT]=MG_Cylinder(0.5f,1.0f,8);

    for (int c=0;c<C_PAL_COUNT;c++)
    {
        sMat[c]=LoadMaterialDefault();
        if (gLit.id>0) sMat[c].shader=gLit;
        sMat[c].maps[MATERIAL_MAP_DIFFUSE].color = PAL[c];
    }

    texGlow=makeGlow(0);
    texFire=makeGlow(1);
    FX_Init();
    GunsNative_Load();
    Grass_Init();
    sReady=true;
}
void Scene_Unload(void)
{
    if (sReady)
    {
        Grass_Unload();
        GunsNative_Unload();
        for (int i=0;i<P_SHAPE_COUNT;i++) UnloadMesh(sMesh[i]);
        UnloadTexture(texGlow); UnloadTexture(texFire);
        if (gLit.id>0) UnloadShader(gLit);
    }
}

void Scene_SetCamera(Camera3D cam)
{
    if (gLit.id==0) return;
    Vector3 sunCol, amb, fog; float fn, ff;
    if(Map_IsNight())
    {   // cold moonlit winter night, gunsmoke on the wind
        sunCol=(Vector3){0.58f,0.64f,0.86f};
        amb   =(Vector3){0.10f,0.11f,0.15f};
        fog   =(Vector3){0.20f,0.20f,0.26f};
        fn=330.0f; ff=2100.0f;
    }
    else if(Map_IsDusk())
    {   // low burning dusk sun through smoke columns
        sunCol=(Vector3){1.20f,0.74f,0.46f};
        amb   =(Vector3){0.20f,0.17f,0.17f};
        fog   =(Vector3){0.60f,0.44f,0.36f};
        fn=300.0f; ff=2200.0f;
    }
    else
    {   // smoke-choked daylight: wan dirty sun, short haze distance
        sunCol=(Vector3){0.98f,0.86f,0.66f};
        amb   =(Vector3){0.20f,0.19f,0.17f};
        fog   =(Vector3){0.52f,0.48f,0.42f};
        fn=260.0f; ff=2000.0f;
    }
    SetShaderValue(gLit,locSun,&SUN_DIR,SHADER_UNIFORM_VEC3);
    SetShaderValue(gLit,locSunCol,&sunCol,SHADER_UNIFORM_VEC3);
    SetShaderValue(gLit,locAmb,&amb,SHADER_UNIFORM_VEC3);
    SetShaderValue(gLit,locCam,&cam.position,SHADER_UNIFORM_VEC3);
    SetShaderValue(gLit,locFogCol,&fog,SHADER_UNIFORM_VEC3);
    SetShaderValue(gLit,locFogNear,&fn,SHADER_UNIFORM_FLOAT);
    SetShaderValue(gLit,locFogFar,&ff,SHADER_UNIFORM_FLOAT);
}

// ---------------------------------------------------------------- part drawing
void DrawPart(int shape, int color, Matrix parent, Matrix local)
{
    // local transform of the vertex happens first, then parent transform
    Matrix w=MatrixMultiply(local,parent);
    DrawMesh(sMesh[shape], sMat[color], w);
}
void DrawParts(int shape, int color, Matrix parent, const Matrix* locals, int n)
{
    for (int i=0;i<n;i++) DrawMesh(sMesh[shape], sMat[color], MatrixMultiply(locals[i],parent));
}

static Matrix root(Vector3 pos, Quaternion q, float s)
{
    Matrix T=MatrixTranslate(pos.x,pos.y,pos.z);
    Matrix R=QuaternionToMatrix(q);
    Matrix S=MatrixScale(s,s,s);
    return MatrixMultiply(MatrixMultiply(S,R),T);
}

// ------------------------------------------------------------- J-20 威龙 (stylized, nose -Z)
void DrawJ20(Vector3 pos, Quaternion q, float scale, int insignia)
{
    Matrix M=root(pos,q,scale);
    const Vector3 X={1,0,0},Y={0,1,0},Z={0,0,1};
    // fuselage
    DrawPart(P_CONE,C_JETSILVER, M, MPart((Vector3){0,0,-9.4f}, (Vector3){1,0,0}, -90*DEG2R, (Vector3){0.92f,3.6f,0.92f}));
    DrawPart(P_BOX,C_JETSILVER, M, MPart((Vector3){0,0,-3.6f}, X,0, (Vector3){1.7f,1.25f,8.6f}));
    DrawPart(P_BOX,C_JETSILVER, M, MPart((Vector3){0,-0.02f,3.3f}, X,0, (Vector3){2.25f,1.45f,4.6f}));
    DrawPart(P_BOX,C_JETSILVER, M, MPart((Vector3){0,-0.78f,-0.5f}, X,0, (Vector3){1.2f,0.5f,11.0f})); // belly chine
    // engine nacelles + nozzles
    DrawPart(P_CYL,C_STEEL, M, MPart((Vector3){-0.78f,-0.05f,6.4f}, X,-90*DEG2R,(Vector3){1.0f,4.8f,1.0f}));
    DrawPart(P_CYL,C_STEEL, M, MPart((Vector3){ 0.78f,-0.05f,6.4f}, X,-90*DEG2R,(Vector3){1.0f,4.8f,1.0f}));
    DrawPart(P_CYL,C_BLACK,M, MPart((Vector3){-0.78f,-0.05f,8.95f},X,-90*DEG2R,(Vector3){1.06f,0.4f,1.06f}));
    DrawPart(P_CYL,C_BLACK,M, MPart((Vector3){ 0.78f,-0.05f,8.95f},X,-90*DEG2R,(Vector3){1.06f,0.4f,1.06f}));
    // canopy
    DrawPart(P_SPHERE,C_GLASS,M, MPart((Vector3){0,0.66f,-6.5f},X,0,(Vector3){0.62f,0.5f,1.7f}));
    // main delta wings (swept)
    DrawPart(P_BOX,C_JETSILVER, M, MPart((Vector3){2.9f,-0.12f,2.7f}, Y,-38*DEG2R,(Vector3){3.6f,0.16f,4.8f}));
    DrawPart(P_BOX,C_JETSILVER, M, MPart((Vector3){-2.9f,-0.12f,2.7f},Y, 38*DEG2R,(Vector3){3.6f,0.16f,4.8f}));
    // canard foreplanes
    DrawPart(P_BOX,C_JETSILVER, M, MPart((Vector3){1.05f,0.18f,-4.7f},Y,-52*DEG2R,(Vector3){1.35f,0.1f,1.5f}));
    DrawPart(P_BOX,C_JETSILVER, M, MPart((Vector3){-1.05f,0.18f,-4.7f},Y,52*DEG2R,(Vector3){1.35f,0.1f,1.5f}));
    // twin canted tails
    Quaternion qr=QuaternionMultiply(QuaternionFromAxisAngle(Y,-6*DEG2R),QuaternionFromAxisAngle(Z,-15*DEG2R));
    Quaternion ql=QuaternionMultiply(QuaternionFromAxisAngle(Y, 6*DEG2R),QuaternionFromAxisAngle(Z, 15*DEG2R));
    DrawPart(P_BOX,C_RUDDER,M, MPartQ((Vector3){1.02f,1.05f,6.1f},qr,(Vector3){0.12f,1.8f,2.8f}));
    DrawPart(P_BOX,C_RUDDER,M, MPartQ((Vector3){-1.02f,1.05f,6.1f},ql,(Vector3){0.12f,1.8f,2.8f}));
    // national markings (stylized)
    if (insignia)
    {
        DrawPart(P_BOX,C_MARK,M,MPart((Vector3){2.5f,0.02f,2.0f},Y,-38*DEG2R,(Vector3){0.7f,0.05f,0.7f}));
        DrawPart(P_BOX,C_MARK,M,MPart((Vector3){-2.5f,0.02f,2.0f},Y,38*DEG2R,(Vector3){0.7f,0.05f,0.7f}));
        DrawPart(P_BOX,C_MARK,M,MPart((Vector3){0,0.66f,-3.0f},X,0,(Vector3){0.5f,0.2f,0.05f}));
    }
}

// ------------------------------------------------------------- F-86 Sabre style enemy
void DrawSabre(Vector3 pos, Quaternion q, float scale)
{
    Matrix M=root(pos,q,scale);
    const Vector3 X={1,0,0},Y={0,1,0},Z={0,0,1};
    DrawPart(P_CONE,C_GREY,M,MPart((Vector3){0,0,-5.7f},X,-90*DEG2R,(Vector3){0.9f,2.6f,0.9f}));
    DrawPart(P_CYL,C_STEEL,M,MPart((Vector3){0,0,0},X,-90*DEG2R,(Vector3){0.95f,9.6f,0.95f}));
    DrawPart(P_CYL,C_BLACK,M,MPart((Vector3){0,0,-7.1f},X,-90*DEG2R,(Vector3){0.9f,0.3f,0.9f})); // intake
    DrawPart(P_SPHERE,C_GLASS,M,MPart((Vector3){0,0.55f,-3.9f},X,0,(Vector3){0.5f,0.42f,1.4f}));
    DrawPart(P_BOX,C_GREY,M,MPart((Vector3){2.0f,-0.05f,0.6f},Y,-32*DEG2R,(Vector3){2.7f,0.14f,2.3f}));
    DrawPart(P_BOX,C_GREY,M,MPart((Vector3){-2.0f,-0.05f,0.6f},Y,32*DEG2R,(Vector3){2.7f,0.14f,2.3f}));
    DrawPart(P_BOX,C_STEEL,M,MPartQ((Vector3){0,0.95f,3.4f},QuaternionFromAxisAngle(X,0),(Vector3){0.1f,1.5f,1.7f}));
    DrawPart(P_BOX,C_GREY,M,MPart((Vector3){0,0.35f,4.6f},X,0,(Vector3){2.4f,0.1f,1.1f})); // horizontal stab
    DrawPart(P_CYL,C_DARK,M,MPart((Vector3){0,0,5.1f},X,-90*DEG2R,(Vector3){0.7f,1.0f,0.7f}));
    DrawPart(P_BOX,C_MARK,M,MPart((Vector3){1.8f,0.06f,0.7f},Y,-32*DEG2R,(Vector3){0.5f,0.04f,0.5f}));
}

// soft contact shadow: flat dark disc hugging the ground under every unit
void DrawBlobShadow(Vector3 feet, float radius)
{
    float gy=Terrain_Height(feet.x,feet.z)+0.04f;
    DrawPart(P_CYL,C_SHADOW,MatrixIdentity(),
        MPart((Vector3){feet.x,gy,feet.z},(Vector3){1,0,0},0,(Vector3){radius,0.02f,radius}));
}

// ------------------------------------------------------------- soldiers (feet origin, facing -Z)
void DrawSoldier(Vector3 feet, float yaw, int uniform, float scale, int rifleUp, float phase, int moving)
{
    Quaternion q=QuaternionFromAxisAngle((Vector3){0,1,0}, yaw);
    Matrix M=root(feet,q,scale);
    int body = uniform==1 ? C_GI : (uniform==2 ? C_WHITE : C_PVA);
    int leg  = uniform==2 ? C_WHITE : C_DARKOLIVE;
    // walk cycle only while actually moving; a standing man no longer marches in
    // place (the old constant "walking in place" was the strange gait).
    float mv = moving?1.0f:0.0f;
    float sw  = sinf(phase)*mv, sw2 = sinf(phase+M_PI)*mv;
    float hipA = 0.55f, kneeB = 0.75f*fabsf(sw);     // hip swing + knee lift
    float bob = fabsf(cosf(phase))*0.045f*mv;

    Vector3 Xv={1,0,0};
    Matrix boxI=MatrixIdentity();
    // part drawn with a fully pre-composed local transform inside M
    #define DP(shape,color,F) DrawPart(shape,color,M,(F))

    // ---- two-segment legs: hip pivot, thigh, bent knee, shin, boot on the foot.
    // The boot is a CHILD of the shin transform so the foot follows the leg. ----
    for(int li=0;li<2;li++)
    {
        float sx=li?0.13f:-0.13f;
        float hipSwing = li? sw2*hipA : sw*hipA;
        Vector3 hip={sx,0.90f,0};
        Matrix thigh=MPartQ(hip,QuaternionFromAxisAngle(Xv,hipSwing),v3(1,1,1));
        DP(P_BOX,leg,MatrixMultiply(MPartQ((Vector3){0,-0.22f,0},QuaternionIdentity(),(Vector3){0.16f,0.46f,0.19f}),thigh));
        // knee sits at the bottom of the thigh; shin folds forward by kneeB
        Matrix knee=MatrixMultiply(MPartQ((Vector3){0,-0.45f,0},
                       QuaternionFromAxisAngle(Xv,-kneeB-0.12f),v3(1,1,1)),thigh);
        DP(P_BOX,leg,MatrixMultiply(MPartQ((Vector3){0,-0.21f,0},QuaternionIdentity(),(Vector3){0.145f,0.43f,0.165f}),knee));
        // boot at the far end of the shin, tilting with it
        DP(P_BOX,C_BLACK,MatrixMultiply(MPartQ((Vector3){0,-0.42f,-0.05f},QuaternionIdentity(),(Vector3){0.17f,0.14f,0.30f}),knee));
    }

    // ---- torso (slightly slimmer, natural head ratio) ----
    DP(P_BOX,body,MPart((Vector3){0,1.28f+bob,0},Xv,0,(Vector3){0.50f,0.72f,0.30f}));
    DP(P_BOX,C_DARK,MPart((Vector3){0,1.04f+bob,0},Xv,0,(Vector3){0.52f,0.09f,0.32f}));     // belt
    if(uniform!=1) DP(P_BOX,C_BROWN,MPart((Vector3){0,1.34f+bob,0.165f},Xv,0,(Vector3){0.16f,0.44f,0.05f})); // bandolier
    DP(P_BOX,C_BROWN,MPart((Vector3){0,1.24f+bob,0.235f},Xv,0,(Vector3){0.42f,0.46f,0.15f}));   // pack
    // neck + smaller head (old head was oversized)
    DP(P_SPHERE,body,MPart((Vector3){0,1.66f+bob,0},Xv,0,(Vector3){0.13f,0.12f,0.13f}));
    DP(P_SPHERE,C_SKIN,MPart((Vector3){0,1.80f+bob,0},Xv,0,(Vector3){0.245f,0.275f,0.245f}));
    if (uniform==1) DP(P_SPHERE,C_HELMET,MPart((Vector3){0,1.93f+bob,-0.01f},Xv,0,(Vector3){0.32f,0.15f,0.32f}));
    else            DP(P_SPHERE,(uniform==2?C_WHITE:C_KHAKI),MPart((Vector3){0,1.91f+bob,0},Xv,0,(Vector3){0.27f,0.11f,0.27f}));

    // ---- the RIFLE, aimed forward (-Z): stock, receiver, fore-end, long barrel,
    // curved magazine and front sight — unmistakably a gun, not a baton. ----
    float aimAng = rifleUp? -0.10f : 0.42f;                 // near-level aim vs low ready
    Matrix arm = MatrixMultiply(
        MPartQ((Vector3){0.10f,1.40f+bob,-0.10f},QuaternionFromAxisAngle(Xv,aimAng),v3(1,1,1)),
        MatrixMultiply(MPartQ(v3(0,0,0),QuaternionFromAxisAngle((Vector3){0,1,0},0.05f),v3(1,1,1)),M));
    // wooden stock toward the shoulder (+Z)
    DrawPart(P_BOX,C_WOOD,arm,MPart((Vector3){-0.10f,-0.01f,0.34f},Xv,8*DEG2R,(Vector3){0.10f,0.13f,0.34f}));
    DrawPart(P_BOX,C_WOOD,arm,MPart((Vector3){0,-0.02f,0.12f},Xv,0,(Vector3){0.085f,0.11f,0.14f}));     // grip
    DrawPart(P_BOX,C_DARK,arm,MPart((Vector3){0,0.02f,-0.05f},Xv,0,(Vector3){0.075f,0.10f,0.26f}));    // receiver/bolt
    DrawPart(P_BOX,C_WOOD,arm,MPart((Vector3){0,-0.02f,-0.34f},Xv,0,(Vector3){0.075f,0.085f,0.40f}));  // fore-end
    DrawPart(P_CYL,C_DARK,arm,MPart((Vector3){0,0.035f,-0.72f},Xv,90*DEG2R,(Vector3){0.028f,0.62f,0.028f})); // barrel
    DrawPart(P_CYL,C_DARK,arm,MPart((Vector3){0,0.035f,-1.02f},Xv,90*DEG2R,(Vector3){0.04f,0.10f,0.04f}));   // muzzle
    DrawPart(P_BOX,C_DARK,arm,MPart((Vector3){0,-0.11f,-0.10f},Xv,-6*DEG2R,(Vector3){0.06f,0.18f,0.10f}));   // curved magazine
    DrawPart(P_BOX,C_DARK,arm,MPart((Vector3){0,0.09f,-0.52f},Xv,0,(Vector3){0.02f,0.05f,0.02f}));     // front sight
    DrawPart(P_BOX,C_DARK,arm,MPart((Vector3){0,0.09f,0.02f},Xv,0,(Vector3){0.035f,0.045f,0.02f}));    // rear sight

    // ---- arms reach forward onto the rifle (rear hand on grip, front on fore-end) ----
    Quaternion qr=QuaternionFromAxisAngle(Xv,rifleUp?-1.15f:-0.75f);
    Quaternion qf=QuaternionFromAxisAngle(Xv,rifleUp?-1.25f:-0.85f);
    DP(P_BOX,body,MPartQ((Vector3){ 0.32f,1.42f+bob,0.0f},qr,(Vector3){0.13f,0.55f,0.15f}));   // right arm
    DP(P_SPHERE,C_SKIN,MPart((Vector3){0.06f,1.36f+bob,-0.06f},Xv,0,(Vector3){0.075f,0.075f,0.075f}));
    DP(P_BOX,body,MPartQ((Vector3){-0.30f,1.42f+bob,0.0f},qf,(Vector3){0.13f,0.58f,0.15f}));   // left arm
    DP(P_SPHERE,C_SKIN,MPart((Vector3){0.0f,1.36f+bob,-0.36f},Xv,0,(Vector3){0.075f,0.075f,0.075f}));
    #undef DP
}

// fallen soldier: body lying flat on its back along local +Z
void DrawSoldierDown(Vector3 feet, float yaw, int uniform, float scale)
{
    Quaternion q=QuaternionFromAxisAngle((Vector3){0,1,0}, yaw);
    Matrix M=root(feet,q,scale);
    int body = uniform==1 ? C_GI : (uniform==2 ? C_WHITE : C_PVA);
    int leg  = uniform==2 ? C_WHITE : C_DARKOLIVE;
    // dropped ~0.06 so the lowest parts rest on the terrain instead of floating
    DrawPart(P_BOX,leg,M,MPart((Vector3){-0.13f,0.12f,-0.62f},(Vector3){1,0,0},0,(Vector3){0.17f,0.26f,1.0f}));
    DrawPart(P_BOX,leg,M,MPart((Vector3){ 0.13f,0.12f,-0.62f},(Vector3){1,0,0},0,(Vector3){0.17f,0.26f,1.0f}));
    DrawPart(P_BOX,body,M,MPart((Vector3){0,0.18f,0.22f},(Vector3){1,0,0},0,(Vector3){0.54f,0.32f,0.78f}));
    DrawPart(P_BOX,body,M,MPart((Vector3){-0.34f,0.14f,0.1f},(Vector3){1,0,0},0,(Vector3){0.15f,0.18f,0.7f}));
    DrawPart(P_BOX,body,M,MPart((Vector3){ 0.34f,0.14f,0.1f},(Vector3){1,0,0},0,(Vector3){0.15f,0.18f,0.7f}));
    DrawPart(P_SPHERE,C_SKIN,M,MPart((Vector3){0,0.20f,0.8f},(Vector3){0,1,0},0,(Vector3){0.3f,0.28f,0.3f}));
    if(uniform==1) DrawPart(P_SPHERE,C_HELMET,M,MPart((Vector3){0,0.28f,0.8f},(Vector3){1,0,0},0,(Vector3){0.36f,0.14f,0.36f}));
}

// --------------------------------------------------------------- missile / bomb
// Local forward is -Z (same as the aircraft); the parent matrix carries pose.
void DrawMissile(Vector3 pos, Quaternion q)
{
    Matrix w=root(pos,q,1.0f);
    // body: cylinder axis is +Y, rotate -90 about X so the long axis points -Z
    DrawPart(P_CYL,  C_WHITE, w, MPart((Vector3){0,0,-0.10f},(Vector3){1,0,0},-90*DEG2RAD,(Vector3){0.11f,2.20f,0.11f}));
    // ogive nose: cone tip (+Y) is rotated to -Z, so the warhead clearly leads
    DrawPart(P_CONE, C_GREY,  w, MPart((Vector3){0,0,-1.32f},(Vector3){1,0,0},-90*DEG2RAD,(Vector3){0.125f,0.58f,0.125f}));
    // four rear fins
    DrawPart(P_BOX, C_WHITE, w, MPart((Vector3){0, 0.17f,0.86f},(Vector3){0,0,1},0,(Vector3){0.03f,0.30f,0.44f}));
    DrawPart(P_BOX, C_WHITE, w, MPart((Vector3){0,-0.17f,0.86f},(Vector3){0,0,1},0,(Vector3){0.03f,0.30f,0.44f}));
    DrawPart(P_BOX, C_WHITE, w, MPart((Vector3){ 0.17f,0,0.86f},(Vector3){0,0,1},0,(Vector3){0.30f,0.03f,0.44f}));
    DrawPart(P_BOX, C_WHITE, w, MPart((Vector3){-0.17f,0,0.86f},(Vector3){0,0,1},0,(Vector3){0.30f,0.03f,0.44f}));
    // exhaust plume trailing toward the rear (+Z)
    DrawPart(P_CONE, C_ORANGE, w, MPart((Vector3){0,0,1.42f},(Vector3){1,0,0}, 90*DEG2RAD,(Vector3){0.09f,0.70f,0.09f}));
    DrawPart(P_CONE, C_YELLOW, w, MPart((Vector3){0,0,1.24f},(Vector3){1,0,0}, 90*DEG2RAD,(Vector3){0.05f,0.40f,0.05f}));
}
void DrawBomb(Vector3 pos, Quaternion q)
{
    Matrix w=root(pos,q,1.0f);
    DrawPart(P_SPHERE,C_NAVY,w,MPart((Vector3){0,0,0},(Vector3){0,0,1},0,(Vector3){0.50f,0.85f,0.50f}));
    DrawPart(P_CYL,  C_DARK,w,MPart((Vector3){0,0,0.72f},(Vector3){1,0,0},-90*DEG2RAD,(Vector3){0.22f,0.36f,0.22f}));
    DrawPart(P_BOX,C_DARK,w,MPart((Vector3){0, 0.22f,0.92f},(Vector3){0,0,1},0,(Vector3){0.03f,0.26f,0.30f}));
    DrawPart(P_BOX,C_DARK,w,MPart((Vector3){0,-0.22f,0.92f},(Vector3){0,0,1},0,(Vector3){0.03f,0.26f,0.30f}));
    DrawPart(P_BOX,C_DARK,w,MPart((Vector3){ 0.22f,0,0.92f},(Vector3){0,0,1},0,(Vector3){0.26f,0.03f,0.30f}));
    DrawPart(P_BOX,C_DARK,w,MPart((Vector3){-0.22f,0,0.92f},(Vector3){0,0,1},0,(Vector3){0.26f,0.03f,0.30f}));
}

// ------------------------------------------------------------- ground vehicles
void DrawVehicle(Vector3 pos, float yaw, int kind, float scale)
{
    Quaternion q=QuaternionFromAxisAngle((Vector3){0,1,0},yaw);
    Matrix M=root(pos,q,scale);
    const Vector3 X={1,0,0}, Z={0,0,1};
    if (kind==2) // tank
    {
        DrawPart(P_BOX,C_GI,M,MPart((Vector3){0,0.7f,0},X,0,(Vector3){3.0f,0.9f,5.2f}));
        DrawPart(P_BOX,C_DARKOLIVE,M,MPart((Vector3){0,0.45f,0},X,0,(Vector3){3.3f,0.5f,5.6f}));
        DrawPart(P_BOX,C_GI,M,MPart((Vector3){0,1.35f,-0.4f},X,0,(Vector3){2.0f,0.8f,2.4f}));
        DrawPart(P_CYL,C_DARK,M,MPart((Vector3){0,1.4f,-2.6f},X,90*DEG2R,(Vector3){0.12f,3.4f,0.12f}));
        for (int s=-1;s<=1;s+=2)
        for (int wz=-2;wz<=2;wz+=2)
            DrawPart(P_CYL,C_BLACK,M,MPart((Vector3){s*1.55f,0.45f,(float)wz},Z,90*DEG2R,(Vector3){0.5f,0.3f,0.5f}));
    }
    else if (kind==1) // AA truck
    {
        DrawPart(P_BOX,C_GI,M,MPart((Vector3){0,0.85f,0.8f},X,0,(Vector3){2.3f,1.1f,3.4f}));
        DrawPart(P_BOX,C_GI,M,MPart((Vector3){0,1.25f,-1.6f},X,0,(Vector3){2.2f,1.4f,1.6f}));
        DrawPart(P_BOX,C_GLASS,M,MPart((Vector3){0,1.4f,-2.0f},X,0,(Vector3){2.0f,0.7f,0.1f}));
        DrawPart(P_BOX,C_DARKOLIVE,M,MPart((Vector3){0,1.5f,1.4f},X,0,(Vector3){1.8f,0.4f,1.6f}));
        for (int b=-1;b<=1;b+=2)
        for (int g=-1;g<=1;g+=2)
            DrawPart(P_CYL,C_DARK,M,MPart((Vector3){b*0.3f,1.7f,1.0f+g*0.18f},X,-90*DEG2R,(Vector3){0.07f,2.4f,0.07f}));
        for (int s=-1;s<=1;s+=2)
        for (int wz=-1;wz<=1;wz+=2)
            DrawPart(P_CYL,C_BLACK,M,MPart((Vector3){s*1.2f,0.45f,(float)wz*1.7f},Z,90*DEG2R,(Vector3){0.55f,0.3f,0.55f}));
    }
    else if (kind==3) // supply tent + crates
    {
        DrawPart(P_CONE,C_SAND,M,MPart((Vector3){0,1.2f,0},X,0,(Vector3){2.6f,2.4f,3.2f}));
        DrawPart(P_BOX,C_BROWN,M,MPart((Vector3){2.2f,0.4f,1.5f},X,0,(Vector3){0.8f,0.8f,0.8f}));
        DrawPart(P_BOX,C_BROWN,M,MPart((Vector3){-2.1f,0.3f,-1.4f},X,0,(Vector3){0.6f,0.6f,0.6f}));
    }
    else // kind 0: clearly readable military 2.5-ton cargo truck (cab at -Z nose)
    {
        // ladder chassis + axles
        DrawPart(P_BOX,C_DARK,M,MPart((Vector3){0,0.55f,0.2f},X,0,(Vector3){2.0f,0.18f,4.6f}));
        DrawPart(P_CYL,C_DARK,M,MPart((Vector3){0,0.5f,-1.7f},Z,90*DEG2R,(Vector3){0.10f,2.5f,0.10f}));
        DrawPart(P_CYL,C_DARK,M,MPart((Vector3){0,0.5f, 1.9f},Z,90*DEG2R,(Vector3){0.10f,2.5f,0.10f}));
        // cab (front): nose, roof and sides leave obvious windows
        DrawPart(P_BOX,C_GI,M,MPart((Vector3){0,0.95f,-1.95f},X,0,(Vector3){2.25f,0.55f,0.6f}));   // hood
        DrawPart(P_BOX,C_GI,M,MPart((Vector3){0,1.55f,-1.35f},X,0,(Vector3){2.25f,0.9f,1.3f}));   // cab block
        DrawPart(P_BOX,C_GI,M,MPart((Vector3){0,2.05f,-1.35f},X,0,(Vector3){2.3f,0.16f,1.4f}));   // cab roof
        DrawPart(P_BOX,C_GLASS,M,MPart((Vector3){0,1.62f,-2.02f},X,0,(Vector3){1.9f,0.62f,0.08f})); // windshield
        DrawPart(P_BOX,C_GLASS,M,MPart((Vector3){ 1.13f,1.6f,-1.35f},X,0,(Vector3){0.06f,0.55f,1.0f})); // side windows
        DrawPart(P_BOX,C_GLASS,M,MPart((Vector3){-1.13f,1.6f,-1.35f},X,0,(Vector3){0.06f,0.55f,1.0f}));
        // grille + bumper + headlamps on the nose
        DrawPart(P_BOX,C_DARK,M,MPart((Vector3){0,1.0f,-2.27f},X,0,(Vector3){1.7f,0.4f,0.08f}));
        DrawPart(P_BOX,C_STEEL,M,MPart((Vector3){0,0.72f,-2.32f},X,0,(Vector3){2.3f,0.12f,0.12f}));
        DrawPart(P_SPHERE,C_YELLOW,M,MPart((Vector3){ 0.85f,1.0f,-2.32f},X,0,(Vector3){0.13f,0.13f,0.13f}));
        DrawPart(P_SPHERE,C_YELLOW,M,MPart((Vector3){-0.85f,1.0f,-2.32f},X,0,(Vector3){0.13f,0.13f,0.13f}));
        // cargo bed: low plank sides + tailgate so the bed shape is unmistakable
        DrawPart(P_BOX,C_WOOD,M,MPart((Vector3){0,0.95f,0.85f},X,0,(Vector3){2.2f,0.12f,3.0f}));
        DrawPart(P_BOX,C_GI,M,MPart((Vector3){ 1.12f,1.25f,0.85f},X,0,(Vector3){0.10f,0.55f,3.0f}));
        DrawPart(P_BOX,C_GI,M,MPart((Vector3){-1.12f,1.25f,0.85f},X,0,(Vector3){0.10f,0.55f,3.0f}));
        DrawPart(P_BOX,C_GI,M,MPart((Vector3){0,1.25f,2.32f},X,0,(Vector3){2.25f,0.55f,0.10f}));
        DrawPart(P_BOX,C_GI,M,MPart((Vector3){0,1.25f,-0.6f},X,0,(Vector3){2.25f,0.45f,0.10f}));
        // four black rubber wheels with light steel hubs
        for(int s=-1;s<=1;s+=2)
        for(int wz=-1;wz<=1;wz+=2)
        {
            DrawPart(P_CYL,C_BLACK,M,MPart((Vector3){s*1.12f,0.48f,(wz<0?-1.7f:1.9f)},Z,90*DEG2R,(Vector3){0.5f,0.3f,0.5f}));
            DrawPart(P_CYL,C_STEEL,M,MPart((Vector3){s*1.13f,0.48f,(wz<0?-1.7f:1.9f)},Z,90*DEG2R,(Vector3){0.22f,0.32f,0.22f}));
        }
    }
}

// ------------------------------------------------------------- first-person rifle
static Vector3 sMuzzle;
static float sSwingT=0;                 // >0 while a blade/fist swing is playing
#define SWING_DUR 0.28f
void Weapon_SwingTick(void){ sSwingT=SWING_DUR; Weapon_SwingTickNative(); }
void Weapon_AnimUpdate(float dt){ if(sSwingT>0){ sSwingT-=dt; if(sSwingT<0)sSwingT=0; } Weapon_AnimUpdateNative(dt); }

// own legs striding at the bottom of the view (so a charge reads as a run)
void DrawFirstPersonLegs(Camera3D cam, int moving, float phase)
{
    if(!moving) return;
    Vector3 f=vnorm((Vector3){cam.target.x-cam.position.x,0,cam.target.z-cam.position.z});
    Vector3 r=vnorm(vcross(f,cam.up));
    float sw=sinf(phase);
    Vector3 hip=vadd(cam.position, vadd(vmul(f,0.62f), vmul(cam.up,-1.02f)));
    for(int side=-1;side<=1;side+=2)
    {
        float s = side<0?sw:-sw;
        Quaternion qL=QuaternionMultiply(
            QuaternionFromAxisAngle((Vector3){0,1,0},atan2f(-f.x,-f.z)),
            QuaternionFromAxisAngle((Vector3){1,0,0}, s*0.55f));
        Vector3 c=vadd(hip, vmul(r,0.17f*side));
        DrawPart(P_BOX,C_PVA,MatrixMultiply(QuaternionToMatrix(qL),MatrixTranslate(c.x,c.y,c.z)),
                 MPart((Vector3){0,-0.42f,0},(Vector3){1,0,0},0,(Vector3){0.20f,0.84f,0.24f}));
        Vector3 fb=vadd(c,vmul(r,0.0f));
        DrawPart(P_BOX,C_BLACK,MatrixMultiply(QuaternionToMatrix(qL),MatrixTranslate(fb.x,fb.y,fb.z)),
                 MPart((Vector3){0,-0.86f,-0.05f},(Vector3){1,0,0},0,(Vector3){0.22f,0.16f,0.32f}));
    }
}

void DrawRifleView(Camera3D cam, int type, float kick, float reload01)
{
    // real embedded PBR weapons for Mosin / AKM / bayonet / RPG-7; fists stay procedural
    if((type<=2||type==4) && GunsNative_Ready()){ GunsNative_DrawView(cam,type,kick,reload01); return; }
    (void)reload01;
    Vector3 f=vnorm(vsub(cam.target,cam.position));
    Vector3 r=vnorm(vcross(f,cam.up));
    Vector3 u=cam.up;
    // recoil drives the held weapon back toward the camera and up a touch
    Vector3 grip=vadd(cam.position,
        vadd(vmul(f,0.42f+0.10f*kick),
        vadd(vmul(r,0.22f), vmul(u,-0.20f+0.045f*kick))));
    // yaw/pitch only transform for the held weapon (+ recoil pitch rise)
    Vector3 refF=f;
    float yaw=atan2f(-refF.x,-refF.z);
    float pitch=asinf(clampf(refF.y,-1,1)) + kick*0.10f;
    Quaternion q=QuaternionMultiply(QuaternionFromAxisAngle((Vector3){0,1,0},yaw),
                                    QuaternionFromAxisAngle((Vector3){1,0,0},pitch));
    // blade / fist swing: a fast raise-and-chop (blade) or jab (fists)
    if((type==2||type==3) && sSwingT>0)
    {
        float ph=1.0f-sSwingT/SWING_DUR;          // 0 start -> 1 end
        float beat=sinf(ph*M_PI);                 // 0..1..0
        if(type==2)
        {
            q=QuaternionMultiply(q,QuaternionFromAxisAngle((Vector3){1,0,0},-1.45f*beat)); // raise overhead then chop
            q=QuaternionMultiply(q,QuaternionFromAxisAngle((Vector3){0,1,0}, 0.6f*(ph-0.5f))); // diagonal slash
            grip=vadd(grip,vmul(f,0.22f*beat));
        }
        else
        {
            q=QuaternionMultiply(q,QuaternionFromAxisAngle((Vector3){1,0,0},-0.5f*beat));
            grip=vadd(grip,vmul(f,0.34f*beat));   // jab straight out
        }
    }
    Matrix M=MatrixMultiply(QuaternionToMatrix(q),MatrixTranslate(grip.x,grip.y,grip.z));
    if(type==0)
    {
        // Mosin-Nagant bolt-action rifle: long steel body + wood furniture
        DrawPart(P_BOX,C_STEEL,M,MPart((Vector3){0,0,-0.7f},(Vector3){1,0,0},0,(Vector3){0.10f,0.10f,1.9f}));
        DrawPart(P_BOX,C_BLACK,M,MPart((Vector3){0,0.02f,-1.75f},(Vector3){1,0,0},0,(Vector3){0.045f,0.045f,0.45f}));
        DrawPart(P_BOX,C_WOOD,M,MPart((Vector3){0,-0.02f,0.10f},(Vector3){1,0,0},0,(Vector3){0.16f,0.17f,1.05f}));
        DrawPart(P_BOX,C_WOOD,M,MPart((Vector3){0,-0.27f,0.42f},(Vector3){1,0,0},-20*DEG2R,(Vector3){0.13f,0.5f,0.16f}));
        DrawPart(P_BOX,C_STEEL,M,MPart((Vector3){0,0.09f,-1.45f},(Vector3){1,0,0},0,(Vector3){0.02f,0.09f,0.04f})); // front sight
        sMuzzle=vadd(grip, vmul(f,2.05f));
    }
    else if(type==1)
    {
        // AKM assault rifle: stamped receiver, long barrel, wood handguard,
        // gas tube, front post, slanted muzzle brake, curved banana magazine.
        DrawPart(P_BOX,C_STEEL,M,MPart((Vector3){0,0,-0.45f},(Vector3){1,0,0},0,(Vector3){0.12f,0.13f,0.95f}));
        DrawPart(P_BOX,C_BLACK,M,MPart((Vector3){0,0.0f,-1.35f},(Vector3){1,0,0},0,(Vector3){0.045f,0.045f,0.95f})); // barrel
        DrawPart(P_BOX,C_WOOD,M,MPart((Vector3){0,-0.03f,-0.95f},(Vector3){1,0,0},0,(Vector3){0.125f,0.11f,0.62f})); // handguard
        DrawPart(P_BOX,C_DARK, M,MPart((Vector3){0,0.08f,-1.05f},(Vector3){1,0,0},0,(Vector3){0.04f,0.04f,0.7f}));  // gas tube
        DrawPart(P_BOX,C_STEEL,M,MPart((Vector3){0,0.10f,-1.62f},(Vector3){1,0,0},0,(Vector3){0.018f,0.10f,0.04f})); // front post
        DrawPart(P_BOX,C_BLACK,M,MPart((Vector3){0,0.0f,-1.86f},(Vector3){1,0,0}, 12*DEG2R,(Vector3){0.055f,0.055f,0.16f})); // slant brake
        DrawPart(P_BOX,C_WOOD,M,MPart((Vector3){0,-0.02f,0.18f},(Vector3){1,0,0},0,(Vector3){0.15f,0.16f,0.55f}));  // pistol-grip area/wood
        DrawPart(P_BOX,C_WOOD,M,MPart((Vector3){0,-0.24f,0.45f},(Vector3){1,0,0},-16*DEG2R,(Vector3){0.12f,0.46f,0.15f})); // fixed stock
        // curved magazine: two stacked boxes, forward one angled -> banana
        DrawPart(P_BOX,C_DARK, M,MPart((Vector3){0,-0.20f,-0.15f},(Vector3){1,0,0},0,(Vector3){0.10f,0.34f,0.16f}));
        DrawPart(P_BOX,C_DARK, M,MPart((Vector3){0,-0.40f,-0.28f},(Vector3){1,0,0}, 10*DEG2R,(Vector3){0.095f,0.26f,0.15f}));
        sMuzzle=vadd(grip, vmul(f,2.0f));
    }
    else if(type==2)
    {
        // 大刀 (Chinese broadsword): wrapped grip, steel guard, long single-edged blade
        DrawPart(P_BOX,C_WOOD, M,MPart((Vector3){0,-0.02f,0.30f},(Vector3){1,0,0},0,(Vector3){0.07f,0.07f,0.34f})); // grip
        DrawPart(P_BOX,C_YELLOW, M,MPart((Vector3){0,0.02f,0.10f},(Vector3){1,0,0},0,(Vector3){0.26f,0.05f,0.06f})); // guard
        DrawPart(P_BOX,C_JETSILVER,M,MPart((Vector3){0,0.02f,-0.62f},(Vector3){1,0,0},0,(Vector3){0.10f,0.03f,1.5f})); // blade
        DrawPart(P_BOX,C_JETSILVER,M,MPart((Vector3){0,0.02f,-1.42f},(Vector3){1,0,0},-4*DEG2R,(Vector3){0.085f,0.03f,0.34f})); // curved tip
        sMuzzle=vadd(grip, vmul(f,1.7f));
    }
    else
    {
        // bare fists: two forearms + hands held low and forward
        DrawPart(P_BOX,C_PVA, M,MPart((Vector3){-0.14f,-0.08f,-0.18f},(Vector3){1,0,0},0,(Vector3){0.16f,0.16f,0.5f}));
        DrawPart(P_BOX,C_PVA, M,MPart((Vector3){ 0.14f,-0.08f,-0.18f},(Vector3){1,0,0},0,(Vector3){0.16f,0.16f,0.5f}));
        DrawPart(P_SPHERE,C_SKIN,M,MPart((Vector3){-0.14f,-0.10f,-0.46f},(Vector3){1,0,0},0,(Vector3){0.13f,0.13f,0.13f}));
        DrawPart(P_SPHERE,C_SKIN,M,MPart((Vector3){ 0.14f,-0.10f,-0.46f},(Vector3){1,0,0},0,(Vector3){0.13f,0.13f,0.13f}));
        sMuzzle=vadd(grip, vmul(f,1.2f));
    }
}
Vector3 RifleMuzzle(Camera3D cam, int type)
{
    if((type<=2||type==4) && GunsNative_Ready()) return GunsNative_MuzzlePoint(cam,type);
    (void)cam;(void)type; return sMuzzle;
}

// MK2-style fragmentation grenade: ovoid olive body with cast knurl ribs,
// steel neck, striker lever (spoon) and pull ring. Drawn at unit size (~0.14 m).
static void grenadeParts(Matrix M)
{
    // ovoid body
    DrawPart(P_SPHERE,C_DARKOLIVE,M,MPart(v3(0,0,0),(Vector3){1,0,0},0,v3(0.115f,0.145f,0.115f)));
    // cast fragmentation ribs: 6 vertical seams + 2 horizontal bands
    for(int i=0;i<6;i++)
    {
        float a=i*(PI/3.0f);
        Matrix R=MatrixMultiply(MPart(v3(0,0,0),(Vector3){0,1,0},a,v3(1,1,1)),M);
        DrawPart(P_BOX,C_DARK,R,MPart(v3(0.116f,0,0),(Vector3){1,0,0},0,v3(0.012f,0.22f,0.02f)));
    }
    DrawPart(P_CYL,C_DARK,M,MPart(v3(0,0.05f,0),(Vector3){1,0,0},0,v3(0.118f,0.018f,0.118f)));
    DrawPart(P_CYL,C_DARK,M,MPart(v3(0,-0.06f,0),(Vector3){1,0,0},0,v3(0.118f,0.018f,0.118f)));
    // steel neck / fuse
    DrawPart(P_CYL,C_STEEL,M,MPart(v3(0,0.165f,0),(Vector3){1,0,0},0,v3(0.05f,0.045f,0.05f)));
    // striker lever (spoon) running down one side
    DrawPart(P_BOX,C_STEEL,M,MPart(v3(0.04f,0.20f,0),(Vector3){0,0,1},0.25f,v3(0.018f,0.12f,0.05f)));
    // pull ring: a real split-ring standing on its side at the end of the fuse,
    // linked by a short straight wire to the striker.
    DrawPart(P_BOX,C_STEEL,M,MPart(v3(0.0f,0.168f,0.05f),(Vector3){1,0,0},0,v3(0.008f,0.008f,0.05f)));
    DrawPart(P_CYL,C_STEEL,M,MPart(v3(0.0f,0.150f,0.085f),(Vector3){1,0,0},0,v3(0.034f,0.012f,0.034f)));
}
void DrawGrenadeModel(Vector3 pos, float spin)
{
    Matrix M=MatrixMultiply(MPart(v3(0,0,0),(Vector3){0,1,0},spin,v3(1,1,1)),
                            MatrixTranslate(pos.x,pos.y,pos.z));
    grenadeParts(M);
}
void DrawGrenadeView(Camera3D cam, float pull)
{
    Vector3 f=vnorm(vsub(cam.target,cam.position));
    Vector3 r=vnorm(vcross(f,cam.up));
    Vector3 u=cam.up;
    // held chest-high; pull raises it into view as the pin is readied
    Vector3 grip=vadd(cam.position,
        vadd(vmul(f,0.46f+0.18f*pull),
        vadd(vmul(r,0.04f), vmul(u,-0.16f+0.10f*pull))));
    float yaw=atan2f(-f.x,-f.z), pitch=asinf(clampf(f.y,-1,1))+0.35f;
    Quaternion q=QuaternionMultiply(QuaternionFromAxisAngle((Vector3){0,1,0},yaw),
                                    QuaternionFromAxisAngle((Vector3){1,0,0},pitch));
    Matrix M=MatrixMultiply(QuaternionToMatrix(q),MatrixTranslate(grip.x,grip.y,grip.z));
    // supporting hand cupping the bottom of the grenade (palm + curled fingers + thumb)
    DrawPart(P_SPHERE,C_SKIN,M,MPart(v3(0,-0.15f,0),(Vector3){1,0,0},0,v3(0.085f,0.06f,0.085f)));
    for(int i=-1;i<=1;i++)
        DrawPart(P_CYL,C_SKIN,M,MPart(v3(i*0.05f,-0.075f,0.085f),(Vector3){0,0,1},0,v3(0.026f,0.10f,0.026f)));
    DrawPart(P_CYL,C_SKIN,M,MPart(v3(0.09f,-0.09f,0.02f),(Vector3){0,0,1},0.5f,v3(0.026f,0.09f,0.026f)));
    grenadeParts(M);
}

// ---------------------------------------------------------------- particles
typedef struct { Vector3 p,v; float life,max,size,grow; int tex; Color col; int dead; } P;
#define MAXP 600
static P ps[MAXP];
int gActiveParticles=0;

typedef struct { Vector3 a,b; float life,max; Color c; int dead; } Tr;
static Tr trs[MAX_TRACERS];

void FX_Init(void){ memset(ps,0,sizeof(ps)); memset(trs,0,sizeof(trs)); }

static void spawn(P x){ for(int i=0;i<MAXP;i++) if(ps[i].dead){ps[i]=x;ps[i].dead=0;return;} }

void FX_Explosion(Vector3 p, float sc)
{
    FX_Fireball(p,sc);
    int n=(int)(22*sc)+12;
    for (int i=0;i<n;i++)
    {
        P k={0}; k.p=p;
        float a=frand(0,2*M_PI), e=frand(-0.2f,1.0f);
        k.v=(Vector3){cosf(a)*frand(8,34)*sc, frand(6,30)*sc+e*10, sinf(a)*frand(8,34)*sc};
        k.life=k.max=frand(0.5f,1.1f); k.size=frand(2.0f,5.0f)*sc; k.grow=frand(2,7)*sc;
        k.tex=1; k.col= i%3? (Color){255,(unsigned char)irand(120,200),40,235}:(Color){255,235,170,235};
        spawn(k);
    }
    for (int i=0;i<10*sc+6;i++)
    {
        P k={0}; k.p=(Vector3){p.x+frand(-2,2),p.y+frand(-1,2),p.z+frand(-2,2)};
        k.v=(Vector3){frand(-4,4),frand(6,18),frand(-4,4)};
        k.life=k.max=frand(1.2f,2.4f); k.size=frand(4,8)*sc; k.grow=frand(5,12)*sc;
        k.tex=0; k.col=(Color){70,68,66,150}; spawn(k);
    }
}
void FX_Fireball(Vector3 p, float sc)
{
    P k={0}; k.p=p; k.v=(Vector3){0,4,0}; k.life=k.max=0.35f; k.size=9*sc; k.grow=26*sc; k.tex=1; k.col=(Color){255,210,120,255}; spawn(k);
}
// persistent flame for a burning wreck: longer-lived, flickering tongues
void FX_FireLong(Vector3 p, float sc)
{
    P k={0}; k.p=(Vector3){p.x+frand(-0.5,0.5)*sc,p.y,p.z+frand(-0.5,0.5)*sc};
    k.v=(Vector3){frand(-1.2,1.2),frand(5,10),frand(-1.2,1.2)};
    k.life=k.max=frand(0.7f,1.3f); k.size=frand(2.2f,3.6f)*sc; k.grow=3.0f*sc; k.tex=1;
    k.col=(irand(0,2)?(Color){255,150,40,235}:(Color){255,210,110,235}); spawn(k);
}
void FX_Smoke(Vector3 p, float sc)
{
    P k={0}; k.p=p; k.v=(Vector3){frand(-2,2),frand(6,14),frand(-2,2)}; k.life=k.max=frand(1.4f,2.6f);
    k.size=frand(3,6)*sc; k.grow=frand(6,12)*sc; k.tex=0; k.col=(Color){60,60,60,130}; spawn(k);
}
void FX_EngineSmoke(Vector3 p){ FX_Smoke(p,0.6f); }
void FX_FlakBurst(Vector3 p)
{
    for (int i=0;i<10;i++){ P k={0}; k.p=p; float a=frand(0,2*M_PI);
        k.v=(Vector3){cosf(a)*frand(4,14),frand(2,12),sinf(a)*frand(4,14)};
        k.life=k.max=frand(0.3f,0.7f); k.size=frand(1.5f,3.5f); k.grow=4; k.tex=0; k.col=(Color){40,40,44,180}; spawn(k);}
    for (int i=0;i<6;i++){ P k={0}; k.p=p; float a=frand(0,2*M_PI);
        k.v=(Vector3){cosf(a)*frand(10,30),frand(8,30),sinf(a)*frand(10,30)};
        k.life=k.max=frand(0.2f,0.5f); k.size=frand(0.6f,1.4f); k.grow=0; k.tex=1; k.col=(Color){255,200,90,220}; spawn(k);}
}
void FX_Muzzle(Vector3 p)
{
    P k={0}; k.p=p; k.v=(Vector3){0,0,0}; k.life=k.max=0.07f; k.size=1.1f; k.grow=1.5f; k.tex=1; k.col=(Color){255,230,150,255}; spawn(k);
}
void FX_Blood(Vector3 p)
{
    for(int i=0;i<14;i++){ P k={0}; k.p=p; float a=frand(0,2*M_PI);
        k.v=(Vector3){cosf(a)*frand(1,6),frand(2,9),sinf(a)*frand(1,6)};
        k.life=k.max=frand(0.25f,0.6f); k.size=frand(1.0f,2.6f); k.grow=1.5f;
        k.tex=0; k.col=(Color){(unsigned char)irand(110,150),(unsigned char)irand(10,22),(unsigned char)irand(10,18),235}; spawn(k);}
}
void FX_Tracer(Vector3 a, Vector3 b, Color c, float life)
{
    for(int i=0;i<MAX_TRACERS;i++) if(trs[i].dead){trs[i].a=a;trs[i].b=b;trs[i].c=c;trs[i].life=trs[i].max=life;trs[i].dead=0;return;}
}

void FX_Update(float dt)
{
    gActiveParticles=0;
    for (int i=0;i<MAXP;i++) if(!ps[i].dead)
    {
        P*k=&ps[i]; k->life-=dt; if(k->life<=0){k->dead=1;continue;}
        k->v.y-= (k->tex==1?6.0f:3.0f)*dt;
        k->v.x*=(1-1.4f*dt); k->v.z*=(1-1.4f*dt);
        k->p=vadd(k->p,vmul(k->v,dt)); k->size+=k->grow*dt; gActiveParticles++;
    }
    for (int i=0;i<MAX_TRACERS;i++) if(!trs[i].dead){ trs[i].life-=dt; if(trs[i].life<=0)trs[i].dead=1; }
}

void FX_Draw3D(Camera3D cam)
{
    // additive fire/sparks
    BeginBlendMode(BLEND_ADDITIVE);
    for (int i=0;i<MAXP;i++) if(!ps[i].dead && ps[i].tex==1)
    { P*k=&ps[i]; float a=k->life/k->max; Color c=k->col; c.a=(unsigned char)(k->col.a*a);
      DrawBillboard(cam,texFire,k->p,k->size,c); }
    EndBlendMode();
    // alpha smoke/glow
    BeginBlendMode(BLEND_ALPHA);
    for (int i=0;i<MAXP;i++) if(!ps[i].dead && ps[i].tex==0)
    { P*k=&ps[i]; float a=k->life/k->max; Color c=k->col; c.a=(unsigned char)(k->col.a*a);
      DrawBillboard(cam,texGlow,k->p,k->size,c); }
    EndBlendMode();
    // tracers
    BeginBlendMode(BLEND_ADDITIVE);
    for (int i=0;i<MAX_TRACERS;i++) if(!trs[i].dead)
    { Tr*t=&trs[i]; float a=t->life/t->max; Color c=t->c; c.a=(unsigned char)(255*a); DrawLine3D(t->a,t->b,c); }
    EndBlendMode();
}

// ---------------------------------------------------------------- clouds / sun
typedef struct { Vector3 p; float s,sp; } Cloud;
#define NCLOUD 16
static Cloud clouds[NCLOUD];
void Env_Load(void)
{
    for (int i=0;i<NCLOUD;i++)
        clouds[i]=(Cloud){{frand(-WORLD_HALF,WORLD_HALF),frand(300,520),frand(-WORLD_HALF,WORLD_HALF)},frand(90,200),frand(4,12)};
}
void Env_Update(float dt)
{
    for (int i=0;i<NCLOUD;i++){ clouds[i].p.x+=clouds[i].sp*dt; if(clouds[i].p.x>WORLD_HALF+200) clouds[i].p.x=-WORLD_HALF-200; }
}
// Vertical sky gradient drawn in 2D right before the 3D pass (BeginMode3D
// only clears depth, so the gradient stays as the backdrop). Cold zenith to a
// slightly warm, hazy horizon gives the flat clear colour real depth.
void Env_DrawSky2D(void)
{
    int sw=GetScreenWidth(), sh=GetScreenHeight();
    Color zen,mid,hor;
    // v1.9: every theatre is a war zone — the sky itself is stained by smoke,
    // ash drifting over the horizon, never a clean peacetime blue.
    if(Map_IsNight())
    {   // cold moonlit night veiled in gunsmoke
        zen=(Color){10,12,22,255}; mid=(Color){26,28,40,255}; hor=(Color){74,64,62,255};
    }
    else if(Map_IsDusk())
    {   // burning dusk: ember horizon under banks of black smoke
        zen=(Color){34,32,40,255}; mid=(Color){104,66,56,255}; hor=(Color){206,108,60,255};
    }
    else
    {   // daylight choked by battle smoke: slate zenith, ash-brown horizon
        zen=(Color){66,70,76,255}; mid=(Color){108,102,96,255}; hor=(Color){176,150,118,255};
    }
    const int BANDS=40;
    int bh=sh/BANDS+1;
    for(int i=0;i<BANDS;i++)
    {
        float t=(float)i/(BANDS-1);       // 0 top, 1 bottom
        Color a,b; float k;
        if(t<0.68f){ a=zen; b=mid; k=t/0.68f; }
        else       { a=mid; b=hor; k=(t-0.68f)/0.32f; }
        Color c=(Color){ (unsigned char)(a.r+(b.r-a.r)*k),
                         (unsigned char)(a.g+(b.g-a.g)*k),
                         (unsigned char)(a.b+(b.b-a.b)*k),255};
        DrawRectangle(0,i*bh,sw,bh+1,c);
    }
}

// national flag (PRC) and PLA "August 1st" flag, pinned to the upper-left HUD
static int inPolyHud(float px,float py,const float*vx,const float*vy,int n)
{
    int in=0;
    for(int i=0,j=n-1;i<n;j=i++)
        if(((vy[i]>py)!=(vy[j]>py)) &&
           (px < (vx[j]-vx[i])*(py-vy[i])/(vy[j]-vy[i]+1e-9f)+vx[i])) in=!in;
    return in;
}
static Texture2D hudStarTex(void)
{
    static Texture2D tx=(Texture2D){0};
    if(tx.id) return tx;
    const int S=64; Image im=GenImageColor(S,S,BLANK);
    unsigned char*d=im.data; float vx[10],vy[10];
    for(int i=0;i<10;i++){ float a=-M_PI/2+i*M_PI/5, rr=(i&1)?S*0.20f:S*0.46f;
        vx[i]=S*0.5f+cosf(a)*rr; vy[i]=S*0.5f+sinf(a)*rr; }
    for(int y=0;y<S;y++)for(int x=0;x<S;x++)
        if(inPolyHud(x+0.5f,y+0.5f,vx,vy,10)){ int o=(y*S+x)*4; d[o]=255;d[o+1]=224;d[o+2]=82;d[o+3]=255; }
    tx=LoadTextureFromImage(im); UnloadImage(im); return tx;
}
static void drawStar2D(float cx,float cy,float r)
{
    Texture2D t=hudStarTex();
    DrawTexturePro(t,(Rectangle){0,0,64,64},(Rectangle){cx-r,cy-r,2*r,2*r},(Vector2){0,0},0,WHITE);
}
void DrawCornerFlags(void)
{
    int w=56,h=38,x0=108,y=10;
    Color red=(Color){222,41,16,255};
    const char* name[2]={"五星红旗","八一军旗"};
    for(int f=0;f<2;f++)
    {
        int x=x0+f*(w+46);
        DrawRectangle(x,y,w,h,red);
        DrawRectangleLines(x,y,w,h,(Color){255,220,150,200});
        float cx=x+w*0.26f, cy=y+h*0.38f;
        if(f==0)
        {
            drawStar2D(cx,cy,h*0.30f);
            static const float sp[4][2]={{0.46f,0.16f},{0.55f,0.34f},{0.55f,0.56f},{0.46f,0.72f}};
            for(int i=0;i<4;i++) drawStar2D(x+w*sp[i][0],y+h*sp[i][1],h*0.10f);
        }
        else
        {
            drawStar2D(cx,cy,h*0.26f);
            DrawTextEx(GameFont(),"八一",(Vector2){cx+7,cy-7},13,0,(Color){255,224,82,255});
        }
        Vector2 sz=MeasureTextEx(GameFont(),name[f],10,1);
        DrawRectangle((int)(x+(w-sz.x)*0.5f-2),y+h+1,(int)sz.x+4,13,(Color){0,0,0,90});
        DrawTextEx(GameFont(),name[f],(Vector2){x+(w-sz.x)*0.5f,(float)y+h+2},10,1,(Color){255,235,200,240});
    }
}

// ------------------------------------------------- 3D waving national flag
static Texture2D texFlag3D[3]={{0}};

static int ptInPoly(float px,float py,const float*vx,const float*vy,int n)
{
    int in=0;
    for(int i=0,j=n-1;i<n;j=i++)
        if(((vy[i]>py)!=(vy[j]>py)) &&
           (px < (vx[j]-vx[i])*(py-vy[i])/(vy[j]-vy[i]+1e-9f)+vx[i])) in=!in;
    return in;
}

// plot a five-point star centered (cx,cy) radius R, rotation rot, into RGBA image
static void plotStar(unsigned char*d,int W,int H,float cx,float cy,float R,float rot,Color c)
{
    float vx[10],vy[10];
    for(int i=0;i<10;i++)
    {
        float a=rot-M_PI*0.5f+i*((float)M_PI/5.0f);
        float rr=(i%2==0)?R:R*0.42f;
        vx[i]=cx+cosf(a)*rr; vy[i]=cy+sinf(a)*rr;
    }
    int x0=(int)(cx-R), x1=(int)(cx+R), y0=(int)(cy-R), y1=(int)(cy+R);
    for(int y=y0;y<=y1;y++)for(int x=x0;x<=x1;x++)
    {
        if(x<0||y<0||x>=W||y>=H)continue;
        if(ptInPoly(x+0.5f,y+0.5f,vx,vy,10))
        { int o=(y*W+x)*4; d[o]=c.r;d[o+1]=c.g;d[o+2]=c.b;d[o+3]=255; }
    }
}

static Texture2D FlagTexture3D(int kind)
{
    if(texFlag3D[kind].id) return texFlag3D[kind];
    const int W=150,H=100;
    Image img=GenImageColor(W,H,(Color){222,41,16,255});
    unsigned char*d=img.data;
    Color yel=(Color){255,222,90,255};
    float ux=W*0.20f, uy=H*0.32f, ur=H*0.18f;
    plotStar(d,W,H,ux,uy,ur,0.0f,yel);
    if(kind==0)
    {
        // four small stars on the national flag, points aimed toward the big one
        const float sp[4][2]={{0.36f,0.16f},{0.45f,0.32f},{0.45f,0.52f},{0.36f,0.66f}};
        for(int i=0;i<4;i++){ float sx=W*sp[i][0],sy=H*sp[i][1];
            float rot=atan2f(uy-sy,ux-sx)+M_PI*0.5f;
            plotStar(d,W,H,sx,sy,H*0.062f,rot,yel); }
    }
    else if(kind==1)
    {   // PLA "August 1st" — numerals beside the star
        ImageDrawText(&img,"81",(int)(ux-10),(int)(uy-12),34,yel);
    }
    else
    {   // US colours: thirteen stripes + blue canton with white stars
        ImageClearBackground(&img,(Color){180,30,30,255});
        int stripeH=H/13;
        for(int s=0;s<13;s++) if(s&1)
            ImageDrawRectangle(&img,0,s*stripeH,W,stripeH,(Color){240,240,240,255});
        int cw=(int)(W*0.42f), ch=stripeH*7;
        ImageDrawRectangle(&img,0,0,cw,ch,(Color){40,48,96,255});
        for(int ry=0;ry<4;ry++)for(int rx=0;rx<5;rx++)
        { float sx=(rx+0.55f)*cw/5.0f, sy=(ry+0.6f)*ch/4.0f;
            plotStar(d,W,H,sx,sy,H*0.035f,0,(Color){245,245,245,255}); }
    }
    texFlag3D[kind]=LoadTextureFromImage(img);
    UnloadImage(img);
    return texFlag3D[kind];
}

// Waving cloth rendered as ONE continuous subdivided mesh (shared vertices),
// so the flag never splits into strips. Vertices are re-uploaded every frame;
// droop (radians) folds the sheet about its attached edge (0 = flying out,
// 90deg = hanging straight down).
#define FL_NX 26
#define FL_NY 7
typedef struct {
    Mesh mesh; Material mat; int built;
    float verts[(FL_NX+1)*(FL_NY+1)*3];
} FlagSheet;
static FlagSheet gSheets[6]; static int gSheetCursor=0;

static void clothStrips(Camera3D cam,Texture2D tex,Vector3 attach,Vector3 dF,
                        float t,float FH,float FW,float droop)
{
    (void)cam;
    FlagSheet*fs=&gSheets[gSheetCursor]; gSheetCursor=(gSheetCursor+1)%6;
    Mesh*m=&fs->mesh;
    int nx=FL_NX, ny=FL_NY, nv=(nx+1)*(ny+1);
    if(!fs->built)
    {
        m->vertexCount=nv; m->triangleCount=nx*ny*2;
        m->vertices=(float*)MemAlloc(sizeof(float)*3*nv);
        m->normals =(float*)MemAlloc(sizeof(float)*3*nv);
        m->texcoords=(float*)MemAlloc(sizeof(float)*2*nv);
        m->colors  =(unsigned char*)MemAlloc(sizeof(unsigned char)*4*nv);
        m->indices=(unsigned short*)MemAlloc(sizeof(unsigned short)*nx*ny*6);
        int vi=0;
        for(int iy=0;iy<=ny;iy++)for(int ix=0;ix<=nx;ix++)
        {
            m->texcoords[vi*2+0]=(float)ix/nx;
            m->texcoords[vi*2+1]=(float)iy/ny;   // top edge samples the image top
            m->colors[vi*4+0]=255; m->colors[vi*4+1]=255;
            m->colors[vi*4+2]=255; m->colors[vi*4+3]=255;
            vi++;
        }
        int ii=0;
        for(int iy=0;iy<ny;iy++)for(int ix=0;ix<nx;ix++)
        {
            unsigned short a=(unsigned short)(iy*(nx+1)+ix), b=(unsigned short)(a+1),
                           c=(unsigned short)(a+nx+1), d=(unsigned short)(c+1);
            m->indices[ii++]=a;m->indices[ii++]=c;m->indices[ii++]=b;
            m->indices[ii++]=b;m->indices[ii++]=c;m->indices[ii++]=d;
        }
        UploadMesh(m,1);   // dynamic: re-uploaded each frame
        fs->mat=LoadMaterialDefault();
        if(gLit.id>0)fs->mat.shader=gLit;
        fs->mat.maps[MATERIAL_MAP_DIFFUSE].texture=tex;
        fs->built=1;
    }
    fs->mat.maps[MATERIAL_MAP_DIFFUSE].texture=tex;

    float cs=cosf(droop), sn=sinf(droop);
    Vector3 up=(Vector3){0,1,0};
    Vector3 side=vnorm(vcross(dF,up));     // axis across the sheet
    Vector3 A=(Vector3){attach.x, attach.y+FH*0.5f, attach.z};  // top outer corner
    int vi=0;
    for(int iy=0;iy<=ny;iy++)
    {
        float v=(float)iy/ny;              // 0 top edge -> 1 bottom edge
        for(int ix=0;ix<=nx;ix++)
        {
            float u=(float)ix/nx*FW;       // distance out from the pole
            float free=u/FW;
            float flut=sinf(t*5.5f-ix*0.5f+iy*0.25f)*0.16f*free*(1.0f-0.25f*sn);
            float bulge=cosf(t*3.6f-ix*0.42f)*0.20f*free;
            // rigid fold about the side axis + wind ripple
            float ex=u*cs - v*FH*sn + bulge;          // along dF
            float ey=-(v*FH*cs - u*sn) + flut;        // vertical (down positive)
            m->vertices[vi*3+0]=A.x+dF.x*ex+side.x*flut*0.35f;
            m->vertices[vi*3+1]=A.y+ey;
            m->vertices[vi*3+2]=A.z+dF.z*ex+side.z*flut*0.35f;
            vi++;
        }
    }
    // smooth normals from grid tangents
    for(int iy=0;iy<=ny;iy++)for(int ix=0;ix<=nx;ix++)
    {
        int i=iy*(nx+1)+ix;
        int i0=i-(ix>0?1:0), i1=i+(ix<nx?1:0);
        int j0=i-(iy>0?nx+1:0), j1=i+(iy<ny?nx+1:0);
        Vector3 pu=vsub(((Vector3*)m->vertices)[i1],((Vector3*)m->vertices)[i0]);
        Vector3 pv=vsub(((Vector3*)m->vertices)[j1],((Vector3*)m->vertices)[j0]);
        Vector3 n=vnorm(vcross(pu,pv));
        m->normals[i*3+0]=n.x; m->normals[i*3+1]=n.y; m->normals[i*3+2]=n.z;
    }
    UpdateMeshBuffer(*m,0,m->vertices,sizeof(float)*3*nv,0);
    UpdateMeshBuffer(*m,2,m->normals, sizeof(float)*3*nv,0);
    rlDisableBackfaceCulling();   // cloth is seen from both sides
    DrawMesh(*m,fs->mat,MatrixIdentity());
    rlEnableBackfaceCulling();
}

void DrawWavingFlag(Camera3D cam,Vector3 base,float yaw,int kind,float t,float poleH)
{
    // pole
    DrawPart(P_CYL,C_DARK,MatrixIdentity(),
        MPart((Vector3){base.x,base.y+poleH*0.5f,base.z},(Vector3){1,0,0},0,(Vector3){0.10f,poleH,0.10f}));
    DrawPart(P_SPHERE,C_YELLOW,MatrixIdentity(),
        MPart((Vector3){base.x,base.y+poleH+0.12f,base.z},(Vector3){1,0,0},0,(Vector3){0.16f,0.16f,0.16f}));
    const float FW=4.2f,FH=2.7f;
    Vector3 dF=(Vector3){cosf(yaw),0,-sinf(yaw)};
    Vector3 attach=(Vector3){base.x,base.y+poleH-FH*0.5f-0.05f,base.z};
    clothStrips(cam,FlagTexture3D(kind),attach,dF,t,FH,FW,0.0f);
}

// objective crest: enemy colours fly until captured; on capture they fold 90
// degrees while the Five-star Red Flag is hoisted and catches the wind.
void DrawObjectiveFlags(Camera3D cam,Vector3 base,int captured,float fallT,float t)
{
    const float poleH=9.0f;
    DrawPart(P_CYL,C_DARK,MatrixIdentity(),
        MPart((Vector3){base.x,base.y+poleH*0.5f,base.z},(Vector3){1,0,0},0,(Vector3){0.13f,poleH,0.13f}));
    DrawPart(P_SPHERE,C_YELLOW,MatrixIdentity(),
        MPart((Vector3){base.x,base.y+poleH+0.15f,base.z},(Vector3){1,0,0},0,(Vector3){0.2f,0.2f,0.2f}));
    Vector3 dF=(Vector3){1,0,0};
    // enemy colours fly until they are struck, then fold down over ~1.2s
    if(fallT<1.5f)
    {
        float droop=clampf(fallT/1.2f,0,1)*(M_PI*0.5f);
        Vector3 ea=(Vector3){base.x,base.y+poleH-1.5f,base.z};
        clothStrips(cam,FlagTexture3D(2),ea,dF,t,2.7f,4.4f,droop);
    }
    // our flag is hoisted up the pole once theirs starts falling
    if(captured && fallT>0.35f)
    {
        float hh=clampf((fallT-0.35f)/1.3f,0,1);
        float yTop=base.y+1.6f+hh*(poleH-3.6f);
        Vector3 oa=(Vector3){base.x,yTop,base.z+0.05f};
        clothStrips(cam,FlagTexture3D(0),oa,dF,t,2.4f,3.8f,0.0f);
    }
}

void Env_Draw(Camera3D cam)
{
    Vector3 sunPos=vadd(cam.position, vmul(vnorm(SUN_DIR),-6800.0f));
    BeginBlendMode(BLEND_ADDITIVE);
    if(Map_IsNight())
    {   // pale moon + cold halo
        DrawBillboardPro(cam,texGlow,(Rectangle){0,0,64,64},sunPos,(Vector3){0,1,0},(Vector2){220,220},(Vector2){110,110},0,(Color){226,234,255,255});
        DrawBillboardPro(cam,texGlow,(Rectangle){0,0,64,64},sunPos,(Vector3){0,1,0},(Vector2){520,520},(Vector2){260,260},0,(Color){150,170,220,60});
    }
    else if(Map_IsDusk())
    DrawBillboardPro(cam,texFire,(Rectangle){0,0,64,64},sunPos,(Vector3){0,1,0},(Vector2){520,520},(Vector2){260,260},0,(Color){255,150,90,255});
    else if(Map_IsScorch())
    DrawBillboardPro(cam,texFire,(Rectangle){0,0,64,64},sunPos,(Vector3){0,1,0},(Vector2){520,520},(Vector2){260,260},0,(Color){235,214,180,210});
    else
    DrawBillboardPro(cam,texFire,(Rectangle){0,0,64,64},sunPos,(Vector3){0,1,0},(Vector2){520,520},(Vector2){260,260},0,(Color){255,250,225,255});
    EndBlendMode();
    BeginBlendMode(BLEND_ALPHA);
    for (int i=0;i<NCLOUD;i++)
    {
        float w=clouds[i].s*2.4f, h=clouds[i].s*0.85f;
        Color c = Map_IsNight() ? (Color){120,124,140,60}
                : Map_IsDusk()  ? (Color){96,80,72,128}
                : (Color){86,82,78,135};
        DrawBillboardPro(cam,texGlow,(Rectangle){0,0,64,64},clouds[i].p,(Vector3){0,1,0},(Vector2){w,h},(Vector2){w*0.5f,h*0.5f},0,c);
        Vector3 p2=(Vector3){clouds[i].p.x+clouds[i].s*0.7f,clouds[i].p.y+18,clouds[i].p.z+60};
        DrawBillboardPro(cam,texGlow,(Rectangle){0,0,64,64},p2,(Vector3){0,1,0},(Vector2){w*0.7f,h*0.6f},(Vector2){w*0.35f,h*0.3f},0,(Color){150,140,128,90});
    }
    EndBlendMode();
}

// ---------------------------------------------------------------- procedural SFX
#include "raylib.h"
static Sound sGun,sBoom,sFlak,sBugle,sEngine,sFoot;
static int audioOk=0, engineOn=0;
static Wave makeWave(int frames, void(*fill)(short*,int,float))
{
    Wave w={0}; w.sampleRate=22050; w.sampleSize=16; w.channels=1; w.frameCount=frames;
    short*buf=(short*)malloc(frames*sizeof(short)); fill(buf,frames,w.sampleRate); w.data=buf; return w;
}
static float nseed=1234.0f;
static float nz(void){ nseed=fmodf(nseed*1.682f+0.371f,1.0f); return nseed*2-1; }

static void fGun(short*b,int n,float sr){ float lp=0; for(int i=0;i<n;i++){float t=i/sr;float e=expf(-t*26);lp=lp*0.55f+nz()*0.45f;
    float v=e*(lp*0.9f+sinf(2*M_PI*92*t)*0.35f); b[i]=(short)(v*22000);} }
static void fBoom(short*b,int n,float sr){ float lp=0; for(int i=0;i<n;i++){float t=i/sr;float e=expf(-t*4.2f);lp=lp*0.86f+nz()*0.14f;
    float v=e*(lp*0.95f+sinf(2*M_PI*46*t)*0.6f); b[i]=(short)(v*26000);} }
static void fFlak(short*b,int n,float sr){ for(int i=0;i<n;i++){float t=i/sr;float e=expf(-t*16);
    float v=e*(nz()*0.7f+sinf(2*M_PI*220*t)*0.2f); b[i]=(short)(v*16000);} }
static void fBugle(short*b,int n,float sr)
{
    float notes[]={392.0f,523.25f,659.25f,783.99f,659.25f,783.99f};
    for(int i=0;i<n;i++){ float t=i/sr; float beat=fmodf(t,0.28f); int idx=(int)(t/0.28f); if(idx>5)idx=5;
        float env=(beat<0.03f)?beat/0.03f:1.0f; if(beat>0.24f)env=(0.28f-beat)/0.04f;
        float f=notes[idx]; float v=env*(sinf(2*M_PI*f*t)*0.6f+sinf(2*M_PI*2*f*t)*0.25f)*0.5f; b[i]=(short)(v*18000);}
}
static void fEng(short*b,int n,float sr){ float lp=0; for(int i=0;i<n;i++){float ph=fmodf(i*70.0f/sr,1.0f);float saw=ph*2-1;
    lp=lp*0.9f+(nz()*0.5f+saw*0.5f)*0.1f; b[i]=(short)(lp*5000);} }
static void fFoot(short*b,int n,float sr){ float lp=0; for(int i=0;i<n;i++){float t=i/sr;float e=expf(-t*22);lp=lp*0.5f+nz()*0.5f;
    float v=e*lp*0.7f; b[i]=(short)(v*9000);} }

void Sfx_Load(void)
{
    InitAudioDevice();
    if (!IsAudioDeviceReady()) { audioOk=0; return; }
    audioOk=1;
    Wave w;
    w=makeWave((int)(22050*0.22f),fGun); sGun=LoadSoundFromWave(w); UnloadWave(w);
    w=makeWave((int)(22050*0.95f),fBoom); sBoom=LoadSoundFromWave(w); UnloadWave(w);
    w=makeWave((int)(22050*0.28f),fFlak); sFlak=LoadSoundFromWave(w); UnloadWave(w);
    w=makeWave((int)(22050*1.7f),fBugle); sBugle=LoadSoundFromWave(w); UnloadWave(w);
    w=makeWave(22050,fEng); sEngine=LoadSoundFromWave(w); UnloadWave(w);
    w=makeWave((int)(22050*0.13f),fFoot); sFoot=LoadSoundFromWave(w); UnloadWave(w);
    SetSoundVolume(sBoom,0.9f); SetSoundVolume(sGun,0.55f); SetSoundVolume(sFlak,0.5f); SetSoundVolume(sBugle,0.7f); SetSoundVolume(sFoot,0.4f);
}
void Sfx_Unload(void){ if(!audioOk)return; if(engineOn)StopSound(sEngine); CloseAudioDevice(); }
void Sfx_Gun(void){ if(audioOk)PlaySound(sGun); }
void Sfx_Boom(float v){ if(audioOk){SetSoundVolume(sBoom,clampf(v,0.1f,1.0f));PlaySound(sBoom);} }
void Sfx_Flak(void){ if(audioOk)PlaySound(sFlak); }
void Sfx_Bugle(void){ if(audioOk)PlaySound(sBugle); }
void Sfx_Foot(void){ if(audioOk)PlaySound(sFoot); }
void Sfx_Engine(float th, int on)
{
    if(!audioOk)return;
    if(on){ if(!engineOn){PlaySound(sEngine);engineOn=1;} SetSoundPitch(sEngine,0.7f+th*0.8f); SetSoundVolume(sEngine,0.04f+th*0.1f); }
    else if(engineOn){StopSound(sEngine);engineOn=0;}
}
