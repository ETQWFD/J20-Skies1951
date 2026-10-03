// guns_native.c - render embedded PBR small-arms (generated guns_data.c) with raylib
#include "common.h"
#include "guns_native.h"
#include "guns_data.h"
#include "rlgl.h"

extern Shader gLit;   // scene.c lit shader

#define SWING_DUR 0.28f
static float sSwingT=0;
static Vector3 sMuzzle={0};
static int sReady=0;

typedef struct { Mesh mesh; Material mat; int hasTex; Texture2D tex; } Sub;
typedef struct { Sub* s; int n; } Gun;
static Gun gRifle={0}, gAkm={0}, gKnife={0}, gRpg={0};
extern const GGunData G_RPG;   // src/rpg_data.c (embedded user RPG-7 GLB)
extern const GGunData GT34;    // src/t34_data.c (embedded user T-34 tank GLB)
static Gun gT34={0};

// first-person arms (khaki volunteer-uniform sleeves + skin hands), drawn
// gripping the real weapon: right hand on the grip/trigger, left hand under
// the fore-end so the rifle is actually supported.
static Mesh mSleeve={0}, mHand={0};
static Material mSleeveMat={0}, mHandMat={0};
static int sArmsReady=0;

// ---- 8x telescopic sight on the Mosin/98k, modelled after the uploaded
// "Teleskop" part: main tube, flared objective bell + front rim, eyepiece,
// elevation turret and side knob, coated lens, two steel mounting rings and a
// solid bridge to the receiver. Pure procedural geometry (the source SLDPRT is
// a SolidWorks B-rep and cannot be meshed here); swap in an exported STL/GLB
// later without touching gameplay. All parts are authored in the rifle's local
// space and ride the same view-model transform as the gun. ----
typedef struct { Mesh m; } SP;
static SP sTube={0},sBell={0},sRim={0},sEye={0},sGlass={0},sRingA={0},sRingB={0},
          sTurret={0},sKnob={0},sBridge={0};
static Material mSteel={0},mSteelDark={0},mGlass={0},mMount={0};
static int sScopeReady=0;
// Rifle local space: barrel runs along Z with the muzzle at -Z, top is +Y,
// width along X. The optic sits over the receiver, objective pointing to -Z.
static Vector3 sScopeC={0}; static float sScopeR=0; static float sRingZ1=0,sRingZ2=0,sFrontZ=0;

static Material mkMat(unsigned char r,unsigned char g,unsigned char b){
    Material mm=LoadMaterialDefault();
    if(gLit.id>0) mm.shader=gLit;
    mm.maps[MATERIAL_MAP_DIFFUSE].color=(Color){r,g,b,255};
    return mm;
}
static void buildScope(const float mn[3],const float mx[3])
{
    float u=mx[2]-mn[2];                       // barrel length (local Z)
    sScopeR=0.030f*u;                          // readable optic over the receiver
    float tubeLen=0.34f*u;
    sFrontZ=mn[2]+0.36f*u;                     // objective face (toward muzzle, -Z)
    sScopeC.z=sFrontZ+tubeLen*0.5f;
    float mountH=0.020f*u;
    sScopeC.y=mx[1]+mountH+sScopeR*1.05f;
    sScopeC.x=(mn[0]+mx[0])*0.5f;
    sRingZ1=sScopeC.z-0.11f*u; sRingZ2=sScopeC.z+0.11f*u;
    int seg=20;
    sTube.m =GenMeshCylinder(sScopeR,          tubeLen,      seg);
    sBell.m =GenMeshCylinder(sScopeR*1.05f,    0.10f*u,      seg);   // flared objective bell
    sRim.m  =GenMeshCylinder(sScopeR*1.62f,    0.020f*u,     seg);   // front steel rim
    sEye.m  =GenMeshCylinder(sScopeR*1.30f,    0.070f*u,     seg);   // eyepiece housing (rear,+Z)
    sGlass.m=GenMeshCylinder(sScopeR*1.46f,    0.008f*u,     seg);   // coated objective lens
    sRingA.m=GenMeshCylinder(sScopeR*1.16f,    0.030f*u,     seg);   // two steel mounting rings
    sRingB.m=GenMeshCylinder(sScopeR*1.16f,    0.030f*u,     seg);
    sTurret.m=GenMeshCylinder(sScopeR*0.62f,   0.055f*u,     12);    // elevation turret (+Y)
    SP *xs[9]={&sTube,&sBell,&sRim,&sEye,&sGlass,&sRingA,&sRingB,&sTurret,(SP*)0};
    for(int i=0;xs[i];i++) UploadMesh(&xs[i]->m,false);
    sKnob.m=GenMeshCylinder(sScopeR*0.50f,0.045f*u,12); UploadMesh(&sKnob.m,false); // windage knob (+X)
    sBridge.m=GenMeshCube(sScopeR*1.6f,mountH,(sRingZ2-sRingZ1)+0.04f*u);
    UploadMesh(&sBridge.m,false);
    mSteel=mkMat(34,36,42); mSteelDark=mkMat(20,21,25);
    mGlass=mkMat(28,46,58); mMount=mkMat(26,27,31);
    sScopeReady=1;
}
// Place a cylinder (mesh axis raylib +Y) along the chosen local axis:
// 0 = Z (tube, +Y->+Z), 1 = Y (turret), 2 = X (windage knob, +Y->+X).
static void scopeCyl(SP*p,Material mat,Vector3 c,int axis,Matrix base)
{
    Matrix r=(axis==0)?MatrixRotateX(PI/2.0f):(axis==2)?MatrixRotateZ(-PI/2.0f):MatrixIdentity();
    Matrix lm=MatrixMultiply(MatrixTranslate(c.x,c.y,c.z),r);
    DrawMesh(p->m,mat,MatrixMultiply(base,lm));
}
static void DrawScopeModel(Matrix base)
{
    if(!sScopeReady)return;
    scopeCyl(&sTube,mSteel,sScopeC,0,base);
    scopeCyl(&sRingA,mMount,(Vector3){sScopeC.x,sScopeC.y,sRingZ1},0,base);
    scopeCyl(&sRingB,mMount,(Vector3){sScopeC.x,sScopeC.y,sRingZ2},0,base);
    scopeCyl(&sBridge,mMount,(Vector3){sScopeC.x,sScopeC.y-sScopeR*1.02f,sScopeC.z},1,base);
    // objective end (-Z): flared bell, coated glass, steel rim
    scopeCyl(&sBell,mSteelDark,(Vector3){sScopeC.x,sScopeC.y,sFrontZ+0.050f},0,base);
    scopeCyl(&sRim,mSteelDark,(Vector3){sScopeC.x,sScopeC.y,sFrontZ},0,base);
    scopeCyl(&sGlass,mGlass,(Vector3){sScopeC.x,sScopeC.y,sFrontZ+0.004f},0,base);
    // eyepiece at the rear (+Z), toward the shooter's eye
    scopeCyl(&sEye,mSteelDark,(Vector3){sScopeC.x,sScopeC.y,sScopeC.z+0.185f},0,base);
    // elevation turret on top and windage knob on the side
    scopeCyl(&sTurret,mMount,(Vector3){sScopeC.x+0.004f,sScopeC.y+sScopeR+0.0275f,sScopeC.z},1,base);
    scopeCyl(&sKnob,mMount,(Vector3){sScopeC.x+sScopeR+0.0225f,sScopeC.y,sScopeC.z+0.004f},2,base);
}
static void unloadScope(void)
{
    if(!sScopeReady)return;
    SP*xs[9]={&sTube,&sBell,&sRim,&sEye,&sGlass,&sRingA,&sRingB,&sTurret,&sKnob};
    for(int i=0;i<9;i++) UnloadMesh(xs[i]->m);
    UnloadMesh(sBridge.m);
    MemFree(mSteel.maps);MemFree(mSteelDark.maps);MemFree(mGlass.maps);MemFree(mMount.maps);
    sScopeReady=0;
}
static void gunParams(int type, float*S, float*fF, float*fR, float*fU,
                      float*pYaw, float*pPitch, float*pRoll);

static Gun build(const GGunData* d)
{
    Gun g={0}; g.n=d->nsub; g.s=(Sub*)calloc(g.n,sizeof(Sub));
    for(int i=0;i<g.n;i++)
    {
        const GSubData* sd=&d->sub[i];
        Mesh m={0};
        m.vertexCount=sd->vcount; m.triangleCount=sd->icount/3;
        m.vertices=(float*)MemAlloc(sizeof(float)*3*sd->vcount);
        m.normals =(float*)MemAlloc(sizeof(float)*3*sd->vcount);
        m.texcoords=(float*)MemAlloc(sizeof(float)*2*sd->vcount);
        memcpy(m.vertices,sd->pos,sizeof(float)*3*sd->vcount);
        memcpy(m.normals, sd->nrm,sizeof(float)*3*sd->vcount);
        // glTF UV origin top-left; raylib image origin top-left after load
        for(int v=0;v<sd->vcount;v++){ m.texcoords[v*2]=sd->uv[v*2]; m.texcoords[v*2+1]=1.0f-sd->uv[v*2+1]; }
        unsigned short* idx=(unsigned short*)MemAlloc(sizeof(unsigned short)*sd->icount);
        for(int k=0;k<sd->icount;k++) idx[k]=(unsigned short)(sd->idx32?sd->idx32[k]:sd->idx16[k]);
        m.indices=idx;
        UploadMesh(&m,false);
        Material mat=LoadMaterialDefault();
        if(gLit.id>0) mat.shader=gLit;
        Sub* o=&g.s[i]; o->mesh=m; o->mat=mat; o->hasTex=0;
        if(sd->tex>=0)
        {
            const unsigned char* bytes=d->tex[sd->tex]; int len=d->texlen[sd->tex];
            Image img=LoadImageFromMemory(".jpg",bytes,len);
            if(img.data)
            {
                Texture2D t=LoadTextureFromImage(img);
                // Old GLES2 GPUs reject REPEAT wrapping on non-power-of-two JPEGs
                // (incomplete texture -> driver crash/black screen). Gun skins
                // never tile, so force edge CLAMP on both axes.
                SetTextureWrap(t,TEXTURE_WRAP_CLAMP);
                SetMaterialTexture(&mat,MATERIAL_MAP_DIFFUSE,t);
                o->tex=t; o->hasTex=1;
                UnloadImage(img);
            }
        }
    }
    return g;
}

void GunsNative_Load(void)
{
    if(sReady)return;
    gRifle=build(&G_RIFLE); gAkm=build(&G_AKM); gKnife=build(&G_KNIFE); gRpg=build(&G_RPG);
    gT34=build(&GT34);
    {
        float mn[3]={1e30f,1e30f,1e30f}, mx[3]={-1e30f,-1e30f,-1e30f};
        for(int i=0;i<G_RIFLE.nsub;i++){ const GSubData*sd=&G_RIFLE.sub[i];
            for(int v=0;v<sd->vcount;v++) for(int k=0;k<3;k++){ float x=sd->pos[v*3+k];
                if(x<mn[k])mn[k]=x; if(x>mx[k])mx[k]=x; } }
        buildScope(mn,mx);   // mount the 8x telescopic sight on the receiver
    }
    // first-person limbs
    mSleeve=GenMeshCylinder(0.038f,0.34f,10);
    mHand  =GenMeshSphere(0.046f,12,10);
    // MUST upload before DrawMesh: a zero VAO id is tolerated by software GL but
    // crashes real desktop/Adreno drivers on the first first-person frame.
    UploadMesh(&mSleeve,false);
    UploadMesh(&mHand,false);
    mSleeveMat=LoadMaterialDefault(); mHandMat=LoadMaterialDefault();
    if(gLit.id>0){ mSleeveMat.shader=gLit; mHandMat.shader=gLit; }
    mSleeveMat.maps[MATERIAL_MAP_DIFFUSE].color=(Color){124,108,72,255};  // khaki wool sleeve
    mHandMat.maps[MATERIAL_MAP_DIFFUSE].color  =(Color){200,166,132,255}; // skin
    sArmsReady=1;
    sReady=1;
}
void GunsNative_Unload(void)
{
    if(!sReady)return;
    Gun gs[5]={gRifle,gAkm,gKnife,gRpg,gT34};
    // NB: this raylib build's UnloadMaterial() also frees material.shader. All gun
    // materials SHARE gLit (owned/freed by scene.c), so tear down maps+texture
    // manually and never let UnloadMaterial release the shared shader.
    for(int gi=0;gi<5;gi++) for(int i=0;i<gs[gi].n;i++)
    {
        Sub* o=&gs[gi].s[i];
        UnloadMesh(o->mesh);
        if(o->hasTex) UnloadTexture(o->tex);
        MemFree(o->mat.maps);
    }
    if(sArmsReady)
    {
        UnloadMesh(mSleeve); UnloadMesh(mHand);
        MemFree(mSleeveMat.maps); MemFree(mHandMat.maps);
        sArmsReady=0;
    }
    unloadScope();
    sReady=0;
}
int GunsNative_Ready(void){ return sReady; }

// Draw the embedded T-34 tank in WORLD space. The baked local frame is already
// metres with nose -Z / up +Y / ground y=0, so the caller supplies a world
// matrix = root(pos, RotY(-heading), scale). Nothing else is scaled here.
void GunsNative_DrawT34(Matrix world)
{
    if(!sReady)return;
    for(int i=0;i<gT34.n;i++)
        if(gT34.s[i].mesh.vaoId>0) DrawMesh(gT34.s[i].mesh,gT34.s[i].mat,world);
}

void Weapon_SwingTickNative(void){ sSwingT=SWING_DUR; }
void Weapon_AnimUpdateNative(float dt){ if(sSwingT>0){sSwingT-=dt; if(sSwingT<0)sSwingT=0;} }
Vector3 GunsNative_Muzzle(void){ return sMuzzle; }

Vector3 GunsNative_MuzzlePoint(Camera3D cam, int type)
{
    if(!sReady)return cam.position;
    float S,fF,fR,fU,y,p,rl; gunParams(type,&S,&fF,&fR,&fU,&y,&p,&rl); (void)y;(void)p;(void)rl;
    Vector3 f=vnorm(vsub(cam.target,cam.position));
    Vector3 r=vnorm(vcross(f,cam.up));
    Vector3 u=cam.up;
    Vector3 grip=vadd(cam.position, vadd(vmul(f,fF), vadd(vmul(r,fR), vmul(u,fU))));
    return vadd(grip,vmul(f,0.5f*S));
}

// per-weapon first-person placement: world length, grip offset (along
// forward / right / up) from the camera, and a view-model pose (local
// yaw/pitch/roll) so the weapon sits diagonally across the lower-right
// instead of being seen edge-on down the barrel.
static void gunParams(int type, float*S, float*fF, float*fR, float*fU,
                      float*pYaw, float*pPitch, float*pRoll)
{
    if(type==1){ *S=0.84f; *fF=0.58f; *fR=0.22f; *fU=-0.22f;
                 *pYaw=0.55f; *pPitch=0.10f; *pRoll=0.0f; }          // AKM (settled FPS pose)
    else if(type==2){ *S=0.58f; *fF=0.56f; *fR=0.13f; *fU=-0.17f;
                      *pYaw=-0.05f; *pPitch=0.10f; *pRoll=-0.15f; }  // bayonet
    else if(type==4){ *S=0.98f; *fF=0.54f; *fR=0.14f; *fU=-0.20f;
                      *pYaw=0.30f; *pPitch=0.06f; *pRoll=0.0f; }          // RPG-7 launcher
    else { *S=0.95f; *fF=0.60f; *fR=0.20f; *fU=-0.20f;
           *pYaw=0.42f; *pPitch=0.08f; *pRoll=0.0f; }                // Mosin / 98k
}
static Vector3 gunGrip(Camera3D cam, int type, float kick)
{
    float S,fF,fR,fU,y,p,rl; gunParams(type,&S,&fF,&fR,&fU,&y,&p,&rl); (void)S;(void)y;(void)p;(void)rl;
    Vector3 f=vnorm(vsub(cam.target,cam.position));
    Vector3 r=vnorm(vcross(f,cam.up));
    Vector3 u=cam.up;
    return vadd(cam.position,
        vadd(vmul(f,fF+0.08f*kick),
        vadd(vmul(r,fR), vmul(u,fU+0.045f*kick))));
}
// draw a khaki-sleeved forearm from elbow to wrist plus a skin hand at the wrist
static void drawLimb(Vector3 wrist, Vector3 elbow)
{
    if(!sArmsReady)return;
    Vector3 d=vsub(wrist,elbow); float len=vlen(d);
    if(len<0.001f)return;
    if(mSleeve.vaoId==0)return;   // never draw an un-uploaded mesh (strict-driver crash)
    Vector3 a=vmul(d,1.0f/len);
    Vector3 mid=vmul(vadd(wrist,elbow),0.5f);
    Quaternion qa=QuaternionFromVector3ToVector3((Vector3){0,1,0},a);
    Matrix ms=MatrixMultiply(MatrixScale(1.0f,len/0.34f,1.0f),
               MatrixMultiply(QuaternionToMatrix(qa),MatrixTranslate(mid.x,mid.y,mid.z)));
    DrawMesh(mSleeve,mSleeveMat,ms);
    Matrix mh=MatrixTranslate(wrist.x,wrist.y,wrist.z);
    DrawMesh(mHand,mHandMat,mh);
}

void GunsNative_DrawView(Camera3D cam, int type, float kick, float reload01)
{
    if(!sReady)return;
    // view-model always renders on top and never clips into nearby walls/ground
    rlDisableDepthTest();
    Gun* g = type==1?&gAkm : type==2?&gKnife : type==4?&gRpg : &gRifle;    float S,fF,fR,fU,pYaw,pPitch,pRoll; gunParams(type,&S,&fF,&fR,&fU,&pYaw,&pPitch,&pRoll);
    Vector3 f=vnorm(vsub(cam.target,cam.position));
    Vector3 grip=gunGrip(cam,type,kick);
    float yaw=atan2f(-f.x,-f.z);
    float pitch=asinf(clampf(f.y,-1,1)) + kick*0.10f;
    Quaternion qCam=QuaternionMultiply(QuaternionFromAxisAngle((Vector3){0,1,0},yaw),
                                       QuaternionFromAxisAngle((Vector3){1,0,0},pitch));
    // local view-model pose: swing the muzzle toward screen centre, dip it a touch
    Quaternion qPose=QuaternionMultiply(
        QuaternionMultiply(QuaternionFromAxisAngle((Vector3){0,1,0},pYaw),
                           QuaternionFromAxisAngle((Vector3){1,0,0},pPitch)),
        QuaternionFromAxisAngle((Vector3){0,0,1},pRoll));
    Quaternion q=QuaternionMultiply(qCam,qPose);
    Vector3 rr=vnorm(vcross(f,cam.up)), uu=cam.up;
    // ---- visible reload animation (rifles only): the gun tips muzzle-down and
    // sinks toward the belt while the magazine is swapped / bolt worked. A sine
    // envelope makes it dip and return, and a mid-point notch gives a two-beat
    // motion (drop mag -> seat fresh mag / work the bolt). ----
    float re=(type!=2 && reload01>0.0f && reload01<1.0f)?sinf(M_PI*reload01):0.0f;
    if(re>0.001f)
    {
        if(type==4)
        {
            // RPG-7 rear-load: the tube rolls onto its side and drops so the open
            // breech faces the loader; a sharp mid dip is the HEAT round going in.
            float seat=0.62f+0.38f*sinf(reload01*M_PI*2.0f);
            q=QuaternionMultiply(q,QuaternionFromAxisAngle((Vector3){1,0,0}, 0.82f*re*seat));
            q=QuaternionMultiply(q,QuaternionFromAxisAngle((Vector3){0,0,1}, 0.62f*re));
            grip=vadd(grip, vadd(vmul(uu,-0.26f*re), vadd(vmul(rr,0.10f*re), vmul(f,-0.16f*re))));
        }
        else
        {
        float beat=0.75f+0.25f*sinf(reload01*M_PI*2.0f);   // little settle on seat
        q=QuaternionMultiply(q,QuaternionFromAxisAngle((Vector3){1,0,0}, 0.62f*re*beat));
        q=QuaternionMultiply(q,QuaternionFromAxisAngle((Vector3){0,0,1},-0.28f*re));
        grip=vadd(grip, vadd(vmul(uu,-0.17f*re), vmul(f,-0.10f*re)));
        }
    }
    if(type==2 && sSwingT>0)
    {
        float ph=1.0f-sSwingT/SWING_DUR;
        float beat=sinf(ph*M_PI);
        q=QuaternionMultiply(q,QuaternionFromAxisAngle((Vector3){1,0,0},-1.45f*beat));
        q=QuaternionMultiply(q,QuaternionFromAxisAngle((Vector3){0,1,0}, 0.6f*(ph-0.5f)));
        grip=vadd(grip,vmul(f,0.22f*beat));
    }
    Matrix M=MatrixMultiply(QuaternionToMatrix(q),MatrixTranslate(grip.x,grip.y,grip.z));
    Matrix base=MatrixMultiply(MatrixScale(S,S,S),M);
    for(int i=0;i<g->n;i++) if(g->s[i].mesh.vaoId>0) DrawMesh(g->s[i].mesh,g->s[i].mat,base);
    if(type==0) DrawScopeModel(base);   // the 8x telescopic sight rides the Mosin/98k
    sMuzzle=vadd(grip,vmul(f,0.5f*S));

    // hands gripping the weapon from below: right on the pistol grip/trigger,
    // left supporting the fore-end. Forearms run front-to-back under the rifle.
    Vector3 rWrist = vadd(grip, vadd(vmul(f,-0.05f), vmul(uu,-0.105f)));
    Vector3 rElbow = vadd(rWrist, vadd(vmul(f,-0.30f), vadd(vmul(rr,0.12f), vmul(uu,-0.20f))));
    drawLimb(rWrist,rElbow);
    if(type!=2)
    {
        Vector3 lWrist = vadd(grip, vadd(vmul(f,0.30f*S), vadd(vmul(rr,-0.02f), vmul(uu,-0.10f))));
        Vector3 lElbow = vadd(lWrist, vadd(vmul(f,-0.26f), vadd(vmul(rr,-0.16f), vmul(uu,-0.22f))));
        drawLimb(lWrist,lElbow);
    }
    rlEnableDepthTest();
}
