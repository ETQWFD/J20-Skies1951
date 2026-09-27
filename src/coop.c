// coop.c - LAN cooperative lobby: host/create, UDP auto-discovery, join, ping.
// Text protocol over the net.c UDP transport (see coop.h).
#include "coop.h"
#include "common.h"

int     gCoopRole=0;
int     gCoopId=0;
int     gCoopScenario=0;
NetAddr gCoopHost;

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
        if(strncmp(b,"SKG",3)==0){ int g=0; if(sscanf(b,"SKG|%d",&g)==1){ sStart=1; sStartSc=g; } }
        else if(sscanf(b,"SKH|%d|%d",&sc,&pl)>=1)
        { roomTouch(&f,"host",sc,pl);
          if(sJoinId>=0 && Net_AddrEq(&f,&sJoinAddr)) { /* ack handled below */ } }
        int id=-1,sc2=0;
        if(sscanf(b,"SKA|%d|%d",&id,&sc2)==2 && sJoinT>0 && Net_AddrEq(&f,&sJoinAddr))
        { sJoinId=id; sJoinT=0; gCoopId=id; gCoopScenario=sc2; }
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

static bool btn(const char*s,int cx,int y,int w,int h)
{
    Rectangle r={cx-w/2,y,w,h}; Vector2 m=GetMousePosition();
    bool hov=CheckCollisionPointRec(m,r);
    DrawRectangleRec(r,hov?(Color){178,58,44,235}:(Color){22,30,44,230});
    DrawRectangleLinesEx(r,2,(Color){255,210,140,255});
    CNC(s,cx,y+h/2-12,20,(Color){240,242,248,255});
    return hov && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
}

int Coop_Lobby(void)
{
    int phase=0; float t=0;   // 0 choose,1 hosting,2 search
    while(!WindowShouldClose())
    {
        float dt=clampf(GetFrameTime(),0,0.05f); t+=dt;
        Vector2 m=GetMousePosition(); (void)m;
        BeginDrawing();
        if(phase==0)
        {
            panelTitle("局域网联机 · 同一 WiFi / 热点下互连",120);
            CNC("先在电脑/手机一端创建主机，另一端点“搜索并加入”即可自动发现在线房间",GetScreenWidth()/2,190,18,(Color){205,212,225,235});
            int cx=GetScreenWidth()/2;
            if(btn("① 创建主机 · 山地攻坚",cx,250,440,58)){ gCoopRole=1;gCoopId=0;gCoopScenario=0; if(Net_Host())phase=1; }
            if(btn("② 创建主机 · 长津湖夜战",cx,318,440,58)){ gCoopRole=1;gCoopId=0;gCoopScenario=1; if(Net_Host())phase=1; }
            if(btn("③ 搜索并加入主机",cx,386,440,58)){ gCoopRole=2; if(Coop_BeginSearch())phase=2; }
            if(btn("④ 返回主菜单",cx,468,440,54)){ Net_Close();gCoopRole=0; EndDrawing(); return 0; }
            CNC("局域网协同作战：同一 WiFi/热点下，一端创建主机，另一端搜索加入，再由主机开始战斗",cx,GetScreenHeight()-70,16,(Color){170,178,194,220});
            CNC("进入同一战场、看到彼此、一起打同一批敌人（主机权威）",cx,GetScreenHeight()-44,16,(Color){170,178,194,220});
        }
        else if(phase==1)
        {
            Coop_HostPoll(dt);
            panelTitle(gCoopScenario==1?"主机已创建 · 长津湖夜战":"主机已创建 · 山地攻坚",110);
            int cx=GetScreenWidth()/2;
            CNC("在另一台设备上点“搜索并加入”；人齐后点下面的“开始战斗”",cx,180,20,(Color){215,220,232,255});
            CNC(TextFormat("在线战友：%d 人（含主机）",Coop_PeerCount()+1),cx,232,26,(Color){120,230,150,255});
            int y=286;
            for(int i=0;i<sPeerN;i++){ CNC(TextFormat("战友 %d   %s",sPeers[i].id,sPeers[i].addr.ip),cx,y,18,(Color){200,210,225,255}); y+=28; }
            if(btn("▶ 开始战斗",cx,GetScreenHeight()-170,360,56)){ Coop_HostStartBattle(); EndDrawing(); return 1; }
            if(btn("④ 返回",cx,GetScreenHeight()-100,300,48)){ Net_Close();gCoopRole=0; EndDrawing(); return 0; }
        }
        else
        {
            CoopRoom rooms[NET_MAXPEERS];
            int nr=Coop_PollRooms(rooms,NET_MAXPEERS,dt);
            keepAlive(dt);
            int sc=0;
            if(gCoopId>=1 && Coop_ClientStart(&sc)){ gCoopScenario=sc; EndDrawing(); return 2; }
            panelTitle("搜索局域网内的主机",110);
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
                if(nr==0) CNC("正在搜索…（请确认主机已创建、两台设备在同一 WiFi/热点）",cx,y,18,(Color){200,206,220,255});
                for(int i=0;i<nr;i++)
                {
                    char lb[96]; snprintf(lb,sizeof lb,"加入：%s · %s · 在线%d人",
                        rooms[i].name, rooms[i].scenario==1?"长津湖夜战":"山地攻坚", rooms[i].players+1);
                    if(btn(lb,cx,y,520,54)){ Coop_Join(&rooms[i].addr); }
                    y+=64;
            }   }
            if(btn("④ 返回",cx,GetScreenHeight()-120,300,52)){ Net_Close();gCoopRole=0;gCoopId=0; EndDrawing(); return 0; }
        }
        EndDrawing();
        if(gSelfTest){ TakeScreenshot(TextFormat("%s/shot_lobby.png",gShotDir)); break; }
    }
    Net_Close(); gCoopRole=0;
    return 0;
}
