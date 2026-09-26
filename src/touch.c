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
static float s_axisX=0, s_axisY=0;
static float s_lookDX=0, s_lookDY=0;
static Btn   bFire,bAct,bB,bSw,bAds,bPause;
static int   px[MAXT], py[MAXT], pvalid[MAXT];
static float sW,sH;

static int inside(float x,float y,Btn*b){ float dx=x-b->bx,dy=y-b->by; return dx*dx+dy*dy<=b->br*b->br; }
static int insideRect(float x,float y,float x0,float y0,float w,float h){ return x>=x0&&x<=x0+w&&y>=y0&&y<=y0+h; }

static int latchAct=0,latchB=0,latchSw=0,latchPause=0;
static int started=0;

// pause-menu buttons (fraction of screen), shared by hit-test and drawing
static int pmRect(float*rx,float*ry,float*rw,float*rh,int which)
{
    *rw=0.30f; *rh=0.09f; *rx=(1.0f-*rw)*0.5f;
    if(which==1){ *ry=0.40f; return 1; }      // 继续
    *ry=0.53f;                                 // 返回主菜单
    return 1;
}
static int s_prevTouchN=0;

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
    } else { started=1; }
    bAct.prev=bAct.held; bB.prev=bB.held; bSw.prev=bSw.held; bPause.prev=bPause.held;

    // 2) (re)place geometry without destroying held/prev
    bFire.bx=0.86f*sW; bFire.by=0.74f*sH; bFire.br=0.105f*sH; bFire.label="火";
    bAct.bx =0.735f*sW; bAct.by=0.85f*sH; bAct.br=0.075f*sH; bAct.label="弹";
    bB.bx   =0.86f*sW;  bB.by=0.50f*sH;  bB.br=0.070f*sH;   bB.label="炸";
    bSw.bx  =0.72f*sW;  bSw.by=0.67f*sH; bSw.br=0.060f*sH;  bSw.label="换";
    bAds.bx =0.72f*sW;  bAds.by=0.51f*sH; bAds.br=0.062f*sH; bAds.label="镜";

    // 3) reset this frame's classification (fire re-classified each frame too)
    bFire.held=0; bAct.held=0; bB.held=0; bSw.held=0; bAds.held=0; bPause.held=0;

    int n=GetTouchPointCount();
    float stickDx=0,stickDy=0,stickOn=0;
    s_lookDX=0; s_lookDY=0;

    for(int k=0;k<n&&k<MAXT;k++)
    {
        Vector2 tp=GetTouchPosition(k);
        float x=tp.x, y=tp.y;
        // pause tab has top priority (it sits in the left half, above the stick)
        if(insideRect(x,y,12,12,84,56)) bPause.held=1;
        else if(x < 0.42f*sW)
        {
            float baseX=0.16f*sW, baseY=0.70f*sH, R=0.17f*sH;
            float dx=(x-baseX)/R, dy=(y-baseY)/R;
            float l=sqrtf(dx*dx+dy*dy); if(l>1){dx/=l;dy/=l;}
            stickDx=dx; stickDy=-dy; stickOn=1;   // screen up -> +Y
        }
        else if(inside(x,y,&bFire)) bFire.held=1;
        else if(inside(x,y,&bAct))  bAct.held=1;
        else if(inside(x,y,&bB))    bB.held=1;
        else if(inside(x,y,&bAds))  bAds.held=1;
        else if(inside(x,y,&bSw))   bSw.held=1;
        else if(mode==1 && pvalid[k])
        {
            // dead-zone: a tap or tiny jitter must not swing the view
            float ddx=(float)(x-px[k]), ddy=(float)(y-py[k]);
            if(fabsf(ddx)>3.0f) s_lookDX += ddx;
            if(fabsf(ddy)>3.0f) s_lookDY += ddy;
        }
        px[k]=(int)x; py[k]=(int)y; pvalid[k]=1;
    }
    s_prevTouchN=n;
    for(int k=n;k<MAXT;k++) pvalid[k]=0;
    s_axisX=stickOn?stickDx:0; s_axisY=stickOn?stickDy:0;
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
int   Touch_IsTouch(void){ return 1; }

// returns 1 = resume, 2 = quit to menu (fires on a fresh touch-press / mouse click)
int Touch_PauseMenuSelect(void)
{
    int n=GetTouchPointCount();
    int rising=(s_prevTouchN==0 && n>0);
    int mouse=IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
    float x=-1,y=-1;
    if(rising){ Vector2 t=GetTouchPosition(0); x=t.x; y=t.y; }
    else if(mouse){ x=(float)GetMouseX(); y=(float)GetMouseY(); }
    else { s_prevTouchN=n; return 0; }
    s_prevTouchN=n;
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
int Touch_PauseMenuSelect(void){ return 0; }
void Touch_DrawPauseMenu(void){}
int Touch_IsTouch(void){ return 0; }

#endif
