// touch.c - on-screen controls for touch devices (Android).
// On every non-Android platform all accessors are inert stubs, so keyboard /
// mouse gameplay is byte-for-byte unchanged.
#include "common.h"

#if defined(PLATFORM_ANDROID)

#define MAXT 8

typedef struct {
    float bx,by,br;     // button circle
    const char *label;
    int held, prev;
} Btn;

static int   s_mode=0;
static int   s_greHeld=0, s_greRel=0;   // grenade charge hold/release
static int   s_mountLive=0;            // near/in a drivable vehicle -> show 车 button
static float s_axisX=0, s_axisY=0;
static float s_lookDX=0, s_lookDY=0;
static Btn   bFire,bAct,bB,bSw,bAds,bPause,bTalk,bGre,bCan,bMount;
static int   s_greArmed=0;        // a grenade is being charged -> show cancel key
static float sW,sH;

static int inside(float x,float y,Btn*b){ float dx=x-b->bx,dy=y-b->by; return dx*dx+dy*dy<=b->br*b->br; }
// expanded landing hit-zone: a thumb landing just outside the drawn circle
// still grabs the button instead of becoming a camera-look finger (anti-mistouch).
static int insideE(float x,float y,Btn*b){ float r=b->br*1.55f; float dx=x-b->bx,dy=y-b->by; return dx*dx+dy*dy<=r*r; }
static int insideRect(float x,float y,float x0,float y0,float w,float h){ return x>=x0&&x<=x0+w&&y>=y0&&y<=y0+h; }

static int latchAct=0,latchB=0,latchSw=0,latchPause=0,latchTalk=0,latchGre=0,latchCan=0,latchMount=0;
static int started=0;
static int s_lastN=0;            // touch count at end of previous frame
static int s_menuTap=0;          // a NEW finger landed this frame (modal UI taps)
static float s_menuX=0,s_menuY=0;
static int s_modal=0;            // pause overlay up: every finger is a UI finger
void Touch_SetPaused(int p){ s_modal=p; }

// A finger keeps the ROLE it got where it first landed (stick / a button /
// look), so sliding off a button or lifting-and-tapping can never suddenly
// swing the camera, and one tap can't trigger two different controls.
enum { ROLE_NONE=0, ROLE_STICK, ROLE_LOOK,
       ROLE_FIRE, ROLE_ACT, ROLE_B, ROLE_SW, ROLE_ADS, ROLE_TALK, ROLE_GRE, ROLE_GRECANCEL, ROLE_MOUNT, ROLE_PAUSE };
static int   s_role[MAXT];
static float s_lx[MAXT], s_ly[MAXT];
// look-latch: a finger that lands in the look zone does NOT move the camera until
// it first travels beyond LOOKLATCH px; a pure tap (or near-still hold) there is
// ignored so an incidental brush can't spin the view / knock the player off aim.
static int   s_lookOn[MAXT];
#define LOOKLATCH 13.0f
static float s_supp=0;           // seconds to swallow every touch (UI handoff)
void Touch_Suppress(float seconds){ s_supp=seconds; }

// pause-menu buttons (fraction of screen), shared by hit-test and drawing
static int pmRect(float*rx,float*ry,float*rw,float*rh,int which)
{
    *rw=0.30f; *rh=0.09f; *rx=(1.0f-*rw)*0.5f;
    if(which==1){ *ry=0.40f; return 1; }      // 继续
    *ry=0.53f;                                 // 返回主菜单
    return 1;
}

void Touch_Update(int mode)
{
    s_mode=mode;
    sW=(float)GetScreenWidth(); sH=(float)GetScreenHeight();

    // 1) resolve one-frame edges using last frame's held/prev, then roll over
    if(started)
    {
        latchAct  = bAct.held  && !bAct.prev;
        latchB    = bB.held    && !bB.prev;
        latchSw   = bSw.held   && !bSw.prev;
        latchPause= bPause.held&& !bPause.prev;
        latchTalk = bTalk.held && !bTalk.prev;
        latchGre  = bGre.held  && !bGre.prev;
        latchCan  = bCan.held  && !bCan.prev;
        latchMount= bMount.held&& !bMount.prev;
    } else { started=1; }
    bAct.prev=bAct.held; bB.prev=bB.held; bSw.prev=bSw.held; bPause.prev=bPause.held; bTalk.prev=bTalk.held; bGre.prev=bGre.held; bCan.prev=bCan.held; bMount.prev=bMount.held;

    // 2) (re)place geometry without destroying held/prev
    bFire.bx=0.86f*sW; bFire.by=0.74f*sH; bFire.br=0.105f*sH; bFire.label="火";
    bAct.bx =0.735f*sW; bAct.by=0.85f*sH; bAct.br=0.075f*sH; bAct.label="弹";
    bB.bx   =0.86f*sW;  bB.by=0.50f*sH;  bB.br=0.070f*sH;   bB.label="炸";
    bSw.bx  =0.71f*sW;  bSw.by=0.66f*sH; bSw.br=0.064f*sH;  bSw.label="换";
    bAds.bx =0.71f*sW;  bAds.by=0.50f*sH; bAds.br=0.074f*sH; bAds.label="镜";
    bTalk.bx=0.585f*sW; bTalk.by=0.85f*sH; bTalk.br=0.058f*sH; bTalk.label="话";
    bGre.bx =0.585f*sW; bGre.by =0.70f*sH; bGre.br =0.058f*sH; bGre.label="雷";
    bMount.bx=0.585f*sW; bMount.by=0.56f*sH; bMount.br=0.058f*sH; bMount.label="车";
    // cancel-throw key lives on the LEFT by the stick and is only live while a
    // grenade is being charged (kept hidden the rest of the time).
    bCan.bx =0.34f*sW;  bCan.by =0.86f*sH; bCan.br =0.062f*sH; bCan.label="取消";

    // a NEW finger landing is a UI tap candidate: works even while other
    // fingers are already holding the stick / a button (old bug: only touch0
    // from zero fingers counted, so the pause buttons often did nothing).
    int nNow=GetTouchPointCount();
    int newCount=(nNow>s_lastN)?(nNow-s_lastN):0;
    s_menuTap=0;
    if(s_supp>0)
    {
        s_supp-=GetFrameTime();
        for(int k=0;k<MAXT;k++) s_role[k]=ROLE_NONE;
        s_lastN=nNow; nNow=0;
    }

    // 3) reset this frame's classification (fire re-classified each frame too)
    bFire.held=0; bAct.held=0; bB.held=0; bSw.held=0; bAds.held=0; bPause.held=0; bTalk.held=0; bGre.held=0; bCan.held=0; bMount.held=0;
    if(s_modal){ bFire.held=0; s_axisX=0; s_axisY=0; s_lookDX=0; s_lookDY=0;
        // while paused every landing finger is a pure tap; strip in-game roles
        for(int k=0;k<nNow&&k<MAXT;k++)
        {
            Vector2 tp=GetTouchPosition(k);
            if(s_role[k]==ROLE_NONE && newCount>0)
            { s_menuTap=1; s_menuX=tp.x; s_menuY=tp.y; newCount--; }
            s_role[k]=ROLE_NONE;
        }
        for(int k=nNow;k<MAXT;k++) s_role[k]=ROLE_NONE;
        s_lastN=GetTouchPointCount();
        s_greRel=0; if(s_greHeld&&!bGre.held)s_greRel=1; s_greHeld=0;
        return;
    }

    int n=nNow;
    float stickDx=0,stickDy=0,stickOn=0;
    s_lookDX=0; s_lookDY=0;

    for(int k=0;k<n&&k<MAXT;k++)
    {
        Vector2 tp=GetTouchPosition(k);
        float x=tp.x, y=tp.y;
        int justLanded=(s_role[k]==ROLE_NONE);
        if(justLanded)
        {
            s_lookOn[k]=0;
            if(newCount>0){ s_menuTap=1; s_menuX=x; s_menuY=y; newCount--; }
            // assign this finger's role exactly once, at its landing point
            s_lx[k]=x; s_ly[k]=y;
            if(insideRect(x,y,12,12,84,56)) s_role[k]=ROLE_PAUSE;
            else if(s_greArmed && insideE(x,y,&bCan)) s_role[k]=ROLE_GRECANCEL;
            else if(x < 0.42f*sW)           s_role[k]=ROLE_STICK;
            else if(insideE(x,y,&bFire))    s_role[k]=ROLE_FIRE;
            else if(insideE(x,y,&bAct))     s_role[k]=ROLE_ACT;
            else if(insideE(x,y,&bB))       s_role[k]=ROLE_B;
            else if(insideE(x,y,&bAds))     s_role[k]=ROLE_ADS;
            else if(insideE(x,y,&bSw))      s_role[k]=ROLE_SW;
            else if(mode==1 && s_mountLive && insideE(x,y,&bMount)) s_role[k]=ROLE_MOUNT;
            else if(mode==1 && insideE(x,y,&bTalk)) s_role[k]=ROLE_TALK;
            else if(mode==1 && insideE(x,y,&bGre))  s_role[k]=ROLE_GRE;
            // free-look only in the central-right band; the extreme right edge is a
            // dead gutter, so a thumb resting on the bezel never turns the camera.
            else if(x < 0.975f*sW)          s_role[k]=ROLE_LOOK;
            else                            s_role[k]=ROLE_NONE;
        }
        switch(s_role[k])
        {
            case ROLE_PAUSE: bPause.held=1; break;
            case ROLE_STICK:
            {
                float baseX=0.16f*sW, baseY=0.70f*sH, R=0.17f*sH;
                float dx=(x-baseX)/R, dy=(y-baseY)/R;
                float l=sqrtf(dx*dx+dy*dy); if(l>1){dx/=l;dy/=l;}
                stickDx=dx; stickDy=-dy; stickOn=1;   // screen up -> +Y
                break;
            }
            case ROLE_FIRE:
                bFire.held=1;
                // holding fire you can still drag your aim with the same thumb
                {
                    float ddx=(float)(x-s_lx[k]), ddy=(float)(y-s_ly[k]);
                    const float FDEAD=7.0f, FCAP=38.0f;
                    if(ddx> FCAP)ddx= FCAP; if(ddx<-FCAP)ddx=-FCAP;
                    if(ddy> FCAP)ddy= FCAP; if(ddy<-FCAP)ddy=-FCAP;
                    if(fabsf(ddx)>FDEAD) s_lookDX += ddx*0.7f;
                    if(fabsf(ddy)>FDEAD) s_lookDY += ddy*0.7f;
                    s_lx[k]=x; s_ly[k]=y;
                }
                break;
            case ROLE_ACT:  bAct.held=1;  break;
            case ROLE_B:    bB.held=1;    break;
            case ROLE_ADS:
                bAds.held=1;
                // the thumb holding the scope can keep sweeping the view, so
                // aiming down sights doesn't lock the camera
                {
                    float ddx=(float)(x-s_lx[k]), ddy=(float)(y-s_ly[k]);
                    const float ADEAD=4.0f, ACAP=42.0f;
                    if(ddx> ACAP)ddx= ACAP; if(ddx<-ACAP)ddx=-ACAP;
                    if(ddy> ACAP)ddy= ACAP; if(ddy<-ACAP)ddy=-ACAP;
                    if(fabsf(ddx)>ADEAD) s_lookDX += ddx;
                    if(fabsf(ddy)>ADEAD) s_lookDY += ddy;
                    s_lx[k]=x; s_ly[k]=y;
                }
                break;
            case ROLE_SW:   bSw.held=1;   break;
            case ROLE_MOUNT: if(mode==1) bMount.held=1; break;
            case ROLE_TALK: if(mode==1) bTalk.held=1; break;
            case ROLE_GRE:  if(mode==1) bGre.held=1;  break;
            case ROLE_GRECANCEL: bCan.held=1; break;
            case ROLE_LOOK:
            {
                // Movement from the ORIGINAL landing point is measured until the
                // thumb crosses the latch threshold; only then does the camera start
                // following, and from that moment the finger's current spot is the
                // pivot. A stationary tap / tiny tremor never rotates the view.
                float ox=(float)(x-s_lx[k]), oy=(float)(y-s_ly[k]);
                if(!s_lookOn[k])
                {
                    if(ox*ox+oy*oy < LOOKLATCH*LOOKLATCH) break;  // still a "press", ignore
                    s_lookOn[k]=1;
                    s_lx[k]=x; s_ly[k]=y;                        // arm the pivot, discard the jump
                    break;
                }
                float ddx=ox, ddy=oy;
                const float DEAD=4.0f, CAP=42.0f;
                if(ddx> CAP)ddx= CAP; if(ddx<-CAP)ddx=-CAP;
                if(ddy> CAP)ddy= CAP; if(ddy<-CAP)ddy=-CAP;
                if(fabsf(ddx)>DEAD) s_lookDX += ddx;
                if(fabsf(ddy)>DEAD) s_lookDY += ddy;
                s_lx[k]=x; s_ly[k]=y;
                break;
            }
        }
    }
    for(int k=n;k<MAXT;k++){ s_role[k]=ROLE_NONE; s_lookOn[k]=0; }
    s_lastN=n;
    s_axisX=stickOn?stickDx:0; s_axisY=stickOn?stickDy:0;
    // grenade charge: release edge of the 雷 button throws the charged grenade
    s_greRel=0;
    if(s_greHeld && !bGre.held) s_greRel=1;
    s_greHeld=bGre.held;
}

float Touch_AxisX(void){ return s_axisX; }
float Touch_AxisY(void){ return s_axisY; }
float Touch_LookDX(void){ return s_lookDX; }
float Touch_LookDY(void){ return s_lookDY; }
int   Touch_FireHeld(void){ return bFire.held; }
int   Touch_ADSHeld(void){ return bAds.held; }
int   Touch_ActPressed(void){ return latchAct; }
int   Touch_BPressed(void){ return latchB; }
int   Touch_SwitchPressed(void){ return latchSw; }
int   Touch_PausePressed(void){ return latchPause; }
int   Touch_TalkPressed(void){ return latchTalk; }
int   Touch_GrePressed(void){ return latchGre; }
int   Touch_GreHeld(void){ return s_greHeld; }
int   Touch_GreReleased(void){ return s_greRel; }
int   Touch_GreCancelPressed(void){ return latchCan; }
int   Touch_MountPressed(void){ return latchMount; }
void  Touch_SetMountLive(int a){ s_mountLive=a; }
void  Touch_SetGrenadeArmed(int a){ s_greArmed=a; }
int   Touch_IsTouch(void){ return 1; }

// one fresh tap this frame, in screen pixels (for the pause overlay)
int Touch_PauseTap(float*x,float*y)
{
    if(!s_menuTap) return 0;
    *x=s_menuX; *y=s_menuY; return 1;
}

// Full-screen menu tap, independent of Touch_Update(). Coordinates are pixels.
//
// IMPORTANT: the old implementation "consumed" the press inside Touch_UITap,
// so whichever button called it FIRST in a frame swallowed the tap and every
// other button saw nothing — on a phone this showed up as "buttons need two
// taps" and "tapping one item opens a different one". We now sample exactly
// ONCE per frame in Touch_UIBegin() and every Touch_UITap() only READS that
// snapshot, so all buttons in a frame hit-test the same fresh tap; because a
// tap is a single point, it activates at most the one rectangle it lands in.
static int s_uiPrevN=0;
static int s_uiTap=0, s_uiMouse=0;
static float s_uiX=0, s_uiY=0;

void Touch_UIBegin(void)
{
    int n=GetTouchPointCount();
    s_uiTap=(n>s_uiPrevN);
    if(s_uiTap)
    {
        int idx=s_uiPrevN<MAXT?s_uiPrevN:0;
        Vector2 tp=GetTouchPosition(idx);
        if(tp.x>=0){ s_uiX=tp.x; s_uiY=tp.y; } else s_uiTap=0;
    }
    s_uiPrevN=n;
    s_uiMouse=IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
    if(s_uiMouse){ s_uiX=(float)GetMouseX(); s_uiY=(float)GetMouseY(); }
}

int Touch_UITap(float*x,float*y)
{
    // read-only snapshot: safe to call from any number of buttons per frame
    if(!s_uiTap && !s_uiMouse) return 0;
    *x=s_uiX; *y=s_uiY;
    return 1;
}

// returns 1 = resume, 2 = quit to menu; hit-tests the fresh tap or a mouse click
int Touch_PauseMenuSelect(void)
{
    float x=-1,y=-1, hit=0;
    if(s_menuTap){ x=s_menuX; y=s_menuY; hit=1; }          // tap coords are PIXELS
    else if(IsMouseButtonPressed(MOUSE_BUTTON_LEFT)){ x=(float)GetMouseX(); y=(float)GetMouseY(); hit=1; }
    if(!hit) return 0;
    for(int which=1;which<=2;which++)
    {
        float rx,ry,rw,rh; pmRect(&rx,&ry,&rw,&rh,which);
        if(insideRect(x,y,rx*sW,ry*sH,rw*sW,rh*sH)) return which;
    }
    return 0;
}

void Touch_DrawPauseMenu(void)
{
    DrawRectangle(0,0,(int)sW,(int)sH,(Color){0,0,0,170});
    Vector2 t=MeasureTextEx(GameFont(),"已 暂 停",64,0);
    DrawTextEx(GameFont(),"已 暂 停",(Vector2){(sW-t.x)*0.5f,sH*0.24f},64,0,(Color){255,236,180,255});
    const char* lbl[2]={"继 续","返回主菜单"};
    for(int i=0;i<2;i++)
    {
        float rx,ry,rw,rh; pmRect(&rx,&ry,&rw,&rh,i+1);
        Rectangle r=(Rectangle){rx*sW,ry*sH,rw*sW,rh*sH};
        DrawRectangleRec(r,(Color){30,40,56,220});
        DrawRectangleLinesEx(r,3,(Color){240,210,140,255});
        Vector2 sz=MeasureTextEx(GameFont(),lbl[i],40,0);
        DrawTextEx(GameFont(),lbl[i],(Vector2){r.x+(r.width-sz.x)*0.5f,r.y+(r.height-sz.y)*0.5f},
                   40,0,(Color){255,255,255,240});
    }
    const char*tip="手机：点选上方按钮    电脑：Enter 继续 / Q 返回菜单";
    Vector2 ts=MeasureTextEx(GameFont(),tip,24,0);
    DrawTextEx(GameFont(),tip,(Vector2){(sW-ts.x)*0.5f,sH*0.70f},24,0,(Color){200,205,215,230});
}

void Touch_DrawHUD(void)
{
    Color ring=(Color){255,255,255,90};
    Color fill=(Color){255,255,255,40};
    // left stick
    float baseX=0.16f*sW, baseY=0.70f*sH, R=0.17f*sH;
    DrawCircleV((Vector2){baseX,baseY},R,fill);
    DrawCircleLinesV((Vector2){baseX,baseY},R,ring);
    DrawCircleV((Vector2){baseX+s_axisX*R,baseY-s_axisY*R},R*0.42f,(Color){255,255,255,120});
    // buttons (labels adapt to mode)
    bAct.label=(s_mode==0)?"弹":"跃";
    bB.label=(s_mode==0)?"炸":"装";
    bFire.label="火"; bSw.label="换";
    Btn* bs[4]={&bFire,&bAct,&bB,&bSw};
    for(int i=0;i<4;i++)
    {
        DrawCircleV((Vector2){bs[i]->bx,bs[i]->by},bs[i]->br,fill);
        DrawCircleLinesV((Vector2){bs[i]->bx,bs[i]->by},bs[i]->br,ring);
        int fs=(int)(bs[i]->br*0.95f);
        Vector2 sz=MeasureTextEx(GameFont(),bs[i]->label,(float)fs,0);
        DrawTextEx(GameFont(),bs[i]->label,
            (Vector2){bs[i]->bx-sz.x*0.5f,bs[i]->by-sz.y*0.5f},(float)fs,0,(Color){255,255,255,220});
    }
    // iron-sight / scope button only in infantry mode
    if(s_mode==1)
    {
        DrawCircleV((Vector2){bAds.bx,bAds.by},bAds.br,fill);
        DrawCircleLinesV((Vector2){bAds.bx,bAds.by},bAds.br,ring);
        int fs=(int)(bAds.br*0.8f);
        Vector2 sz=MeasureTextEx(GameFont(),"镜",(float)fs,0);
        DrawTextEx(GameFont(),"镜",(Vector2){bAds.bx-sz.x*0.5f,bAds.by-sz.y*0.5f},
                   (float)fs,0,(Color){255,255,255,220});
        // interact (last words of a wounded comrade)
        DrawCircleV((Vector2){bTalk.bx,bTalk.by},bTalk.br,fill);
        DrawCircleLinesV((Vector2){bTalk.bx,bTalk.by},bTalk.br,ring);
        int fst=(int)(bTalk.br*0.8f);
        Vector2 szt=MeasureTextEx(GameFont(),"话",(float)fst,0);
        DrawTextEx(GameFont(),"话",(Vector2){bTalk.bx-szt.x*0.5f,bTalk.by-szt.y*0.5f},
                   (float)fst,0,(Color){255,230,170,220});
        // grenade
        DrawCircleV((Vector2){bGre.bx,bGre.by},bGre.br,fill);
        DrawCircleLinesV((Vector2){bGre.bx,bGre.by},bGre.br,ring);
        int fsg=(int)(bGre.br*0.8f);
        Vector2 szg=MeasureTextEx(GameFont(),"雷",(float)fsg,0);
        DrawTextEx(GameFont(),"雷",(Vector2){bGre.bx-szg.x*0.5f,bGre.by-szg.y*0.5f},
                   (float)fsg,0,(Color){255,200,160,220});
        // dedicated vehicle mount/dismount button: only near/in a drivable wreck
        if(s_mountLive)
        {
            Color mfill=(Color){90,150,210,150}, mring=(Color){170,215,255,235};
            DrawCircleV((Vector2){bMount.bx,bMount.by},bMount.br,mfill);
            DrawCircleLinesV((Vector2){bMount.bx,bMount.by},bMount.br,mring);
            int fsm=(int)(bMount.br*0.8f);
            Vector2 szm=MeasureTextEx(GameFont(),"车",(float)fsm,0);
            DrawTextEx(GameFont(),"车",(Vector2){bMount.bx-szm.x*0.5f,bMount.by-szm.y*0.5f},
                       (float)fsm,0,(Color){255,255,255,235});
        }
        // cancel-throw key: only appears while a grenade is being readied
        if(s_greArmed)
        {
            Color cfill=(Color){200,60,50,150}, cring=(Color){255,150,140,230};
            DrawCircleV((Vector2){bCan.bx,bCan.by},bCan.br,cfill);
            DrawCircleLinesV((Vector2){bCan.bx,bCan.by},bCan.br,cring);
            int fsc=(int)(bCan.br*0.52f);
            Vector2 szc=MeasureTextEx(GameFont(),"取消",(float)fsc,0);
            DrawTextEx(GameFont(),"取消",(Vector2){bCan.bx-szc.x*0.5f,bCan.by-szc.y*0.5f},
                       (float)fsc,0,(Color){255,255,255,235});
        }
    }
    DrawRectangle(12,12,84,56,(Color){0,0,0,80});
    DrawRectangleLines(12,12,84,56,ring);
    CNC("暂停",54,40,20,(Color){255,255,255,220});
}

#else  // ---------------- desktop stubs ----------------

void Touch_Update(int mode){ (void)mode; }
void Touch_BeginFrame(void){}
void Touch_DrawHUD(void){}
float Touch_AxisX(void){ return 0; }
float Touch_AxisY(void){ return 0; }
float Touch_LookDX(void){ return 0; }
float Touch_LookDY(void){ return 0; }
int Touch_FireHeld(void){ return 0; }
int Touch_ADSHeld(void){ return 0; }
int Touch_ActPressed(void){ return 0; }
int Touch_BPressed(void){ return 0; }
int Touch_SwitchPressed(void){ return 0; }
int Touch_PausePressed(void){ return 0; }
int Touch_TalkPressed(void){ return 0; }
int Touch_GrePressed(void){ return 0; }
int Touch_GreHeld(void){ return 0; }
int Touch_GreReleased(void){ return 0; }
int Touch_GreCancelPressed(void){ return 0; }
int Touch_MountPressed(void){ return 0; }
void Touch_SetMountLive(int a){ (void)a; }
void Touch_SetGrenadeArmed(int a){ (void)a; }
int Touch_PauseTap(float*x,float*y){ (void)x;(void)y; return 0; }
// Desktop UI still goes through the SAME per-frame snapshot as touch, so lobby
// menus (coop.c rectTap) are clickable with a real mouse. Touch_UIBegin() runs
// once per frame and samples the left-button edge; Touch_UITap() only reads it.
static int s_deskTap=0; static float s_deskX=0.0f,s_deskY=0.0f;
int Touch_UITap(float*x,float*y)
{
    if(!s_deskTap) return 0;
    if(x)*x=s_deskX; if(y)*y=s_deskY; return 1;
}
void Touch_UIBegin(void)
{
    s_deskTap=IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
    if(s_deskTap){ s_deskX=(float)GetMouseX(); s_deskY=(float)GetMouseY(); }
}
void Touch_SetPaused(int p){ (void)p; }
int Touch_PauseMenuSelect(void){ return 0; }
void Touch_DrawPauseMenu(void){}
void Touch_Suppress(float seconds){ (void)seconds; }
int Touch_IsTouch(void){ return 0; }

// desktop: drive pause-menu clicks with a real mouse press, in 0..1 fraction form
#ifdef SUPPORT_DESKTOP_PAUSE
#endif

#endif
