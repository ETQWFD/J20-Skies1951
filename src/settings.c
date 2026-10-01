// settings.c - display / language settings screen + persistence.
#include "settings.h"
#include "common.h"
#include "raylib.h"
#include <stdio.h>
#include <string.h>

int gSetFPS=1;
int gSetQuality=1;
int gSetLang=0;

#define SET_FILE "skies_settings.txt"

void Settings_Save(void)
{
    char b[64]; snprintf(b,sizeof b,"%d %d %d\n",gSetFPS,gSetQuality,gSetLang);
    SaveFileText(SET_FILE,b);
}
void Settings_Load(void)
{
    char* s=LoadFileText(SET_FILE);
    if(s){ int f=1,q=1,l=0;
           if(sscanf(s,"%d %d %d",&f,&q,&l)==3)
           { if(f>=0&&f<=2)gSetFPS=f; if(q>=0&&q<=3)gSetQuality=q; if(l>=0&&l<=3)gSetLang=l; }
           UnloadFileText(s); }
}
float Settings_GrassDensity(void)
{ static const float d[4]={0.40f,0.70f,1.05f,1.55f}; return d[gSetQuality]; }
void Settings_Apply(void)
{
    static const int fps[3]={60,120,0};   // 0 = uncapped, cap shown as "480/硬件上限"
    SetTargetFPS(fps[gSetFPS]);
    Grass_SetDensity(Settings_GrassDensity());
}

// ---- phrase table: key, zh, en, ja, ru -------------------------------------
typedef struct { const char*k; const char*t[4]; } Ph;
static const Ph PH[]={
 {"title",   {"设置 · Settings","Settings","設定","Настройки"}},
 {"fps",     {"帧率上限","Frame rate","フレーム上限","Лимит FPS"}},
 {"quality", {"画质","Graphics","画質","Качество"}},
 {"lang",    {"语言","Language","言語","Язык"}},
 {"update",  {"检测更新","Check update","更新を確認","Проверить обновление"}},
 {"back",    {"返回主菜单","Back","戻る","Назад"}},
 {"hint",    {"左右切换，即时生效","Tap arrows, applies instantly","左右で切替、即時反映","Стрелки — сразу применяется"}},
 {"air",     {"空战 · 驾驶歼-20","Air War · Fly J-20","空中戦 · J-20","Воздушный бой · J-20"}},
 {"coop",    {"联机作战","Multiplayer","協力プレイ","Сетевая игра"}},
 {"history", {"操作说明 / 历史","Controls / History","操作 / 歴史","Управление / История"}},
 {"settings",{"设置","Settings","設定","Настройки"}},
 {"motto",   {"铭记历史 · 珍爱和平 · 吾辈自强","Remember history · Cherish peace · Strive on","歴史を忘れず · 平和を尊び · 自強せよ","Помнить историю · беречь мир · крепнуть"}},
 {"latest",  {"当前安装版本","Installed version","現在のバージョン","Установлена версия"}},
 {"site",    {"最新版与下载见官网","Latest build & downloads:","最新版・入手先：","Свежая сборка:"}},
};
const char* tr(const char* key)
{
    for(unsigned i=0;i<sizeof(PH)/sizeof(PH[0]);i++)
        if(!strcmp(PH[i].k,key)) return PH[i].t[gSetLang];
    return key;
}

static const char* FPSN[3]={"60 帧","120 帧","480 / 不封顶"};
static const char* QN[4]={"流畅","经典","高清","真实"};
static const char* LN[4]={"中文","English","日本語","Русский"};

static bool sbtn(const char*s,int cx,int y,int w,int h)
{
    Rectangle r={(float)cx-w/2,(float)y,(float)w,(float)h};
    Vector2 m=GetMousePosition();
    float tx=0,ty=0; int tt=Touch_UITap(&tx,&ty);
    bool hov=CheckCollisionPointRec(m,r)||(tt&&CheckCollisionPointRec((Vector2){tx,ty},r));
    DrawRectangleRec(r,hov?(Color){178,58,44,235}:(Color){22,30,44,232});
    DrawRectangleLinesEx(r,2,(Color){255,210,140,255});
    CNC(s,cx,y+h/2-11,19,(Color){240,242,248,255});
    return (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)&&CheckCollisionPointRec(m,r))
        || (tt&&CheckCollisionPointRec((Vector2){tx,ty},r));
}
static bool arrow(int cx,int y,const char*sgn)
{
    Rectangle r={(float)cx-30,(float)y,60,46};
    Vector2 m=GetMousePosition();
    float tx=0,ty=0; int tt=Touch_UITap(&tx,&ty);
    bool hov=CheckCollisionPointRec(m,r)||(tt&&CheckCollisionPointRec((Vector2){tx,ty},r));
    DrawRectangleRec(r,hov?(Color){178,58,44,235}:(Color){30,40,58,235});
    DrawRectangleLinesEx(r,2,(Color){255,210,140,255});
    CNC(sgn,cx,y+13,24,(Color){255,225,170,255});
    return (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)&&CheckCollisionPointRec(m,r))
        || (tt&&CheckCollisionPointRec((Vector2){tx,ty},r));
}

void Settings_Screen(void)
{
    while(!WindowShouldClose())
    {
        float dt=GetFrameTime(); (void)dt;
        BeginDrawing();
        DrawRectangle(0,0,GetScreenWidth(),GetScreenHeight(),(Color){10,14,22,240});
        int cx=GetScreenWidth()/2;
        CNC(tr("title"),cx,70,38,(Color){255,226,150,255});
        DrawLine(cx-220,120,cx+220,120,(Color){120,110,80,255});

        int y=160;
        // row helper: label left, [-] value [+] center-right
        #define ROW(label,valstr,idx,max) do{ \
            CNC(label,cx-150,y+12,21,(Color){225,230,240,255}); \
            if(arrow(cx+40,y,"-")){ idx=(idx+(max)-1)%(max); Settings_Apply(); Settings_Save(); } \
            DrawRectangle(cx+80,y,220,46,(Color){18,24,36,255}); DrawRectangleLines(cx+80,y,220,46,(Color){255,210,140,200}); \
            CNC((valstr),cx+190,y+13,20,(Color){255,240,200,255}); \
            if(arrow(cx+330,y,"+")){ idx=(idx+1)%(max); Settings_Apply(); Settings_Save(); } \
            y+=72; }while(0)
        ROW(tr("fps"),FPSN[gSetFPS],gSetFPS,3);
        ROW(tr("quality"),QN[gSetQuality],gSetQuality,4);
        ROW(tr("lang"),LN[gSetLang],gSetLang,4);
        #undef ROW

        CNC(tr("hint"),cx,y+4,16,(Color){170,182,200,230}); y+=40;
        if(sbtn(tr("update"),cx,y,360,50))
        {
            // Direct-download distribution: open the official site so the player
            // can compare v%s and grab the newest EXE / APK straight away.
            OpenURL("https://kanmei.fucku.top");
        }
        y+=64;
        CNC(TextFormat("%s  v%s",tr("latest"),SKIES_VERSION),cx,y,20,(Color){150,225,175,255}); y+=28;
        CNC(TextFormat("%s  https://kanmei.fucku.top",tr("site")),cx,y,17,(Color){180,195,215,255});
        CNC(tr("motto"),cx,GetScreenHeight()-60,18,(Color){255,225,180,225});
        if(sbtn(tr("back"),cx,GetScreenHeight()-120,300,52)){ EndDrawing(); return; }
        EndDrawing();
    }
}
