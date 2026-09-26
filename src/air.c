// air.c - J-20 flight combat over Korean-war terrain
#include "common.h"
#include "noise.h"

extern Shader gLit;

AirResult gAirResult = {0};

#define FWDQ(q) Vector3RotateByQuaternion((Vector3){0,0,-1},(q))
#define UPQ(q)  Vector3RotateByQuaternion((Vector3){0,1,0},(q))
#define RGTQ(q) Vector3RotateByQuaternion((Vector3){1,0,0},(q))

typedef enum { PATROL, ENGAGE, EVADE } ESt;
typedef struct {
    Vector3 pos; Quaternion q; float speed, hp; int alive; ESt st;
    float fireCd, stateT; Vector3 wp;
} EJet;
typedef struct { Vector3 pos,vel; Quaternion q; int alive,target; float age; int bomb; float trail; } Proj;

#define MAXE 12
#define MAXG 18
#define MAXM 24
static EJet es[MAXE];
static Proj ms[MAXM];
typedef struct { Vector3 pos; float yaw; int kind,alive; float hp,fireCd; Vector3 vel; } G;
static G gs[MAXG];

static struct { Vector3 pos; Quaternion q; float speed,hp,gunCd; int missiles,bombs; } P;
static int wave, jetsKilled, groundKilled, score;
static float waveTimer, bannerT, radioT;
static char bannerBuf[160], radioMsg[160];
static Camera3D cam; static Vector3 camPos; static int lock;
static int frame=0, airSaid=0;
static int airPaused=0;

static void say(const char* t){ strncpy(radioMsg,t,sizeof(radioMsg)-1); radioMsg[sizeof(radioMsg)-1]=0; radioT=4.0f; }
static void ban(const char* t,float sec){ strncpy(bannerBuf,t,sizeof(bannerBuf)-1); bannerBuf[sizeof(bannerBuf)-1]=0; bannerT=sec; }

static int aliveEnemies(void){ int n=0; for(int i=0;i<MAXE;i++) if(es[i].alive)n++; return n; }
static int aliveGround(void){ int n=0; for(int i=0;i<MAXG;i++) if(gs[i].alive)n++; return n; }

static void spawnWave(int wv)
{
    int n = wv+1; // 2,3,4,5...
    Vector3 c = (Vector3){frand(-500,500), 0, frand(-900,-400)};
    int made=0;
    for (int i=0;i<MAXE && made<n;i++)
    {
        if (es[i].alive) continue;
        es[i].alive=1; es[i].hp=100; es[i].st=PATROL; es[i].fireCd=frand(0.5f,2); es[i].stateT=0;
        es[i].pos=(Vector3){c.x+frand(-220,220), frand(380,720), c.z+frand(-220,220)};
        es[i].speed=frand(150,200);
        es[i].q=QuaternionFromEuler(0,frand(0,6.28f),0);
        es[i].wp=(Vector3){frand(-700,700),frand(300,650),frand(-1000,-200)};
        made++;
    }
}
static void setupGround(void)
{
    int n=0;
    for (int g=0;g<MAXG && n<15;g++)
    {
        gs[g].alive=1; gs[g].hp=100; gs[g].fireCd=frand(0.5f,2);
        gs[g].kind = (g%5==1)?1:(g%4==0?2:(g%6==5?3:0));
        float a=frand(0,6.28f), r=frand(20,260);
        gs[g].pos=(Vector3){700+cosf(a)*r, 0, -680+sinf(a)*r};
        gs[g].pos.y=Terrain_Height(gs[g].pos.x,gs[g].pos.z);
        gs[g].yaw=frand(0,6.28f);
        gs[g].vel = (gs[g].kind==2)? v3(frand(-3,3),0,frand(-3,3)) : v3(0,0,0);
        n++;
    }
    for(int g=n;g<MAXG;g++) gs[g].alive=0;
}

static void killEnemy(int i)
{
    es[i].alive=0; jetsKilled++; score+=100;
    FX_Explosion(es[i].pos,1.6f); Sfx_Boom(0.9f);
}
static void killGround(int i)
{
    gs[i].alive=0; groundKilled++; score+=80;
    Vector3 ep=(Vector3){gs[i].pos.x,gs[i].pos.y+2,gs[i].pos.z};
    FX_Explosion(ep,1.3f); Sfx_Boom(0.8f);
}

static Vector3 noseOf(Vector3 pos,Quaternion q,float d){ return vadd(pos, vmul(FWDQ(q),d)); }

static void playerGun(float dt)
{
    P.gunCd-=dt;
    if ((IsKeyDown(KEY_SPACE)||Touch_FireHeld()) && P.gunCd<=0)
    {
        P.gunCd=0.055f; Sfx_Gun();
        Vector3 f=FWDQ(P.q), from=noseOf(P.pos,P.q,11);
        Vector3 end=vadd(from,vmul(f,900));
        int hit=-1; float bestT=1e9f;
        for (int i=0;i<MAXE;i++) if(es[i].alive)
        {
            Vector3 to=vsub(es[i].pos,from); float t=vdot(to,f);
            if(t<0||t>900)continue; Vector3 cp=vadd(from,vmul(f,t));
            float perp=vlen(vsub(cp,es[i].pos));
            if(perp<3.2f && t<bestT){bestT=t;hit=i;}
        }
        int gh=-1;
        for (int i=0;i<MAXG;i++) if(gs[i].alive)
        {
            Vector3 c=(Vector3){gs[i].pos.x,gs[i].pos.y+2,gs[i].pos.z};
            Vector3 to=vsub(c,from); float t=vdot(to,f);
            if(t<0||t>900)continue; Vector3 cp=vadd(from,vmul(f,t));
            if(vlen(vsub(cp,c))<4.0f && t<bestT){bestT=t;gh=i;hit=-1;}
        }
        if(hit>=0){ end=es[hit].pos; es[hit].hp-=frand(16,24); FX_Muzzle(from);
            if(es[hit].hp<=0)killEnemy(hit); else FX_Tracer(from,end,(Color){255,230,150,255},0.06f);}
        else if(gh>=0){ end=(Vector3){gs[gh].pos.x,gs[gh].pos.y+2,gs[gh].pos.z}; gs[gh].hp-=frand(20,30); FX_Muzzle(from);
            if(gs[gh].hp<=0)killGround(gh);}
        else FX_Muzzle(from);
        FX_Tracer(from,end,(Color){255,240,170,255},0.05f);
    }
}

static void fireMissile(void)
{
    if(P.missiles<=0||lock<0){ say("没有可用导弹或未锁定目标"); return; }
    for(int i=0;i<MAXM;i++) if(!ms[i].alive)
    {
        ms[i].alive=1; ms[i].bomb=0; ms[i].target=lock; ms[i].age=0; ms[i].trail=0;
        ms[i].pos=noseOf(P.pos,P.q,12); ms[i].vel=vmul(FWDQ(P.q),P.speed+60);
        ms[i].q=P.q; P.missiles--; say("导弹发射！"); break;
    }
}
static void dropBomb(void)
{
    if(P.bombs<=0){say("航弹已用完");return;}
    for(int i=0;i<MAXM;i++) if(!ms[i].alive)
    {
        ms[i].alive=1; ms[i].bomb=1; ms[i].target=-1; ms[i].age=0;
        ms[i].pos=vadd(P.pos,(Vector3){0,-1,0}); ms[i].vel=vmul(FWDQ(P.q),P.speed*0.6f);
        ms[i].q=QuaternionIdentity(); P.bombs--; say("投弹！"); break;
    }
}

static void updatePlayer(float dt)
{
    float pitch=0,roll=0,yaw=0,thr=0;
    if(!gSelfTest)
    {
        if(IsKeyDown(KEY_W))thr+=1; if(IsKeyDown(KEY_S))thr-=1;
        if(IsKeyDown(KEY_UP))pitch+=1; if(IsKeyDown(KEY_DOWN))pitch-=1;
        if(IsKeyDown(KEY_A))roll+=1; if(IsKeyDown(KEY_D))roll-=1;
        if(IsKeyDown(KEY_Q))yaw+=1; if(IsKeyDown(KEY_E))yaw-=1;
        // touch (inert on desktop)
        float tax=Touch_AxisX(), tay=Touch_AxisY();
        pitch += tay; roll -= tax;
#if defined(PLATFORM_ANDROID)
        thr += 0.62f;   // auto cruise thrust on touch devices
#endif
    }
    P.speed += thr*95*dt;
    if (P.speed<78){ P.speed=78; pitch-=0.55f*dt; }          // stall: nose drops
    if (P.speed>305)P.speed=305;
    Vector3 r=RGTQ(P.q), f=FWDQ(P.q), u=UPQ(P.q);
    Quaternion dq=QuaternionIdentity();
    if(pitch!=0) dq=QuaternionMultiply(QuaternionFromAxisAngle(r, pitch*1.35f*dt),dq);
    if(roll !=0) dq=QuaternionMultiply(QuaternionFromAxisAngle(f, roll*2.6f*dt),dq);
    if(yaw  !=0) dq=QuaternionMultiply(QuaternionFromAxisAngle(u, yaw*1.05f*dt),dq);
    P.q=QuaternionMultiply(dq,P.q);
    f=FWDQ(P.q);
    P.pos=vadd(P.pos,vmul(f,P.speed*dt));
    // soft world bounds
    if(fabsf(P.pos.x)>WORLD_HALF-120||fabsf(P.pos.z)>WORLD_HALF-120) P.q=SteerForward(P.q,vnorm(vsub(v3(0,400,0),P.pos)),1.2f,dt,0);

    playerGun(dt);
    if(!gSelfTest)
    {
        if(IsKeyPressed(KEY_F)||Touch_ActPressed())fireMissile();
        if(IsKeyPressed(KEY_B)||Touch_BPressed())dropBomb();
    }
    float ground=Terrain_Height(P.pos.x,P.pos.z);
    if(P.pos.y<ground+2.0f && !gSelfTest){ FX_Explosion(P.pos,2.2f); Sfx_Boom(1); P.hp=-1; }
}

static bool canRTB(void)
{
    return (P.pos.x*P.pos.x+P.pos.z*P.pos.z < 320*320) &&
           (P.pos.y-Terrain_Height(P.pos.x,P.pos.z)<95) && P.speed<100;
}

static int chooseEnding(void)
{
    int airDone = (wave>=4 && aliveEnemies()==0);
    int grDone = (aliveGround()==0);
    int tot=jetsKilled+groundKilled;
    if(airDone&&grDone)return 101;
    if(airDone)return 102;
    if(grDone)return 103;
    if(tot>=6)return 105;
    return 106;
}

static void updateEnemies(float dt)
{
    for(int i=0;i<MAXE;i++) if(es[i].alive)
    {
        EJet*e=&es[i];
        Vector3 toP=vsub(P.pos,e->pos); float dist=vlen(toP); Vector3 dirP=vmul(toP,1/dist);
        Vector3 ef=FWDQ(e->q);
        // state transitions
        if(e->st==PATROL && dist<1500) e->st=ENGAGE;
        if(e->st==ENGAGE && dist<520 && vdot(ef,dirP)<-0.2f && frand(0,1)<0.01f){ e->st=EVADE; e->stateT=frand(0.8f,1.4f); e->wp=vadd(e->pos,v3(frand(-200,200),frand(40,150),frand(-200,200))); }
        if(e->st==EVADE){ e->stateT-=dt; if(e->stateT<=0)e->st=ENGAGE; }
        Vector3 aim; float turn;
        if(e->st==EVADE){ aim=vnorm(vsub(e->wp,e->pos)); turn=2.2f; e->speed+=(220-e->speed)*dt*0.6f; }
        else if(e->st==ENGAGE)
        {
            // lead pursuit, keep slight offset
            Vector3 pv=vmul(FWDQ(P.q),P.speed);
            Vector3 pred=vadd(P.pos,vmul(pv,dist/420.0f));
            Vector3 off=v3(cosf(frame*0.5f+i)*40,0,sinf(frame*0.5f+i)*40);
            aim=vnorm(vsub(vadd(pred,off),e->pos)); turn=1.15f;
            e->speed+=(185-e->speed)*dt*0.8f;
            // fire
            e->fireCd-=dt;
            if(!gSelfTest && vdot(ef,dirP)>0.955f && dist<900 && dist>180 && e->fireCd<=0)
            {
                e->fireCd=frand(0.9f,1.6f);
                Vector3 mz=noseOf(e->pos,e->q,7);
                for(int b=0;b<3;b++)
                    FX_Tracer(mz,vadd(P.pos,v3(frand(-4,4),frand(-4,4),frand(-4,4))),(Color){255,180,90,255},0.08f);
                float chance=clampf(0.62f-dist*0.00045f,0.08f,0.55f);
                if(frand(0,1)<chance){ P.hp-=frand(2.5f,6); }
            }
        }
        else
        {
            if(vlen(vsub(e->wp,e->pos))<120) e->wp=(Vector3){frand(-800,800),frand(320,680),frand(-1100,-150)};
            aim=vnorm(vsub(e->wp,e->pos)); turn=0.8f; e->speed+=(165-e->speed)*dt;
        }
        // terrain avoidance
        if(e->pos.y<Terrain_Height(e->pos.x,e->pos.z)+120) aim=vnorm(vadd(aim,(Vector3){0,1.2f,0}));
        e->q=SteerForward(e->q,aim,turn,dt,1);
        e->pos=vadd(e->pos,vmul(FWDQ(e->q),e->speed*dt));
        if(fabsf(e->pos.x)>WORLD_HALF-60||fabsf(e->pos.z)>WORLD_HALF-60) e->wp=v3(frand(-400,400),400,frand(-800,-200));
    }
}

static void updateGroundAI(float dt)
{
    Vector3 pp=(Vector3){P.pos.x,P.pos.y,P.pos.z};
    for(int i=0;i<MAXG;i++) if(gs[i].alive)
    {
        G*g=&gs[i];
        // slow convoy/tank drift
        if(g->vel.x||g->vel.z)
        {
            g->pos=vadd(g->pos,vmul(g->vel,dt));
            g->pos.y=Terrain_Height(g->pos.x,g->pos.z);
            if(frand(0,1)<0.004f)g->vel=v3(frand(-3,3),0,frand(-3,3));
        }
        if(g->kind==1)
        {
            Vector3 to=vsub(pp,g->pos); float d=vlen(to); float agl=P.pos.y-Terrain_Height(P.pos.x,P.pos.z);
            g->fireCd-=dt;
            if(!gSelfTest && d<950 && agl<780 && g->fireCd<=0)
            {
                g->fireCd=frand(1.3f,2.1f);
                Vector3 burst=(Vector3){P.pos.x+frand(-30,30),P.pos.y+frand(-20,20),P.pos.z+frand(-30,30)};
                FX_FlakBurst(burst); Sfx_Flak();
                if(vlen(vsub(burst,P.pos))<28) P.hp-=frand(4,9);
            }
        }
    }
}

static void updateMissiles(float dt)
{
    for(int i=0;i<MAXM;i++) if(ms[i].alive)
    {
        Proj*m=&ms[i]; m->age+=dt;
        if(m->bomb)
        {
            m->vel.y-=GRAVITY*2.2f*dt;
            m->pos=vadd(m->pos,vmul(m->vel,dt));
            float gy=Terrain_Height(m->pos.x,m->pos.z);
            if(m->pos.y<=gy+1.0f || m->age>12)
            {
                m->alive=0; FX_Explosion(m->pos,2.4f); Sfx_Boom(1);
                for(int g=0;g<MAXG;g++)if(gs[g].alive){ Vector3 c=(Vector3){gs[g].pos.x,gs[g].pos.y+2,gs[g].pos.z};
                    if(vlen(vsub(c,m->pos))<58){ gs[g].hp-=120; if(gs[g].hp<=0)killGround(g);} }
            }
        }
        else
        {
            m->trail-=dt; if(m->trail<=0){FX_Smoke((Vector3){m->pos.x,m->pos.y,m->pos.z},0.4f);m->trail=0.03f;}
            if(m->target>=0 && es[m->target].alive)
            {
                Vector3 lead=vadd(es[m->target].pos, vmul(FWDQ(es[m->target].q),es[m->target].speed*0.35f));
                m->q=SteerForward(m->q,vnorm(vsub(lead,m->pos)),3.2f,dt,0);
            }
            float sp=vlen(m->vel); sp+=(260-sp)*dt*1.5f;
            m->vel=vmul(FWDQ(m->q),sp);
            m->pos=vadd(m->pos,vmul(m->vel,dt));
            for(int e=0;e<MAXE;e++)if(es[e].alive && vlen(vsub(es[e].pos,m->pos))<13){ killEnemy(e); m->alive=0; break; }
            if(m->age>6 || m->pos.y<Terrain_Height(m->pos.x,m->pos.z)+1){ if(m->alive)FX_Explosion(m->pos,0.8f); m->alive=0; }
        }
    }
}

static void findLock(void)
{
    lock=-1; float best=0.92f;
    Vector3 f=FWDQ(P.q);
    for(int i=0;i<MAXE;i++)if(es[i].alive)
    {
        Vector3 d=vnorm(vsub(es[i].pos,P.pos));
        float dot=vdot(f,d);
        if(dot>best && vlen(vsub(es[i].pos,P.pos))<2600){best=dot;lock=i;}
    }
}

static void updateCamera(float dt)
{
    Vector3 f=FWDQ(P.q), u=UPQ(P.q);
    Vector3 want=vadd(P.pos, vadd(vmul(f,-26),vmul(u,9.5f)));
    camPos = (frame<2)?want:vlerp(camPos,want,1-powf(0.001f,dt));
    cam.position=camPos; cam.target=vadd(P.pos,vmul(f,6)); cam.up=u;
}

// ---------------------------------------------------------------- HUD
static void bar(int x,int y,int w,int h,float frac,Color c)
{ DrawRectangle(x,y,w,h,(Color){0,0,0,140}); DrawRectangle(x,y,(int)(w*clampf(frac,0,1)),h,c); DrawRectangleLines(x,y,w,h,WHITE); }

static void drawHud(void)
{
    // crosshair
    int cx=GetScreenWidth()/2, cy=GetScreenHeight()/2;
    DrawLine(cx-12,cy,cx-4,cy,WHITE); DrawLine(cx+4,cy,cx+12,cy,WHITE);
    DrawLine(cx,cy-12,cx,cy-4,WHITE); DrawLine(cx,cy+4,cx,cy+12,WHITE);
    // lock brackets / off-screen arrows
    Vector3 cf=vnorm(vsub(cam.target,cam.position));
    if(lock>=0&&es[lock].alive)
    {
        Vector3 to=vsub(es[lock].pos,cam.position);
        if(vdot(cf,to)>0)
        {
            Vector2 s=GetWorldToScreen(es[lock].pos,cam);
            DrawRectangleLines((int)s.x-16,(int)s.y-16,32,32,(Color){80,255,120,255});
            CN("LOCK",(int)s.x-18,(int)s.y-34,13,(Color){120,255,150,255});
        }
    }
    Vector2 center={(float)GetScreenWidth()/2,(float)GetScreenHeight()/2};
    for(int i=0;i<MAXE;i++)if(es[i].alive)
    {
        Vector2 s=GetWorldToScreen(es[i].pos,cam);
        bool off= s.x<24||s.x>GetScreenWidth()-24||s.y<24||s.y>GetScreenHeight()-24;
        if(off)
        {
            float dx=s.x-center.x, dy=s.y-center.y, l=sqrtf(dx*dx+dy*dy); if(l<1)l=1; dx/=l;dy/=l;
            int ax=(int)(center.x+dx*170), ay=(int)(center.y+dy*170);
            DrawTriangle((Vector2){ax+dy*6,ay-dx*6},(Vector2){ax-dy*6,ay+dx*6},
                         (Vector2){ax+dx*13,ay+dy*13},(Color){255,90,70,220});
        }
    }
    // left panel
    CN("歼-20 威龙", 16,12,22,(Color){255,230,120,255});
    float agl=P.pos.y-Terrain_Height(P.pos.x,P.pos.z);
    CN(TextFormat("高度 %d m",(int)agl),16,42,18,WHITE);
    CN(TextFormat("空速 %d km/h",(int)(P.speed*3.6f)),16,64,18,WHITE);
    CN(TextFormat("导弹 x%d   航弹 x%d",P.missiles,P.bombs),16,86,18,WHITE);
    CN(TextFormat("击落 %d  对地 %d",jetsKilled,groundKilled),16,108,18,WHITE);
    CN(TextFormat("第 %d/4 波",wave<1?1:wave),16,130,18,(Color){200,230,255,255});
    CN("机体",16,156,16,(Color){220,220,220,255}); bar(70,156,150,14,P.hp/100.0f,P.hp>40?(Color){90,220,90,255}:(Color){230,70,60,255});
    // minimap
    int ms2=120, mx=GetScreenWidth()-ms2-24, my=24;
    DrawRectangle(mx,my,ms2,ms2,(Color){0,20,10,120}); DrawRectangleLines(mx,my,ms2,ms2,(Color){120,255,160,220});
    DrawCircle(mx+ms2/2,my+ms2/2,3,(Color){80,255,120,255}); // player
    float sc=ms2/2600.0f;
    for(int i=0;i<MAXE;i++)if(es[i].alive)DrawCircle(mx+ms2/2+(int)((es[i].pos.x-P.pos.x)*sc),my+ms2/2+(int)((es[i].pos.z-P.pos.z)*sc),2,(Color){255,80,70,255});
    for(int i=0;i<MAXG;i++)if(gs[i].alive)DrawCircle(mx+ms2/2+(int)((gs[i].pos.x-P.pos.x)*sc),my+ms2/2+(int)((gs[i].pos.z-P.pos.z)*sc),2,(Color){255,200,80,255});
    CN("雷达",mx+2,my+ms2+2,14,(Color){150,255,180,255});
    // objectives
    const char *obj = (aliveGround()>0)?"目标：歼灭来袭美机群，摧毁北方河谷敌军装甲纵队":"目标：空域与地面均已肃清，返航(H)";
    CNC(obj,GetScreenWidth()/2,GetScreenHeight()-70,17,(Color){255,240,190,230});
    CN("W/S油门  方向舵俯仰  A/D滚转  Q/E偏航  空格机炮  F导弹  B投弹  H返航  ESC退出",16,GetScreenHeight()-34,15,(Color){210,220,230,220});
    if(radioT>0) CNC(radioMsg,GetScreenWidth()/2,GetScreenHeight()-110,20,(Color){120,230,255,255});
    if(bannerT>0) CNC(bannerBuf,GetScreenWidth()/2,120,34,(Color){255,120,90,255});
    if(P.hp<35) DrawRectangle(0,0,GetScreenWidth(),GetScreenHeight(),(Color){120,0,0,(unsigned char)(80*(1.0f-P.hp/35.0f))});}

void Air_Run(int *outMode, int *outEnding)
{
    // init
    memset(es,0,sizeof(es)); memset(ms,0,sizeof(ms)); memset(gs,0,sizeof(gs));
    P.pos=(Vector3){0,520,220}; P.q=QuaternionFromEuler(0,0,0); P.speed=210; P.hp=100; P.gunCd=0;
    P.missiles=8; P.bombs=6; wave=0; jetsKilled=groundKilled=score=0; waveTimer=99; bannerT=0; radioT=0;
    lock=-1; frame=0; airSaid=0; airPaused=0; bannerBuf[0]=0; radioMsg[0]=0;
    setupGround();
    cam=(Camera3D){0}; cam.fovy=62; cam.projection=CAMERA_PERSPECTIVE; cam.up=(Vector3){0,1,0};
    camPos=P.pos;
    Sfx_Engine(0.6f,1);
    ban("1951 · 朝鲜北部空域",4);
    say("指挥所：歼-20，迎击美军机群，支援地面部队！");

    int endId=0;
    while(!WindowShouldClose())
    {
        float dt=clampf(GetFrameTime(),0,0.033f); frame++;
        Touch_Update(0);
        if(IsKeyPressed(KEY_ESCAPE)){ Sfx_Engine(0,0); *outMode=0; return; }
        if(!gSelfTest && (Touch_PausePressed()||IsKeyPressed(KEY_P))) airPaused^=1;
        if(airPaused)
        {
            int pm=Touch_PauseMenuSelect();
            if(pm==2||IsKeyPressed(KEY_Q)){ Sfx_Engine(0,0); *outMode=0; return; }
            if(pm==1||IsKeyPressed(KEY_ENTER)||IsKeyPressed(KEY_KP_ENTER)) airPaused=0;
        }

        if(!airPaused){
        // wave management
        if(wave==0){ wave=1; spawnWave(1); ban("第一波敌机 来袭！",3); waveTimer=99; }
        if(aliveEnemies()==0)
        {
            if(wave<4){ waveTimer-=dt; if(waveTimer>60){ waveTimer=4; ban(TextFormat("第 %d 波敌机 来袭！",wave+1),3); P.missiles+=2; }
                        if(waveTimer<=0){ wave++; spawnWave(wave); waveTimer=99; } }
            else if(!airSaid){ airSaid=1; ban("空域已肃清！可返航(H)，或继续摧毁地面目标",4); }
        }
        if(!gSelfTest){ updatePlayer(dt); } else {
            // autopilot straight and level for automated verification
            P.pos=vadd(P.pos,vmul(FWDQ(P.q),P.speed*dt));
            if(frame==60) playerGun(0.06f);
        }
        updateEnemies(dt); updateGroundAI(dt); updateMissiles(dt); FX_Update(dt); Env_Update(dt);
        findLock(); updateCamera(dt);
        Scene_SetCamera(cam);
        radioT-=dt; bannerT-=dt;
        }

        // draw
        BeginDrawing();
        ClearBackground((Color){158,188,214,255});
        Env_DrawSky2D();
        BeginMode3D(cam);
        Terrain_Draw(cam); Sea_Draw(cam); Env_Draw(cam);
        // ground assets
        for(int i=0;i<MAXG;i++)if(gs[i].alive) DrawVehicle(gs[i].pos,gs[i].yaw,gs[i].kind,1.0f);
        // projectiles
        for(int i=0;i<MAXM;i++)if(ms[i].alive)
        {
            if(ms[i].bomb) DrawBomb(ms[i].pos,ms[i].q);
            else DrawMissile(ms[i].pos,ms[i].q);
        }
        DrawJ20(P.pos,P.q,1.0f,1);
        for(int i=0;i<MAXE;i++)if(es[i].alive) DrawSabre(es[i].pos,es[i].q,1.0f);
        FX_Draw3D(cam);
        EndMode3D();
        if(airPaused) Touch_DrawPauseMenu();
        else { drawHud();
          DrawFPS(GetScreenWidth()-90,GetScreenHeight()-24);
          Touch_DrawHUD(); }
        EndDrawing();
        if(gSelfTest && frame==140) TakeScreenshot(TextFormat("%s/shot_air.png",gShotDir));

        // RTB / death (frozen while paused)
        if(!airPaused)
        {
        if(!gSelfTest && IsKeyPressed(KEY_H))
        {
            if(canRTB()){ endId=chooseEnding(); break; }
            else say("需在机场上空：低空、慢速，再按 H 降落返航");
        }
        if(P.hp<=0){ endId = (jetsKilled+groundKilled>=10||groundKilled>=8)?110:111; break; }
        }
        if(gSelfTest && frame>=190){ endId=101; break; }
    }
    Sfx_Engine(0,0);

    gAirResult=(AirResult){0};
    gAirResult.hp=P.hp; gAirResult.jetsKilled=jetsKilled; gAirResult.groundKilled=groundKilled;
    gAirResult.wavesTotal=4; gAirResult.missilesLeft=P.missiles;
    gAirResult.timeAlive=frame/60.0f; gAirResult.score=score;
    gAirResult.win = (endId==111)?2:(endId==110?3:1);
    gAirResult.endingId=endId;
    *outEnding=endId; *outMode=9;
}
