// ground.c - PVA infantry first-person assault on a Korean ridge
#include "common.h"
#include "noise.h"

GroundResult gGroundResult={0};

#define NF 16   // US GIs
#define NP 8    // friendly PVA
typedef struct { Vector3 pos; float ang,hp,fireCd,vy; int alive,state,cryT,cry; } Man;
static Man foes[NF], pals[NP];

static Vector3 eye, pvel; static float yaw,pitch; static float hp;
static int weapon, mag[2], reload, foesKilled;
static float fireCd;
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
static int jumps=0;            // airborne jumps used (0 on ground -> allows double jump)
static float introT=0;         // mission intro banner timer
static const Vector3 OBJV={0,0,-380.0f};
static const char* CRIES[]={"冲啊——！","跟我上！","守住阵地！","为了祖国——！","压过去！"};

static float groundAt(Vector3 p){ return Terrain_Height(p.x,p.z); }
static Vector3 flatFwd(float a){ return (Vector3){sinf(a),0,-cosf(a)}; }
static float angTo(Vector3 from,Vector3 to){ return atan2f(to.x-from.x, -(to.z-from.z)); }

static void spawnBattle(void)
{
    for(int i=0;i<NF;i++)
    {
        float x=frand(-260,260), z=frand(-520,-300);
        foes[i].pos=(Vector3){x,0,z}; foes[i].pos.y=Terrain_Height(x,z);
        foes[i].ang=frand(-0.4f,0.4f)+M_PI; foes[i].hp=55; foes[i].alive=1;
        foes[i].fireCd=frand(0.5f,2.5f); foes[i].state=0; foes[i].cryT=0;
    }
    for(int i=0;i<NP;i++)
    {
        float x=(i-(NP-1)*0.5f)*16+frand(-4,4), z=150+frand(-12,12);
        pals[i].pos=(Vector3){x,0,z}; pals[i].pos.y=Terrain_Height(x,z);
        pals[i].ang=0; pals[i].hp=100; pals[i].alive=1; pals[i].fireCd=frand(0.6f,2);
        pals[i].state=0; pals[i].cryT=0; pals[i].cry=irand(0,4);
    }
}

static int foesAlive(void){int n=0;for(int i=0;i<NF;i++)if(foes[i].alive)n++;return n;}
static int palsAlive(void){int n=0;for(int i=0;i<NP;i++)if(pals[i].alive)n++;return n;}

static bool los(Vector3 a,Vector3 b)
{
    Vector3 m=vmul(vadd(a,b),0.5f);
    float h=Terrain_Height(m.x,m.z)+1.6f;
    return m.y>h;
}

static void killFoe(int i,int byPlayer)
{
    foes[i].alive=0;
    Vector3 c=(Vector3){foes[i].pos.x,foes[i].pos.y+1,foes[i].pos.z};
    FX_Fireball(c,0.35f); FX_Smoke(c,0.5f);
    if(byPlayer)foesKilled++;
}

static void reloadWeapon(void)
{
    if(reload>0)return;
    reload = weapon==0?75:100; // frames @60fps (~1.25s / ~1.67s)
}

static float terrainRayT(Vector3 o,Vector3 d)
{
    float t=0;
    for(int i=0;i<140;i++)
    {
        t+=3.0f; if(t>320)break;
        Vector3 p=vadd(o,vmul(d,t));
        if(p.y<Terrain_Height(p.x,p.z)+0.1f)return t;
    }
    return 320;
}

static void shoot(void)
{
    if(reload>0||fireCd>0)return;
    if(mag[weapon]<=0){ reloadWeapon(); return; }
    mag[weapon]--; fireCd = weapon==0?0.75f:0.10f; Sfx_Gun();
    Vector3 d=vnorm(vadd(aimDir(),v3(frand(-.012f,.012f),frand(-.012f,.012f),frand(-.012f,.012f))));
    Vector3 mz=RifleMuzzle(cam,weapon);
    FX_Muzzle(mz);
    float gT=terrainRayT(eye,d);
    int hit=-1; float best=gT;
    for(int i=0;i<NF;i++)if(foes[i].alive)
    {
        Vector3 c=(Vector3){foes[i].pos.x,foes[i].pos.y+1.35f,foes[i].pos.z};
        Vector3 to=vsub(c,eye); float t=vdot(to,d);
        if(t<=0)continue; Vector3 cp=vadd(eye,vmul(d,t));
        if(vlen(vsub(cp,c))<0.95f && t<best){best=t;hit=i;}
    }
    if(hit>=0)
    {
        Vector3 c=(Vector3){foes[hit].pos.x,foes[hit].pos.y+1.35f,foes[hit].pos.z};
        FX_Tracer(mz,c,(Color){255,235,170,255},0.06f);
        foes[hit].hp-= weapon==0?62:26;
        hitMark=0.18f;
        if(foes[hit].hp<=0)killFoe(hit,1);
    }
    else
    {
        Vector3 ep=vadd(eye,vmul(d,best));
        FX_Tracer(mz,ep,(Color){255,225,150,255},0.05f);
        FX_Muzzle(ep);
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
        Vector3 chest=(Vector3){m->pos.x,m->pos.y+1.5f,m->pos.z};
        float d=vlen(vsub(eye,chest));
        bool see = d<430 && los(chest,eye);
        if(see) m->state=1;
        Vector3 move={0};
        if(m->state==0) // advance toward the line, then dig in
        {
            if(m->pos.z>-150){ move=flatFwd(0); move=vmul(move,7.0f); m->ang=angTo(m->pos,vadd(m->pos,move)); }
            else m->state=2;
        }
        else if(m->state==1)
        {
            m->ang=angTo(m->pos,eye);
            // occasional sidestep
            Vector3 side=(Vector3){cosf(m->ang),0,sinf(m->ang)};
            move=vmul(side, sinf(frame*0.6f+i)*3.0f);
            m->fireCd-=dt;
            if(!gSelfTest && m->fireCd<=0 && d<420)
            {
                m->fireCd=frand(0.9f,2.0f);
                Vector3 mzc=(Vector3){m->pos.x,m->pos.y+1.45f,m->pos.z};
                for(int b=0;b<3;b++)
                    FX_Tracer(mzc,vadd(eye,v3(frand(-2.5f,2.5f),frand(-2.5f,2.5f),frand(-2.5f,2.5f))),(Color){255,190,110,255},0.07f);
                float chance=clampf(0.42f-d*0.0009f,0.03f,0.3f);
                if(frand(0,1)<chance){ hp-=frand(3.0f,8.0f); dmgCd=4.0f; }
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
        if(tgt>=0)
        {
            Vector3 tc=(Vector3){foes[tgt].pos.x,foes[tgt].pos.y+1.4f,foes[tgt].pos.z};
            m->ang=angTo(m->pos,tc);
            // advance until in range, keep loose line with player
            Vector3 want=(Vector3){(i-(NP-1)*0.5f)*12,0,eye.z-30};
            Vector3 mv={0};
            if(bd>170) mv=vmul(vnorm(vsub(tc,chest)),9.0f);
            else if(m->pos.z>want.z) mv=vmul(flatFwd(m->ang),8.0f);
            m->pos=vadd(m->pos,vmul(mv,dt)); m->pos.y=Terrain_Height(m->pos.x,m->pos.z);
            // fire
            m->fireCd-=dt;
            if(bd<330 && m->fireCd<=0 && los(chest,tc))
            {
                m->fireCd=frand(0.5f,1.4f);
                Vector3 aim=vadd(tc,v3(frand(-2,2),frand(-2,2),frand(-2,2)));
                FX_Tracer(chest,aim,(Color){255,235,170,255},0.06f);
                if(frand(0,1)<0.12f){ foes[tgt].hp-=34; if(foes[tgt].hp<=0)killFoe(tgt,0); }
                if(frand(0,1)<0.12f){ m->cryT=1.8f; m->cry=irand(0,4); }
            }
        }
        else
        {
            Vector3 want=(Vector3){eye.x+(i-(NP-1)*0.5f)*10,0,eye.z-24};
            Vector3 mv=vnorm(vsub(want,m->pos)); mv.y=0;
            m->pos=vadd(m->pos,vmul(mv,8*dt)); m->pos.y=Terrain_Height(m->pos.x,m->pos.z);
            m->ang=angTo(m->pos,want);
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
        yaw+=mdx*0.0026f; pitch-=mdy*0.0026f;   // 鼠标右移→视角右转
        pitch=clampf(pitch,-1.5f,1.5f);
        if(IsKeyPressed(KEY_ONE))weapon=0;
        if(IsKeyPressed(KEY_TWO))weapon=1;
        if(Touch_SwitchPressed())weapon^=1;
        if(IsKeyPressed(KEY_R)||Touch_BPressed())reloadWeapon();
        if(fireMouse||Touch_FireHeld())shoot();
        adsMouseHold=adsMouse;
    }
    else
    {
        // automated verification: march north and fire
        yaw=0;
        if(frame%30==0){mag[weapon]=10;shoot();}
        eye.z-=22*dt;
    }
    if(reload>0){ reload-=dt*60.0f; if(reload<=0){reload=0;mag[weapon]=weapon==0?5:30;} }
    fireCd-=dt;
    Vector3 f=flatFwd(yaw), r=(Vector3){cosf(yaw),0,sinf(yaw)};
    Vector3 keyWish={0};
    if(IsKeyDown(KEY_W))keyWish=vadd(keyWish,f);
    if(IsKeyDown(KEY_S))keyWish=vsub(keyWish,f);
    if(IsKeyDown(KEY_D))keyWish=vadd(keyWish,r);
    if(IsKeyDown(KEY_A))keyWish=vsub(keyWish,r);
    float spd=IsKeyDown(KEY_LEFT_SHIFT)?8.5f:5.2f;
    if(vlen(keyWish)>0)keyWish=vmul(vnorm(keyWish),spd);
    // touch stick (analog magnitude; inert on desktop)
    Vector3 tWish={0};
    { float ax=Touch_AxisX(), ay=Touch_AxisY();
      tWish=vmul(vadd(vmul(f,ay),vmul(r,ax)),spd); }
    Vector3 wish=vadd(keyWish,tWish);
    if(vlen(wish)>spd)wish=vmul(vnorm(wish),spd);
    pvel.x=wish.x; pvel.z=wish.z;
    if(!gSelfTest&&(IsKeyPressed(KEY_SPACE)||Touch_ActPressed())&&jumps<2)
    { pvel.y=(jumps==0)?7.2f:6.4f; jumps++; }
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
    }
    float gy=Terrain_Height(eye.x,eye.z)+1.68f;
    if(eye.y<gy){eye.y=gy; if(pvel.y<=0.0f){pvel.y=0; jumps=0;}}
    if(fabsf(eye.x)>WORLD_HALF-10)eye.x=WORLD_HALF-10;
    if(fabsf(eye.z)>WORLD_HALF-10)eye.z=WORLD_HALF-10;
    if(dmgCd>0)dmgCd-=dt; else hp+=6*dt;
    hp=clampf(hp,0,100);
    if(hitMark>0)hitMark-=dt;
    // 右键机瞄/狙击镜（仅桌面鼠标；触屏无右键）
    float adsWant=(!gSelfTest && (adsMouseHold||Touch_ADSHeld()))?1.0f:0.0f;
    ads+=(adsWant-ads)*(1.0f-powf(0.0001f,dt));
    if(ads<0.001f)ads=0.0f; if(ads>0.999f)ads=1.0f;
}

static void drawScope(void)
{
    if(ads<=0.02f) return;
    int sw=GetScreenWidth(), sh=GetScreenHeight();
    int cx=sw/2, cy=sh/2;
    float R=(float)(sh/2)*0.92f;
    Color black=(Color){8,8,8,(unsigned char)(235*ads)};
    // 四块外围遮罩，留出圆形视窗
    DrawRectangle(0,0,sw,cy-(int)R,black);                 // 上
    DrawRectangle(0,cy+(int)R,sw,sh-(cy+(int)R),black);   // 下
    DrawRectangle(0,cy-(int)R,cx-(int)R,(int)(2*R),black);// 左
    DrawRectangle(cx+(int)R,cy-(int)R,sw-(cx+(int)R),(int)(2*R),black);// 右
    // 镜筒环
    DrawCircleLines(cx,cy,R,(Color){60,60,66,(unsigned char)(255*ads)});
    DrawCircleLines(cx,cy,R-3,(Color){120,120,128,(unsigned char)(200*ads)});
    // 十字刻度
    Color ret=(Color){0,0,0,(unsigned char)(210*ads)};
    DrawLine(cx-(int)R+8,cy,cx-14,cy,ret); DrawLine(cx+14,cy,cx+(int)R-8,cy,ret);
    DrawLine(cx,cy-(int)R+8,cx,cy-14,ret); DrawLine(cx,cy+14,cx,cy+(int)R-8,ret);
    DrawCircleLines(cx,cy,10,ret); DrawCircleLines(cx,cy,2,ret);
    // 密位点
    for(int i=1;i<=4;i++){ int d=i*(int)(R/5);
        DrawPixel(cx-d,cy,ret); DrawPixel(cx+d,cy,ret);
        DrawPixel(cx,cy-d,ret); DrawPixel(cx,cy+d,ret); }
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

static void drawHud(void)
{
    int cx=GetScreenWidth()/2, cy=GetScreenHeight()/2;
    Color cc=(hitMark>0)?(Color){255,80,60,255}:WHITE;
    if(ads<0.5f){
    DrawLine(cx-10,cy,cx-3,cy,cc);DrawLine(cx+3,cy,cx+10,cy,cc);
    DrawLine(cx,cy-10,cx,cy-3,cc);DrawLine(cx,cy+3,cx,cy+10,cc);
    }
    if(hitMark>0) DrawTextEx(GameFont(),"X",(Vector2){cx-7,cy-16},22,2,(Color){255,70,60,255});

    CN(weapon==0?"莫辛-纳甘步枪":"AKM 突击步枪",16,GetScreenHeight()-60,18,(Color){255,236,180,255});
    CN(reload>0?"装填中...":TextFormat("%d / ∞",mag[weapon]),16,GetScreenHeight()-34,18,WHITE);
    CN("生命",16,16,16,(Color){230,230,230,255});
    DrawRectangle(70,16,160,14,(Color){0,0,0,140});
    DrawRectangle(70,16,(int)(160*hp/100.0f),14,hp>35?(Color){200,60,50,255}:(Color){235,90,70,255});
    DrawRectangleLines(70,16,160,14,WHITE);
    CN(TextFormat("残敌 %d   战友 %d",foesAlive(),palsAlive()),16,40,17,WHITE);
    CN("WASD移动  Shift冲刺  空格跳跃(可二段跳)  鼠标瞄准射击  1步枪 2冲锋枪 R装填  ESC撤退",16,GetScreenHeight()-12,14,(Color){215,220,230,220});
    drawCompass();
    if(introT>0)
        CNC(gScenario==1?"长津湖 · 冰雕连 —— 卧雪潜伏，号响即冲":"夺取前方高地 · 冲啊！",
            GetScreenWidth()/2,(int)(GetScreenHeight()*0.30f),26,(Color){255,228,170,235});

    // objective marker
    Vector3 op=(Vector3){OBJV.x, Terrain_Height(OBJV.x,OBJV.z)+4, OBJV.z};
    Vector2 os=GetWorldToScreen(op,cam);
    const char* goal=gScenario==1?"目标：夜袭隘口·冲锋":"目标：夺取前方高地";
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
    DrawFPS(GetScreenWidth()-90,GetScreenHeight()-24);
}

void Ground_Run(int *outMode,int *outEnding)
{
    memset(foes,0,sizeof(foes)); memset(pals,0,sizeof(pals));
    spawnBattle();
    eye=(Vector3){0,0,180}; eye.y=Terrain_Height(0,180)+1.68f;
    yaw=0; pitch=-0.05f; hp=100; weapon=0; mag[0]=5;mag[1]=30; reload=0; fireCd=0;
    foesKilled=0; hitMark=0; dmgCd=0; holdT=0; timeAlive=0; frame=0; pvel=v3(0,0,0); ads=0; paused=0; kickP=kickY=gunKick=0;
    cam=(Camera3D){0}; cam.fovy=72; cam.projection=CAMERA_PERSPECTIVE; cam.up=(Vector3){0,1,0};
    jumps=0; introT=gScenario==1?7.0f:3.5f;
    DisableCursor();
    Sfx_Bugle();

    int endId=0;
    while(!WindowShouldClose())
    {
        float dt=clampf(GetFrameTime(),0,0.033f); frame++; timeAlive+=dt;
        Touch_Update(1);
        if(IsKeyPressed(KEY_ESCAPE)){ EnableCursor(); *outMode=0; return; }
        if(!gSelfTest && (Touch_PausePressed()||IsKeyPressed(KEY_P))) paused^=1;
        if(paused)
        {
            int pm=Touch_PauseMenuSelect();
            if(pm==2||IsKeyPressed(KEY_Q)){ EnableCursor(); *outMode=0; return; }
            if(pm==1||IsKeyPressed(KEY_ENTER)||IsKeyPressed(KEY_KP_ENTER)) paused=0;
        }
        if(!paused){ updatePlayer(dt); updateFoes(dt); updatePals(dt);
            FX_Update(dt); Env_Update(dt); if(introT>0)introT-=dt; }

        Vector3 dir=aimDir();
        cam.position=eye; cam.target=vadd(eye,dir);
        cam.fovy=72.0f-ads*(72.0f-30.0f);   // 右键瞄准：收窄视场=放大
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
        ClearBackground((Color){158,188,214,255});
        Env_DrawSky2D();
        BeginMode3D(cam);
        Terrain_Draw(cam); Sea_Draw(cam); Env_Draw(cam);
        // objective flag
        float gy=Terrain_Height(OBJV.x,OBJV.z);
        DrawPart(P_CYL,C_BROWN,MatrixIdentity(),MPart((Vector3){OBJV.x,gy+4,OBJV.z},(Vector3){1,0,0},0,(Vector3){0.12f,8,0.12f}));
        DrawPart(P_BOX,C_RED,MatrixIdentity(),MPart((Vector3){OBJV.x+1.4f,gy+6.6f,OBJV.z},(Vector3){1,0,0},0,(Vector3){2.8f,1.6f,0.08f}));
        // sandbag cover
        for(int i=0;i<6;i++){ float x=-60+i*24; float z=-260-((i%2)*20); DrawPart(P_BOX,C_SAND,MatrixIdentity(),MPart((Vector3){x,Terrain_Height(x,z)+0.5f,z},(Vector3){0,1,0},i*0.4f,(Vector3){4,1,1.2f})); }
        // wrecked truck decoys
        DrawVehicle((Vector3){-90, Terrain_Height(-90,-60), -60}, 0.6f, 0, 1.0f);
        DrawVehicle((Vector3){110, Terrain_Height(110,-120), -120}, 2.2f, 2, 1.0f);
        for(int i=0;i<NF;i++)if(foes[i].alive) DrawSoldier(foes[i].pos,-foes[i].ang,1,1.0f,1);
        for(int i=0;i<NP;i++)if(pals[i].alive) DrawSoldier(pals[i].pos,-pals[i].ang,0,1.0f,1);
        FX_Draw3D(cam);
        if(ads<0.5f && !gunOccluded) DrawRifleView(cam,weapon,gunKick);  // 瞄准/贴墙时隐去枪
        EndMode3D();
        if(paused) Touch_DrawPauseMenu();
        else { drawHud(); drawScope(); Touch_DrawHUD(); }
        EndDrawing();
        if(gSelfTest && frame==260) TakeScreenshot(TextFormat("%s/shot_ground.png",gShotDir));

        // win / lose (frozen while paused)
        if(!paused)
        {
        float dz=eye.z-OBJV.z, dx=eye.x-OBJV.x;
        bool nearObj=(dx*dx+dz*dz)<70*70;
        int fa=foesAlive();
        if(nearObj && fa<=3) holdT+=dt; else holdT=0;
        if(hp<=0){ endId=(foesKilled>=5||(eye.z< -250))?(gScenario==1?206:203):(gScenario==1?206:204); break; }
        if((fa==0 && nearObj) || holdT>=5){ endId=(fa==0)?(gScenario==1?205:201):(gScenario==1?205:202); break; }
        if(gSelfTest && frame>=420){ endId=201; break; }
        }
    }
    EnableCursor();

    gGroundResult=(GroundResult){0};
    gGroundResult.hp=hp; gGroundResult.foesKilled=foesKilled; gGroundResult.friendliesAlive=palsAlive();
    gGroundResult.holdTime=(int)holdT; gGroundResult.timeAlive=timeAlive;
    gGroundResult.win=(endId==203||endId==204)?(endId==203?3:2):1;
    gGroundResult.endingId=endId;
    *outEnding=endId; *outMode=9;
}
