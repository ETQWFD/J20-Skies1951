// main.c - 《长空·1951》 entry, menus, endings, history
#include "common.h"
#include "noise.h"
#include "rlgl.h"
#include "coop.h"
#include "settings.h"

extern Shader gLit; // scene.c lit shader, reapplied after terrain (re)build

// Portable strsep replacement (strsep is BSD/glibc and absent from the Windows
// CRT); same semantics so the '|'-delimited text wrapping works on every platform.
static char* NextSeg(char **stringp, const char *delim)
{
    char *s = (stringp != NULL) ? *stringp : NULL;
    if (s == NULL) return NULL;
    char *p = s;
    while ((*p != '\0') && (strchr(delim, *p) == NULL)) p++;
    char *tok = s;
    if (*p != '\0') { *p = '\0'; *stringp = p + 1; }
    else *stringp = NULL;
    return tok;
}

int gSelfTest=0, gUncap=1, gFlagTest=0;
char gShotDir[512]=".";
Font gFont;

Font GameFont(void){ return gFont; }
void LoadGameFont(void)
{
#if defined(FONT_EMBEDDED)
    extern const unsigned char _binary_gamefont_ttf_start[];
    extern const unsigned char _binary_gamefont_ttf_end[];
    #include "font_cps.h"
    gFont = LoadFontFromMemory(".ttf",(unsigned char*)_binary_gamefont_ttf_start,
             (int)(_binary_gamefont_ttf_end-_binary_gamefont_ttf_start),
             48,(int*)gFontCps,gFontCpsCount);
    GenTextureMipmaps(&gFont.texture);
    if (gFont.texture.id==0) gFont=GetFontDefault();
#else
    gFont=GetFontDefault();
#endif
    SetTextureFilter(gFont.texture, TEXTURE_FILTER_BILINEAR);
}

void CN(const char* t,int x,int y,int sz,Color c)
{ if(t) DrawTextEx(gFont,t,(Vector2){(float)x,(float)y},(float)sz,2.0f,c); }
int CNWidth(const char* t,int sz){ return (int)MeasureTextEx(gFont,t,(float)sz,2.0f).x; }
void CNC(const char* t,int cx,int y,int sz,Color c)
{ CN(t,cx-CNWidth(t,sz)/2,y,sz,c); }

enum { ST_MENU, ST_HELP, ST_AIR, ST_GROUND, ST_END, ST_SETTINGS };

int gScenario=0; // 0=general ridge assault, 1=Chosin Reservoir / Ice Company night battle

// ----------------------------------------------------------------- ending data
static const char* AIR_TITLE[16]={0};
static const char* GND_TITLE[16]={0};

static const char* airPara(int id)
{
    switch(id){
    case 101: return "你夺取了制空权，又摧毁了地面装甲纵队，干净利落地返航着陆。|年轻的鹰，第一次出击就把天空和大地一起守住了。";
    case 102: return "四波美机群全部被击落，北方的天空安静下来。|战友们抬头望着你——后来人们把这片天空称作'米格走廊'。";
    case 103: return "你摧毁了河谷里的全部敌军车辆与高炮，地面的冲锋号准时吹响。|钢铁没有挡住步兵，因为他们头顶有你。";
    case 105: return "你带着战果与战伤低空返航，机务看见了机身上的弹孔。|初战告捷，而真正的战争才刚刚开始。";
    case 106: return "你在任务完成前选择了返航。跑道尽头，新的弹药和命令正在等你。|胜利从不属于一次犹豫，但属于活着回来继续战斗的人。";
    case 110: return "战机失去控制的最后一刻，你仍朝着敌机压了过去。|火光里没有人跳伞。群山记得这个没有留下名字的飞行员。";
    default:  return "战机坠落在异国的群山之间。|天空还在战斗，战友们会接替你拉起来——这场战争没有因为一个人的坠落而结束。";
    }
}
static const char* gndPara(int id)
{
    switch(id){
    case 201: return "高地上最后一个火力点被拔掉，红旗插上了阵地。|你和剩下的战友站在寒风里，听见后方传来新一轮的冲锋号。";
    case 202: return "你突入高地、死死顶住了反扑，为后续部队撕开了口子。|阵地在我们手里——这句话，是用很多人的命换来的。";
    case 203: return "你在冲锋路上倒下时，身边已经躺着数倍于你的敌人。|身后的战友跨过你继续向前，号声没有停。";
    case 205: return "冲锋号在黎明前的雪原上吹响，你们踏过齐膝深的雪夺下了隘口阵地。|长津湖畔的寒夜里，有人永远保持着冲锋的姿态，化成了冰雪中的雕像。";
    case 206: return "你在零下三十多度的雪地里战斗到最后，手指已扣不动枪栓。|号声远去时，阵地上仍保持着伏击的队形——冰与火都没能让这支连队后退一步。";
    case 210: return "雪原上的最后一辆军车冒起黑烟，守敌被全歼，你们踩着没膝的雪追过了公路。|捷报传回时，后方的运输队正顶着轰炸把炒面和炮弹往前送——你们守住了他们脚下的路。";
    case 211: return "夜色是最好的伪装。你们摸到阵地前沿，一声号响撕开了缺口，天亮前结束了战斗。|夜战近战，是当年装备落后的志愿军最擅长的打法——拼的就是一口气。";
    case 212: return "残阳把阵地染成血色的时候，你们终于把残敌压了下去，红旗在黄昏的风里立住。|身后是被炮火翻耕过无数遍的山坡，前面是连夜南撤的敌军。";
    case 213: return "高地上的敌人被一个不剩地解决掉，你和战友在硝烟里互相搀扶着站起来。|这一仗打得干净——这样的干净，是用很多次不那么干净的冲锋换来的。";
    case 214: return "你主动留下来断后，把敌人的追兵死死钉在公路上，给主力和伤员争取了时间。|后来主力跳出了包围圈，而那个留下来的火力点，再没有说过一句话。";
    case 216: return "没等敌人的援军赶到，你们已经把阵地攥在了手里，剩下的敌人丢下山头连夜溃退。|守住，有时比全歼更难——补给断了、人也不多了，可阵地就是没丢。";
    case 217: return "你几乎就要把旗插上山顶，最后几米却倒在了火力网里。|后来冲上去的战友捡起你身边的旗——你没走完的那几步，他们替你走完了。";
    default:  return "冲锋被压在半山腰。你没能看到天亮时的高地。|可总有人要先冲上去——后来上去的人里，有人记得你。";
    }
}
static const char* HISTORY[]={
"【重返战场 · 为什么是1950—1953】",
"这是一款以抗美援朝为题材的纪念游戏。它不是纪录片，但每一关都指向一段真实的历史。",
"1950年6月，朝鲜战争爆发。随后以美国为主的'联合国军'介入并越过三八线，战火烧到鸭绿江边，新中国安全受到严重威胁。",
"1950年10月，中国人民志愿军跨过鸭绿江——抗美援朝，保家卫国。武器落后、补给艰难，战士们靠双脚和意志在冰与火中穿插。",
"【长津湖 · 冰雕连】1950年冬，志愿军第九兵团在零下三四十摄氏度的盖马高原设伏，冻伤减员巨大。",
"有的连队在雪地里保持着冲锋的队形，直到战斗结束仍紧握钢枪，化作阵地上的'冰雕'。这就是游戏中夜战一关的由来。",
"新兴里、下碣隅里、三炸水门桥……志愿军在长津湖重创美军王牌部队，收复了三八线以北广大地区。",
"【松骨峰 · 上甘岭】松骨峰阻击战、上甘岭坑道战，志愿军在绝对火力劣势下死守阵地，打出了'最可爱的人'。",
"【米格走廊】年轻的人民空军1950年底起在朝鲜北部上空奋勇作战，那片空域后来被对手称作'米格走廊'。",
"1953年7月27日，《朝鲜停战协定》签署。这一战打出了新中国的国威与军威，换来了长期和平建设的外部环境。",
"我们用游戏'重返'战场，不是为了歌颂战争，而是为了记住：步枪、棉衣、雪水和生命，是怎样把和平换来的。",
};
static const char* DEVNOTE=
"【关于这款游戏】歼-20'威龙'2011年才首飞，从未参加过那场战争。让它出现在1951年的天空，|是一句'如果当年有我们'的告慰：今天你随手能驾驶的隐身战机，是当年冰雕连、坑道里的战士们做梦也不敢想的东西。|做这款游戏，不是为了宣扬战争，而是希望操作它的人记得——是哪一代人用步枪、棉衣和命，把和平打了下来。|铭记历史，珍爱和平，吾辈自强。";

static void drawWrapped(const char** lines,int n,int x,int y,int sz,int gap,Color c)
{
    for(int i=0;i<n;i++)
    {
        // split on '|'
        char buf[512]; strncpy(buf,lines[i],sizeof(buf)-1); buf[sizeof(buf)-1]=0;
        char*p=buf; char*seg;
        while((seg=NextSeg(&p,"|"))!=NULL){ CN(seg,x,y,sz,c); y+=sz+gap; }
    }
}

void DrawEnding(int mode,int endingId,int fromAir,void* res)
{
    (void)res;
    bool sacrifice = (endingId==110||endingId==203||endingId==214||endingId==217);
    Color titleC = sacrifice?(Color){255,120,100,255}:((mode==2&&endingId>=111)?(Color){255,160,140,255}:(Color){255,220,120,255});
    int y=70;
    const char* title;
    char tbuf[64]={0};
    if(fromAir)
    {
        switch(endingId){
        case 101:title="长空铸剑 · 全胜";break;
        case 102:title="制空权 · 米格走廊";break;
        case 103:title="铁拳遮断 · 地面肃清";break;
        case 105:title="带伤返航 · 初战告捷";break;
        case 106:title="鸣金收兵";break;
        case 110:title="血染长空 · 壮烈";break;
        default:title="折戟长空";break;
        }
    }
    else
    {
        switch(endingId){
        case 201:title="攻克高地 · 胜利";break;
        case 202:title="阵地在手 · 胜利";break;
        case 203:title="英勇牺牲 · 浩气长存";break;
        case 205:title="长津湖 · 冰血隘口";break;
        case 206:title="冰雕连 · 军魂永驻";break;
        case 210:title="雪原追击 · 全线告捷";break;
        case 211:title="夜袭破阵 · 拂晓收兵";break;
        case 212:title="浴血黄昏 · 红旗不倒";break;
        case 213:title="全歼守敌 · 攻克山头";break;
        case 214:title="孤胆断后 · 掩护主力";break;
        case 216:title="残敌溃退 · 阵地在我";break;
        case 217:title="差一步的旗 · 后继有人";break;
        default:title="倒在冲锋路上";break;
        }
    }
    (void)tbuf;(void)AIR_TITLE;(void)GND_TITLE;
    DrawRectangle(0,0,GetScreenWidth(),GetScreenHeight(),(Color){10,12,18,245});
    CNC(title,GetScreenWidth()/2,y,40,titleC); y+=64;
    DrawLine(GetScreenWidth()/2-220,y-16,GetScreenWidth()/2+220,y-16,(Color){120,110,80,255});

    const char* para = fromAir?airPara(endingId):gndPara(endingId);
    char pbuf[640]; strncpy(pbuf,para,sizeof(pbuf)-1); pbuf[sizeof(pbuf)-1]=0;
    char* one=pbuf; char* seg; int x=120;
    while((seg=NextSeg(&one,"|"))){CN(seg,x,y,19,(Color){225,228,235,255});y+=28;}
    y+=8;

    // stats
    if(fromAir)
    {
        CN(TextFormat("击落美机 %d 架    摧毁地面目标 %d 个    幸存波次 4/4    剩余导弹 %d    作战时长 %ds    得分 %d",
            gAirResult.jetsKilled,gAirResult.groundKilled,gAirResult.missilesLeft,(int)gAirResult.timeAlive,(int)gAirResult.score),
            x,y,17,(Color){170,200,230,255});
    }
    else
    {
        CN(TextFormat("歼敌 %d 人    幸存战友 %d/%d    作战时长 %ds    剩余生命 %d%%",
            gGroundResult.foesKilled,gGroundResult.friendliesAlive,8,(int)gGroundResult.timeAlive,(int)gGroundResult.hp),
            x,y,17,(Color){170,200,230,255});
    }
    y+=34; DrawLine(x,y,GetScreenWidth()-x,y,(Color){70,74,84,255}); y+=18;
    drawWrapped(HISTORY,sizeof(HISTORY)/sizeof(HISTORY[0]),x,y,16,6,(Color){205,200,180,255});
    y+= sizeof(HISTORY)/sizeof(HISTORY[0])*0; // recompute below
    // history height approx: 6 entries * (22 + wraps) — draw devnote near fixed lower area
    int dy=GetScreenHeight()-128;
    char db[700]; strncpy(db,DEVNOTE,sizeof(db)-1); db[sizeof(db)-1]=0;
    char* dp=db;
    while((seg=NextSeg(&dp,"|"))){CN(seg,x,dy,15,(Color){180,170,140,255});dy+=21;}
    CNC("按 空格 / 回车 / 鼠标点击 返回主菜单",GetScreenWidth()/2,GetScreenHeight()-34,16,(Color){200,200,210,220});
}

// ----------------------------------------------------------------- help
static void drawHelp(void)
{
    DrawRectangle(0,0,GetScreenWidth(),GetScreenHeight(),(Color){12,16,22,240});
    int x=110,y=80;
    CNC("操作与说明",GetScreenWidth()/2,y,36,(Color){255,224,140,255}); y+=56;
    const char* L[]={
    "【空战 · 歼-20】",
    "W/S 加减速(失速会掉高度)   ↑/↓ 俯仰   A/D 滚转   Q/E 偏航",
    "空格 机炮    F 锁定后发射导弹    B 投掷航弹",
    "在机场(出生点)附近 低空慢速 按 H 返航着陆，根据战果进入不同结局；被击落按战绩判定。",
    "消灭4波F-86并摧毁北方河谷的15个地面目标可得全胜；雷达红点=敌机，黄点=地面目标。",
    "",
    "【陆战 · 志愿军步兵】",
    "WASD 移动   Shift 冲刺   空格 单跳/跃进(步兵只能跳一次)   鼠标 瞄准   左键 攻击",
    "1 莫辛-纳甘步枪(共5发,拉栓,爆头一枪致命,腿/躯干约半血)   2 AKM突击步枪(共100发)",
    "3 大刀(近战劈砍)   0 拳头   R 装填(余弹打光即空仓,无法再补)   右键 机瞄狙击镜",
    "每人1颗手雷：长按 M 蓄力、松手按抛物线投出(手机点 雷)；靠近伤员按 E/话 听他的遗言。",
    "敌头顶红条、战友头顶蓝条；跟随战友冲锋夺下高地。胜利时班长(牺牲则随机一名战友)冲上去插红旗、吹冲锋号。",
    "",
    "【其它】",
    "每次启动都会实时编译GLSL光照着色器、用柏林噪声重新生成地形；帧率不封顶，实际帧率取决于硬件。",
    "本作为单机程序化原型，无外部资源依赖；局域网/跨网联机、可驾驶载具、写实PBR材质为后续版本，详见随附README。",
    };
    for(unsigned i=0;i<sizeof(L)/sizeof(L[0]);i++)
    { Color c = ((unsigned char)L[i][0]==0xE3)?(Color){255,200,120,255}:(Color){220,224,232,255}; CN(L[i],x,y,17,c); y+=25; }
    CNC("按 空格 / ESC 返回",GetScreenWidth()/2,GetScreenHeight()-40,17,(Color){200,200,210,230});
}

// ----------------------------------------------------------------- menu
static Camera3D menuCam; static float menuT=0;
static void drawMenuBg(void)
{
    menuT+=0.016f;
    float r=120; Vector3 c={cosf(menuT*0.3f)*r,40,sinf(menuT*0.3f)*r};
    menuCam.position=c; menuCam.target=(Vector3){0,18,0}; menuCam.up=(Vector3){0,1,0};
    Scene_SetCamera(menuCam);
    BeginMode3D(menuCam);
    Terrain_Draw(menuCam); Sea_Draw(menuCam); Env_Draw(menuCam);
    Quaternion q=QuaternionMultiply(QuaternionFromAxisAngle((Vector3){0,1,0},menuT*0.6f),
                                    QuaternionFromAxisAngle((Vector3){1,0,0},0.12f));
    DrawJ20((Vector3){0,34,0},q,2.2f,1);
    FX_Draw3D(menuCam);
    EndMode3D();
}

typedef struct { Rectangle r; const char* name; int key, to, scn; } Btn;

static const char* CAMP_NAME[27]={
    "温井伏击战","云山攻坚战","长津湖·冰雕连(雪夜)","松骨峰阻击战(黄昏雪)",
    "上甘岭坑道战(焦土)","金城反击战","汉江夜渡","三八线阵地战(硝烟)",
    "铁原阻击战","横城反击战(黄昏)","平壤外围战","黄草岭阻击战(雪山口)",
    "飞虎山阻击战","德川宁远反击战","清川江围歼战","三所里·龙源里穿插战",
    "突破临津江(雪夜)","釜谷里阻击战","雪马里围歼战","马良山攻防战","黑云吐岭反击战",
    "文登公路狙击战","兴南港突围战(雪)","阳德高原穿插战(黄昏)","元山登陆支援战(夜)",
    "咸镜南道追击战(雪)","汉城外围防御战(黄昏)" };
static const int CAMP_KEY[12]={
    KEY_ONE,KEY_TWO,KEY_THREE,KEY_FOUR,KEY_FIVE,KEY_SIX,
    KEY_SEVEN,KEY_EIGHT,KEY_NINE,KEY_ZERO,KEY_MINUS,KEY_EQUAL };

static int menuLoop(int *go)
{
    static int menuInit=0;
    if(!menuInit){ menuInit=1; if(!gSelfTest && gScenario!=0){gScenario=0; Terrain_Init(); Terrain_ApplyShader(gLit);} }
    int frame=0, sel=-1;
    float guard=0.30f;   // swallow the tap/click that carried over from a sub-screen
    while(!WindowShouldClose())
    {
        float dt=GetFrameTime(); frame++; FX_Update(dt); Env_Update(dt);
        if(guard>0)guard-=dt;
        Vector2 m=GetMousePosition();
        float txp=0,typ=0; int tapped=(guard<=0 && Touch_UITap(&txp,&typ));
        int mousePressed=(guard<=0 && IsMouseButtonPressed(MOUSE_BUTTON_LEFT));
        // unified hit helper: works with mouse and with fresh finger taps
        #define M_HIT(rp) (CheckCollisionPointRec(m,(rp)) || \
            (tapped && CheckCollisionPointRec((Vector2){txp,typ},(rp))))
        #define M_PRESS(rp) (mousePressed && CheckCollisionPointRec(m,(rp)) || \
            (tapped && CheckCollisionPointRec((Vector2){txp,typ},(rp))))
        BeginDrawing();
        ClearBackground((Color){150,180,210,255});
        drawMenuBg();
        DrawRectangle(0,0,GetScreenWidth(),GetScreenHeight(),(Color){6,10,18,120});
        int SW=GetScreenWidth();
        CNC("长 空 · 1951",SW/2,40,46,(Color){255,232,150,255});
        CNC("J-20 SKIES OVER KOREA · 抗美援朝二十七大战役 · 重返战场",SW/2,92,18,(Color){225,230,240,235});

        // ---- top row: air war / LAN coop / settings / history & controls ----
        const char* topN[4]={"① 空战 · 驾驶歼-20","⑨ 联机作战","⑪ 设置","⑩ 操作说明 / 历史"};
        int topTo[4]={ST_AIR,-2,ST_SETTINGS,ST_HELP};
        float topW=268, topGap=18, topY=112, topH=42;
        float topX0=SW/2.0f-(4*topW+3*topGap)/2.0f;
        for(int i=0;i<4;i++)
        {
            Rectangle r={topX0+i*(topW+topGap),topY,topW,topH};
            bool hov=M_HIT(r);
            DrawRectangleRec(r,hov?(Color){180,60,45,225}:(Color){20,28,40,205});
            DrawRectangleLinesEx(r,2,(Color){255,210,140,255});
            CNC(topN[i],(int)(r.x+topW/2),(int)r.y+11,19,(Color){240,240,245,255});
            if(M_PRESS(r))
            {
                if(topTo[i]==-2){ guard=0.3f; int lr=Coop_Lobby();
                    if(lr==1||lr==2){ sel=ST_GROUND; gScenario=gCoopScenario; } }
                else sel=topTo[i];
            }
        }

        // ---- 21 army campaigns: 7 columns x 3 rows ----
        int NC=Map_Count();
        float gap=8, cellH=54, gridY=172;
        int cols=7, rows=(NC+cols-1)/cols;
        float cellW=(SW-36-(cols-1)*gap)/(float)cols;
        float gridW=cols*cellW+(cols-1)*gap, gx0=SW/2.0f-gridW/2.0f;
        Btn cb;
        for(int i=0;i<NC;i++)
        {
            int col=i%cols, row=i/cols;
            Rectangle r={gx0+col*(cellW+gap), gridY+row*(cellH+10), cellW, cellH};
            (void)cb;
            bool hov=M_HIT(r);
            DrawRectangleRec(r,hov?(Color){180,60,45,225}:(Color){20,28,40,205});
            DrawRectangleLinesEx(r,2,(Color){255,210,140,255});
            char num[8]; snprintf(num,sizeof num,"%d",i+1);
            CNC(num,(int)(r.x+cellW/2),(int)r.y+7,15,(Color){255,200,120,255});
            // wrap a two-part name around the middle dot if it is long
            const char*nm=CAMP_NAME[i];
            Vector2 sz=MeasureTextEx(GameFont(),nm,15,0);
            if(sz.x<=cellW-8) CNC(nm,(int)(r.x+cellW/2),(int)r.y+30,15,(Color){235,238,245,255});
            else
            {
                const char*dot=strchr(nm,0xB7); // UTF-8 of '·'
                char a[40]={0},b[40]={0};
                if(dot){ int la=(int)(dot-nm); strncpy(a,nm,la); strncpy(b,dot+3,39); }
                else { strncpy(a,nm,39); }
                if(a[0]) CNC(a,(int)(r.x+cellW/2),(int)r.y+26,15,(Color){235,238,245,255});
                if(b[0]) CNC(b,(int)(r.x+cellW/2),(int)r.y+42,15,(Color){235,238,245,255});
            }
            if(M_PRESS(r)){ sel=ST_GROUND; gScenario=i; }
            if(guard<=0 && i<12 && IsKeyPressed(CAMP_KEY[i])){ sel=ST_GROUND; gScenario=i; }
        }
        #undef M_HIT
        #undef M_PRESS
        CNC("铭记历史 · 珍爱和平 · 吾辈自强",SW/2,GetScreenHeight()-46,18,(Color){255,225,180,220});
        CNC("点击 / 触屏点选或按 1~0、-、= 选择 · ESC 退出",SW/2,GetScreenHeight()-22,15,(Color){210,215,225,210});
        EndDrawing();
        if(sel>=0){ if(sel==ST_AIR||sel==ST_GROUND){ Terrain_Init(); Terrain_ApplyShader(gLit); } *go=sel;return sel; }
        if(gSelfTest){ TakeScreenshot(TextFormat("%s/shot_menu.png",gShotDir)); *go=ST_AIR; return ST_AIR; }
    }
    *go=-1; return -1;
}

int main(int argc,char**argv)
{
    int devLobby=0, devCoop=0, devBattle=0;
    for(int i=1;i<argc;i++)
    {
        if(strcmp(argv[i],"--selftest")==0) gSelfTest=1;
        else if(strcmp(argv[i],"--lobby")==0){ gSelfTest=1; devLobby=1; } // dev: screenshot LAN lobby
        else if(strcmp(argv[i],"--coophost")==0){ devCoop=1; }
        else if(strcmp(argv[i],"--coopclient")==0){ devCoop=2; }
        else if(strcmp(argv[i],"--bhost")==0){ gSelfTest=1; devBattle=1; }
        else if(strcmp(argv[i],"--bclient")==0){ gSelfTest=1; devBattle=2; }
        else if(strcmp(argv[i],"--night")==0) gScenario=2; // dev: preview Chosin night theme
        else if(strcmp(argv[i],"--map")==0 && i+1<argc){ gScenario=atoi(argv[++i]); gSelfTest=1; } // dev: preview map N
        else if(strcmp(argv[i],"--flagtest")==0){ gSelfTest=1; gFlagTest=1; }
        else if(strcmp(argv[i],"--shotdir")==0 && i+1<argc){ strncpy(gShotDir,argv[++i],sizeof(gShotDir)-1); }
        else if(strcmp(argv[i],"--vsync")==0) gUncap=0;
    }
    srand(19511025);
    SetConfigFlags(FLAG_MSAA_4X_HINT|FLAG_WINDOW_RESIZABLE);
    InitWindow(1280,720,APP_TITLE);
    Settings_Load();
    Settings_Apply();   // user frame-rate cap + grass density, persisted
    rlSetClipPlanes(0.1f,9000.0f);
    SetExitKey(0); // we manage ESC ourselves

    Noise_Seed(19510125);
    LoadGameFont();

    // loading frame (shader compile + terrain happens right after)
    BeginDrawing(); ClearBackground((Color){10,12,18,255});
    CNC("正在编译着色器 · 用柏林噪声生成朝鲜地形 …",640,340,22,(Color){220,225,235,255});
    EndDrawing();

    Terrain_Init();
    Scene_Load();
    Env_Load();
    Sfx_Load();
    menuCam=(Camera3D){0}; menuCam.fovy=60; menuCam.projection=CAMERA_PERSPECTIVE; menuCam.up=(Vector3){0,1,0};

    if(devLobby){ Coop_Lobby(); Scene_Unload(); CloseWindow(); return 0; }

    if(devCoop)   // headless-ish end-to-end lobby protocol test (two real processes)
    {
        int frames=(int)(8.0f*60);
        if(devCoop==1)
        {
            gCoopRole=1; gCoopId=0; gCoopScenario=0; Net_Host();
            for(int i=0;i<frames&&!WindowShouldClose();i++)
            { Coop_HostPoll(1.0f/60.0f);
              BeginDrawing(); ClearBackground((Color){10,14,22,255});
              CNC(TextFormat("HOST  peers=%d",Coop_PeerCount()),640,340,28,(Color){120,230,150,255});
              EndDrawing(); }
            TakeScreenshot(TextFormat("%s/shot_coophost.png",gShotDir));
            printf("COOPHOST_DONE peers=%d\n",Coop_PeerCount());
        }
        else
        {
            gCoopRole=2; Coop_BeginSearch(); int joined=0, status=-1;
            for(int i=0;i<frames&&!WindowShouldClose();i++)
            {
                CoopRoom rooms[4]; int nr=Coop_PollRooms(rooms,4,1.0f/60.0f);
                if(!joined && nr>0){ Coop_Join(&rooms[0].addr); joined=1; }
                if(joined) status=Coop_JoinStatus();
                BeginDrawing(); ClearBackground((Color){10,14,22,255});
                CNC(TextFormat("CLIENT rooms=%d joined=%d status=%d id=%d",nr,joined,status,gCoopId),
                    640,340,26,(Color){120,200,255,255});
                EndDrawing();
                if(status==1 && i>frames-60) break;
            }
            TakeScreenshot(TextFormat("%s/shot_coopclient.png",gShotDir));
            printf("COOPCLIENT_DONE id=%d status=%d\n",gCoopId,status);
        }
        Net_Close(); Scene_Unload(); CloseWindow(); return gCoopId>=1||devCoop==1?0:2;
    }

    if(devBattle)   // two-process host-authoritative ground battle test
    {
        if(devBattle==1)
        {
            gCoopRole=1; gCoopId=0; gScenario=0;
            if(!Net_Host()){ printf("NETBATTLE host bind fail\n"); }
        }
        else
        {
            gCoopRole=2; Coop_BeginSearch();
            int joined=0; float tmr=0;
            while(tmr<3.5f && !WindowShouldClose())
            {
                float dt=GetFrameTime(); tmr+=dt;
                BeginDrawing(); ClearBackground(BLACK); EndDrawing();
                CoopRoom rooms[4]; int nr=Coop_PollRooms(rooms,4,dt);
                if(!joined && nr>0){ Coop_Join(&rooms[0].addr); joined=1; }
                if(joined && Coop_JoinStatus()==1){ gScenario=gCoopScenario; break; }
            }
            printf("NETBATTLE client joined=%d id=%d sc=%d\n",joined,gCoopId,gCoopScenario);
        }
        int om=0,en=0;
        Ground_Run(&om,&en);
        Net_Close(); gCoopRole=0;
        Scene_Unload(); CloseWindow(); return 0;
    }

    int state=ST_MENU, endMode=1, endId=0, pending=-1, endFrames=0;
    while(!WindowShouldClose())
    {
        if(state==ST_MENU)
        {
            int go=ST_MENU; menuLoop(&go);
            if(go<0) break;
            state=go;
        }
        else if(state==ST_HELP)
        {
            int f=0;
            while(!WindowShouldClose()){
                BeginDrawing(); drawHelp(); EndDrawing(); f++;
                if(IsKeyPressed(KEY_SPACE)||IsKeyPressed(KEY_ESCAPE)||IsKeyPressed(KEY_ENTER)||IsMouseButtonPressed(0))break;
                if(gSelfTest && f==20){ TakeScreenshot(TextFormat("%s/shot_help.png",gShotDir)); break; }
            }
            state=ST_MENU;
            if(gSelfTest) break;
        }
        else if(state==ST_SETTINGS)
        {
            Settings_Screen();
            state=ST_MENU;
        }
        else if(state==ST_AIR)
        {
            int m,e; Air_Run(&m,&e);
            if(m==0){state=ST_MENU;continue;}
            endMode=1;endId=e;state=ST_END;pending=gSelfTest?ST_GROUND:-1;endFrames=0;
        }
        else if(state==ST_GROUND)
        {
            int m,e; Ground_Run(&m,&e);
            if(gCoopRole!=0){ Net_Close(); gCoopRole=0; gCoopId=0; }
            if(m==0){state=ST_MENU;continue;}
            endMode=2;endId=e;state=ST_END;pending=gSelfTest?ST_HELP:-1;endFrames=0;
        }
        else if(state==ST_END)
        {
            endFrames++;
            BeginDrawing();
            DrawEnding(endMode,endId,endMode==1,endMode==1?(void*)&gAirResult:(void*)&gGroundResult);
            EndDrawing();
            if(gSelfTest && endFrames==18)
                TakeScreenshot(TextFormat("%s/shot_%s.png",gShotDir,endMode==1?"endair":"endground"));
            if(gSelfTest && endFrames>42){ state=(pending>=0)?pending:ST_MENU; }
            else if(!gSelfTest && (IsKeyPressed(KEY_SPACE)||IsKeyPressed(KEY_ENTER)||IsMouseButtonPressed(0)))
                state=ST_MENU;
        }
    }

    Sfx_Unload();
    Scene_Unload();
    CloseWindow();
    return 0;
}
