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
static const MapCfg MAPS[12]={
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
};
static const MapCfg* mapCfg(void){ int s=gScenario; if(s<0||s>11)s=0; return &MAPS[s]; }

// theme queries used by sky / environment / colours / mission text
int Map_IsNight(void){ return gScenario==2||gScenario==6; }
int Map_IsSnow(void){ return gScenario==2||gScenario==3||gScenario==11; }
int Map_IsScorch(void){ return gScenario==4||gScenario==7; }
int Map_IsDusk(void){ return gScenario==3||gScenario==9; }
int Map_IsChosin(void){ return gScenario==2; }

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
    {   // shell-blasted heights: ash, scorched grass, churned mud
        if(slope>0.45f)      c=(Color){78,70,60,255};
        else if(patch>0.28f) c=(Color){104,98,86,255};
        else if(patch>0.02f) c=(Color){74,72,46,255};
        else                 c=(Color){70,58,46,255};
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

void Terrain_Draw(Camera3D cam)
{
    (void)cam;
    if (gTerrainReady) DrawModel(gTerrain, (Vector3){0,0,0}, 1.0f, WHITE);
}
