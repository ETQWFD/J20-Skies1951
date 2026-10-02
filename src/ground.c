// ground.c - PVA infantry first-person assault on a Korean ridge
#include "common.h"
#include "noise.h"
#include "coop.h"
#include "bnet.h"
#if defined(PLATFORM_ANDROID)
#include <sys/system_properties.h>
#endif

GroundResult gGroundResult={0};

#define NF 40   // US GIs (a dug-in reinforced company line; see release notes on the "division" abstraction)
#define NP 12   // friendly PVA
typedef struct { Vector3 pos; float ang,hp,fireCd,vy; float aware; int alive,state,cryT,cry; } Man;
static Man foes[NF], pals[NP];

// ---- LAN co-op storage (host-authoritative); methods defined further below ----
typedef struct {
    int      active, alive, jumps;
    NetAddr  addr;
    Vector3  pos, vel;
    float    yaw, pitch, hp, fireCd, reload, reloadTake, lastSeen;
    int      weapon, mag[2], reserve[2];
    uint32_t lastSeq;
    char     name[BN_NAME];
} Avatar;
static Avatar av[BNET_MAXPLY];          // index = player id (1..3)
static BnWorld gSnap;                   // latest world snapshot (client)
static int   gHaveSnap=0, gNetWin=0, gAvatarShots=0;
static float gAnimClock=0;              // shared walk-cycle clock for rendering
static char  gNetName[BN_NAME]={0};     // local player nick over the network
static void ensureNetName(void)
{
    if(gNetName[0])return;
    const char*s=NULL;
#if defined(PLATFORM_ANDROID)
    {
        char model[64]={0};
        if(__system_property_get("ro.product.manufacturer",model)>0 && model[0])
        {
            char full[80]={0};
            char m2[64]={0};
            if(__system_property_get("ro.product.model",m2)>0 && m2[0])
                snprintf(full,sizeof full,"%s %s",model,m2);
            else snprintf(full,sizeof full,"%s",model);
            strncpy(gNetName,full,BN_NAME-1); gNetName[BN_NAME-1]=0; s=gNetName;
        }
    }
#endif
    if(!s||!s[0])s=getenv("USERNAME");
    if(!s||!s[0])s=getenv("USER");
    if(!s||!s[0])s="志愿军";
    if(!gNetName[0]){ strncpy(gNetName,s,BN_NAME-1); gNetName[BN_NAME-1]=0; }
}
static int   avThreat(const Vector3*chest,Vector3*outPos);


// persistent blood pools left on the snow/earth where men fall
#define MAXBLD 80
typedef struct { Vector3 p; float r,rot; int on; } Bld;
static Bld blds[MAXBLD];
static Man wound;                 // one interactable wounded comrade
static int woundOn=0;
static int talkOpen=0, talkPage=0;
static float headMsgT=0;          // "爆头!" feedback
// kill feed, bottom-left: who just dropped
#define KF_MAX 5
typedef struct { char txt[64]; float t; } KFItem;
static KFItem kf[KF_MAX];
static void kfPush(const char* t)
{
    for(int i=KF_MAX-1;i>0;i--) kf[i]=kf[i-1];
    snprintf(kf[0].txt,sizeof kf[0].txt,"%s",t); kf[0].t=5.5f;
}
static void kfUpdate(float dt){ for(int i=0;i<KF_MAX;i++) if(kf[i].t>0)kf[i].t-=dt; }
static void drawKillFeed(void)
{
    int y=GetScreenHeight()-150;
    for(int i=KF_MAX-1;i>=0;i--) if(kf[i].t>0)
    {
        float a=kf[i].t<0.8f?(kf[i].t/0.8f):1.0f;
        unsigned char al=(unsigned char)(225*a);
        Vector2 sz=MeasureTextEx(GameFont(),kf[i].txt,17,0);
        DrawRectangle(14,y-3,(int)sz.x+16,24,(Color){10,12,16,(unsigned char)(150*a)});
        DrawRectangle(14,y-3,4,24,(Color){214,60,50,al});
        CNC(kf[i].txt,26,y,17,(Color){235,225,205,al});
        y+=27;
    }
}

static Vector3 eye, pvel; static float yaw,pitch; static float hp;
static int weapon, mag[2], reserve[2], reloadTake, reload, foesKilled;
static float fireCd;
// ---- RPG-7 anti-tank mode (menu "RPG·反坦克") ----
int   gRpgMode=0;                 // set before Ground_Run for the AT scenario
static int   rpgLoaded=0, rpgReserve=0;  // one round in the tube + carried rounds
static float rpgReload=0, rpgFireCd=0;   // rear-load animation timer / refire lock
typedef struct { Vector3 p,v; int on; float age; } Rok;
static Rok gRok[4];                      // in-flight 73 mm rockets
typedef struct { Vector3 pos; float yaw,hp; int kind,alive,burn; } Armor;
#define NARM 7
static Armor gArm[NARM];
static int armorAlive(void){ int n=0; for(int i=0;i<NARM;i++) if(gArm[i].alive)n++; return n; }
static float meleeCd=0, meleeSwing=0;        // broadsword / fists
static int   grenades=1;                     // one grenade each man carries
typedef struct { Vector3 p,v; int on,landed; float age; } Gre;
static Gre gre={0}; static int greCharging=0; static float greHold=0;
static float greCd=0;     // 误触取消后 1 秒内不能再次蓄力手雷（M 键 / 雷 键共用）
static float greArmCd=0.0f;   // 1 s lockout after a cancelled throw (anti mis-tap)
static float stepAcc=0, ambT=0;              // footsteps / distant battle
static float celebrate=0; static int planter=-1, planted=0, bugled=0; static float objFall=0; // victory flag ceremony
static float hitMark, dmgCd, holdT, timeAlive;
static float ads;        // 0..1 aim-down-sights (right mouse / scope button)
static int   adsMouseHold=0;
static int   paused=0;
// real recoil: separate view kick that recovers, plus a held-gun kick
static float kickP=0, kickY=0, gunKick=0;
static Vector3 aimDir(void)
{
    float ap=pitch+kickP, ay=yaw+kickY;
    return (Vector3){cosf(ap)*sinf(ay),sinf(ap),-cosf(ap)*cosf(ay)};
}
static Camera3D cam; static int frame=0;
static int jumps=0;            // ground jumps used (single jump only)
static float introT=0;         // mission intro banner timer
static const Vector3 OBJV={0,0,-380.0f};
// persistent burning / crater points across the battlefield
#define NBURN 10
static const float BURN_PT[NBURN][2]={
    {-120,-40},{60,-90},{-200,-150},{170,-180},{-60,-230},
    {90,-270},{-150,-310},{40,-350},{200,-300},{-260,-120}};
static float burnT=0; static int burnI=0; static float warAmb=0;
static void battleFX(float dt)
{
    burnT-=dt;
    if(burnT<=0)
    {
        burnT=0.055f;
        float bx=BURN_PT[burnI][0], bz=BURN_PT[burnI][1]; burnI=(burnI+1)%NBURN;
        Vector3 p=(Vector3){bx,Terrain_Height(bx,bz)+1.0f,bz};
        FX_FireLong(p,1.25f);
        if((burnI&1)==0) FX_Smoke((Vector3){p.x,p.y+2.0f,p.z},1.7f);
    }
    // roiling black smoke and flame across the whole front, not just fixed points
    warAmb-=dt;
    if(warAmb<=0)
    {
        warAmb=0.16f;
        float rx=frand(-620,620), rz=frand(-760,-24);
        Vector3 p=(Vector3){rx,Terrain_Height(rx,rz)+1.2f,rz};
        FX_FireLong(p,frand(1.0f,2.0f));
        FX_Smoke((Vector3){p.x,p.y+frand(1.5f,4.5f),p.z},frand(1.8f,3.2f));
        // one in ~5 ambient events is a full distant shell burst
        if((irand(0,4))==0){ FX_Explosion(p,1.5f); if(!gSelfTest) Sfx_Boom(0.35f); }
    }
}
static const char* CRIES[]={"冲啊——！","跟我上！","守住阵地！","为了祖国——！","压过去！"};

// ================= battlefield support: mines, enemy tank, strafing runs =====
#define NMINE 16
typedef struct { Vector3 p; int live; } Mine;
static Mine gMines[NMINE];
typedef struct { Vector3 pos; float yaw, hp; int alive, fireFlare; float cd; } ETank;
static ETank gTank;
static int   gInTank=0; static float gTankCd=0, gTankTur=0;
static float gVehArmor=0;    // while mounted: first 50 dmg is absorbed by the hull
static float gRunCd=0;       // throttle for running foes over
static int touchSprint=0;    // mobile: left stick pushed to the outer ring = sprint
// single damage entry for the player: hull armour absorbs first, HP clamped to 0
static void hurtPlayer(float d)
{
    if(d<=0||hp<=0)return;
    if(gInTank && gVehArmor>0){ float a=d<gVehArmor?d:gVehArmor; gVehArmor-=a; d-=a; }
    hp-=d; if(hp<0)hp=0; if(d>0.5f)dmgCd=fmaxf(dmgCd,0.35f);
}
typedef struct { int on,bombed; Vector3 pos,vel,aim; float t,mg; } EJet;
static EJet  gJet; static float gJetNext=16.0f;

// ---- pine forest / splintered burnt trunks: deterministic scatter so every
// machine (host, client, every re-run) sees exactly the same woods. ----
#define NTREE 220
typedef struct { Vector3 p; float sc; int burnt; } Tree;
static Tree gTrees[NTREE];
static unsigned int thash(unsigned int i,unsigned int j)
{ unsigned int n=i*73856093u ^ j*19349663u; n^=n<<13; return n*1597334677u; }
static void buildTrees(void)
{
    int n=0;
    for(int gx=-13;gx<=13 && n<NTREE;gx++)for(int gz=-13;gz<=13 && n<NTREE;gz++)
    {
        unsigned h=thash((unsigned)(gx+1000),(unsigned)(gz+1000));
        if(h%100>40) continue;
        float fx=gx*94.0f+(int)(h%64)-32;
        float fz=gz*94.0f+(int)((h>>7)%64)-32;
        if(fx*fx+fz*fz<245.0f*245.0f) continue;
        float th=Terrain_Height(fx,fz);
        if(th>148.0f) continue;
        if(Terrain_Normal(fx,fz).y<0.70f) continue;
        int burnt=Map_IsSnow()?0:((h>>15)%100<26);
        float sc=0.85f+((h>>20)%100)/100.0f*0.85f;
        gTrees[n++]=(Tree){{fx,th,fz},sc,burnt};
    }
    for(;n<NTREE;n++) gTrees[n].p=(Vector3){0,-9999,0};
}
static void drawTrees(void)
{
    const Vector3 X={1,0,0};
    for(int i=0;i<NTREE;i++)
    {
        Tree*t=&gTrees[i]; if(t->p.y<-9000) continue;
        float dx=t->p.x-eye.x, dz=t->p.z-eye.z;
        if(dx*dx+dz*dz>260.0f*260.0f) continue;
        float y=t->p.y, k=t->sc;
        if(t->burnt)
        {
            DrawPart(P_CYL,C_BLACK,MatrixIdentity(),MPart((Vector3){t->p.x,y+0.85f*k,t->p.z},X,0,(Vector3){0.20f*k,1.7f*k,0.20f*k}));
            DrawPart(P_BOX,C_BLACK,MatrixIdentity(),MPart((Vector3){t->p.x+0.5f*k,y+1.5f*k,t->p.z},(Vector3){0,1,0},0.5f,(Vector3){0.9f*k,0.10f,0.10f}));
        }
        else
        {
            DrawPart(P_CYL,C_BROWN,MatrixIdentity(),MPart((Vector3){t->p.x,y+1.15f*k,t->p.z},X,0,(Vector3){0.17f*k,2.3f*k,0.17f*k}));
            DrawPart(P_CONE,C_DARKOLIVE,MatrixIdentity(),MPart((Vector3){t->p.x,y+2.6f*k,t->p.z},X,0,(Vector3){1.18f*k,2.1f*k,1.18f*k}));
            DrawPart(P_CONE,C_DARKOLIVE,MatrixIdentity(),MPart((Vector3){t->p.x,y+3.55f*k,t->p.z},X,0,(Vector3){0.90f*k,1.8f*k,0.90f*k}));
            DrawPart(P_CONE,C_DARKOLIVE,MatrixIdentity(),MPart((Vector3){t->p.x,y+4.4f*k,t->p.z},X,0,(Vector3){0.62f*k,1.5f*k,0.62f*k}));
            if(Map_IsSnow())
            {
                DrawPart(P_CONE,C_WHITE,MatrixIdentity(),MPart((Vector3){t->p.x,y+2.75f*k,t->p.z},X,0,(Vector3){1.02f*k,0.55f*k,1.02f*k}));
                DrawPart(P_CONE,C_WHITE,MatrixIdentity(),MPart((Vector3){t->p.x,y+3.72f*k,t->p.z},X,0,(Vector3){0.76f*k,0.45f*k,0.76f*k}));
                DrawPart(P_CONE,C_WHITE,MatrixIdentity(),MPart((Vector3){t->p.x,y+4.6f*k,t->p.z},X,0,(Vector3){0.52f*k,0.4f*k,0.52f*k}));
            }
        }
    }
}
static float gShake=0;
static void warReset(void);
static void warUpdate(float dt);
static void warDraw(void);
static int  tankBulletHit(Vector3 o,Vector3 d,float gT,float dmg);
static void tankToggleMount(void);
static void tankFire(Vector3 muzzle,Vector3 dir,int foesToo,int oursToo);

// fictional last wishes — homage to the frozen company at Chosin, not a quote of a real person
static const char* TALK[]={
    "同志……我是三连的……太冷了，腿没知觉了……",
    "别管我……号一响你们就冲……高地，一定得拿下来……",
    "替我……看看胜利的那天……祖国——万岁……" };

static float groundAt(Vector3 p){ return Terrain_Height(p.x,p.z); }
static Vector3 flatFwd(float a){ return (Vector3){sinf(a),0,-cosf(a)}; }
static float angTo(Vector3 from,Vector3 to){ return atan2f(to.x-from.x, -(to.z-from.z)); }

static void addBlood(Vector3 p)
{
    float specs[3]={1.7f,0.8f,0.6f};
    for(int k=0;k<3;k++)
        for(int i=0;i<MAXBLD;i++) if(!blds[i].on)
        {
            float a=frand(0,2*M_PI), d=frand(0,k?1.2f:0.3f);
            float x=p.x+cosf(a)*d, z=p.z+sinf(a)*d;
            blds[i].p=(Vector3){x,Terrain_Height(x,z)+0.03f,z};
            blds[i].r=specs[k]*frand(0.8f,1.2f); blds[i].rot=frand(0,6.28f); blds[i].on=1;
            break;
        }
}

static void spawnBattle(void)
{
    memset(blds,0,sizeof(blds));
    for(int i=0;i<NF;i++)
    {
        // defenders are dug in ON the objective ridge (three rings around OBJV),
        // facing south toward the assault, so taking the hill actually clears it.
        float aa=i*2.39996f; float rr=26.0f+(float)(i%3)*40.0f;
        float x=OBJV.x+cosf(aa)*rr+frand(-8,8), z=OBJV.z+sinf(aa)*rr+frand(-8,8);
        foes[i].pos=(Vector3){x,0,z}; foes[i].pos.y=Terrain_Height(x,z);
        foes[i].ang=frand(-0.3f,0.3f); foes[i].hp=100; foes[i].alive=1;
        foes[i].fireCd=frand(0.5f,2.5f); foes[i].state=0; foes[i].cryT=0; foes[i].aware=0;
    }
    for(int i=0;i<NP;i++)
    {
        float x=(i-(NP-1)*0.5f)*14+frand(-4,4), z=150+frand(-12,12);
        pals[i].pos=(Vector3){x,0,z}; pals[i].pos.y=Terrain_Height(x,z);
        pals[i].ang=0; pals[i].hp=100; pals[i].alive=1; pals[i].fireCd=frand(0.6f,2);
        pals[i].state=0; pals[i].cryT=0; pals[i].cry=irand(0,4);
    }
    // one wounded comrade waiting behind the start line
    woundOn=1; talkOpen=0; talkPage=0;
    float wx=-18.0f, wz=118.0f;
    wound=(Man){0}; wound.pos=(Vector3){wx,Terrain_Height(wx,wz),wz};
    wound.ang=0.4f; wound.hp=30; wound.alive=1;
}

static int foesAlive(void){int n=0;for(int i=0;i<NF;i++)if(foes[i].alive)n++;return n;}
static int palsAlive(void){int n=0;for(int i=0;i<NP;i++)if(pals[i].alive)n++;return n;}

static bool los(Vector3 a,Vector3 b)
{
    // dense multi-sample march: any sample below the ridge line means a hill
    // blocks the shot/sight. A single midpoint test let rounds clip through
    // ridgelines, which read as "bullets through walls / aimbot".
    Vector3 d=vsub(b,a); float L=vlen(d);
    if(L<1e-3f) return true;
    int n=(int)(L/3.5f); if(n<2)n=2; if(n>60)n=60;
    Vector3 s=vmul(d,1.0f/(float)n);
    for(int k=1;k<n;k++)
    {
        Vector3 p=vadd(a,vmul(s,(float)k));
        if(p.y < Terrain_Height(p.x,p.z)+1.35f) return false;
    }
    return true;
}

// smallest signed turn from current heading a to desired heading b (-PI..PI)
static float fAngDiff(float want,float cur)
{
    float d=want-cur;
    while(d> M_PI)d-=2.0f*M_PI;
    while(d<-M_PI)d+=2.0f*M_PI;
    return d;
}

static void killFoe(int i,int byPlayer,const char*how)
{
    if(!foes[i].alive)return;
    foes[i].alive=0; foes[i].state=9;
    Vector3 c=(Vector3){foes[i].pos.x,foes[i].pos.y+1.1f,foes[i].pos.z};
    FX_Blood(c); addBlood(foes[i].pos);
    if(byPlayer)foesKilled++;
    if(how&&how[0])kfPush(how);
}

static void killPal(int i)
{
    if(!pals[i].alive)return;
    pals[i].alive=0; pals[i].state=9;
    Vector3 c=(Vector3){pals[i].pos.x,pals[i].pos.y+1.1f,pals[i].pos.z};
    FX_Blood(c); addBlood(pals[i].pos);
    kfPush("战友中弹 · 壮烈牺牲");
}

static int magCap(int w){ return w==0?5:30; }   // Mosin 5, AKM 30

static void reloadWeapon(void)
{
    if(reload>0||weapon>=2)return;
    int need=magCap(weapon)-mag[weapon];
    int take=need<reserve[weapon]?need:reserve[weapon];
    if(take<=0) return;                  // no reserve ammunition left: dry, cannot reload
    reloadTake=take;
    reload = weapon==0?75:100;          // frames @60fps (~1.25s / ~1.67s)
}

// broadsword or bare-hand melee strike
static void melee(void)
{
    if(meleeCd>0)return;
    int blade=(weapon==2);
    meleeCd=blade?0.55f:0.40f; meleeSwing=1.0f; Weapon_SwingTick();
    float reach=blade?2.7f:1.9f, dmg=blade?120.0f:20.0f;
    Vector3 d=aimDir();
    int best=-1; float bd=1e9f;
    for(int i=0;i<NF;i++)if(foes[i].alive)
    {
        Vector3 c=(Vector3){foes[i].pos.x,foes[i].pos.y+1.2f,foes[i].pos.z};
        Vector3 to=vsub(c,eye); float dist=vlen(to);
        if(dist<reach && dist<bd && vdot(vnorm(to),d)>0.5f){bd=dist;best=i;}
    }
    if(best>=0)
    {
        Vector3 c=(Vector3){foes[best].pos.x,foes[best].pos.y+1.2f,foes[best].pos.z};
        FX_Blood(c); Sfx_Foot(); hitMark=0.18f;
        foes[best].hp-=dmg;
        if(foes[best].hp<=0)killFoe(best,1,weapon==2?"大刀劈倒美军士兵":"拳击毙美军士兵");
    }
}

// --------------------------------------------------------------------- grenade
static void throwGrenade(float power)
{
    if(grenades<=0||gre.on)return;
    grenades--;
    Vector3 d=aimDir();
    gre.on=1; gre.age=0; gre.landed=0;
    gre.p=vadd(eye,vadd(vmul(d,0.7f),(Vector3){0,0.15f,0}));
    gre.v=vadd(vmul(d,9.0f+11.0f*power),(Vector3){0,3.6f+1.6f*power,0});
}
static void explodeGrenade(void)
{
    FX_Explosion(gre.p,1.7f); FX_Fireball(gre.p,1.1f); FX_Smoke(gre.p,1.4f); Sfx_Boom(0.95f);
    for(int i=0;i<NF;i++)if(foes[i].alive)
    {
        float d=vlen(vsub(foes[i].pos,gre.p));
        if(d<7.0f){ foes[i].hp-=120.0f*(1.0f-d/7.0f); if(foes[i].hp<=0)killFoe(i,1,"手榴弹炸死美军士兵"); }
    }
    float pd=vlen((Vector3){eye.x-gre.p.x,0,eye.z-gre.p.z});
    if(pd<7.5f){ hurtPlayer((1.0f-pd/7.5f)*85.0f); dmgCd=fmaxf(dmgCd,2.0f); }
    // friendly fire is real: a bad throw wounds or kills comrades too
    for(int i=0;i<NP;i++)if(pals[i].alive)
    {
        float d=vlen(vsub(pals[i].pos,gre.p));
        if(d<6.5f)
        {
            pals[i].hp-=120.0f*(1.0f-d/6.5f);
            if(pals[i].hp<=0)
            {
                pals[i].alive=0; pals[i].state=9;
                FX_Blood(pals[i].pos);
                Vector3 bp=(Vector3){pals[i].pos.x,Terrain_Height(pals[i].pos.x,pals[i].pos.z)+0.03f,pals[i].pos.z};
                FX_Blood(bp);
                int nearp=-1; float nd=1e9f;
                for(int q=0;q<NP;q++) if(pals[q].alive)
                { float dd=vlen(vsub(pals[q].pos,pals[i].pos)); if(dd<nd){nd=dd;nearp=q;} }
                if(nearp>=0){ pals[nearp].cryT=1.8f; pals[nearp].cry=2; }
            }
        }
    }
#if defined(HOST_NET)
    if(gCoopRole==1)for(int i=1;i<BNET_MAXPLY;i++)if(av[i].alive)
    {
        float d=vlen(vsub(av[i].pos,gre.p));
        if(d<6.5f)av[i].hp-=120.0f*(1.0f-d/6.5f);
    }
#endif
    // a bundle grenade can knock out the enemy tank
    if(gTank.alive)
    {
        float d=vlen(vsub(gTank.pos,gre.p));
        if(d<6.0f)
        {
            gTank.hp-=95.0f*(1.0f-d/6.0f);
            if(gTank.hp<=0)
            {
                gTank.alive=0; gTank.hp=0; gTank.fireFlare=1; gTank.cd=0;
                FX_Explosion((Vector3){gTank.pos.x,gTank.pos.y+1.4f,gTank.pos.z},3.0f);
                FX_Fireball((Vector3){gTank.pos.x,gTank.pos.y+1.4f,gTank.pos.z},2.0f);
                Sfx_Boom(1.0f); gShake=1.0f;
            }
        }
    }
    gre.on=0;
}
static void updateGrenade(float dt)
{
    if(!gre.on)return;
    gre.age+=dt;
    gre.v.y-=GRAVITY*dt;
    gre.p=vadd(gre.p,vmul(gre.v,dt));
    float gh=Terrain_Height(gre.p.x,gre.p.z)+0.18f;
    if(gre.p.y<gh)
    {
        gre.p.y=gh;
        if(gre.v.y<0)gre.v.y=-gre.v.y*0.34f;
        gre.v.x*=0.62f; gre.v.z*=0.62f;
        if(fabsf(gre.v.y)<1.3f)gre.landed=1;
    }
    // fixed 2.5-second fuse from release (MK2-style timed grenade), independent
    // of bounce/rest, so the player can cook it but it always pops at 2.5 s.
    if(gre.age>=2.5f)explodeGrenade();
}

// ============================================================ RPG-7 anti-tank
#define ROK_SPEED 56.0f
static void killArmor(Armor* a)
{
    a->alive=0; a->hp=0; a->burn=1;
    Vector3 bp=(Vector3){a->pos.x,Terrain_Height(a->pos.x,a->pos.z)+1.3f,a->pos.z};
    FX_Explosion(bp,3.0f); FX_Fireball(bp,2.2f); FX_Smoke(bp,2.6f); Sfx_Boom(1.0f); gShake=1.0f;
}
static void explodeRpg(Vector3 p)
{
    p.y=Terrain_Height(p.x,p.z)+0.7f;
    FX_Explosion(p,2.7f); FX_Fireball(p,1.9f); FX_Smoke(p,2.6f); Sfx_Boom(1.0f);
    float dc=vlen(vsub(p,cam.position)); if(dc<46.0f) gShake=fmaxf(gShake,1.1f*(1.0f-dc/46.0f));
    // infantry inside the shaped-charge blast
    for(int i=0;i<NF;i++)if(foes[i].alive)
    { float d=vlen(vsub(foes[i].pos,p));
      if(d<8.5f){ foes[i].hp-=160.0f*(1.0f-d/8.5f); if(foes[i].hp<=0)killFoe(i,1,"火箭弹轰毙美军士兵"); } }
    // armoured targets: a direct/near hit knocks out a truck/AA; a tank needs a
    // near-direct hit (its radius is tighter), matching a 73 mm HEAT warhead.
    for(int i=0;i<NARM;i++)if(gArm[i].alive)
    { float rad=(gArm[i].kind==2)?4.2f:3.4f;
      float d=vlen(vsub(gArm[i].pos,p));
      if(d<rad){ gArm[i].hp-=210.0f*(1.0f-d/(rad+1.5f)); if(gArm[i].hp<=0) killArmor(&gArm[i]); } }
    // the roaming enemy tank can be knocked out the same way
    if(gTank.alive)
    { float d=vlen(vsub(gTank.pos,p));
      if(d<4.2f){ gTank.hp-=180.0f*(1.0f-d/5.5f);
        if(gTank.hp<=0){ gTank.alive=0; gTank.hp=0; gTank.fireFlare=1;
            Vector3 bp=(Vector3){gTank.pos.x,gTank.pos.y+1.4f,gTank.pos.z};
            FX_Explosion(bp,3.0f); FX_Fireball(bp,2.0f); Sfx_Boom(1.0f); gShake=1.0f; } } }
    // backfire / friendly fire is real if a rocket lands short
    if(hp>0){ float d=vlen((Vector3){eye.x-p.x,0,eye.z-p.z}); if(d<7.0f){ hurtPlayer((1.0f-d/7.0f)*95.0f); dmgCd=fmaxf(dmgCd,2.5f); } }
    for(int i=0;i<NP;i++)if(pals[i].alive)
    { float d=vlen(vsub(pals[i].pos,p));
      if(d<6.5f){ pals[i].hp-=150.0f*(1.0f-d/6.5f); if(pals[i].hp<=0)killPal(i); } }
}
static void fireRpg(void)
{
    if(rpgReload>0||rpgFireCd>0||!rpgLoaded)return;
    rpgLoaded=0; rpgFireCd=0.65f;
    Vector3 d=aimDir();
    Vector3 mz=vadd(eye,vadd(vmul(d,1.15f),(Vector3){0,-0.04f,0}));
    for(int s=0;s<4;s++) if(!gRok[s].on){ gRok[s].on=1; gRok[s].age=0; gRok[s].p=mz; gRok[s].v=vmul(d,ROK_SPEED); break; }
    FX_Muzzle(RifleMuzzle(cam,4)); FX_Smoke(mz,1.2f); Sfx_Boom(0.95f);
    kickP+=0.075f; gunKick=1.0f;
    // dangerous back-blast cone directly behind the gunner
    Vector3 back=(Vector3){eye.x-d.x*3.2f,eye.y-0.2f,eye.z-d.z*3.2f};
    FX_Smoke(back,1.6f);
    for(int i=0;i<NP;i++)if(pals[i].alive)
    { float dd=vlen(vsub(pals[i].pos,back)); if(dd<3.2f){ pals[i].hp-=30.0f*(1.0f-dd/3.2f); if(pals[i].hp<=0)killPal(i); } }
}
static void startRpgReload(void)
{
    if(rpgReload>0||rpgLoaded||rpgReserve<=0)return;
    rpgReload=112.0f;   // ~1.9 s rear-load animation
}
static void drawRocketInFlight(Rok* rk)
{
    Vector3 d=vnorm(rk->v);
    Quaternion q=QuaternionFromVector3ToVector3((Vector3){0,1,0},d);
    Vector3 bc=vsub(rk->p,vmul(d,0.22f));
    Matrix Mb=MatrixMultiply(MatrixScale(0.05f,0.62f,0.05f),
               MatrixMultiply(QuaternionToMatrix(q),MatrixTranslate(bc.x,bc.y,bc.z)));
    DrawPart(P_CYL,C_DARKOLIVE,MatrixIdentity(),Mb);
    Matrix Mh=MatrixMultiply(MatrixScale(0.085f,0.15f,0.085f),
               MatrixMultiply(QuaternionToMatrix(q),MatrixTranslate(rk->p.x,rk->p.y,rk->p.z)));
    DrawPart(P_CONE,C_DARK,MatrixIdentity(),Mh);
}
static void updateRockets(float dt)
{
    for(int s=0;s<4;s++) if(gRok[s].on)
    {
        Rok* r=&gRok[s]; r->age+=dt;
        r->v.y-=GRAVITY*0.10f*dt;                 // fin-stabilised, very flat arc
        r->p=vadd(r->p,vmul(r->v,dt));
        Vector3 d=vnorm(r->v);
        FX_Smoke(vsub(r->p,vmul(d,0.4f)),0.55f);
        if(((int)(r->age*40))%2==0) FX_EngineSmoke(r->p);
        int hit=0;
        if(r->p.y<=Terrain_Height(r->p.x,r->p.z)+0.12f) hit=1;
        for(int i=0;i<NARM&&!hit;i++)if(gArm[i].alive)
        { float rad=(gArm[i].kind==2)?3.4f:2.6f;
          if(vlen((Vector3){r->p.x-gArm[i].pos.x,0,r->p.z-gArm[i].pos.z})<rad
             && fabsf(r->p.y-(Terrain_Height(gArm[i].pos.x,gArm[i].pos.z)+1.3f))<2.4f) hit=1; }
        if(!hit && gTank.alive &&
           vlen((Vector3){r->p.x-gTank.pos.x,0,r->p.z-gTank.pos.z})<3.2f
           && fabsf(r->p.y-(gTank.pos.y+1.3f))<2.6f) hit=1;
        if(r->age>6.0f) hit=1;
        if(hit){ Vector3 hp2=r->p; r->on=0; explodeRpg(hp2); }
    }
}
static void drawRockets(void)
{
    for(int s=0;s<4;s++) if(gRok[s].on) drawRocketInFlight(&gRok[s]);
}
static void drawArmor(void)
{
    for(int i=0;i<NARM;i++)
    {
        Armor* a=&gArm[i]; float th=Terrain_Height(a->pos.x,a->pos.z);
        Vector3 p=(Vector3){a->pos.x,th,a->pos.z};
        if(a->alive) DrawVehicle(p,a->yaw,a->kind,1.0f);
        else
        {   // burned-out hulk + persistent flames and black smoke
            DrawVehicle(p,a->yaw,0,1.0f);
            Vector3 f=(Vector3){p.x,th+1.2f,p.z};
            FX_FireLong(f,1.5f);
            if(((int)(timeAlive*3.0f)+i)%2==0) FX_Smoke((Vector3){f.x,f.y+1.5f,f.z},1.9f);
        }
    }
}

static float terrainRayT(Vector3 o,Vector3 d)
{
    // dense 1.1 m march out to 700 m: ridges always occlude rounds (no wall hacks)
    float t=0;
    for(int i=0;i<640;i++)
    {
        t+=1.1f; if(t>700)break;
        Vector3 p=vadd(o,vmul(d,t));
        if(p.y<Terrain_Height(p.x,p.z)+0.05f)return t;
    }
    return 700;
}

static void shoot(void)
{
    if(reload>0||fireCd>0)return;
    if(mag[weapon]<=0){ reloadWeapon(); return; }
    mag[weapon]--; fireCd = weapon==0?0.75f:0.10f; Sfx_Gun();
    // 散布：莫辛本就极准(.002)，AKM 连发有散布；开镜后整体再收窄到 0.4（对齐网页版）
    float sp=(weapon==0?0.002f:0.012f)*(ads>0.6f?0.4f:1.0f);
    Vector3 d=vnorm(vadd(aimDir(),v3(frand(-sp,sp),frand(-sp,sp),frand(-sp,sp))));
    Vector3 mz=RifleMuzzle(cam,weapon);
    FX_Muzzle(mz);
    float gT=terrainRayT(eye,d);
    int hit=-1, zone=1; float best=gT; Vector3 hitPoint={0};
    // body zones: head / torso / legs, each its own radius and height
    const float zy[3]={1.76f,1.22f,0.45f}, zr[3]={0.33f,0.62f,0.55f};
    for(int i=0;i<NF;i++)if(foes[i].alive)
        for(int z=0;z<3;z++)
    {
        Vector3 c=(Vector3){foes[i].pos.x,foes[i].pos.y+zy[z],foes[i].pos.z};
        Vector3 to=vsub(c,eye); float t=vdot(to,d);
        if(t<=0)continue; Vector3 cp=vadd(eye,vmul(d,t));
        if(vlen(vsub(cp,c))<zr[z] && t<best){best=t;hit=i;zone=z;hitPoint=cp;}
    }
    if(hit>=0)
    {
        FX_Tracer(mz,hitPoint,(Color){255,235,170,255},0.06f);
        // sniper: headshot is one-shot kill anywhere on the head; torso/legs take half
        // AKM: lethal headshot, lighter torso/leg wounds
        float dmg;
        if(weapon==0) dmg=(zone==0)?110.0f:50.0f;
        else          dmg=(zone==0)?85.0f:(zone==1?26.0f:16.0f);
        foes[hit].hp-=dmg;
        hitMark=0.18f;
        if(zone==0){ headMsgT=0.6f; Sfx_Flak(); }
        if(foes[hit].hp<=0)killFoe(hit,1,
            zone==0?(weapon==0?"狙击爆头 · 一枪毙命":"AKM 爆头击毙敌人")
                   :(weapon==0?"莫辛-纳甘步枪击毙敌人":"AKM 击毙美军士兵"));
    }
    else
    {
        float tdmg=(weapon==0)?5.0f:2.5f;
        if(!tankBulletHit(eye,d,gT,tdmg))
        {
            Vector3 ep=vadd(eye,vmul(d,best));
            FX_Tracer(mz,ep,(Color){255,225,150,255},0.05f);
            FX_Muzzle(ep);
        }
        else FX_Tracer(mz,vadd(eye,vmul(d,best<gT?best:gT)),(Color){255,225,150,255},0.05f);
    }
    // TRUE recoil: muzzle rises (view pitches UP) and a tiny random yaw;
    // aiming down sights keeps it tighter. It recovers in updatePlayer().
    float adsMul=1.0f-0.45f*ads;
    if(weapon==1){ kickP+=0.0105f*adsMul; kickY+=frand(-0.005f,0.005f)*adsMul; }
    else         { kickP+=0.020f*adsMul;  kickY+=frand(-0.004f,0.004f)*adsMul; }
    gunKick=1.0f;
}

static void updateFoes(float dt)
{
    for(int i=0;i<NF;i++) if(foes[i].alive)
    {
        Man*m=&foes[i];
        // engage the NEAREST visible target — player, a charging comrade, or a
        // co-op comrade — instead of every rifle tunnelling on the player.
        Vector3 chest=(Vector3){m->pos.x,m->pos.y+1.5f,m->pos.z};
        Vector3 threat=eye; float bestD=1e30f; int tKind=0, tIdx=-1;
        if(hp>0){ float dd=vlen(vsub(eye,chest)); if(dd<bestD){bestD=dd;threat=eye;tKind=0;} }
        for(int k=0;k<NP;k++) if(pals[k].alive)
        { Vector3 pc=(Vector3){pals[k].pos.x,pals[k].pos.y+1.4f,pals[k].pos.z};
          float dd=vlen(vsub(pc,chest)); if(dd<bestD){bestD=dd;threat=pc;tKind=1;tIdx=k;} }
#if defined(HOST_NET)
        if(gCoopRole==1) for(int k=1;k<BNET_MAXPLY;k++) if(av[k].alive)
        { float dd=vlen(vsub(av[k].pos,chest)); if(dd<bestD){bestD=dd;threat=av[k].pos;tKind=2;tIdx=k;} }
#endif
        float d=bestD;
        bool see = d<430 && los(chest,threat);
        if(see) m->state=1;
        Vector3 move={0};
        if(m->state==0) // dug in on the objective ridge: hold the ring, face the assault
        {
            float aa=i*2.39996f; float rr=26.0f+(float)(i%3)*40.0f;
            Vector3 anc=(Vector3){OBJV.x+cosf(aa)*rr,0,OBJV.z+sinf(aa)*rr};
            float ad=vlen((Vector3){m->pos.x-anc.x,0,m->pos.z-anc.z});
            if(ad>14.0f) move=vmul(vnorm(vsub(anc,m->pos)),6.0f);   // drift back into position
            else
            {
                // watch toward the approaching player; skirmishers step south to make contact
                float pd=vlen((Vector3){eye.x-m->pos.x,0,eye.z-m->pos.z});
                if(pd>250.0f && (i%6)==(frame/120)%6) move=vmul(flatFwd(angTo(m->pos,eye)),6.5f);
            }
            float wantAng=angTo(m->pos,eye);
            m->ang+=clampf(fAngDiff(wantAng,m->ang),-2.2f*dt,2.2f*dt);
        }
        else if(m->state==1)
        {
            float wantAng=angTo(m->pos,threat);
            float turn=clampf(fAngDiff(wantAng,m->ang),-2.6f*dt,2.6f*dt);
            m->ang+=turn;                                   // 转身需要时间，不再瞬移锁头
            float facing=fabsf(fAngDiff(wantAng,m->ang));
            // occasional sidestep
            Vector3 side=(Vector3){cosf(m->ang),0,sinf(m->ang)};
            move=vmul(side, sinf(frame*0.6f+i)*2.2f);
            if(see)m->aware+=dt; else { m->aware=0.0f; if(d>60)m->state=0; }
            m->fireCd-=dt;
            // 必须先发现(aware)、转身对准(facing)、视线无遮挡(los)，且首发有延迟才开火
            if(!gSelfTest && see && m->aware>0.55f && facing<0.22f && m->fireCd<=0 && d<320)
            {
                m->fireCd=frand(1.3f,2.7f);
                Vector3 mzc=(Vector3){m->pos.x,m->pos.y+1.45f,m->pos.z};
                // 两发点射；弹着散布随距离明显放大，不再“枪枪上身”
                float spread=1.6f+d*0.012f;
                for(int b=0;b<2;b++)
                    FX_Tracer(mzc,vadd(threat,v3(frand(-spread,spread),frand(-spread,spread),frand(-spread,spread))),(Color){255,190,110,255},0.07f);
                float chance=clampf(0.30f-d*0.0011f,0.02f,0.17f);
                for(int b=0;b<2;b++) if(frand(0,1)<chance)
                {
                    float hd=frand(3.0f,7.0f);
                    if(tKind==2){
#if defined(HOST_NET)
                        av[tIdx].hp-=hd; if(av[tIdx].hp<=0){av[tIdx].hp=0;av[tIdx].alive=0;}
#endif
                    } else if(tKind==1){
                        hd*=0.5f;                          // 战友不被瞬间融化
                        pals[tIdx].hp-=hd; if(pals[tIdx].hp<=0) killPal(tIdx);
                    } else hurtPlayer(hd);
                }
            }
        }
        else
        {
            // dug-in: still engage if visible
            if(see){m->state=1;continue;}
            m->ang=angTo(m->pos,(Vector3){m->pos.x,0,m->pos.z-1});
        }
        m->pos=vadd(m->pos,vmul(move,dt));
        m->pos.y=Terrain_Height(m->pos.x,m->pos.z);
        if(fabsf(m->pos.x)>WORLD_HALF-20)m->pos.x=WORLD_HALF-20;
    }
}

static void updatePals(float dt)
{
    for(int i=0;i<NP;i++) if(pals[i].alive)
    {
        Man*m=&pals[i];
        // find nearest visible foe
        int tgt=-1; float bd=1e9;
        for(int j=0;j<NF;j++)if(foes[j].alive)
        {
            float d=vlen(vsub(foes[j].pos,m->pos));
            if(d<bd){bd=d;tgt=j;}
        }
        Vector3 chest=(Vector3){m->pos.x,m->pos.y+1.5f,m->pos.z};
        // squad assault line: spread across the player and keep pressing north
        // toward the objective; the leader (i==0) stays a few steps ahead.
        float lane=(i-(NP-1)*0.5f)*9.0f;
        float leadZ=OBJV.z+30.0f + (i==0?-6.0f:0.0f);
        Vector3 form=(Vector3){eye.x+lane,0,(eye.z-16.0f<leadZ)?eye.z-16.0f:leadZ};
        Vector3 mv={0};
        if(tgt>=0)
        {
            Vector3 tc=(Vector3){foes[tgt].pos.x,foes[tgt].pos.y+1.4f,foes[tgt].pos.z};
            float wantAng=angTo(m->pos,tc);
            m->ang+=clampf(fAngDiff(wantAng,m->ang),-3.4f*dt,3.4f*dt);   // turn rate-limited
            // charge to contact, then use the ground (short strafes) instead of bunching up
            if(bd>118.0f) mv=vmul(flatFwd(m->ang),10.5f);
            else
            {
                Vector3 side=(Vector3){cosf(m->ang),0,sinf(m->ang)};
                mv=vmul(side, sinf(timeAlive*1.7f+i*1.3f)*3.2f);
                // keep roughly on the assault line so the squad doesn't clump on the player
                Vector3 toForm=vsub(form,m->pos); toForm.y=0;
                if(vlen(toForm)>10.0f) mv=vadd(mv,vmul(vnorm(toForm),4.5f));
            }
            m->pos=vadd(m->pos,vmul(mv,dt)); m->pos.y=Terrain_Height(m->pos.x,m->pos.z);
            // aimed fire once they can actually see the ridge line
            m->fireCd-=dt;
            if(bd<400 && m->fireCd<=0 && los(chest,tc))
            {
                m->fireCd=frand(0.45f,1.15f);
                Vector3 aim=vadd(tc,v3(frand(-1.6f,1.6f),frand(-1.6f,1.6f),frand(-1.6f,1.6f)));
                FX_Tracer(chest,aim,(Color){255,235,170,255},0.06f);
                if(frand(0,1)<0.22f){ foes[tgt].hp-=36; if(foes[tgt].hp<=0)killFoe(tgt,0,"战友击毙美军士兵"); }
                if(frand(0,1)<0.10f){ m->cryT=1.8f; m->cry=irand(0,4); }
            }
        }
        else
        {
            Vector3 mv2=vsub(form,m->pos); mv2.y=0;
            if(vlen(mv2)>1.5f) mv=vmul(vnorm(mv2),9.5f);
            m->pos=vadd(m->pos,vmul(mv,dt)); m->pos.y=Terrain_Height(m->pos.x,m->pos.z);
            m->ang+=clampf(fAngDiff(angTo(m->pos,form),m->ang),-3.4f*dt,3.4f*dt);
        }
        if(m->cryT>0)m->cryT-=dt;
    }
}

static void updatePlayer(float dt)
{
    if(!gSelfTest)
    {
        // On Android every touch is also faked as a mouse by raylib; using the
        // mouse here would fire/look from ANY touch. So mouse is desktop-only,
        // touch comes only through Touch_* (fire only from the 火 button).
#if defined(PLATFORM_ANDROID)
        float mdx=Touch_LookDX(), mdy=Touch_LookDY();
        int fireMouse=0, adsMouse=0;
#else
        Vector2 md=GetMouseDelta();
        float mdx=md.x+Touch_LookDX(), mdy=md.y+Touch_LookDY();
        int fireMouse=IsMouseButtonDown(MOUSE_BUTTON_LEFT);
        int adsMouse=IsMouseButtonDown(MOUSE_BUTTON_RIGHT);
#endif
        // 8x sniper glass needs much finer aim at full ADS; AKM red dot stays quick.
        float lookMul=1.0f-ads*(weapon==0?0.66f:0.35f);
        yaw+=mdx*0.0026f*lookMul; pitch-=mdy*0.0026f*lookMul;   // 鼠标右移→视角右转
        pitch=clampf(pitch,-1.5f,1.5f);
        if(IsKeyPressed(KEY_ONE))weapon=0;
        if(IsKeyPressed(KEY_TWO))weapon=1;
        if(IsKeyPressed(KEY_THREE))weapon=2;
        if(IsKeyPressed(KEY_ZERO))weapon=3;
        if(gRpgMode && IsKeyPressed(KEY_FOUR))weapon=4;   // RPG-7 only carried in the AT scenario
        if(Touch_SwitchPressed())
        {
            if(gRpgMode){ static const int SEQ[5]={4,0,1,2,3}; int k=0;
                for(int q=0;q<5;q++) if(SEQ[q]==weapon){k=q;break;} weapon=SEQ[(k+1)%5]; }
            else weapon=(weapon+1)%4;
        }
        if(IsKeyPressed(KEY_R)||Touch_BPressed()){ if(weapon==4)startRpgReload(); else reloadWeapon(); }
        if(IsKeyPressed(KEY_F))tankToggleMount();
        if(Touch_MountPressed())tankToggleMount();   // dedicated on-screen 车 button
        if(fireMouse||Touch_FireHeld()){ if(gInTank){ /* cannon handled in warUpdate */ }
            else if(weapon==4)fireRpg(); else if(weapon>=2)melee(); else shoot(); }
        // grenade: hold M (desktop) or HOLD the 雷 button (phone) to charge,
        // release to lob; the dotted parabola is drawn in the 3D pass.
        if(grenades>0&&!gre.on)
        {
            if(greCd>0) greCd-=dt;   // cooldown ticking: neither charge nor throw
            // cancel a readied throw: left on-screen 取消 key (phone) or X (desktop).
            // The grenade is NOT consumed; a 1 s cooldown then blocks re-arming so a
            // stray tap on 雷 / M right after cancelling can't instantly re-ready it.
            else if(greCharging && (Touch_GreCancelPressed()||IsKeyPressed(KEY_X))){ greCharging=0; greHold=0; greCd=1.0f; }
            else if(IsKeyDown(KEY_M)||Touch_GreHeld()){greCharging=1;greHold+=dt/1.15f;if(greHold>1)greHold=1;}
            else if(greCharging){ float pw=greHold; greCharging=0;greHold=0; throwGrenade(pw<0.12f?0.45f:pw); }
        }
        else { greCharging=0;greHold=0; }
        adsMouseHold=adsMouse;
    }
    else
    {
        // automated verification: march north and fire. In co-op the host stays
        // dug in so the charging client avatar stays visible in its screenshot.
        yaw=0;
        if(frame%30==0){ if(weapon==4){ startRpgReload(); rpgLoaded=1; fireRpg(); } else { mag[weapon]=10; shoot(); } }
        if(gCoopRole!=1) eye.z-=22*dt;
    }
    if(reload>0){ reload-=dt*60.0f;
        if(reload<=0){ reload=0; mag[weapon]+=reloadTake; reserve[weapon]-=reloadTake; reloadTake=0; } }
    // RPG-7 rear-load animation: a fresh HEAT round is slid into the tube from behind
    if(rpgReload>0){ rpgReload-=dt*60.0f;
        if(rpgReload<=0){ rpgReload=0; if(rpgReserve>0){ rpgReserve--; rpgLoaded=1; } } }
    if(rpgFireCd>0)rpgFireCd-=dt;
    updateRockets(dt);
    fireCd-=dt;
    if(meleeCd>0)meleeCd-=dt;
    if(meleeSwing>0)meleeSwing-=dt*3.2f; if(meleeSwing<0)meleeSwing=0;
    if(headMsgT>0)headMsgT-=dt;
    updateGrenade(dt);
    Vector3 f=flatFwd(yaw), r=(Vector3){cosf(yaw),0,sinf(yaw)};
    Vector3 keyWish={0};
    if(!gInTank)
    {
    if(IsKeyDown(KEY_W))keyWish=vadd(keyWish,f);
    if(IsKeyDown(KEY_S))keyWish=vsub(keyWish,f);
    if(IsKeyDown(KEY_D))keyWish=vadd(keyWish,r);
    if(IsKeyDown(KEY_A))keyWish=vsub(keyWish,r);
    }
    float spd=IsKeyDown(KEY_Q)?9.6f:6.2f;
    if(vlen(keyWish)>0)keyWish=vmul(vnorm(keyWish),spd);
    // touch stick (analog magnitude; inert on desktop; inert while driving tank).
    // 手机端：左摇杆推到底(>0.92)即冲刺，桌面端则用 Q 键冲刺。
    Vector3 tWish={0}; touchSprint=0;
    if(!gInTank)
    { float ax=Touch_AxisX(), ay=Touch_AxisY();
      float mag=sqrtf(ax*ax+ay*ay); if(mag>1)mag=1;
      touchSprint=(mag>0.92f);
      float tsp=touchSprint?9.6f:6.2f;
      tWish=vmul(vadd(vmul(f,ay),vmul(r,ax)),tsp); }
    Vector3 wish=vadd(keyWish,tWish);
    float maxSpd=9.6f;
    if(vlen(wish)>maxSpd)wish=vmul(vnorm(wish),maxSpd);
    pvel.x=wish.x; pvel.z=wish.z;
    // historical infantry: a single jump / bound only — no double jump
    if(!gSelfTest&&!gInTank&&(IsKeyPressed(KEY_SPACE)||Touch_ActPressed())&&jumps==0)
    { pvel.y=7.2f; jumps=1; }
    pvel.y-=GRAVITY*dt;
    // recoil recovery (view settles back down; gun returns to rest)
    kickP*=expf(-9.0f*dt); kickY*=expf(-12.0f*dt); gunKick*=expf(-13.0f*dt);
    if(kickP<0.0002f)kickP=0; if(fabsf(kickY)<0.0002f)kickY=0;

    if(gSelfTest)
    {
        eye=vadd(eye,vmul(pvel,dt));
    }
    else
    {
        // ---- collision: can't walk through steep terrain (mountains), with a
        // small step-up so low banks / sandbags are climbable. Axis-separated
        // movement lets the player slide along the obstacle. ----
        float feetNow=Terrain_Height(eye.x,eye.z);
        const float STEP=0.95f, RAD=0.42f;
        float nx=eye.x+pvel.x*dt;
        if(fabsf(pvel.x)>0.01f)
        {
            float h=Terrain_Height(nx+(pvel.x>0?RAD:-RAD),eye.z);
            if(h<=feetNow+STEP) eye.x=nx;
        }
        float nz=eye.z+pvel.z*dt;
        if(fabsf(pvel.z)>0.01f)
        {
            float h=Terrain_Height(eye.x,nz+(pvel.z>0?RAD:-RAD));
            if(h<=feetNow+STEP) eye.z=nz;
        }
        eye.y+=pvel.y*dt;
        // can't overlap friend or foe (simple capsule push-out)
        Man* all[NF+NP]; int na=0;
        for(int i=0;i<NF;i++) if(foes[i].alive) all[na++]=&foes[i];
        for(int i=0;i<NP;i++) if(pals[i].alive) all[na++]=&pals[i];
        for(int i=0;i<na;i++)
        {
            float dx=eye.x-all[i]->pos.x, dz=eye.z-all[i]->pos.z;
            float d2=dx*dx+dz*dz;
            if(d2<0.85f*0.85f && d2>1e-6f)
            { float d=sqrtf(d2), push=(0.85f-d)/d; eye.x+=dx*push; eye.z+=dz*push; }
            else if(d2<=1e-6f){ eye.z+=0.85f; }
        }
        // static wrecked vehicles block the player too (no walking through steel)
        const Vector3 ob[2]={{-90,-1,-60},{110,-1,-120}};
        for(int k=0;k<2;k++)
        {
            float dx=eye.x-ob[k].x, dz=eye.z-ob[k].z, rr=3.0f;
            float d2=dx*dx+dz*dz;
            if(d2<rr*rr && d2>1e-6f){ float d=sqrtf(d2),push=(rr-d)/d; eye.x+=dx*push; eye.z+=dz*push; }
        }
        // tree trunks are solid as well (nearby check only)
        for(int k=0;k<NTREE;k++) if(gTrees[k].p.y>-9000)
        {
            float dx=eye.x-gTrees[k].p.x, dz=eye.z-gTrees[k].p.z;
            float d2=dx*dx+dz*dz;
            if(d2<30.0f && d2>1e-6f && d2<0.75f*0.75f)
            { float d=sqrtf(d2),push=(0.75f-d)/d; eye.x+=dx*push; eye.z+=dz*push; }
        }
    }
    float gy=Terrain_Height(eye.x,eye.z)+1.68f;
    if(eye.y<gy){eye.y=gy; if(pvel.y<=0.0f){pvel.y=0; jumps=0;}}
    // glue the feet to the ground when not airborne: no sinking / seeing under
    if(jumps==0 && eye.y>gy+0.05f) eye.y=gy;
    if(gInTank){ eye=(Vector3){gTank.pos.x,gTank.pos.y+4.3f,gTank.pos.z}; pvel.x=pvel.y=pvel.z=0; }
    if(fabsf(eye.x)>WORLD_HALF-10)eye.x=WORLD_HALF-10;
    if(fabsf(eye.z)>WORLD_HALF-10)eye.z=WORLD_HALF-10;
    // footstep cadence while actually moving on the ground
    if(!gSelfTest)
    {
        int grounded=(eye.y<=gy+0.03f); float hs=sqrtf(pvel.x*pvel.x+pvel.z*pvel.z);
        if(grounded&&hs>1.2f){ stepAcc+=hs*dt; float stride=(IsKeyDown(KEY_Q)||touchSprint)?1.9f:2.3f;
            if(stepAcc>=stride){stepAcc=0;Sfx_Foot();} }
        else stepAcc=0;
    }
    if(dmgCd>0)dmgCd-=dt; else hp+=6*dt;
    hp=clampf(hp,0,100);
    if(hitMark>0)hitMark-=dt;
    // 右键机瞄/狙击镜（仅桌面鼠标；触屏无右键）
    // 莫辛/98k：完整狙击镜（黑幕留圆窗）；AKM：内红点式开镜
    float adsWant=0.0f;
    if(!gSelfTest && (adsMouseHold||Touch_ADSHeld()))
        adsWant=(weapon==0||weapon==1||weapon==4)?1.0f:0.0f;
    ads+=(adsWant-ads)*(1.0f-powf(0.000004f,dt));   // 更快的开镜/收镜过渡（旧版偏慢）
    if(ads<0.001f)ads=0.0f; if(ads>0.999f)ads=1.0f;
}

static void drawScope(void)
{
    if(ads<=0.02f) return;
    int sw=GetScreenWidth(), sh=GetScreenHeight();
    int cx=sw/2, cy=sh/2;
    unsigned char a=(unsigned char)(235*ads);
    if(weapon==0)
    {
        // ---- 莫辛-纳甘 / 98k 狙击镜：黑色幕布遮四周，只留中央圆形镜窗 ----
        float R=sh*0.30f;
        Color black=(Color){0,0,0,a};
        // thick annulus masks everything outside the circular lens
        DrawRing((Vector2){(float)cx,(float)cy},R,sh*1.6f,0,360,96,black);
        // steel lens rim
        DrawRingLines((Vector2){(float)cx,(float)cy},R,R+2.0f,0,360,96,(Color){20,20,22,a});
        DrawRingLines((Vector2){(float)cx,(float)cy},R-3.0f,R-2.0f,0,360,96,(Color){60,60,66,(unsigned char)(150*ads)});
        // subtle in-lens vignette ring
        DrawRingLines((Vector2){(float)cx,(float)cy},R*0.62f,R*0.62f+1,0,360,96,(Color){0,0,0,(unsigned char)(40*ads)});
        // crosshair (fine black reticle) stopping short of the dot
        Color ret=(Color){8,8,10,a};
        int gap=5, L=(int)(R*0.92f);
        DrawLine(cx-L,cy,cx-gap,cy,ret); DrawLine(cx+gap,cy,cx+L,cy,ret);
        DrawLine(cx,cy-L,cx,cy-gap,ret); DrawLine(cx,cy+gap,cx,cy+L,ret);
        // mil-dot ticks
        for(int i=1;i<=4;i++){int x=(int)(i*R*0.2f); DrawLine(cx-x,cy-3,cx-x,cy+3,ret); DrawLine(cx+x,cy-3,cx+x,cy+3,ret);}
        for(int i=1;i<=3;i++){int y=(int)(i*R*0.2f); DrawLine(cx-3,cy-y,cx+3,cy-y,ret); DrawLine(cx-3,cy+y,cx+3,cy+y,ret);}
        // central solid black dot
        DrawCircleV((Vector2){(float)cx,(float)cy},3.0f,ret);
        // 8x marking at the lower arc of the lens
        CNC("8×",cx+ (int)(R*0.72f),cy+(int)(R*0.30f),16,(Color){10,10,12,(unsigned char)(200*ads)});
    }
    else if(weapon==1)
    {
        // ---- AKM：内红点 / 全息式开镜，干净十字分划 + 上下轻压暗 ----
        Color ret=(Color){12,12,14,a};
        Color vg=(Color){0,0,0,(unsigned char)(90*ads)};
        DrawRectangle(0,0,sw,70,vg); DrawRectangle(0,sh-70,sw,70,vg);
        int gap=7, L=150;
        DrawLine(cx-L,cy,cx-gap,cy,ret); DrawLine(cx+gap,cy,cx+L,cy,ret);
        DrawLine(cx,cy-L,cx,cy-gap,ret); DrawLine(cx,cy+gap,cx,cy+L,ret);
        DrawCircleV((Vector2){(float)cx,(float)cy},2.0f,ret);
        for(int i=1;i<=4;i++){int x=i*26; DrawLine(cx-x,cy-4,cx-x,cy+4,ret); DrawLine(cx+x,cy-4,cx+x,cy+4,ret);}
    }
    else if(weapon==4)
    {
        // ---- RPG-7 folding leaf sight + front post (mechanical, no scope) ----
        Color vg=(Color){0,0,0,(unsigned char)(70*ads)};
        DrawRectangle(0,0,sw,60,vg); DrawRectangle(0,sh-60,sw,60,vg);
        Color ret=(Color){10,10,12,(unsigned char)(225*ads)};
        int gap=12, L=120;
        DrawLine(cx-L,cy,cx-gap,cy,ret); DrawLine(cx+gap,cy,cx+L,cy,ret); // rear leaf wings
        // front post rising to a bead just under the aiming point
        Vector2 fp1={(float)cx,(float)cy+4}, fp2={(float)cx,(float)cy+64};
        DrawLineEx(fp1,fp2,5.0f,ret);
        DrawCircleV((Vector2){(float)cx,(float)cy-2},3.2f,ret);
        for(int i=1;i<=4;i++){int y=cy+16+i*15; DrawLine(cx-7,y,cx+7,y,ret);}  // range ladder
        DrawTextEx(GameFont(),"RPG-7 HEAT",(Vector2){cx+L+10,cy-9},15,0,
                   (Color){rpgLoaded?230:255,rpgLoaded?230:60,rpgLoaded?230:50,(unsigned char)(225*ads)});
    }
}

static float wrap180(float a){ while(a>180.0f)a-=360.0f; while(a<-180.0f)a+=360.0f; return a; }

// top-center heading ribbon: 北/东/南/西 + objective bearing chevron, so the
// player always knows which way the fight is.
static void drawCompass(void)
{
    int cx=GetScreenWidth()/2, y=16, half=168; float pd=2.8f;
    DrawRectangle(cx-half,y,half*2,30,(Color){0,0,0,128});
    DrawRectangleLines(cx-half,y,half*2,30,(Color){255,255,255,90});
    float hd=yaw*RAD2DEG;
    for(int a=-180;a<=180;a+=15)
    {
        int x=(int)(cx+wrap180((float)a-hd)*pd);
        if(x<cx-half||x>cx+half)continue;
        if(a%90==0) DrawLine(x,y+7,x,y+17,WHITE); else DrawLine(x,y+10,x,y+15,(Color){220,220,220,180});
    }
    static const struct{int a;const char*t;} L[4]={{0,"北"},{90,"东"},{180,"南"},{-90,"西"}};
    for(int i=0;i<4;i++)
    {
        int x=(int)(cx+wrap180((float)L[i].a-hd)*pd);
        if(x>=cx-half+12&&x<=cx+half-12) CN(L[i].t,x-8,y+17,15,(Color){255,236,190,255});
    }
    // objective bearing
    float dx=OBJV.x-eye.x, dz=OBJV.z-eye.z;
    float bear=atan2f(dx,-dz)*RAD2DEG, dist=sqrtf(dx*dx+dz*dz);
    int ox=(int)(cx+wrap180(bear-hd)*pd);
    if(ox<cx-half+10)ox=cx-half+10; if(ox>cx+half-10)ox=cx+half-10;
    DrawTriangle((Vector2){ox-6,y+30},(Vector2){ox+6,y+30},(Vector2){ox,y+22},(Color){255,90,70,255});
    CNC(TextFormat("目标 %d m",(int)dist),cx,y+50,14,(Color){255,170,150,255});
    // heading tick (where you face now)
    DrawLine(cx,y+4,cx,y+12,(Color){255,240,180,255});
}

// floating health bars: red over enemies, blue over comrades
static void drawHealthBars(void)
{
    Vector3 fwd=vnorm(vsub(cam.target,cam.position));
    for(int team=0;team<2;team++)
    {
        int n=team?NF:NP; float maxD=team?230.0f:150.0f;
        Color col=team?(Color){222,52,46,255}:(Color){60,140,240,255};
        for(int i=0;i<n;i++)
        {
            Man*m=team?&foes[i]:&pals[i];
            if(!m->alive)continue;
            Vector3 head=(Vector3){m->pos.x,m->pos.y+2.15f,m->pos.z};
            Vector3 to=vsub(head,cam.position);
            if(vdot(to,fwd)<=0)continue;
            if(vlen(to)>maxD)continue;
            Vector2 s=GetWorldToScreen(head,cam);
            if(s.x<10||s.x>GetScreenWidth()-10||s.y<10||s.y>GetScreenHeight()-10)continue;
            int w=30,x=(int)s.x-w/2,y=(int)s.y;
            DrawRectangle(x-1,y-1,w+2,6,(Color){0,0,0,170});
            DrawRectangle(x,y,w,4,(Color){40,40,44,230});
            DrawRectangle(x,y,(int)(w*clampf(m->hp/100.0f,0,1)),4,col);
        }
    }
}

static void drawHud(void)
{
    int cx=GetScreenWidth()/2, cy=GetScreenHeight()/2;
    Color cc=(hitMark>0)?(Color){255,80,60,255}:WHITE;
    if(ads<0.5f){
    DrawLine(cx-10,cy,cx-3,cy,cc);DrawLine(cx+3,cy,cx+10,cy,cc);
    DrawLine(cx,cy-10,cx,cy-3,cc);DrawLine(cx,cy+3,cx,cy+10,cc);
    }
    if(hitMark>0) DrawTextEx(GameFont(),"X",(Vector2){cx-7,cy-16},22,2,(Color){255,70,60,255});

    const char* wn=weapon==0?"莫辛-纳甘步枪(5发)":weapon==1?"AKM 突击步枪(100发)":weapon==2?"大刀(近战)":weapon==4?"RPG-7 火箭筒(单发)":"拳头(近战)";
    CN(wn,16,GetScreenHeight()-60,18,(Color){255,236,180,255});
    if(weapon==4)
        CN(rpgReload>0?"装填火箭弹中… 尾部装入 HEAT 弹":TextFormat("膛内 %d / 1    携行弹 %d",rpgLoaded?1:0,rpgReserve),
           16,GetScreenHeight()-34,18,(rpgLoaded||rpgReserve>0)?WHITE:(Color){255,120,110,255});
    else if(weapon>=2) CN("近战 · 左键劈砍/出拳",16,GetScreenHeight()-34,18,WHITE);
    else CN(reload>0?"装填中...":TextFormat("%d / %d   (余弹 %d)",mag[weapon],mag[weapon]+reserve[weapon],reserve[weapon]),
            16,GetScreenHeight()-34,18,WHITE);
    CN(TextFormat("手雷 × %d  (长按M蓄力投掷)",grenades),230,GetScreenHeight()-34,17,(Color){255,205,160,255});
    CN("生命",16,52,16,(Color){230,230,230,255});
    DrawRectangle(70,52,160,14,(Color){0,0,0,140});
    DrawRectangle(70,52,(int)(160*hp/100.0f),14,hp>35?(Color){200,60,50,255}:(Color){235,90,70,255});
    DrawRectangleLines(70,52,160,14,WHITE);
    if(gInTank) CNC(TextFormat("车体护甲 %d / 50",(int)(gVehArmor+0.5f)),150,42,14,(Color){180,220,255,255});
    if(gRpgMode) CNC(TextFormat("装甲目标剩余 %d / %d    歼敌 %d",armorAlive(),NARM,foesKilled),16,74,17,(Color){255,170,120,255});
    else CN(TextFormat("歼敌 %d   残敌 %d   战友 %d",foesKilled,foesAlive(),palsAlive()),16,74,17,(Color){255,232,160,255});
    CN(gRpgMode
        ? "WASD移动 Q冲刺 空格单跳 左键发射火箭弹(单发) 右键机械表尺 1步枪2AKM3大刀0拳头 4=RPG R尾部装弹 M手雷 F登车 E对话 Esc撤退"
        : "WASD移动 Q冲刺 空格单跳 左键攻击 右键瞄准(莫辛贴腮机瞄/AKM开镜) 1步枪 2AKM 3大刀 0拳头 R装填 M(长按)手雷 F登车 E对话  Backspace撤退",
        16,GetScreenHeight()-12,14,(Color){215,220,230,220});
    DrawCornerFlags();
    if(headMsgT>0) CNC("爆 头！",GetScreenWidth()/2,GetScreenHeight()/2+40,26,(Color){255,90,70,235});
    drawCompass();
    if(celebrate>0)
        CNC(planted?"胜 利！红旗插上了高地！":"冲上去——把红旗插上高地！",
            GetScreenWidth()/2,(int)(GetScreenHeight()*0.34f),28,(Color){255,230,150,255});
    else if(introT>0)
        CNC(Map_IsChosin()?"长津湖 · 冰雕连 —— 卧雪潜伏，号响即冲":"夺取前方高地 · 冲啊！",
            GetScreenWidth()/2,(int)(GetScreenHeight()*0.30f),26,(Color){255,228,170,235});

    // objective marker
    Vector3 op=(Vector3){OBJV.x, Terrain_Height(OBJV.x,OBJV.z)+4, OBJV.z};
    Vector2 os=GetWorldToScreen(op,cam);
    const char* goal=Map_IsChosin()?"目标：夜袭隘口·冲锋":"目标：夺取前方高地";
    bool onscreen=os.x>20&&os.x<GetScreenWidth()-20&&os.y>20&&os.y<GetScreenHeight()-20;
    if(onscreen)
    {
        DrawCircleLines((int)os.x,(int)os.y,18,(Color){255,80,60,230});
        CNC(goal,(int)os.x,(int)os.y-34,15,(Color){255,180,160,255});
    }
    else
    {
        Vector2 c={(float)GetScreenWidth()/2,(float)GetScreenHeight()/2};
        float dx=os.x-c.x,dy=os.y-c.y,l=sqrtf(dx*dx+dy*dy); if(l<1)l=1;
        int ax=(int)(c.x+dx/l*150),ay=(int)(c.y+dy/l*150);
        DrawTriangle((Vector2){ax+dy/l*7,ay-dx/l*7},(Vector2){ax-dy/l*7,ay+dx/l*7},(Vector2){ax+dx/l*14,ay+dy/l*14},(Color){255,90,70,220});
    }

    // friendly battle cries
    for(int i=0;i<NP;i++)if(pals[i].alive&&pals[i].cryT>0)
    {
        Vector3 h=(Vector3){pals[i].pos.x,pals[i].pos.y+2.3f,pals[i].pos.z};
        Vector2 s=GetWorldToScreen(h,cam);
        if(s.x>0&&s.x<GetScreenWidth()&&s.y>0&&s.y<GetScreenHeight())
            CNC(CRIES[pals[i].cry],(int)s.x,(int)s.y,14,(Color){255,230,150,235});
    }
    if(dmgCd>0){ int a=(int)(90*(dmgCd/4.0f)); DrawRectangle(0,0,GetScreenWidth(),GetScreenHeight(),(Color){150,0,0,a}); }
    else if(hp<35) DrawRectangle(0,0,GetScreenWidth(),GetScreenHeight(),(Color){90,0,0,60});
    drawHealthBars();
    DrawFPS(GetScreenWidth()-90,GetScreenHeight()-24);
}

static void drawWrapped(const char*t,int x,int y,int sz,int maxW,Color c)
{
    int cx=x;
    for(const unsigned char*p=(const unsigned char*)t;*p;)
    {
        int nb=1; if((*p&0xE0)==0xC0)nb=2; else if((*p&0xF0)==0xE0)nb=3; else if((*p&0xF8)==0xF0)nb=4;
        char ch[5]={0,0,0,0,0}; for(int k=0;k<nb&&p[k];k++)ch[k]=(char)p[k];
        int w=(nb>1)?sz:sz/2;
        if(cx+w>x+maxW){cx=x;y+=sz+6;}
        DrawTextEx(GameFont(),ch,(Vector2){(float)cx,(float)y},(float)sz,2,c);
        cx+=w+1; p+=nb;
    }
}

static void drawTalk(void)
{
    if(talkOpen)
    {
        int bw=(int)(GetScreenWidth()*0.66f), bx=(GetScreenWidth()-bw)/2;
        int by=GetScreenHeight()-250, bh=170;
        DrawRectangle(bx,by,bw,bh,(Color){12,16,24,235});
        DrawRectangleLinesEx((Rectangle){bx,by,bw,bh},3,(Color){240,210,140,255});
        CNC("伤员 · 三连战士",bx+bw/2,by+16,22,(Color){255,228,170,255});
        drawWrapped(TALK[talkPage],bx+24,by+52,21,bw-48,(Color){235,238,245,255});
        CNC(talkPage<2?"按 E / 话 继续":"按 E / 话 合上",bx+bw/2,by+bh-34,17,(Color){200,210,225,235});
    }
    else if(woundOn)
    {
        float wd=vlen((Vector3){eye.x-wound.pos.x,0,eye.z-wound.pos.z});
        if(wd<3.2f)
        {
            Vector3 h=(Vector3){wound.pos.x,wound.pos.y+1.4f,wound.pos.z};
            Vector2 s=GetWorldToScreen(h,cam);
            if(s.x>0&&s.x<GetScreenWidth()&&s.y>0&&s.y<GetScreenHeight())
                CNC("按 E / 话 —— 听他说说",(int)s.x,(int)s.y,16,(Color){255,230,160,255});
        }
    }
}

// =====================================================================
// LAN co-op (host-authoritative). See bnet.h for the wire protocol.
//   (Avatar storage is declared near the top of this file.)
static Vector3 avDir(const Avatar*a)
{ float cp=cosf(a->pitch), sp=sinf(a->pitch);
  return (Vector3){cp*sinf(a->yaw),sp,-cp*cosf(a->yaw)}; }

static void avatarSpawn(Avatar*a)
{
    a->active=1; a->alive=1; a->jumps=0;
    float ox=frand(-6,6);
    a->pos=(Vector3){ox,0,178+frand(-6,6)}; a->pos.y=Terrain_Height(a->pos.x,a->pos.z)+1.68f;
    a->vel=v3(0,0,0); a->yaw=0; a->pitch=-0.05f; a->hp=100;
    a->fireCd=0; a->reload=0; a->reloadTake=0; a->lastSeen=0; a->weapon=0;
    a->mag[0]=5; a->mag[1]=30; a->reserve[0]=0; a->reserve[1]=70; a->lastSeq=0;
    strncpy(a->name,"志愿军",BN_NAME-1); a->name[BN_NAME-1]=0;
}

// hitscan for a remote client's avatar (host resolves all damage)
static void avatarShoot(Avatar*a)
{
    if(a->reload>0||a->fireCd>0||!a->alive)return;
    int w=a->weapon;
    if(w>=2)return;                       // melee/grenade over network: v1.6 not synced
    if(a->mag[w]<=0)
    {
        if(a->reserve[w]>0 && a->reload<=0)
        { int need=(w==0?5:30)-a->mag[w]; int take=need<a->reserve[w]?need:a->reserve[w];
          a->reloadTake=(float)take; a->reload=w==0?75.0f:100.0f; }
        return;
    }
    a->mag[w]--; a->fireCd=w==0?0.75f:0.10f; gAvatarShots++;
    Vector3 eye=a->pos;
    Vector3 d=vnorm(vadd(avDir(a),v3(frand(-.012f,.012f),frand(-.012f,.012f),frand(-.012f,.012f))));
    Vector3 mz=vadd(eye,vmul(d,0.8f));
    FX_Muzzle(mz);
    float gT=terrainRayT(eye,d);
    int hit=-1,zone=1; float best=gT; Vector3 hp_={0};
    const float zy[3]={1.76f,1.22f,0.45f}, zr[3]={0.33f,0.62f,0.55f};
    for(int i=0;i<NF;i++)if(foes[i].alive)
        for(int z=0;z<3;z++)
    {
        Vector3 c=(Vector3){foes[i].pos.x,foes[i].pos.y+zy[z],foes[i].pos.z};
        Vector3 to=vsub(c,eye); float t=vdot(to,d);
        if(t<=0)continue; Vector3 cp=vadd(eye,vmul(d,t));
        if(vlen(vsub(cp,c))<zr[z] && t<best){best=t;hit=i;zone=z;hp_=cp;}
    }
    if(hit>=0)
    {
        FX_Tracer(mz,hp_,(Color){255,235,170,255},0.06f);
        float dmg=(w==0)?(zone==0?110.0f:50.0f):(zone==0?85.0f:(zone==1?26.0f:16.0f));
        foes[hit].hp-=dmg; if(foes[hit].hp<=0)killFoe(hit,0,"联机战友击毙美军士兵");
    }
    else FX_Tracer(mz,vadd(eye,vmul(d,best)),(Color){255,225,150,255},0.05f);
}

static void applyAvatar(Avatar*a,const BnInput*in,float dt)
{
    if(!a->alive)return;
    a->yaw=in->yaw; a->pitch=clampf(in->pitch,-1.5f,1.5f);
    if(in->weapon<4)a->weapon=in->weapon;
    float spd=(in->bits&BN_B_SPRINT)?8.5f:5.2f;
    Vector3 f=flatFwd(a->yaw), r=(Vector3){cosf(a->yaw),0,sinf(a->yaw)};
    float fx=in->ax/100.0f, fy=in->ay/100.0f;
    Vector3 wish=vmul(vadd(vmul(f,fy),vmul(r,fx)),spd);
    a->vel.x=wish.x; a->vel.z=wish.z;
    a->vel.y-=GRAVITY*dt;

    float feetNow=Terrain_Height(a->pos.x,a->pos.z); const float STEP=0.95f,RAD=0.42f;
    float nx=a->pos.x+a->vel.x*dt;
    if(fabsf(a->vel.x)>0.01f){ float h=Terrain_Height(nx+(a->vel.x>0?RAD:-RAD),a->pos.z); if(h<=feetNow+STEP)a->pos.x=nx; }
    float nz=a->pos.z+a->vel.z*dt;
    if(fabsf(a->vel.z)>0.01f){ float h=Terrain_Height(a->pos.x,nz+(a->vel.z>0?RAD:-RAD)); if(h<=feetNow+STEP)a->pos.z=nz; }
    a->pos.y+=a->vel.y*dt;
    float gy=Terrain_Height(a->pos.x,a->pos.z)+1.68f;
    if(a->pos.y<gy){a->pos.y=gy; if(a->vel.y<=0){a->vel.y=0;a->jumps=0;}}
    if(fabsf(a->pos.x)>WORLD_HALF-10)a->pos.x=WORLD_HALF-10;
    if(fabsf(a->pos.z)>WORLD_HALF-10)a->pos.z=WORLD_HALF-10;

    if(a->fireCd>0)a->fireCd-=dt;
    if(a->reload>0){ a->reload-=dt*60.0f;
        if(a->reload<=0){ a->reload=0; a->mag[a->weapon]+=(int)a->reloadTake;
            a->reserve[a->weapon]-=(int)a->reloadTake; a->reloadTake=0; } }
    if(in->bits&BN_B_FIRE) avatarShoot(a);
    a->hp+=6.0f*dt; if(a->hp>100)a->hp=100; if(a->hp<0)a->hp=0;
}

static int avThreat(const Vector3*chest,Vector3*outPos)
{
    int best=-1; float bd=vlen(vsub(eye,*chest));
    for(int id=1;id<BNET_MAXPLY;id++) if(av[id].active&&av[id].alive)
    { float d=vlen((Vector3){av[id].pos.x-chest->x,av[id].pos.y-chest->y,av[id].pos.z-chest->z});
      if(d<bd){bd=d;best=id;} }
    if(best>=1){ outPos->x=av[best].pos.x; outPos->y=av[best].pos.y; outPos->z=av[best].pos.z; }
    else *outPos=eye;
    return best;
}

static void netHostInit(void)
{ memset(av,0,sizeof(av)); gAvatarShots=0; }

static void netHostRecv(float dt)
{
    for(int id=1;id<BNET_MAXPLY;id++) if(av[id].active) av[id].lastSeen+=dt;
    // keep advertising the running room once a second (LAN + relay) so friends can
    // find/join even after the relay entry would otherwise have expired.
    static float sBattleAnn=0.0f; sBattleAnn-=dt;
    if(sBattleAnn<=0.0f)
    {   sBattleAnn=1.0f;
        char r[64]; int k=snprintf(r,sizeof r,"SKH|%d|%d",gScenario,0);
        Net_Broadcast(r,k);
    }
    NetAddr from; char buf[NET_MAXPKT]; int n;
    while((n=Net_Poll(&from,buf,sizeof buf))>0)
    {
        // still answer lobby discovery / joins while the battle runs (late join)
        if(n>=3 && buf[0]=='S'&&buf[1]=='K'&&(buf[2]=='D'||buf[2]=='J'))
        {
            if(buf[2]=='D')
            { char r[64]; int k=snprintf(r,sizeof r,"SKH|%d|%d",gScenario,0); Net_Send(&from,r,k); }
            else
            { int id=1; while(id<BNET_MAXPLY&&av[id].active)id++;
              if(id<BNET_MAXPLY){ char r[64]; int k=snprintf(r,sizeof r,"SKA|%d|%d",id,gScenario); Net_Send(&from,r,k); } }
            continue;
        }
        BnInput in;
        if(!Bn_DecodeInput(buf,n,&in))continue;
        int id=in.id; if(id<1||id>=BNET_MAXPLY)continue;
        Avatar*a=&av[id];
        if(!a->active){ avatarSpawn(a); a->addr=from; }
        a->addr=from; a->lastSeen=0;
        { int has=0; for(int ni=0;ni<BN_NAME;ni++) if(in.name[ni]){has=1;break;}
          if(has){ memcpy(a->name,in.name,BN_NAME); a->name[BN_NAME-1]=0; } }
        if(in.seq<=a->lastSeq)continue; a->lastSeq=in.seq;
        // edge events handled immediately
        if((in.bits&BN_B_JUMP)&&a->jumps==0&&a->alive){ a->vel.y=7.2f; a->jumps=1; }
        if((in.bits&BN_B_RELOAD)&&a->reload<=0&&a->weapon<2)
        { int w=in.weapon<2?in.weapon:a->weapon;
          if(a->reserve[w]>0){ int need=(w==0?5:30)-a->mag[w]; int take=need<a->reserve[w]?need:a->reserve[w];
            a->reloadTake=(float)take; a->reload=w==0?75.0f:100.0f; } }
        applyAvatar(a,&in,dt);
    }
    for(int id=1;id<BNET_MAXPLY;id++) if(av[id].active&&av[id].lastSeen>5.0f) av[id].active=0;
}

static void netHostSend(int win)
{
    ensureNetName();
    BnWorld w; memset(&w,0,sizeof w);
    w.scenario=(uint8_t)gScenario; w.win=(uint8_t)win; w.planted=(uint8_t)planted;
    w.foeN=NF; w.palN=NP;
    int pn=0;
    BnPlayer p0={0,eye.x,eye.y,eye.z,(int16_t)(yaw*1000),(int8_t)hp,1,(uint8_t)weapon,{0}};
    memcpy(p0.name,gNetName,BN_NAME); w.players[pn++]=p0;
    for(int id=1;id<BNET_MAXPLY;id++) if(av[id].active)
    {
        BnPlayer pv={(uint8_t)id,av[id].pos.x,av[id].pos.y,av[id].pos.z,
            (int16_t)(av[id].yaw*1000),(int8_t)av[id].hp,(uint8_t)(av[id].alive?1:0),(uint8_t)av[id].weapon,{0}};
        memcpy(pv.name,av[id].name,BN_NAME);
        w.players[pn++]=pv;
    }
    w.plyN=(uint8_t)pn; w.foesAlive=(int16_t)foesAlive(); w.palsAlive=(int16_t)palsAlive();
    for(int i=0;i<NF;i++) w.foes[i]=(BnSoldier){foes[i].pos.x,foes[i].pos.z,(int16_t)(foes[i].ang*1000),
        (int8_t)foes[i].hp,(uint8_t)(foes[i].alive?1:(foes[i].state==9?2:0))};
    for(int i=0;i<NP;i++) w.pals[i]=(BnSoldier){pals[i].pos.x,pals[i].pos.z,(int16_t)(pals[i].ang*1000),
        (int8_t)pals[i].hp,(uint8_t)(pals[i].alive?1:(pals[i].state==9?2:0))};
    static char pkt[NET_MAXPKT];
    int len=Bn_EncodeWorld(pkt,sizeof pkt,&w);
    for(int id=1;id<BNET_MAXPLY;id++) if(av[id].active)
    {
        BnWorld per=w; per.myHp=(int8_t)av[id].hp; per.myState=(uint8_t)(av[id].alive?1:0);
        int l2=Bn_EncodeWorld(pkt,sizeof pkt,&per);
        Net_Send(&av[id].addr,pkt,l2>0?l2:len);
    }
}

// ---- client side ----
static void netClientSend(void)
{
    ensureNetName();
    BnInput in; memset(&in,0,sizeof in);
    in.id=(uint8_t)gCoopId;
    in.yaw=yaw; in.pitch=pitch; in.weapon=(uint8_t)weapon;
    memcpy(in.name,gNetName,BN_NAME);
    // derive a -100..100 move vector from the same controls as updatePlayer
    Vector3 f=flatFwd(yaw), r=(Vector3){cosf(yaw),0,sinf(yaw)};
    Vector3 wish={0};
    if(!gSelfTest)
    {
        if(IsKeyDown(KEY_W))wish=vadd(wish,f);
        if(IsKeyDown(KEY_S))wish=vsub(wish,f);
        if(IsKeyDown(KEY_D))wish=vadd(wish,r);
        if(IsKeyDown(KEY_A))wish=vsub(wish,r);
        wish=vadd(wish,vmul(vadd(vmul(f,Touch_AxisY()),vmul(r,Touch_AxisX())),1.0f));
    }
    else { wish=f; }       // automated test marches north
    float spd=IsKeyDown(KEY_Q)?8.5f:5.2f; (void)spd;
    Vector3 nf=vnorm(f), nr=vnorm(r);
    float fy=vdot(wish,nf)/5.2f, fx=vdot(wish,nr)/5.2f;
    if(fy>1)fy=1; if(fy<-1)fy=-1; if(fx>1)fx=1; if(fx<-1)fx=-1;
    in.ay=(int8_t)(fy*100); in.ax=(int8_t)(fx*100);
    uint8_t bits=0;
    if(!gSelfTest)
    {
#if defined(PLATFORM_ANDROID)
        if(Touch_FireHeld())bits|=BN_B_FIRE;
        if(Touch_ADSHeld())bits|=BN_B_ADS;
#else
        if(IsMouseButtonDown(MOUSE_BUTTON_LEFT)||Touch_FireHeld())bits|=BN_B_FIRE;
        if(IsMouseButtonDown(MOUSE_BUTTON_RIGHT)||Touch_ADSHeld())bits|=BN_B_ADS;
#endif
        if(IsKeyDown(KEY_Q)||touchSprint)bits|=BN_B_SPRINT;
        if(IsKeyPressed(KEY_SPACE)||Touch_ActPressed())bits|=BN_B_JUMP;
        if(IsKeyPressed(KEY_R)||Touch_BPressed())bits|=BN_B_RELOAD;
    }
    else { bits|=BN_B_FIRE; }
    in.bits=bits;
    static uint32_t s_seq=0; in.seq=++s_seq;
    char buf[64]; int len=Bn_EncodeInput(buf,&in);
    Net_Send(Coop_HostAddr(),buf,len);
}

static void netClientRecv(void)
{
    static int prevFoe[NF];
    NetAddr from; char buf[NET_MAXPKT]; int n;
    while((n=Net_Poll(&from,buf,sizeof buf))>0)
    {
        if(!Bn_DecodeWorld(buf,n,&gSnap))continue;
        gHaveSnap=1; gNetWin=gSnap.win;
        for(int i=0;i<NF;i++)
        {
            int nowAlive=(gSnap.foes[i].state==1)?1:0;
            if(gHaveSnap && prevFoe[i] && !nowAlive) kfPush("战友击毙美军士兵");
            foes[i].pos.x=gSnap.foes[i].x; foes[i].pos.z=gSnap.foes[i].z;
            foes[i].pos.y=Terrain_Height(foes[i].pos.x,foes[i].pos.z);
            foes[i].ang=gSnap.foes[i].angC/1000.0f;
            foes[i].hp=gSnap.foes[i].hp;
            foes[i].alive=nowAlive;
            foes[i].state=(gSnap.foes[i].state==2)?9:0;
            prevFoe[i]=nowAlive;
        }
        for(int i=0;i<NP;i++)
        {
            pals[i].pos.x=gSnap.pals[i].x; pals[i].pos.z=gSnap.pals[i].z;
            pals[i].pos.y=Terrain_Height(pals[i].pos.x,pals[i].pos.z);
            pals[i].ang=gSnap.pals[i].angC/1000.0f;
            pals[i].hp=gSnap.pals[i].hp;
            pals[i].alive=(gSnap.pals[i].state==1)?1:0;
            pals[i].state=(gSnap.pals[i].state==2)?9:0;
        }
        planted=gSnap.planted?1:0;
        if(gSnap.myState==0) hp=0; else hp=(float)gSnap.myHp;
    }
}

// real co-op players' floating nicks (screen-space), distinct from AI squaddies
static void drawNetNames(void)
{
    Vector3 fwd=vnorm(vsub(cam.target,cam.position));
    int role=gCoopRole, have=gHaveSnap;
    for(int slot=0; slot<BNET_MAXPLY; slot++)
    {
        int id; int alive; Vector3 eye; const char*nm;
        char tmp[BN_NAME];
        if(role==1)
        {
            if(slot==0)continue;                       // host never labels itself
            Avatar*a=&av[slot]; if(!a->active)continue;
            id=slot; alive=a->alive; eye=a->pos; nm=a->name;
        }
        else if(role==2 && have)
        {
            BnPlayer*p=&gSnap.players[slot];
            if(slot>=gSnap.plyN)continue;
            if(p->id==(uint8_t)gCoopId)continue;
            id=p->id; alive=(p->state==1); eye=(Vector3){p->x,p->y,p->z};
            memcpy(tmp,p->name,BN_NAME); tmp[BN_NAME-1]=0; nm=tmp;
            if(p->id==0&&!tmp[0])continue;
        }
        else continue;
        if(!alive)continue;
        Vector3 head=(Vector3){eye.x,eye.y+0.55f,eye.z};
        Vector3 to=vsub(head,cam.position);
        if(vdot(to,fwd)<=0||vlen(to)>120.0f)continue;
        Vector2 s=GetWorldToScreen(head,cam);
        if(s.x<10||s.x>GetScreenWidth()-10||s.y<10||s.y>GetScreenHeight()-10)continue;
        char shown[BN_NAME+1]; strncpy(shown,nm,BN_NAME); shown[BN_NAME]=0;
        if(!shown[0])strcpy(shown,"战友");
        int fs=15; Vector2 sz=MeasureTextEx(GameFont(),shown,fs,0);
        int x=(int)(s.x-sz.x/2), y=(int)(s.y-6);
        DrawRectangle(x-3,y-1,(int)sz.x+6,(int)sz.y+2,(Color){0,0,0,150});
        DrawTextEx(GameFont(),shown,(Vector2){x,y},fs,0,(Color){120,220,255,255});
        (void)id;
    }
}

// draw the other real players (host: joined clients; client: host + peers)
static void drawNetBodies(void)
{
    if(gCoopRole==1)
    {
        for(int id=1;id<BNET_MAXPLY;id++) if(av[id].active)
        {
            // av.pos.y is EYE height (ground + 1.68); DrawSoldier expects feet.
            Vector3 bp={av[id].pos.x,av[id].pos.y-1.68f,av[id].pos.z};
            if(av[id].alive) DrawSoldier(bp,-av[id].yaw,0,1.0f,1,gAnimClock*9.0f+id*1.7f);
            else DrawSoldierDown(bp,av[id].yaw,0,1.0f);
        }
    }
    else if(gCoopRole==2 && gHaveSnap)
    {
        for(int i=0;i<gSnap.plyN;i++)
        {
            BnPlayer*p=&gSnap.players[i];
            if(p->id==(uint8_t)gCoopId)continue;   // never draw yourself
            Vector3 pp={p->x,p->y-1.68f,p->z};
            float ya=p->yawC/1000.0f;
            if(p->state==1) DrawSoldier(pp,-ya,0,1.0f,1,gAnimClock*9.0f+p->id*1.7f);
            else DrawSoldierDown(pp,ya,0,1.0f);
        }
    }
}

// ===================== mines · enemy tank · enemy strafing runs ==============
static const float MINE_PT[NMINE][2]={
    {-40,-60},{55,-80},{-120,-110},{150,-90},{20,-160},{-70,-180},{110,-200},
    {-180,-170},{90,-250},{-40,-270},{170,-260},{-150,-290},{30,-320},
    {200,-150},{-220,-240},{80,-350}};

static void warReset(void)
{
    gInTank=0; gVehArmor=0; gRunCd=0; gTankCd=0; gTankTur=0; gShake=0; gJetNext=14.0f; gJet.on=0; gJet.bombed=0;
    buildTrees();
    gTank=(ETank){(Vector3){70,Terrain_Height(70,-330),-330},M_PI,120.0f,1,0,4.0f};
    for(int i=0;i<NMINE;i++)
    { float x=MINE_PT[i][0], z=MINE_PT[i][1];
      gMines[i].p=(Vector3){x,Terrain_Height(x,z)+0.06f,z}; gMines[i].live=1; }
}

// shell / bomb explosion. foesToo also knocks out enemy units; oursToo wounds
// the player, comrades and co-op players (blocked shells pass 0,0).
static void warSplash(Vector3 p,float rad,float dmg,int foesToo,int oursToo)
{
    p.y=Terrain_Height(p.x,p.z)+0.6f;
    FX_Explosion(p,2.2f); FX_Fireball(p,1.4f); FX_Smoke(p,2.3f); Sfx_Boom(1.0f);
    float dc=vlen(vsub(p,cam.position));
    if(dc<rad+34.0f) gShake=fmaxf(gShake,1.0f*(1.0f-dc/(rad+34.0f)));
    if(oursToo)
    {
        if(hp>0){ float d=vlen((Vector3){eye.x-p.x,0,eye.z-p.z});
            if(d<rad){ hurtPlayer(dmg*(1.0f-d/rad)); dmgCd=fmaxf(dmgCd,3.0f); } }
        for(int i=0;i<NP;i++) if(pals[i].alive)
        { float d=vlen(vsub(pals[i].pos,p));
          if(d<rad){ pals[i].hp-=dmg*(1.0f-d/rad); if(pals[i].hp<=0)killPal(i); } }
#if defined(HOST_NET)
        if(gCoopRole==1)for(int i=1;i<BNET_MAXPLY;i++) if(av[i].alive)
        { float d=vlen(vsub(av[i].pos,p));
          if(d<rad){ av[i].hp-=dmg*(1.0f-d/rad); if(av[i].hp<=0){av[i].hp=0;av[i].alive=0;} } }
#endif
    }
    if(foesToo) for(int i=0;i<NF;i++) if(foes[i].alive)
    { float d=vlen(vsub(foes[i].pos,p));
      if(d<rad){ foes[i].hp-=dmg*1.25f*(1.0f-d/rad); if(foes[i].hp<=0)killFoe(i,1,"炮火击毙美军士兵"); } }
}

// tank cannon shell along dir; hills stop the shell and the blast with it
static void tankFire(Vector3 muzzle,Vector3 dir,int foesToo,int oursToo)
{
    Sfx_Boom(0.85f);
    float gt=terrainRayT(muzzle,dir);
    Vector3 imp=vadd(muzzle,vmul(dir,gt));
    FX_Tracer(muzzle,imp,(Color){255,210,130,255},0.12f);
    warSplash(imp,8.5f,110.0f,foesToo,oursToo);
}

int tankBulletHit(Vector3 o,Vector3 d,float gT,float dmg)
{
    if(!gTank.alive) return 0;
    float hx=gTank.pos.x, hz=gTank.pos.z;
    // 2-D ground ray vs the tank's footprint
    float dx=d.x, dz=d.z;
    if(fabsf(dx)+fabsf(dz)<1e-4f)return 0;
    float t; Vector3 c=(Vector3){hx,0,hz};
    Vector3 oo=(Vector3){o.x,0,o.z}, dd=(Vector3){dx,0,dz};
    float L=vdot(dd,dd), t0=vdot(vsub(c,oo),dd)/L;
    if(t0<=0||t0>gT)return 0;
    Vector3 q=vadd(oo,vmul(dd,t0));
    if(vlen(vsub(q,c))>3.1f)return 0;
    float hy=o.y+d.y*t0, th=Terrain_Height(hx,hz);
    if(hy<th-0.6f||hy>th+3.4f)return 0;
    gTank.hp-=dmg;
    FX_Muzzle(vadd(o,vmul(d,t0)));
    if(gTank.hp<=0)
    {
        gTank.alive=0; gTank.hp=0; gTank.fireFlare=1; gTank.cd=0;
        Vector3 bp=(Vector3){gTank.pos.x,th+1.2f,gTank.pos.z};
        FX_Explosion(bp,3.0f); FX_Fireball(bp,2.0f); FX_Smoke(bp,3.0f); Sfx_Boom(1.0f);
        gShake=1.0f;
    }
    return 1;
}

void tankToggleMount(void)
{
    if(gSelfTest)return;
    if(gInTank)
    {   // bail out beside the hull
        gInTank=0; jumps=0;
        eye.x=gTank.pos.x+3.2f; eye.z=gTank.pos.z;
        eye.y=Terrain_Height(eye.x,eye.z)+1.68f;
        return;
    }
    if(gTank.alive)return;                    // crew still alive — can't take it
    float dd=vlen((Vector3){eye.x-gTank.pos.x,0,eye.z-gTank.pos.z});
    if(dd<4.6f){ gInTank=1; gVehArmor=50.0f; gRunCd=0; }   // 登车：车体先扛 50 点
}

// UI hint for the dedicated on-screen 车 button: lit while mounted (dismount)
// or standing next to a wrecked, drivable enemy vehicle (mount).
int Ground_VehiclePrompt(void)
{
    if(gSelfTest)return 0;
    if(gInTank)return 1;
    if(gTank.alive)return 0;
    float dd=vlen((Vector3){eye.x-gTank.pos.x,0,eye.z-gTank.pos.z});
    return dd<4.6f;
}
int Ground_InVehicle(void){ return gInTank; }

static Vector3 tankFwd(void){ return (Vector3){sinf(gTank.yaw),0,cosf(gTank.yaw)}; }

static void drawEnemyJet(Vector3 p,float yy)
{
    Matrix M=MPart(p,(Vector3){0,1,0},yy,v3(1,1,1));
    DrawPart(P_CYL,C_NAVY,M,MPart(v3(0,0,0),(Vector3){1,0,0},-90*DEG2R,(Vector3){0.65f,4.4f,0.65f}));
    DrawPart(P_BOX,C_GREY,M,MPart(v3(0,-0.05f,0),(Vector3){0,1,0},0,(Vector3){7.0f,0.16f,1.35f}));
    DrawPart(P_BOX,C_GREY,M,MPart(v3(0,0.05f,2.0f),(Vector3){0,1,0},0,(Vector3){2.6f,0.12f,1.3f}));
    DrawPart(P_BOX,C_GLASS,M,MPart(v3(0,0.55f,-1.1f),(Vector3){0,1,0},0,(Vector3){0.7f,0.5f,1.0f}));
}

static void warUpdate(float dt)
{
    if(gShake>0) gShake-=dt*1.7f; if(gShake<0)gShake=0;

    // ---- mines: a boot or a track sets them off ----
    if(!gSelfTest) for(int i=0;i<NMINE;i++) if(gMines[i].live)
    {
        float d=vlen((Vector3){eye.x-gMines[i].p.x,0,eye.z-gMines[i].p.z});
        float td = gInTank?2.9f:1.25f;
        if(d<td)
        {
            gMines[i].live=0;
            warSplash(gMines[i].p,6.0f, gInTank?45.0f:130.0f, 0,1);
        }
    }

    // ---- wrecked tank burns on ----
    if(!gTank.alive && gTank.fireFlare)
    {
        gTank.cd-=dt;
        if(gTank.cd<=0)
        { gTank.cd=0.22f; Vector3 bp=(Vector3){gTank.pos.x,gTank.pos.y+1.6f,gTank.pos.z};
          FX_FireLong(bp,1.7f); FX_Smoke((Vector3){bp.x,bp.y+1.2f,bp.z},2.0f); }
    }

    if(gInTank)
    {
        // drive: W/S throttle, A/D steer the hull; turret follows the view
        float thr=0;
        if(IsKeyDown(KEY_W))thr+=1; if(IsKeyDown(KEY_S))thr-=1;
        float ax=Touch_AxisX(), ay=Touch_AxisY();
        if(fabsf(ax)>0.05f||fabsf(ay)>0.05f){ gTank.yaw-=ax*1.6f*dt; thr+=-ay; }
        if(IsKeyDown(KEY_A))gTank.yaw-=1.35f*dt;
        if(IsKeyDown(KEY_D))gTank.yaw+=1.35f*dt;
        gTankTur=yaw;   // turret aims where the player looks
        Vector3 fwd=tankFwd();
        float sp=12.0f;
        Vector3 wish=vmul(fwd,thr*sp);
        float ox=gTank.pos.x, oz=gTank.pos.z;
        float nx=gTank.pos.x+wish.x*dt, nz=gTank.pos.z+wish.z*dt;
        if(Terrain_Height(nx,nz)<=Terrain_Height(gTank.pos.x,gTank.pos.z)+2.0f)
        { gTank.pos.x=nx; gTank.pos.z=nz; }
        gTank.pos.x=clampf(gTank.pos.x,-WORLD_HALF+10,WORLD_HALF-10);
        gTank.pos.z=clampf(gTank.pos.z,-WORLD_HALF+10,WORLD_HALF-10);
        gTank.pos.y=Terrain_Height(gTank.pos.x,gTank.pos.z);
        // 高速冲撞：车开过去，沿途敌人成片撞倒（必须真的在移动，避免原地误杀）
        float moved=sqrtf((gTank.pos.x-ox)*(gTank.pos.x-ox)+(gTank.pos.z-oz)*(gTank.pos.z-oz));
        if(moved>sp*dt*0.55f) for(int j=0;j<NF;j++) if(foes[j].alive)
        {
            float rd=vlen((Vector3){foes[j].pos.x-gTank.pos.x,0,foes[j].pos.z-gTank.pos.z});
            if(rd<3.4f){ FX_Blood((Vector3){foes[j].pos.x,foes[j].pos.y+1.1f,foes[j].pos.z});
                         addBlood(foes[j].pos); killFoe(j,1,"军车冲撞 · 横扫毙敌"); }
        }
        gTankCd-=dt;
        if(!gSelfTest && gTankCd<=0 && (IsMouseButtonDown(MOUSE_BUTTON_LEFT)||Touch_FireHeld()))
        {
            gTankCd=2.3f;
            Vector3 mz=(Vector3){gTank.pos.x,gTank.pos.y+2.1f,gTank.pos.z};
            Vector3 dir=vnorm(aimDir());
            // shells lob slightly downward toward ground range
            tankFire(mz,dir,1,1);
        }
    }
    else if(gTank.alive)
    {
        // pick nearest visible target among player + comrades
        Vector3 origin=(Vector3){gTank.pos.x,gTank.pos.y+1.7f,gTank.pos.z};
        Vector3 tgt=eye; float bd=1e30f; int have=(hp>0);
        if(hp>0)bd=vlen(vsub(eye,origin));
        for(int i=0;i<NP;i++) if(pals[i].alive)
        { float d=vlen(vsub(pals[i].pos,origin)); if(d<bd){bd=d;tgt=pals[i].pos;have=1;} }
        if(have && bd<480)
        {
            Vector3 want=(Vector3){sinf(atan2f(tgt.x-gTank.pos.x,tgt.z-gTank.pos.z)),0,
                                   cosf(atan2f(tgt.x-gTank.pos.x,tgt.z-gTank.pos.z))};
            (void)want;
            gTank.yaw=atan2f(tgt.x-gTank.pos.x,tgt.z-gTank.pos.z);
            gTank.cd-=dt;
            if(gTank.cd<=0 && los(origin,(Vector3){tgt.x,tgt.y+1.2f,tgt.z}))
            {
                gTank.cd=frand(4.5f,6.5f); gTank.fireFlare=0;
                Vector3 fwd=tankFwd();
                Vector3 mz=vadd(gTank.pos,v3(fwd.x*3.6f,1.9f,fwd.z*3.6f));
                Vector3 aim=vadd(tgt,v3(frand(-4,4),1.0f,frand(-4,4)));
                Vector3 dir=vnorm(vsub(aim,mz));
                float dist=vlen(vsub(aim,mz)), gt=terrainRayT(mz,dir);
                int blocked=gt<dist-2.0f;
                FX_Tracer(mz,vadd(mz,vmul(dir,blocked?gt:dist)),(Color){255,200,120,255},0.12f);
                if(blocked){ FX_Explosion(vadd(mz,vmul(dir,gt)),1.6f); Sfx_Boom(0.6f); }
                else warSplash(aim,8.0f,95.0f,0,1);
            }
        }
        // grind toward our line, then hold at the ridge
        if(gTank.pos.z>-265.0f)
        {
            Vector3 fwd=tankFwd();
            float nx=gTank.pos.x+fwd.x*2.6f*dt, nz=gTank.pos.z+fwd.z*2.6f*dt;
            if(Terrain_Height(nx,nz)<=gTank.pos.y+1.6f){ gTank.pos.x=nx; gTank.pos.z=nz; }
        }
        gTank.pos.y=Terrain_Height(gTank.pos.x,gTank.pos.z);
    }

    // ---- enemy strafing run ----
    if(!gJet.on)
    {
        if(!gSelfTest){ gJetNext-=dt;
            if(gJetNext<=0)
            {
                int side=(irand(0,1)?1:-1);
                gJet.on=1; gJet.t=0; gJet.bombed=0; gJet.mg=0;
                gJet.pos=(Vector3){side*760.0f,235.0f,eye.z+frand(-50,40)};
                gJet.vel=(Vector3){-side*150.0f,0,frand(-10,10)};
                gJet.aim=(Vector3){eye.x+frand(-16,16),0,eye.z+frand(-16,16)};
            } }
    }
    else
    {
        gJet.t+=dt; gJet.pos=vadd(gJet.pos,vmul(gJet.vel,dt));
        gJet.mg-=dt;
        if(fabsf(gJet.pos.x)<480 && gJet.mg<=0 && !gSelfTest)
        {   // cannon run walks across the ground near the aim point
            gJet.mg=0.12f;
            Vector3 nose=vadd(gJet.pos,v3(0,-1.0f,0));
            Vector3 gp=(Vector3){gJet.aim.x+frand(-26,26),Terrain_Height(gJet.aim.x,gJet.aim.z)+0.5f,gJet.aim.z+frand(-26,26)};
            FX_Tracer(nose,gp,(Color){255,205,130,255},0.08f);
            float pd=vlen((Vector3){eye.x-gp.x,0,eye.z-gp.z});
            if(pd<5.0f && hp>0){ hurtPlayer(frand(10,20)); dmgCd=fmaxf(dmgCd,2.0f); }
        }
        if(!gJet.bombed && ((gJet.vel.x<0 && gJet.pos.x<=gJet.aim.x)||(gJet.vel.x>0 && gJet.pos.x>=gJet.aim.x)))
        {   // bomb drops where the jet crosses the aim line
            gJet.bombed=1;
            Vector3 bp=gJet.aim;
            FX_Tracer(gJet.pos,(Vector3){bp.x,Terrain_Height(bp.x,bp.z)+2,bp.z},(Color){120,120,120,255},0.1f);
            if(!gSelfTest) warSplash(bp,9.5f,120.0f,0,1);
            else { FX_Explosion((Vector3){bp.x,Terrain_Height(bp.x,bp.z)+1,bp.z},2.0f); Sfx_Boom(1.0f); }
        }
        if(fabsf(gJet.pos.x)>820){ gJet.on=0; gJetNext=frand(16,28); }
    }
}

static void warDraw(void)
{
    // mines: dark disc with three tiny pressure prongs
    for(int i=0;i<NMINE;i++) if(gMines[i].live)
    {
        DrawPart(P_CYL,C_BLACK,MatrixIdentity(),MPart(gMines[i].p,(Vector3){0,1,0},i*0.7f,(Vector3){0.42f,0.05f,0.42f}));
        Vector3 top=(Vector3){gMines[i].p.x,gMines[i].p.y+0.08f,gMines[i].p.z};
        DrawPart(P_BOX,C_DARK,MatrixIdentity(),MPart(top,(Vector3){0,1,0},0,(Vector3){0.5f,0.06f,0.12f}));
        DrawPart(P_BOX,C_DARK,MatrixIdentity(),MPart(top,(Vector3){0,1,0},90*DEG2R,(Vector3){0.5f,0.06f,0.12f}));
    }
    // contact shadows under the tank
    DrawBlobShadow(gTank.pos,3.6f);
    // enemy / captured tank (slightly enlarged model)
    DrawVehicle((Vector3){gTank.pos.x,gTank.pos.y,gTank.pos.z}, gTank.yaw, 2, 1.55f);
    // the strafing Sabre
    if(gJet.on)
    {
        float jy=atan2f(-gJet.vel.x,-gJet.vel.z)+M_PI;
        drawEnemyJet(gJet.pos,jy);
    }
}

void Ground_Run(int *outMode,int *outEnding)
{
    memset(foes,0,sizeof(foes)); memset(pals,0,sizeof(pals));
    spawnBattle();
    eye=(Vector3){0,0,180}; eye.y=Terrain_Height(0,180)+1.68f;
    if(gFlagTest){ eye=(Vector3){26,0,-330}; eye.y=Terrain_Height(26,-330)+1.68f; }
    yaw=gFlagTest?0.18f:0; pitch=-0.05f; hp=100;
    weapon=0;
    for(int w2=0;w2<4;w2++)gRok[w2].on=0; rpgReload=0; rpgFireCd=0;
    if(gRpgMode)
    {
        // RPG anti-tank: begin with the launcher up, one round in tube + five carried
        weapon=4; rpgLoaded=1; rpgReserve=5;
        // 7 armoured targets spread along the ridge: tanks lead, trucks and an
        // AA gun cover them. Fixed layout so the line reads like an enemy column.
        static const float AP[NARM][3]={ {60,-280,0},{-70,-300,2},{150,-250,0},
            {-170,-330,2},{20,-360,1},{0,-250,0},{210,-330,1} };
        for(int i=0;i<NARM;i++)
        { float x=AP[i][0], z=AP[i][1]; int kind=(int)AP[i][2];
          gArm[i].pos=(Vector3){x,0,z}; gArm[i].yaw=M_PI*0.5f+frand(-0.4f,0.4f);
          gArm[i].kind=kind; gArm[i].alive=1; gArm[i].burn=0;
          gArm[i].hp=(kind==2)?180.0f:(kind==1?120.0f:110.0f); }
    }
    else { rpgLoaded=0; rpgReserve=0; for(int i=0;i<NARM;i++)gArm[i].alive=0; }
    mag[0]=5; mag[1]=30; reserve[0]=0; reserve[1]=70;   // rifle 5 total, AKM 100 total
    reload=0; reloadTake=0; fireCd=0;
    grenades=1; gre=(Gre){0}; greCharging=0; greHold=0; greCd=0;
    meleeCd=meleeSwing=0; stepAcc=0; ambT=3.0f;
    celebrate=0; planter=-1; planted=0; bugled=0; objFall=0; talkOpen=0; talkPage=0;
    foesKilled=0; hitMark=0; dmgCd=0; holdT=0; timeAlive=0; frame=0; pvel=v3(0,0,0); ads=0; paused=0; kickP=kickY=gunKick=0; headMsgT=0;
    cam=(Camera3D){0}; cam.fovy=72; cam.projection=CAMERA_PERSPECTIVE; cam.up=(Vector3){0,1,0};
    jumps=0; introT=Map_IsChosin()?7.0f:3.5f;
    warReset();
    if(gFlagTest){ planted=1; celebrate=5.0f; objFall=0; bugled=0; introT=0; }
    if(gCoopRole==1) netHostInit();
    if(gCoopRole==2){ gHaveSnap=0; gNetWin=0; }
    DisableCursor();
    Sfx_Bugle();

    int endId=0;
    while(!WindowShouldClose())
    {
        float dt=clampf(GetFrameTime(),0,0.033f); frame++; timeAlive+=dt; gAnimClock=timeAlive;
        Weapon_AnimUpdate(dt);
        Touch_SetPaused(paused);
        Touch_Update(1);
        Touch_SetGrenadeArmed(greCharging);   // reveal left cancel key only while a throw is readied
        Touch_SetMountLive(Ground_VehiclePrompt());  // show 车 button only near/in a vehicle
        if(IsKeyPressed(KEY_ESCAPE)){ EnableCursor(); *outMode=0; return; }
        if(IsKeyPressed(KEY_ESCAPE)){ EnableCursor(); *outMode=0; return; }
        int wantPause = (!gSelfTest && (Touch_PausePressed()||IsKeyPressed(KEY_P)));

        // wounded-comrade "last words" interaction (E on desktop, 话 on touch)
        if(!paused && celebrate<=0)
        {
            float wd=vlen((Vector3){eye.x-wound.pos.x,0,eye.z-wound.pos.z});
            if(wd<3.2f && !gSelfTest && (IsKeyPressed(KEY_E)||Touch_TalkPressed()))
            {
                if(!talkOpen){talkOpen=1;talkPage=0;}
                else if(talkPage<2)talkPage++; else talkOpen=0;
            }
        }

        if(wantPause && !paused){ paused=1; }
        else if(paused)
        {
            int pm=Touch_PauseMenuSelect();
            if(pm==2||IsKeyPressed(KEY_BACKSPACE)){ Touch_Suppress(0.6f); EnableCursor(); *outMode=0; return; }
            if(pm==1||IsKeyPressed(KEY_ENTER)||IsKeyPressed(KEY_KP_ENTER)){ paused=0; Touch_Suppress(0.25f); }
        }

        int frozen=(paused||talkOpen||celebrate>0);
        if(!frozen){
            if(gCoopRole==2)
            {
                // client: predict own walk, send intent, receive authoritative world
                Net_RelayTick(dt);
                updatePlayer(dt); netClientSend(); netClientRecv();
                FX_Update(dt); battleFX(dt); Env_Update(dt); kfUpdate(dt); if(introT>0)introT-=dt;
            }
            else
            {
                Net_RelayTick(dt);
                if(gCoopRole==1) netHostRecv(dt);
                updatePlayer(dt); updateFoes(dt); updatePals(dt); warUpdate(dt);
                if(gCoopRole==1) netHostSend(celebrate>0?1:0);
                FX_Update(dt); battleFX(dt); Env_Update(dt); kfUpdate(dt); if(introT>0)introT-=dt;
                // distant, off-screen battle: random booms and smoke over the ridge
                if(!gSelfTest){ ambT-=dt;
                    if(ambT<=0){ ambT=frand(4,9); Sfx_Boom(0.22f);
                        Vector3 wp=(Vector3){frand(-500,500),0,frand(-700,-500)}; wp.y=Terrain_Height(wp.x,wp.z)+20;
                        FX_Smoke(wp,2.2f); } }
            }
        }
        else if(celebrate>0)
        {
            // the planter (squad leader, else a random survivor, else the player)
            // runs to the crest and raises the red flag; the bugle sounds once.
            celebrate-=dt; objFall+=dt;
            if(planter>=0 && pals[planter].alive)
            {
                Man*s=&pals[planter];
                Vector3 t=(Vector3){OBJV.x,0,OBJV.z};
                s->ang=angTo(s->pos,t);
                float dd=vlen(vsub(t,s->pos));
                if(dd>3.0f){ Vector3 mv=vmul(flatFwd(s->ang),7.5f*dt);
                    s->pos=vadd(s->pos,mv); s->pos.y=Terrain_Height(s->pos.x,s->pos.z); }
                else planted=1;
            } else planted=1;
            if(planted&&!bugled){bugled=1;Sfx_Bugle();}
            if(gCoopRole==1) netHostSend(1);
            if(gCoopRole==2){ netClientSend(); netClientRecv(); }
            FX_Update(dt); battleFX(dt); Env_Update(dt);
            if(celebrate<=0) celebrate=0;
        }

        Vector3 dir=aimDir();
        cam.position=eye; cam.target=vadd(eye,dir);
        if(gShake>0.01f)   // shell shock camera shake
        { float s=gShake*0.55f; Vector3 sh=v3(frand(-s,s),frand(-s,s),frand(-s,s));
          cam.position=vadd(cam.position,sh); cam.target=vadd(cam.target,sh); }
        { float fovTarget=(weapon==0)?9.0f:(weapon==1?40.0f:44.0f);  // 8x 72/9=8×；AKM 红点40；RPG 机械表尺44
          cam.fovy=72.0f-ads*(72.0f-fovTarget); }
        // hide the first-person gun when something is right in front of the
        // muzzle (steep ground / wall / a soldier) so it can't clip through.
        int gunOccluded=0;
        {
            Vector3 fp=(Vector3){dir.x,0,dir.z};
            if(vlen(fp)>0.001f)
            {
                fp=vnorm(fp);
                Vector3 ahead=(Vector3){eye.x+fp.x*0.95f,0,eye.z+fp.z*0.95f};
                if(Terrain_Height(ahead.x,ahead.z) > eye.y-0.55f) gunOccluded=1;
            }
            if(!gunOccluded)
            {
                for(int i=0;i<NF&&!gunOccluded;i++)if(foes[i].alive){
                    Vector3 c=vsub(foes[i].pos,eye);
                    if(vlen(c)<1.05f && vdot(c,dir)>0) gunOccluded=1; }
            }
        }
        Scene_SetCamera(cam);

        BeginDrawing();
        ClearBackground((Color){118,110,96,255});
        Env_DrawSky2D();
        BeginMode3D(cam);
        Terrain_Draw(cam); Sea_Draw(cam); Env_Draw(cam);
        // our camp behind the start line: Five-star Red Flag + PLA August-1st flag
        float c1y=Terrain_Height(-16,168), c2y=Terrain_Height(16,168);
        DrawWavingFlag(cam,(Vector3){-16,c1y,168},0.5f,0,timeAlive,8.0f);
        DrawWavingFlag(cam,(Vector3){ 16,c2y,168},-0.5f,1,timeAlive,8.0f);
        // objective: enemy colours over the crest; on victory they fold 90
        // degrees and the Five-star Red Flag is hoisted (DrawObjectiveFlags).
        float gy=Terrain_Height(OBJV.x,OBJV.z);
        DrawObjectiveFlags(cam,(Vector3){OBJV.x,gy,OBJV.z},planted,objFall,timeAlive);
        // sandbag cover
        for(int i=0;i<6;i++){ float x=-60+i*24; float z=-260-((i%2)*20); DrawPart(P_BOX,C_SAND,MatrixIdentity(),MPart((Vector3){x,Terrain_Height(x,z)+0.5f,z},(Vector3){0,1,0},i*0.4f,(Vector3){4,1,1.2f})); }
        // wrecked truck decoys
        DrawVehicle((Vector3){-90, Terrain_Height(-90,-60), -60}, 0.6f, 0, 1.0f);
        DrawVehicle((Vector3){110, Terrain_Height(110,-120), -120}, 2.2f, 2, 1.0f);
        DrawVehicle((Vector3){-150, Terrain_Height(-150,-310), -310}, 0.2f, 2, 1.0f);
        DrawVehicle((Vector3){170, Terrain_Height(170,-180), -180}, 1.0f, 0, 1.0f);
        // live battlefield systems: mines, the enemy/captured tank, strafing runs
        if(gCoopRole!=2) warDraw();
        if(gRpgMode) drawArmor();
        drawTrees();
        // shell craters / scorch marks — the ground must read as fought over
        for(int i=0;i<NBURN;i++){ float bx=BURN_PT[i][0],bz=BURN_PT[i][1];
            DrawPart(P_CYL,C_DARK,MatrixIdentity(),
                MPart((Vector3){bx,Terrain_Height(bx,bz)+0.05f,bz},(Vector3){0,1,0},i*0.7f,(Vector3){4.0f+(i%3),0.02f,4.0f+(i%3)})); }
        // restrained dark stains where men fell
        for(int i=0;i<MAXBLD;i++)if(blds[i].on)
            DrawPart(P_CYL,C_BLOOD,MatrixIdentity(),MPart(blds[i].p,(Vector3){0,1,0},blds[i].rot,(Vector3){blds[i].r,0.02f,blds[i].r}));
        // fallen soldiers (both sides lie on the ground; no dismemberment)
        for(int i=0;i<NF;i++)if(!foes[i].alive&&foes[i].state==9) DrawSoldierDown(foes[i].pos,foes[i].ang,1,1.0f);
        for(int i=0;i<NP;i++)if(!pals[i].alive&&pals[i].state==9) DrawSoldierDown(pals[i].pos,pals[i].ang,0,1.0f);
        // wounded comrade you can talk to
        if(woundOn) DrawSoldierDown(wound.pos,wound.ang,0,1.0f);
        for(int i=0;i<NF;i++)if(foes[i].alive){ DrawBlobShadow(foes[i].pos,1.05f); DrawSoldier(foes[i].pos,-foes[i].ang,1,1.0f,1,timeAlive*9.0f+i*1.3f); }
        for(int i=0;i<NP;i++)if(pals[i].alive){ DrawBlobShadow(pals[i].pos,1.05f); DrawSoldier(pals[i].pos,-pals[i].ang,0,1.0f,1,timeAlive*9.0f+i*1.3f); }
        drawNetBodies();   // other real co-op players
        // grenade in flight + charging arc preview
        if(gre.on) DrawGrenadeModel(gre.p,gre.age*10.0f);
        drawRockets();
        if(greCharging)
        {
            Vector3 d2=aimDir();
            Vector3 p0=vadd(eye,vadd(vmul(d2,0.7f),(Vector3){0,0.15f,0}));
            Vector3 v0=vadd(vmul(d2,9+11*greHold),(Vector3){0,3.6f+1.6f*greHold,0});
            for(int s2=1;s2<=24;s2++)
            {
                float tt=0.11f*s2;
                Vector3 qp=(Vector3){p0.x+v0.x*tt,p0.y+v0.y*tt-0.5f*GRAVITY*tt*tt,p0.z+v0.z*tt};
                DrawPart(P_SPHERE,C_YELLOW,MatrixIdentity(),MPart(qp,(Vector3){1,0,0},0,(Vector3){0.10f,0.10f,0.10f}));
            }
        }
        FX_Draw3D(cam);
        if(gSelfTest && gCoopRole==0 && frame>=148)
        {
            weapon = (frame<200)?1:(frame<260?2:(frame<348?0:(frame<408?4:0)));   // ... + RPG-7 calibration
            ads = (frame>=292 && frame<340)?1.0f:0.0f;  // capture the 8x scope fully aimed
            gunOccluded=0; greCharging=0;    // force a clean view-model capture
        }
        if(!gInTank)
        {
            float hsp2=pvel.x*pvel.x+pvel.z*pvel.z; int moving=(hsp2>1.0f && jumps==0);
            DrawFirstPersonLegs(cam,moving,timeAlive*9.0f);
        }
        if(!gInTank && ads<0.5f && !gunOccluded)
        {
            float rMax=(weapon==0)?75.0f:100.0f;
            float reload01=(weapon==4)
                ? ((rpgReload>0)?clampf(1.0f-rpgReload/112.0f,0.0f,1.0f):0.0f)
                : ((reload>0)?clampf(1.0f-reload/rMax,0.0f,1.0f):0.0f);
            if(greCharging) DrawGrenadeView(cam,greHold);
            else DrawRifleView(cam,weapon,gunKick,reload01);
        }
        EndMode3D();
        if(paused) Touch_DrawPauseMenu();
        else {
            drawHud(); drawKillFeed(); drawScope(); drawNetNames(); Touch_DrawHUD(); drawTalk();
            if(gInTank) CNC("坦克：W/S 行驶 · A/D 转向 · 鼠标/火 开炮 · F / “话”下车",
                GetScreenWidth()/2,GetScreenHeight()-26,16,(Color){255,230,170,235});
            else if(!gTank.alive && gCoopRole!=2)
            { float td=vlen((Vector3){eye.x-gTank.pos.x,0,eye.z-gTank.pos.z});
              if(td<4.6f) CNC("敌坦克已被击毁 —— 按 F（手机点“话”）登车驾驶",
                GetScreenWidth()/2,GetScreenHeight()-58,17,(Color){255,220,150,255}); }
        }
        EndDrawing();
        if(gSelfTest && gCoopRole==0){
            if(frame==150) TakeScreenshot(TextFormat("%s/shot_akm.png",gShotDir));
            if(frame==200) TakeScreenshot(TextFormat("%s/shot_knife.png",gShotDir));
            if(frame==260) TakeScreenshot(TextFormat("%s/shot_ground.png",gShotDir));
            if(frame==278) TakeScreenshot(TextFormat("%s/shot_scope_model.png",gShotDir));
            if(frame==330) TakeScreenshot(TextFormat("%s/shot_scope_ads.png",gShotDir));
            if(frame==372) TakeScreenshot(TextFormat("%s/shot_rpg.png",gShotDir));
        }
        if(gFlagTest && frame==150) TakeScreenshot(TextFormat("%s/shot_flag.png",gShotDir));

        // win / lose (frozen while paused / in dialogue)
        if(!paused&&!talkOpen)
        {
        float dz=eye.z-OBJV.z, dx=eye.x-OBJV.x;
        bool nearObj=(dx*dx+dz*dz)<80.0f*80.0f;
        if(gCoopRole==1) for(int id=1;id<BNET_MAXPLY;id++) if(av[id].active&&av[id].alive)
        { float ax=av[id].pos.x-OBJV.x, az=av[id].pos.z-OBJV.z; if(ax*ax+az*az<80.0f*80.0f) nearObj=true; }
        int fa=foesAlive();
        // client: victory is declared by the host snapshot
        if(gCoopRole==2 && gHaveSnap && gNetWin==1 && celebrate<=0)
        { celebrate=3.4f; planted=1; }
        if(hp<=0){ endId=(gRpgMode)?204:(foesKilled>=5||(eye.z< -250))?(Map_IsChosin()?206:203):(Map_IsChosin()?206:204); break; }
        if(gRpgMode && gCoopRole!=2 && celebrate<=0)
        {
            // Anti-tank objective: every armoured vehicle on the ridge must burn.
            if(armorAlive()==0)
            {
                celebrate=3.4f;
                if(pals[0].alive) planter=0;
                else { int ids[NP],k=0; for(int i=0;i<NP;i++)if(pals[i].alive)ids[k++]=i;
                       planter=k?ids[irand(0,k-1)]:-1; }
                if(gSelfTest && gCoopRole==0){endId=207;break;}
            }
        }
        else if(!gRpgMode && gCoopRole!=2 && celebrate<=0)
        {
            // Take the hill: stand on the objective with the garrison broken
            // (annihilated, or routed to a remnant <=12) and hold it 4.5 s.
            if(nearObj && fa<=12) holdT+=dt; else holdT=0;
            if((fa==0 && nearObj) || holdT>=4.5f)
            {
                celebrate=3.4f;
                if(pals[0].alive) planter=0;                       // squad leader first
                else { int ids[NP],k=0; for(int i=0;i<NP;i++)if(pals[i].alive)ids[k++]=i;
                       planter=k?ids[irand(0,k-1)]:-1; }            // else a random survivor
                if(gSelfTest && gCoopRole==0){endId=(fa==0)?(Map_IsChosin()?205:201):(Map_IsChosin()?205:202);break;}
            }
        }
        else if(celebrate>0 && (celebrate-dt)<=0)
        { endId=gRpgMode?207:(fa==0)?(Map_IsChosin()?205:201):(Map_IsChosin()?205:202); break; }
        // dev co-op verification: run ~13s, screenshot around 9s, then report
        if(gSelfTest && gCoopRole!=0)
        {
            static int shotNet=0;
            if(!shotNet && timeAlive>9.0f)
            { shotNet=1; TakeScreenshot(TextFormat("%s/shot_net%s.png",gShotDir,gCoopRole==1?"host":"client")); }
            if(timeAlive>13.0f)
            {
                if(gCoopRole==1)
                {
                    int moved=0; for(int id=1;id<BNET_MAXPLY;id++) if(av[id].active&&fabsf(av[id].pos.z-178)>15)moved=1;
                    printf("NETHOST_DONE active=%d moved=%d avatarShots=%d foesAlive=%d\n",
                        (av[1].active||av[2].active||av[3].active)?1:0,moved,gAvatarShots,fa);
                }
                else
                {
                    int hostBody=0; for(int i=0;i<gSnap.plyN;i++) if(gSnap.players[i].id==0)hostBody=1;
                    printf("NETCLIENT_DONE haveSnap=%d foesAlive=%d hostBody=%d myHp=%d win=%d\n",
                        gHaveSnap?1:0, gHaveSnap?gSnap.foesAlive:-1, hostBody, (int)hp, gNetWin);
                }
                endId=201; break;
            }
        }
        if(gSelfTest && gCoopRole==0 && frame>=420){ endId=201; break; }
        }
    }
    EnableCursor();

    gGroundResult=(GroundResult){0};
    gGroundResult.hp=hp; gGroundResult.foesKilled=foesKilled; gGroundResult.friendliesAlive=palsAlive();
    gGroundResult.holdTime=(int)holdT; gGroundResult.timeAlive=timeAlive;
    // classify win/loss BEFORE the id is re-themed into one of many endings
    int victory=(endId==201||endId==202||endId==205||endId==207);
    if(endId==207) { /* RPG anti-tank ending stays its own win */ }
    else if(victory)
    {
        if(Map_IsChosin()) endId=205;
        else if(Map_IsSnow()) endId=210;        // 雪原歼敌
        else if(Map_IsNight()) endId=211;       // 夜袭破阵
        else if(Map_IsDusk()) endId=212;        // 黄昏夺旗
        else endId=(foesAlive()==0)?213:216;    // 213 全歼夺山 / 216 残敌溃退、阵地在手
    }
    else
    {
        if(Map_IsChosin()) endId=206;
        else if(foesKilled>=8) endId=214;       // 断后阻击，掩护主力
        else if(holdT>=3.0f) endId=217;         // 差一步插上旗
        else endId=(foesKilled>=5||eye.z<-250)?203:204;
    }
    gGroundResult.win=victory?1:((endId==203||endId==214)?3:2);
    gGroundResult.endingId=endId;
    *outEnding=endId; *outMode=9;
}
