// coop.c - LAN cooperative lobby: host/create, UDP auto-discovery, join, ping.
// Text protocol over the net.c UDP transport (see coop.h). Also supports the
// public Kanye UDP relay (frp tunnel) for play across different networks.
#include "coop.h"
#include "common.h"
#include <stdio.h>

int     gCoopRole=0;
int     gCoopId=0;
int     gCoopScenario=0;
NetAddr gCoopHost;

static const char* COOP_MAP[27]={
 "温井伏击战","云山攻坚战","长津湖·冰雕连","松骨峰阻击战","上甘岭坑道战","金城反击战","汉江夜渡",
 "三八线阵地战","铁原阻击战","横城反击战","平壤外围战","黄草岭阻击战","飞虎山阻击战","德川宁远反击战",
 "清川江围歼战","三所里·龙源里穿插","突破临津江","釜谷里阻击战","雪马里围歼战","马良山攻防战","黑云吐岭反击战",
 "文登公路狙击战","兴南港突围战","阳德高原穿插战","元山登陆支援战","咸镜南道追击战","汉城外围防御战"};
static const char* coopMapName(int sc){ if(sc<0||sc>26)sc=0; return COOP_MAP[sc]; }

typedef struct { NetAddr addr; int id; float lastSeen; } Peer;
static Peer sPeers[NET_MAXPEERS]; static int sPeerN=0;
static int sStart=0, sStartSc=0;   // client received host "battle start"

static void peersRefresh(float dt)
{
    for(int i=0;i<sPeerN;i++) sPeers[i].lastSeen+=dt;
    for(int i=sPeerN-1;i>=0;i--) if(sPeers[i].lastSeen>5.0f)
    { for(int j=i;j<sPeerN-1;j++)sPeers[j]=sPeers[j+1]; sPeerN--; }
}
static Peer* peerFind(const NetAddr*a)
{ for(int i=0;i<sPeerN;i++) if(Net_AddrEq(&sPeers[i].addr,a)) return &sPeers[i]; return 0; }
static void peerTouch(const NetAddr*a,int id)
{
    Peer*p=peerFind(a);
    if(!p && sPeerN<NET_MAXPEERS){ p=&sPeers[sPeerN++]; p->addr=*a; if(id<1) id=sPeerN; p->id=id; }
    if(p){ if(id>=1)p->id=id; p->lastSeen=0; }
}

void Coop_HostPoll(float dt)
{
    peersRefresh(dt);
    NetAddr f; char b[200]; int n;
    while((n=Net_Poll(&f,b,sizeof b-1))>0)
    {
        b[n]=0;
        if(strncmp(b,"SKD",3)==0)
        {
            char r[120]; int k=snprintf(r,sizeof r,"SKH|%d|%d|%d",gCoopScenario,sPeerN,NET_PORT);
            Net_Send(&f,r,k);
        }
        else if(strncmp(b,"SKJ",3)==0)
        {
            int id=0;
            Peer*p=peerFind(&f);
            if(p)id=p->id;
            if(id<1){ id=1; int used[NET_MAXPEERS+1]={0};
                for(int i=0;i<sPeerN;i++) if(sPeers[i].id>=1&&sPeers[i].id<=NET_MAXPEERS) used[sPeers[i].id]=1;
                while(id<=NET_MAXPEERS&&used[id])id++; }
            peerTouch(&f,id);
            char r[80]; int k=snprintf(r,sizeof r,"SKA|%d|%d",id,gCoopScenario);
            Net_Send(&f,r,k);
        }
        else if(strncmp(b,"SKP",3)==0)
        {
            int id=0; sscanf(b,"SKP|%d",&id); peerTouch(&f,id);
        }
    }
}

int Coop_PeerCount(void){ return sPeerN; }

const NetAddr* Coop_HostAddr(void){ return &gCoopHost; }
int Coop_PeerAddr(int id, NetAddr*out)
{ for(int i=0;i<sPeerN;i++) if(sPeers[i].id==id){ *out=sPeers[i].addr; return 1; } return 0; }
void Coop_HostStartBattle(void)
{
    char b[24]; int k=snprintf(b,sizeof b,"SKG|%d",gCoopScenario);
    for(int i=0;i<sPeerN;i++) Net_Send(&sPeers[i].addr,b,k);
}
void Coop_Reset(void)
{ Net_Close(); sPeerN=0; sStart=0; gCoopRole=0; gCoopId=0; }

// ---------------- client discovery ----------------
static CoopRoom sRooms[NET_MAXPEERS]; static int sRoomN=0;
static float sDiscT=1.0f, sJoinT=0; static int sJoinId=-1; static NetAddr sJoinAddr;

static void roomTouch(const NetAddr*a,const char*name,int sc,int players)
{
    (void)name;
    for(int i=0;i<sRoomN;i++) if(Net_AddrEq(&sRooms[i].addr,a))
    { sRooms[i].lastSeen=0; sRooms[i].players=players; sRooms[i].scenario=sc; return; }
    if(sRoomN<NET_MAXPEERS)
    { CoopRoom*r=&sRooms[sRoomN++]; r->addr=*a; r->scenario=sc; r->players=players;
      r->pingMs=0; r->lastSeen=0; snprintf(r->name,sizeof r->name,"主机房间 %d",sc+1); }
}
int Coop_BeginSearch(void)
{ sRoomN=0; sDiscT=1.0f; sJoinT=0; sJoinId=-1; sStart=0; return Net_Client(); }
int Coop_ClientStart(int*sc){ if(sStart){ if(sc)*sc=sStartSc; return 1; } return 0; }

int Coop_PollRooms(CoopRoom*out,int cap,float dt)
{
    sDiscT-=dt;
    if(sDiscT<=0){ sDiscT=0.8f; Net_Broadcast("SKD",3); }
    NetAddr f; char b[200]; int n;
    while((n=Net_Poll(&f,b,sizeof b-1))>0)
    {
        b[n]=0; int sc=0,pl=0;
        if(strncmp(b,"SKG",3)==0){ int g=0; if(sscanf(b,"SKG|%d",&g)==1){ sStart=1; sStartSc=(g>=0&&g<=26)?g:0; } }
        else if(sscanf(b,"SKH|%d|%d",&sc,&pl)>=1)
        { roomTouch(&f,"host",(sc>=0&&sc<=26)?sc:0,pl);
          if(sJoinId>=0 && Net_AddrEq(&f,&sJoinAddr)) { /* ack handled below */ } }
        int id=-1,sc2=0;
        if(sscanf(b,"SKA|%d|%d",&id,&sc2)==2 && sJoinT>0 && Net_AddrEq(&f,&sJoinAddr))
        { sJoinId=id; sJoinT=0; gCoopId=id; gCoopScenario=(sc2>=0&&sc2<=26)?sc2:0; }
    }
    for(int i=0;i<sRoomN;i++)sRooms[i].lastSeen+=dt;
    for(int i=sRoomN-1;i>=0;i--) if(sRooms[i].lastSeen>4.0f)
    { for(int j=i;j<sRoomN-1;j++)sRooms[j]=sRooms[j+1]; sRoomN--; }
    if(sJoinId>=-100 && sJoinT>0)   // waiting for ack
    {
        sJoinT-=dt;
        if(sJoinT<=0 && sJoinId<0) sJoinId=-1;  // timed out (id == -1)
    }
    int k=sRoomN<cap?sRoomN:cap;
    for(int i=0;i<k;i++) out[i]=sRooms[i];
    return k;
}
int Coop_Join(const NetAddr*host)
{
    sJoinAddr=*host; gCoopHost=*host; sJoinT=1.5f; sJoinId=-100;
    Net_Send(host,"SKJ",3);
    return 1;
}
// returns 1 if join accepted (id assigned), -1 waiting, 0 timeout/failed
int Coop_JoinStatus(void)
{
    if(sJoinId>=1)return 1;
    if(sJoinId==-1)return 0;
    return -1;
}
static void keepAlive(float dt)
{
    static float t=0; t-=dt;
    if(gCoopId>=1 && t<=0){ t=0.6f; char b[32]; int k=snprintf(b,sizeof b,"SKP|%d",gCoopId);
        if(gCoopHost.port) Net_Send(&gCoopHost,b,k); else Net_Broadcast(b,k); }
}

// ================= UI =================
static void panelTitle(const char*s,int y)
{ DrawRectangle(0,0,GetScreenWidth(),GetScreenHeight(),(Color){10,14,22,235});
  CNC(s,GetScreenWidth()/2,y,34,(Color){255,226,150,255}); }

// ONE fresh touch per frame is snapshotted here and shared by every button, so
// several on-screen buttons in the same frame no longer steal the tap (old bug:
// only the first button could ever be tapped on a phone -> "need double taps").
static int   gTap=0; static float gTapX=0,gTapY=0;
static void  tapBegin(void)
{
    static int pn=0; int n=GetTouchPointCount();
    gTap=(n>pn);
    if(gTap){ Vector2 t=GetTouchPosition(pn<n?pn:0); gTapX=t.x; gTapY=t.y; }
    pn=n;
}
static bool rectTap(Rectangle r)
{
    bool mouse = IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(GetMousePosition(),r);
    bool touch = gTap && CheckCollisionPointRec((Vector2){gTapX,gTapY},r);
    return mouse||touch;
}

static bool btn(const char*s,int cx,int y,int w,int h)
{
    Rectangle r={cx-w/2,y,w,h};
    bool hov=CheckCollisionPointRec(GetMousePosition(),r) ||
             (gTap && CheckCollisionPointRec((Vector2){gTapX,gTapY},r));
    DrawRectangleRec(r,hov?(Color){178,58,44,235}:(Color){22,30,44,230});
    DrawRectangleLinesEx(r,2,(Color){255,210,140,255});
    CNC(s,cx,y+h/2-12,20,(Color){240,242,248,255});
    return rectTap(r);
}

// ============ remote relay servers (frp / Kanye tunnel) ====================
#define SRV_MAX 8
typedef struct {
    char name[24], atxt[64];
    NetAddr a; int valid, active, online;
    float pingMs, sentT, lastSeen;
} Srv;
static Srv gSrv[SRV_MAX]; static int gSrvN=0; static int gSrvLoaded=0;
#define SRV_FILE "skies_servers.txt"

static void srvSave(void)
{
    char buf[1024]; int used=0; buf[0]=0;
    for(int i=0;i<gSrvN;i++){
        int n=snprintf(buf+used,sizeof(buf)-used,"%s|%s\n",gSrv[i].name,gSrv[i].atxt);
        if(n>0)used+=n;
    }
    SaveFileText(SRV_FILE,buf);
}
static void srvLoad(void)
{
    if(gSrvLoaded)return; gSrvLoaded=1;
    char* s=LoadFileText(SRV_FILE); if(!s)return;
    char *p=s;
    while(*p && gSrvN<SRV_MAX)
    {
        char line[100]; int li=0;
        while(*p && *p!='\n' && li<99) line[li++]=*p++;
        line[li]=0; if(*p=='\n')p++;
        char*bar=strchr(line,'|'); if(!bar)continue;
        *bar=0; char*nm=line,*ad=bar+1;
        Srv*v=&gSrv[gSrvN]; memset(v,0,sizeof(*v));
        strncpy(v->name,nm,sizeof(v->name)-1); strncpy(v->atxt,ad,sizeof(v->atxt)-1);
        v->valid=Net_ParseRelayAddr(v->atxt,&v->a);
        if(gSrvN==0)v->active=1;
        gSrvN++;
    }
    UnloadFileText(s);
}
static Srv* srvActive(void){ for(int i=0;i<gSrvN;i++) if(gSrv[i].active) return &gSrv[i]; return 0; }
static void srvApplyActive(void)
{ Srv*s=srvActive(); if(s&&s->valid) Net_SetRelayAddr(&s->a); else Net_ClearRelay(); }

// ping the ACTIVE relay: SKR heartbeat -> server answers SKO, measure RTT
static void srvPing(float dt)
{
    Srv*s=srvActive(); if(!s||!s->valid)return;
    if(!Net_IsOpen()) Net_Client();
    srvApplyActive();
    Net_RelayTick(dt);
    s->sentT-=dt;
    if(s->sentT<=0){ s->sentT=1.0f; Net_Broadcast("SKR",3); s->lastSeen=-GetTime(); }
    NetAddr f; char b[64]; int n;
    while((n=Net_Poll(&f,b,sizeof(b)-1))>0)
    {
        b[n]=0;
        if(n==3 && strncmp(b,"SKO",3)==0 && s->lastSeen<0)
        { s->online=1; s->pingMs=(-s->lastSeen)*1000.0f; s->lastSeen=GetTime(); }
    }
    if(s->lastSeen>0 && GetTime()-s->lastSeen>3.0f){ s->online=0; s->pingMs=0; }
}

// compact on-screen / clickable keypad result for the address field
static char gKeyBuf[64]; static int gKeyFocus=0;   // 0 name, 1 address
static const char* KEYS[]={
  "1","2","3","4","5","6","7","8","9","0",".",":","-","退格","清","完"};
static void keyPress(const char*k,char*nm,char*ad)
{
    char*cur=(gKeyFocus==0)?nm:ad;
    int cap=(gKeyFocus==0)?24:64; int L=(int)strlen(cur);
    if(!strcmp(k,"退格")){ if(L>0)cur[L-1]=0; }
    else if(!strcmp(k,"清")){ cur[0]=0; }
    else if(!strcmp(k,"完")){ gKeyBuf[0]=0; }
    else if(L+(int)strlen(k)<cap-1){ strcat(cur,k); }
}

// returns 1 when "完成/返回" pressed (close the form)
static int serverFormScreen(void)
{
    static char nm[24], ad[64]; static int opened=0;
    if(!opened){ memset(nm,0,sizeof nm); memset(ad,0,sizeof ad);
                 snprintf(nm,sizeof nm,"服务器%d",gSrvN+1);
                 snprintf(ad,sizeof ad,"%s",""); gKeyFocus=1; opened=1; }
    panelTitle("添加远程服务器（frp / Kanye 穿透）",96);
    int cx=GetScreenWidth()/2;
    CNC("地址填隧道公网 IP 或域名加端口，例如  123.234.1.2:24463",cx,150,17,(Color){200,208,222,235});
    Rectangle rn={(float)cx-220,176,440,46}, ra={(float)cx-220,232,440,46};
    DrawRectangleRec(rn,gKeyFocus==0?(Color){46,60,92,235}:(Color){22,30,44,230});
    DrawRectangleRec(ra,gKeyFocus==1?(Color){46,60,92,235}:(Color){22,30,44,230});
    DrawRectangleLinesEx(rn,2,(Color){255,210,140,255}); DrawRectangleLinesEx(ra,2,(Color){255,210,140,255});
    char lab[96];
    snprintf(lab,sizeof lab,"名称：%s",nm); CNC(lab,(int)rn.x+14,(int)rn.y+14,20,(Color){235,240,248,255});
    snprintf(lab,sizeof lab,"地址：%s",ad[0]?ad:"点这里再用下方键盘输入"); CNC(lab,(int)ra.x+14,(int)ra.y+14,20,ad[0]?(Color){235,240,248,255}:(Color){150,160,176,255});
    if(rectTap(rn))gKeyFocus=0;
    if(rectTap(ra))gKeyFocus=1;
    // physical / desktop keyboard input
    int ch; while((ch=GetCharPressed())>0)
    { char s[2]={(char)ch,0}; if(ch<128) keyPress(s,nm,ad); }
    if(IsKeyPressed(KEY_BACKSPACE)) keyPress("退格",nm,ad);
    // on-screen keypad
    int kbPer=8; float kw=92, kh=44, gx=10, gy=300;
    for(int i=0;i<16;i++)
    {
        int col=i%kbPer,row=i/kbPer;
        Rectangle r={(float)cx-(kbPer*kw+(kbPer-1)*gx)/2 + col*(kw+gx), gy+row*(kh+10), kw,kh};
        bool hov=CheckCollisionPointRec(GetMousePosition(),r)||
                 (gTap&&CheckCollisionPointRec((Vector2){gTapX,gTapY},r));
        DrawRectangleRec(r,hov?(Color){178,58,44,235}:(Color){30,40,58,235});
        DrawRectangleLinesEx(r,2,(Color){255,210,140,230});
        CNC(KEYS[i],(int)(r.x+kw/2),(int)(r.y+kh/2-10),18,(Color){238,242,250,255});
        if(rectTap(r)) keyPress(KEYS[i],nm,ad);
    }
    int done=0;
    if(btn("确认添加",cx-120,408,210,50))
    {
        NetAddr na;
        if(nm[0]&&ad[0]&&Net_ParseRelayAddr(ad,&na)&&gSrvN<SRV_MAX)
        {
            Srv*v=&gSrv[gSrvN++]; memset(v,0,sizeof(*v));
            strncpy(v->name,nm,sizeof(v->name)-1); strncpy(v->atxt,ad,sizeof(v->atxt)-1);
            v->a=na; v->valid=1; v->active=1;
            for(int i=0;i<gSrvN-1;i++) gSrv[i].active=0;
            srvSave(); done=1;
        }
    }
    if(btn("返回",cx+120,408,210,50)) done=1;
    if(done) opened=0;
    return done;
}

// returns: 0 stay, 1 join(double-tap a server as client), 2 host via active
static int serverListScreen(float dt)
{
    srvLoad();
    panelTitle("远程服务器 · 跨网联机（Kanye / frp）",92);
    int cx=GetScreenWidth()/2;
    srvPing(dt);
    Srv* act=srvActive();
    CNC(act?TextFormat("当前穿透：%s  %s  %s",act->name,act->atxt,
                       act->online?TextFormat("在线 · %dms",(int)act->pingMs):"离线/检测中")
             : "未选择服务器：点一行设为当前，双击一行直接加入",cx,140,18,
         act?(Color){150,235,170,255}:(Color){220,200,150,255});
    CNC("主机端：在自己电脑/手机上用 Kanye+frpc 开好隧道，点下方“用当前服务器创建主机”",cx,168,15,(Color){180,190,206,235});
    int y=196; static int lastIdx=-1; static float lastT=-10;
    int clicked=-1;
    for(int i=0;i<gSrvN;i++)
    {
        Rectangle r={(float)cx-280,(float)y,560,52};
        bool hov=CheckCollisionPointRec(GetMousePosition(),r)||
                 (gTap&&CheckCollisionPointRec((Vector2){gTapX,gTapY},r));
        DrawRectangleRec(r,hov?(Color){40,54,80,235}:(Color){22,30,44,230});
        DrawRectangleLinesEx(r,2,gSrv[i].active?(Color){120,220,150,255}:(Color){255,210,140,200});
        char row[128];
        snprintf(row,sizeof row,"%s %s   %s",gSrv[i].active?"●":"○",gSrv[i].name,gSrv[i].atxt);
        CNC(row,(int)r.x+16,(int)r.y+8,18,(Color){236,240,248,255});
        const char* st= gSrv[i].active? (gSrv[i].online?TextFormat("在线 %dms · 双击加入",(int)gSrv[i].pingMs):"离线/检测中")
                                       : "点此设为当前";
        CNC(st,(int)(r.x+r.width-12),(int)r.y+30,15,
            gSrv[i].active?(gSrv[i].online?(Color){140,235,170,255}:(Color){230,180,150,255}):(Color){190,198,214,235});
        if(rectTap(r)) clicked=i;
        y+=60;
    }
    if(clicked>=0)
    {
        float now=GetTime();
        if(lastIdx==clicked && now-lastT<0.45)
        {   // double tap -> join as client via this server
            for(int i=0;i<gSrvN;i++)gSrv[i].active=(i==clicked);
            srvSave(); lastIdx=-1; return 1;
        }
        for(int i=0;i<gSrvN;i++)gSrv[i].active=(i==clicked);  // single tap -> active
        srvSave(); lastIdx=clicked; lastT=now;
    }
    int ret=0;
    if(btn("+ 添加新服务器",cx-220,GetScreenHeight()-150,260,54)) ret=3;
    if(act && btn("用当前服务器创建主机",cx+40,GetScreenHeight()-150,300,54)) ret=2;
    if(btn("④ 返回",cx,GetScreenHeight()-84,260,48)) ret=4;
    return ret;
}

int Coop_Lobby(void)
{
    int phase=0; float t=0;   // 0 choose,1 hosting,2 search,3 relay list,4 add form
    srvLoad();
    while(!WindowShouldClose())
    {
        float dt=clampf(GetFrameTime(),0,0.05f); t+=dt;
        Vector2 m=GetMousePosition(); (void)m;
        BeginDrawing(); tapBegin();
        if(phase==0)
        {
            panelTitle("联机作战 · 局域网 / 远程穿透",120);
            CNC("同一 WiFi 直接搜；不在同一网络就用“远程服务器”（Kanye + frp 穿透）",GetScreenWidth()/2,190,18,(Color){205,212,225,235});
            int cx=GetScreenWidth()/2;
            if(btn("① 创建主机（可选全部 27 场战役）",cx,244,440,54)){ gCoopRole=1;gCoopId=0;gCoopScenario=0; if(Net_Host()){ srvApplyActive(); phase=1; } }
            if(btn("② 创建主机 · 长津湖·冰雕连",cx,306,440,54)){ gCoopRole=1;gCoopId=0;gCoopScenario=2; if(Net_Host()){ srvApplyActive(); phase=1; } }
            if(btn("③ 搜索并加入主机（局域网）",cx,368,440,54)){ gCoopRole=2; Net_ClearRelay(); if(Coop_BeginSearch())phase=2; }
            if(btn("④ 远程服务器 · 跨网联机（添加/穿透）",cx,430,440,54)){ phase=3; }
            if(btn("⑤ 返回主菜单",cx,496,440,50)){ Net_Close();gCoopRole=0; EndDrawing(); return 0; }
            Srv* ac=srvActive();
            CNC(ac?TextFormat("当前穿透服务器：%s %s（主机会在左上角显示穿透地址）",ac->name,ac->atxt)
                   : "未设置远程服务器（仅局域网）；要跨网请点 ④",
                cx,GetScreenHeight()-44,16,(Color){170,190,214,225});
        }
        else if(phase==1)
        {
            srvApplyActive(); Net_RelayTick(dt);
            Coop_HostPoll(dt);
            char ht[80]; snprintf(ht,sizeof ht,"主机已创建 · %s",coopMapName(gCoopScenario)); panelTitle(ht,110);
            int cx=GetScreenWidth()/2;
            Srv*ac=srvActive();
            if(ac&&ac->valid)
            {   // tunnel connection string, shown top-left as requested
                DrawRectangle(12,70,430,64,(Color){8,14,24,220});
                DrawRectangleLines(12,70,430,64,(Color){120,220,150,255});
                CNC("穿透地址（发给队友，填到“添加服务器”）：",24,80,15,(Color){170,225,185,255});
                CNC(ac->atxt,24,104,22,(Color){120,235,170,255});
            }
            CNC("局域网点“搜索并加入”；跨网把左上角穿透地址发给队友添加",cx,180,20,(Color){215,220,232,255});
            CNC(TextFormat("在线战友：%d 人（含主机）",Coop_PeerCount()+1),cx,232,26,(Color){120,230,150,255});
            // host can pick ANY of the 27 campaigns before starting
            if(btn("◀ 上一场役",cx-200,272,170,46)){ gCoopScenario=(gCoopScenario+26)%27; }
            if(btn("下一场役 ▶",cx+200,272,170,46)){ gCoopScenario=(gCoopScenario+1)%27; }
            int y=340;
            for(int i=0;i<sPeerN;i++){ CNC(TextFormat("战友 %d   %s",sPeers[i].id,sPeers[i].addr.ip),cx,y,18,(Color){200,210,225,255}); y+=28; }
            if(btn("▶ 开始战斗",cx,GetScreenHeight()-170,360,56)){ Coop_HostStartBattle(); EndDrawing(); return 1; }
            if(btn("⑤ 返回",cx,GetScreenHeight()-100,300,48)){ Net_Close();gCoopRole=0; EndDrawing(); return 0; }
        }
        else if(phase==2)
        {
            Net_RelayTick(dt);
            CoopRoom rooms[NET_MAXPEERS];
            int nr=Coop_PollRooms(rooms,NET_MAXPEERS,dt);
            keepAlive(dt);
            int sc=0;
            if(gCoopId>=1 && Coop_ClientStart(&sc)){ gCoopScenario=sc; EndDrawing(); return 2; }
            panelTitle(Net_HasRelay()?"通过穿透服务器搜索主机":"搜索局域网内的主机",110);
            int cx=GetScreenWidth()/2, y=200;
            if(gCoopId>=1)
            {
                int js=Coop_JoinStatus();
                if(js==1)
                {
                    CNC(TextFormat("已加入主机！你的战友编号：%d",gCoopId),cx,250,26,(Color){120,230,150,255});
                    CNC("联机链路正常，等待主机点击“开始战斗”进入同一战场…",cx,292,19,(Color){205,215,230,255});
                }
            }
            else
            {
                if(nr==0) CNC(Net_HasRelay()?"正在通过穿透服务器搜索…（请确认对端 Kanye+frpc 已启动、主机已创建）"
                                            :"正在搜索…（请确认主机已创建、两台设备在同一 WiFi/热点）",
                              cx,y,18,(Color){200,206,220,255});
                for(int i=0;i<nr;i++)
                {
                    char lb[96]; snprintf(lb,sizeof lb,"加入：%s · %s · 在线%d人",
                        rooms[i].name, coopMapName(rooms[i].scenario), rooms[i].players+1);
                    if(btn(lb,cx,y,520,54)){ Coop_Join(&rooms[i].addr); }
                    y+=64;
            }   }
            if(btn("⑤ 返回",cx,GetScreenHeight()-120,300,52)){ Net_Close();gCoopRole=0;gCoopId=0; phase=0; }
        }
        else if(phase==3)
        {
            int r=serverListScreen(dt);
            if(r==1){   // join as client through the active relay
                Srv*ac=srvActive();
                gCoopRole=2; gCoopId=0;
                if(ac&&ac->valid){ if(Coop_BeginSearch()){ srvApplyActive(); phase=2; } }
            }
            else if(r==2){ // host through the active relay
                Srv*ac=srvActive();
                if(ac&&ac->valid){ gCoopRole=1;gCoopId=0;gCoopScenario=0;
                    if(Net_Host()){ srvApplyActive(); phase=1; } }
            }
            else if(r==3){ phase=4; }
            else if(r==4){ phase=0; }
        }
        else if(phase==4)
        {
            if(serverFormScreen()) phase=3;
        }
        EndDrawing();
        if(gSelfTest){ TakeScreenshot(TextFormat("%s/shot_lobby.png",gShotDir)); break; }
    }
    Net_Close(); gCoopRole=0;
    return 0;
}
