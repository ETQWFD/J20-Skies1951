// terrain.c - procedural Korean-peninsula style heightmap (Perlin/fBm/ridged)
#include "common.h"
#include "noise.h"

#define TER_SEG 168
#define TER_SIZE (WORLD_HALF*2.0f)

Model gTerrain = (Model){0};
int   gTerrainReady = 0;
static Model gSea = (Model){0};

// one row per campaign (gScenario index): noise offset makes genuinely
// different ground; ridgeMul/northMul shape the theatre's terrain.
typedef struct { float ox,oz,ridgeMul,northMul,eastMul,flatR; } MapCfg;
static const MapCfg MAPS[27]={
    {  0.0f,   0.0f, 225.0f,1.00f,1.00f,170.0f}, // 0  温井伏击战 昼·山谷
    { 41.7f, -23.3f, 235.0f,1.05f,0.85f,150.0f}, // 1  云山攻坚战 昼·丘陵
    {-67.4f,  35.9f, 258.0f,1.18f,0.70f,150.0f}, // 2  长津湖·冰雕连 雪夜
    { 18.2f,  66.1f, 242.0f,1.06f,0.90f,140.0f}, // 3  松骨峰阻击战 雪·黄昏
    { 73.5f,  12.8f, 305.0f,1.38f,1.20f, 92.0f}, // 4  上甘岭坑道战 陡峭焦土
    {-88.1f,  47.6f, 215.0f,0.90f,0.70f,150.0f}, // 5  金城反击战 昼·河谷
    {-29.6f, -58.2f, 205.0f,0.82f,0.60f,170.0f}, // 6  汉江夜渡 夜·泥滩
    { 91.3f, -34.5f, 228.0f,0.96f,1.05f,150.0f}, // 7  三八线阵地战 硝烟对峙
    { 55.2f,  88.4f, 268.0f,1.16f,0.95f,110.0f}, // 8  铁原阻击战 宽谷山地
    {-52.7f,  71.9f, 232.0f,1.02f,0.80f,150.0f}, // 9  横城反击战 黄昏
    { 6.8f,  -77.3f, 240.0f,1.04f,1.10f,130.0f}, // 10 平壤外围战
    {-12.4f,  30.6f, 272.0f,1.22f,0.90f,120.0f}, // 11 黄草岭阻击战 雪·山口
    { 33.9f,  93.7f, 250.0f,1.10f,0.88f,120.0f}, // 12 飞虎山阻击战 秋·焦土
    {-74.2f, -12.8f,230.0f,1.00f,0.82f,140.0f}, // 13 德川宁远反击战 昼·河谷
    { 88.6f,  62.4f, 246.0f,1.08f,0.96f,130.0f}, // 14 清川江围歼战 硝烟
    {-40.8f,-102.3f,236.0f,1.02f,0.78f,140.0f}, // 15 三所里·龙源里穿插战 黄昏
    { 63.1f,-128.6f,212.0f,0.88f,0.66f,150.0f}, // 16 突破临津江 雪夜
    {102.5f,  21.9f, 252.0f,1.12f,1.00f,120.0f}, // 17 釜谷里阻击战 焦土
    {-96.7f,  80.5f, 244.0f,1.06f,0.92f,130.0f}, // 18 雪马里围歼战 昼·丘陵
    { 17.8f, 118.2f, 276.0f,1.20f,0.98f,110.0f}, // 19 马良山攻防战 陡山
    {-62.4f,-135.1f,262.0f,1.14f,0.84f,120.0f}, // 20 黑云吐岭反击战 黄昏
    {118.4f,-66.2f,258.0f,1.10f,1.02f,118.0f}, // 21 文登公路狙击战 昼·山谷公路
    {-128.7f, 54.8f,266.0f,1.16f,0.86f,132.0f}, // 22 兴南港突围战 雪·港湾
    { 46.9f,140.3f,272.0f,1.20f,0.92f,112.0f}, // 23 阳德高原穿插战 黄昏·高原
    {136.2f, 8.6f, 238.0f,1.04f,1.06f,128.0f}, // 24 元山登陆支援战 夜·海岸
    {-104.6f,122.9f,270.0f,1.18f,0.90f,116.0f}, // 25 咸镜南道追击战 雪·山地
    { 78.3f,-150.4f,248.0f,1.06f,0.98f,134.0f}, // 26 汉城外围防御战 黄昏·硝烟
};
static const MapCfg* mapCfg(void){ int s=gScenario; if(s<0||s>26)s=0; return &MAPS[s]; }

// theme queries used by sky / environment / colours / mission text
// v1.9: every theatre is a war zone — the sky is smoke-stained everywhere.
int Map_IsNight(void){ return gScenario==2||gScenario==6||gScenario==16||gScenario==24; }
int Map_IsSnow(void){ return gScenario==2||gScenario==3||gScenario==11||gScenario==16||gScenario==22||gScenario==25; }
int Map_IsScorch(void){ return 1; }   // shell-blasted front, all 27 campaigns
int Map_IsDusk(void){ return gScenario==3||gScenario==9||gScenario==15||gScenario==20||gScenario==23||gScenario==26; }
int Map_IsChosin(void){ return gScenario==2; }
int Map_Count(void){ return 27; }

static inline float heightRaw(float x, float z)
{
    const MapCfg*c=mapCfg();
    float nx = x*0.00085f+c->ox*0.013f, nz = z*0.00085f+c->oz*0.013f;
    float base = Noise_Fbm2(nx+11.3f, nz-7.1f, 5, 2.03f, 0.55f);   // broad hills
    float ridge = Noise_Ridged2(nx*1.8f-31.7f, nz*1.8f+12.4f, 4);   // mountain crests
    float north = clampf((-z - 180.0f)/980.0f, 0.0f, 1.0f);         // northern highlands
    float east  = clampf((  x - 500.0f)/700.0f, 0.0f, 0.6f);
    float h = base*52.0f;
    h += powf(ridge>0?ridge:0, 1.25f)*c->ridgeMul*north*c->northMul;
    h += powf(ridge>0?ridge:0, 1.6f)*120.0f*east*c->eastMul;
    h -= 8.0f;
    // flatten a forward airstrip / assembly area near origin
    float FR=mapCfg()->flatR;
    float r2 = x*x + z*z;
    if (r2 < FR*FR)
    {
        float t = clampf((sqrtf(r2)-(FR-50.0f))/50.0f, 0.0f, 1.0f);
        t = t*t*(3.0f-2.0f*t);
        h = h*t + 2.0f*(1.0f-t);
    }
    return h;
}

float Terrain_Height(float x, float z)
{
    if (x < -WORLD_HALF) x=-WORLD_HALF; if (x > WORLD_HALF) x=WORLD_HALF;
    if (z < -WORLD_HALF) z=-WORLD_HALF; if (z > WORLD_HALF) z=WORLD_HALF;
    return heightRaw(x,z);
}

Vector3 Terrain_Normal(float x, float z)
{
    float e = 6.0f;
    float hL = heightRaw(x-e,z), hR = heightRaw(x+e,z);
    float hD = heightRaw(x,z-e), hU = heightRaw(x,z+e);
    return vnorm(v3(hL-hR, 2.0f*e, hD-hU));
}

static Color landColor(float h, float slope, float x, float z)
{
    Color c;
    float r2 = x*x+z*z;
    if(Map_IsSnow())
    {
        // Chosin Reservoir, winter night: deep snow almost everywhere,
        // dark wind-swept crags and a frozen pale-blue lake / river flats.
        float patch = Noise_Fbm2(x*0.02f+5,z*0.02f+5,2,2.0f,0.5f);
        if(slope>0.60f)        c=(Color){64,66,78,255};     // dark frozen crag
        else if(h>34.0f)       c=(Color){228,234,244,255};  // snow slopes
        else if(h>SEA_Y+2.0f)  c=(patch>0.1f)?(Color){214,222,234,255}:(Color){196,206,220,255}; // drifted snow flats
        else                   c=(Color){176,192,208,255};  // frozen lake ice
        float vv=1.0f+patch*0.06f;
        c.r=(unsigned char)clampf(c.r*vv,0,255); c.g=(unsigned char)clampf(c.g*vv,0,255); c.b=(unsigned char)clampf(c.b*vv,0,255);
        return c;
    }
    // airfield runway strip along X (only in the air-support map)
    if(gScenario==0 && r2 < 165.0f*165.0f && fabsf(z) < 13.0f) return (Color){70,72,74,255};
    if(gScenario==0 && r2 < 165.0f*165.0f && fabsf(z) < 24.0f) return (Color){110,104,86,255};
    float patch = Noise_Fbm2(x*0.02f+5,z*0.02f+5,2,2.0f,0.5f);
    if (slope > 0.62f)            c = (Color){112,96,80,255};          // cliff rock
    else if (h > 165.0f)          c = (Color){236,238,242,255};       // snow
    else if (h > 120.0f)          c = (Color){124,110,96,255};        // high rock
    else if(Map_IsScorch())
    {   // war-torn ground: scorched olive grass, ash, churned mud patches
        if(slope>0.45f)      c=(Color){82,72,60,255};
        else if(patch>0.30f) c=(Color){86,92,58,255};   // shell-shocked grass
        else if(patch>0.12f) c=(Color){104,96,74,255};  // dry trampled earth
        else if(patch>-0.06f)c=(Color){64,66,42,255};   // scorched grass
        else                 c=(Color){58,48,38,255};   // blackened blast craters
    }
    else if (h > 70.0f)           c = (Color){64,86,52,255};          // forest
    else if (h > SEA_Y+6.0f)
    {
        if (patch>0.15f) c=(Color){78,108,56,255}; else c=(Color){96,116,62,255}; // grass/field
    }
    else                          c = (Color){196,184,142,255};       // shore sand
    // subtle per-vertex variation
    float v = 1.0f + patch*0.10f;
    c.r=(unsigned char)clampf(c.r*v,0,255); c.g=(unsigned char)clampf(c.g*v,0,255); c.b=(unsigned char)clampf(c.b*v,0,255);
    return c;
}

void Terrain_Init(void)
{
    if(gTerrainReady){ UnloadModel(gTerrain); UnloadModel(gSea); gTerrainReady=0; }
    int vcount = (TER_SEG+1)*(TER_SEG+1);
    Mesh m = {0};
    m.vertexCount = vcount;
    m.triangleCount = TER_SEG*TER_SEG*2;
    m.vertices = (float*)malloc(vcount*3*sizeof(float));
    m.normals  = (float*)malloc(vcount*3*sizeof(float));
    m.texcoords= (float*)malloc(vcount*2*sizeof(float));
    m.colors   = (unsigned char*)malloc(vcount*sizeof(unsigned char)*4);
    m.indices  = (unsigned short*)malloc((size_t)TER_SEG*TER_SEG*6*sizeof(unsigned short));

    float cell = TER_SIZE/TER_SEG;
    int vi=0;
    for (int j=0;j<=TER_SEG;j++)
    {
        for (int i=0;i<=TER_SEG;i++)
        {
            float x = -WORLD_HALF + i*cell;
            float z = -WORLD_HALF + j*cell;
            float y = heightRaw(x,z);
            m.vertices[vi*3+0]=x; m.vertices[vi*3+1]=y; m.vertices[vi*3+2]=z;
            Vector3 n = Terrain_Normal(x,z);
            m.normals[vi*3+0]=n.x; m.normals[vi*3+1]=n.y; m.normals[vi*3+2]=n.z;
            m.texcoords[vi*2+0]=(float)i/TER_SEG; m.texcoords[vi*2+1]=(float)j/TER_SEG;
            float slope = 1.0f-n.y;
            Color c = landColor(y,slope,x,z);
            m.colors[vi*4+0]=c.r; m.colors[vi*4+1]=c.g; m.colors[vi*4+2]=c.b; m.colors[vi*4+3]=255;
            vi++;
        }
    }
    int ii=0;
    for (int j=0;j<TER_SEG;j++)
        for (int i=0;i<TER_SEG;i++)
        {
            unsigned short a=(unsigned short)(j*(TER_SEG+1)+i);
            unsigned short b=(unsigned short)(a+1);
            unsigned short c=(unsigned short)(a+TER_SEG+1);
            unsigned short d=(unsigned short)(c+1);
            m.indices[ii++]=a; m.indices[ii++]=c; m.indices[ii++]=b;
            m.indices[ii++]=b; m.indices[ii++]=c; m.indices[ii++]=d;
        }
    UploadMesh(&m, 0);
    gTerrain = LoadModelFromMesh(m);
    gTerrainReady = 1;

    Mesh sea = GenMeshPlane(WORLD_HALF*3.2f, WORLD_HALF*3.2f, 1, 1);
    gSea = LoadModelFromMesh(sea);
    gSea.materials[0].maps[MATERIAL_MAP_DIFFUSE].color =
        Map_IsNight() ? (Color){18,30,52,170} : (Color){34,74,112,150};
}

extern Shader gLit; // defined in scene.c

void Terrain_ApplyShader(Shader s)
{
    if (gTerrainReady) gTerrain.materials[0].shader = s;
    if (gSea.meshes)   gSea.materials[0].shader = s;
}

void Sea_Draw(Camera3D cam)
{
    (void)cam;
    if (!gSea.meshes) return;
    Matrix tr = MatrixTranslate(0, SEA_Y, 0);
    BeginBlendMode(BLEND_ALPHA);
    DrawMesh(gSea.meshes[0], gSea.materials[0], tr);
    EndBlendMode();
}

// ---- battlefield ground cover: snow tufts / scorched grass / charred clumps ----
// ALL visible tufts are baked into merged meshes (two draw calls max) in a disc
// that follows the camera. GPU instancing is avoided (unreliable on old GLES2 /
// low-end phones). Anchored to WORLD cells, rebuilt only on crossing a cell.
//   mesh A: snow-covered white tufts (winter maps) or scorched olive grass
//   mesh B: blackened, burnt clumps scattered on scorched-earth maps (no snow)
extern Shader gLit;
#if defined(PLATFORM_ANDROID)
  #define GR_MAX 700      // low/old phones: fewer tufts to keep the frame budget
#else
  #define GR_MAX 2200
#endif
#define GR_VPER 8
#define GR_IPER 12
static Mesh     gGrassMesh={0}, gCharMesh={0};
static Material gGrassMat={0},  gCharMat={0};
static int      gGrassN=0, gCharN=0, gGrassReady=0, gHasMesh=0, gHasChar=0;
static int      gCellX=1<<30, gCellZ=1<<30, gCellSc=-1;
static float    gDensity=1.0f;   // quality setting: 0.45 / 0.7 / 1.0 / 1.35
void Grass_SetDensity(float d){ if(d<0.25f)d=0.25f; if(d>1.6f)d=1.6f; if(fabsf(d-gDensity)>0.01f)gCellSc=-1; gDensity=d; }

// one crossed blade in local space: quad A along X, quad B along Z
static const float BL_V[GR_VPER*3]={
    -0.09f,0,0,  0.09f,0,0,  0.09f,0.62f,0,  -0.09f,0.62f,0,
     0,0,-0.09f, 0,0,0.09f, 0,0.62f,0.09f,  0,0.62f,-0.09f };
static const float BL_N[GR_VPER*3]={
    0,0.35f,0.94f, 0,0.35f,0.94f, 0,0.35f,0.94f, 0,0.35f,0.94f,
    0.94f,0.35f,0, 0.94f,0.35f,0, 0.94f,0.35f,0, 0.94f,0.35f,0 };
static const float BL_UV[GR_VPER*2]={0,0, 1,0, 1,1, 0,1, 0,0, 1,0, 1,1, 0,1};
static const unsigned short BL_I[GR_IPER]={0,1,2,0,2,3, 4,5,6,4,6,7};

static unsigned int grHash(int x,int z){ unsigned int h=(unsigned int)(x*73856093) ^ (unsigned int)(z*19349663); h^=h>>13; h*=1274126177u; h^=h>>16; return h; }
static float h01(unsigned int h){ return (h>>8)*(1.0f/16777216.0f); }

typedef struct { float*vx,*nx,*uv; unsigned short*ix; int n; } Bld;

static void buildTuft(Bld*b,float X,float Y,float Z,float yaw,float sy,float sxx)
{
    float cs=cosf(yaw), sn=sinf(yaw); int base=b->n*GR_VPER;
    for(int v=0;v<GR_VPER;v++)
    {
        float lx=BL_V[v*3]*sxx, ly=BL_V[v*3+1]*sy, lz=BL_V[v*3+2]*sxx;
        b->vx[(base+v)*3]   = X + lx*cs - lz*sn;
        b->vx[(base+v)*3+1] = Y - 0.02f + ly;
        b->vx[(base+v)*3+2] = Z + lx*sn + lz*cs;
        float nvx=BL_N[v*3], nvy=BL_N[v*3+1], nvz=BL_N[v*3+2];
        b->nx[(base+v)*3]   = nvx*cs - nvz*sn;
        b->nx[(base+v)*3+1] = nvy;
        b->nx[(base+v)*3+2] = nvx*sn + nvz*cs;
        b->uv[(base+v)*2]=BL_UV[v*2]; b->uv[(base+v)*2+1]=BL_UV[v*2+1];
    }
    for(int t=0;t<GR_IPER;t++) b->ix[b->n*GR_IPER+t]=(unsigned short)(base+BL_I[t]);
    b->n++;
}

static void grassRebuild(float px, float pz)
{
    const float STEP=2.6f; const int RCELL=20;
    int cx=(int)floorf(px/STEP), cz=(int)floorf(pz/STEP);
    gCellX=cx; gCellZ=cz; gCellSc=gScenario; gGrassN=0; gCharN=0;
    int snow=Map_IsSnow();
    int capA=(int)(GR_MAX*gDensity); if(capA<40)capA=40;
    Bld A={ MemAlloc(sizeof(float)*3*capA*GR_VPER), MemAlloc(sizeof(float)*3*capA*GR_VPER),
            MemAlloc(sizeof(float)*2*capA*GR_VPER), MemAlloc(sizeof(unsigned short)*capA*GR_IPER), 0 };
    Bld B={ MemAlloc(sizeof(float)*3*capA*GR_VPER), MemAlloc(sizeof(float)*3*capA*GR_VPER),
            MemAlloc(sizeof(float)*2*capA*GR_VPER), MemAlloc(sizeof(unsigned short)*capA*GR_IPER), 0 };
    int capB=capA;
    for(int iz=-RCELL; iz<=RCELL; iz++)
    for(int ix_= -RCELL; ix_<=RCELL; ix_++)
    {
        int wx=cx+ix_, wz=cz+iz;
        unsigned int h1=grHash(wx,wz);
        if(h01(h1) < 0.30f) continue;                 // leave bare patches / mud
        float jx=(h01(h1^0x9e37u)-0.5f)*STEP;
        float jz=(h01(h1^0x85ebu)-0.5f)*STEP;
        float X=(wx*STEP)+jx, Z=(wz*STEP)+jz;
        if(X<-WORLD_HALF+4||X>WORLD_HALF-4||Z<-WORLD_HALF+4||Z>WORLD_HALF-4) continue;
        float dx=X-px, dz=Z-pz; if(dx*dx+dz*dz > (RCELL*STEP)*(RCELL*STEP)) continue;
        float Y=Terrain_Height(X,Z);
        if(Y<=SEA_Y+1.2f) continue;                  // no tufts under water
        Vector3 nm=Terrain_Normal(X,Z);
        if(nm.y<0.62f) continue;                     // skip steep rock faces
        float yaw=h01(h1^0x1234u)*6.2832f;
        int burnt = (!snow) && (h01(h1^0x7777u)<0.20f);   // ~20% blackened clumps
        if(burnt && B.n<capB)
        {   // charred, short black-brown stubble left by a shell / flamethrower
            float sy=0.34f+h01(h1^0x5678u)*0.40f, sxx=0.80f+h01(h1^0x9abcu)*0.3f;
            buildTuft(&B,X,Y,Z,yaw,sy,sxx);
        }
        else if(A.n<capA)
        {   // snow maps: short snow-covered white tufts; others: scorched olive
            float sy=(snow?0.42f:0.8f)+h01(h1^0x5678u)*(snow?0.46f:0.9f);
            float sxx=0.85f+h01(h1^0x9abcu)*0.3f;
            buildTuft(&A,X,Y,Z,yaw,sy,sxx);
        }
    }
    // ---- publish A ----
    if(gHasMesh) UnloadMesh(gGrassMesh);
    memset(&gGrassMesh,0,sizeof gGrassMesh); gGrassN=A.n;
    if(A.n==0){ MemFree(A.vx);MemFree(A.nx);MemFree(A.uv);MemFree(A.ix); gHasMesh=0; }
    else{ gGrassMesh.vertexCount=A.n*GR_VPER; gGrassMesh.triangleCount=A.n*(GR_IPER/3);
          gGrassMesh.vertices=A.vx; gGrassMesh.normals=A.nx; gGrassMesh.texcoords=A.uv; gGrassMesh.indices=A.ix;
          UploadMesh(&gGrassMesh,true); gHasMesh=1; }
    // ---- publish B (charred) ----
    if(gHasChar) UnloadMesh(gCharMesh);
    memset(&gCharMesh,0,sizeof gCharMesh); gCharN=B.n;
    if(B.n==0){ MemFree(B.vx);MemFree(B.nx);MemFree(B.uv);MemFree(B.ix); gHasChar=0; }
    else{ gCharMesh.vertexCount=B.n*GR_VPER; gCharMesh.triangleCount=B.n*(GR_IPER/3);
          gCharMesh.vertices=B.vx; gCharMesh.normals=B.nx; gCharMesh.texcoords=B.uv; gCharMesh.indices=B.ix;
          UploadMesh(&gCharMesh,true); gHasChar=1; }
}

// GL resources are created per scene OUTSIDE BeginMode3D and bound to that
// scene's gLit, so a scene re-entry can never leave a dead shader / VAO.
void Grass_Init(void)
{
    if(gGrassReady)return;
    gGrassMat=LoadMaterialDefault(); if(gLit.id>0) gGrassMat.shader=gLit;
    gCharMat =LoadMaterialDefault(); if(gLit.id>0) gCharMat.shader=gLit;
    gCellX=1<<30; gCellZ=1<<30; gCellSc=-1; gGrassN=gCharN=0; gHasMesh=gHasChar=0;
    gGrassReady=1;
}
void Grass_Unload(void)
{
    if(!gGrassReady)return;
    if(gHasMesh){ UnloadMesh(gGrassMesh); gHasMesh=0; }
    if(gHasChar){ UnloadMesh(gCharMesh); gHasChar=0; }
    MemFree(gGrassMat.maps); MemFree(gCharMat.maps);
    gGrassReady=0; gGrassN=gCharN=0;
    memset(&gGrassMesh,0,sizeof gGrassMesh); memset(&gCharMesh,0,sizeof gCharMesh);
}

void Grass_Draw(Camera3D cam)
{
    if(!gGrassReady)return;
    // winter theatre: snow-laden WHITE tufts, never green; scorched maps: olive
    gGrassMat.maps[MATERIAL_MAP_DIFFUSE].color =
        Map_IsSnow() ? (Color){224,230,240,255}      // snow-covered white tufts
                     : (Color){86,92,56,255};         // shell-shocked olive grass
    gCharMat.maps[MATERIAL_MAP_DIFFUSE].color=(Color){26,22,18,255};  // burnt black clumps
    const float STEP=2.6f;
    int cx=(int)floorf(cam.position.x/STEP), cz=(int)floorf(cam.position.z/STEP);
    if(cx!=gCellX||cz!=gCellZ||gScenario!=gCellSc) grassRebuild(cam.position.x,cam.position.z);
    if(gHasMesh) DrawMesh(gGrassMesh,gGrassMat,MatrixIdentity());
    if(gHasChar) DrawMesh(gCharMesh,gCharMat,MatrixIdentity());
}

void Terrain_Draw(Camera3D cam)
{
    if (gTerrainReady) DrawModel(gTerrain, (Vector3){0,0,0}, 1.0f, WHITE);
    Grass_Draw(cam);
}
