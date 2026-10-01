/*
 * Kanye — 长空·1951 (J20-Skies1951) 专用 UDP 中继 / 内网穿透搭档
 * ----------------------------------------------------------------------------
 * 纯 C、无第三方依赖。运行在你自己的公网服务器（或 frpc 隧道背后）上，为不同
 * NAT 后的玩家提供一个“星型”UDP 中继，让“添加服务器→双击加入”跨网联机可用。
 *
 * 启动：
 *   Windows : Kanye.exe 24463 & frpc -c frpc.ini
 *   Linux/Android(Termux) : ./Kanye 24463 & frpc -c ./frpc.ini
 *     （24463 是中继监听的 UDP 端口，可改成任意端口，要与 frpc.ini 一致）
 *
 * 玩法：
 *   1) 主机在游戏“局域网协同作战”里创建主机；客户端在“添加服务器”里填
 *      中继公网地址（域名或 IP）和这里的端口。
 *   2) 所有玩家的报文都汇聚到 Kanye，Kanye 按信封转发，彼此无需同局域网。
 *
 * 同目录 etcioad.txt：只写一个数字（默认 1）。Kanye 会把主机开局广播
 *   "SKG|<战役编号>" 强制改成这个编号，方便用同一个服务器固定开某一场战役。
 *   改成 27 即最后一场（编号 1..27 显示，内部 0..26）。
 *
 * 报文（与游戏 net.c 完全对应）：
 *   裸包（发现/注册）：SKD / SKH / SKJ / SKA / SKP / SKG / SKR —— 广播给其它全部端点
 *   信封（定向）：'K'[sport u16 LE][dport u16 LE][payload]，dport=0xFFFF 广播
 * ----------------------------------------------------------------------------
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if defined(_WIN32)
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #include <windows.h>
  #pragma comment(lib,"ws2_32.lib")
  typedef int socklen_t;
  #define KA_CLOSE closesocket
#else
  #define KA_CLOSE close
  #include <sys/socket.h>
  #include <sys/types.h>
  #include <netinet/in.h>
  #include <arpa/inet.h>
  #include <unistd.h>
#endif

#define KA_CAP      1500
#define KA_MAXPEER  64
#define KA_EXPIRE   30.0     /* 秒：30 秒没动静视为离线 */

typedef struct {
    struct sockaddr_in a;
    int  used;
    int  id;
    float last;
    char ip[32];
    unsigned short natPort;
} Peer;

static Peer gP[KA_MAXPEER];
static int  gPN=0;
static int  gForceMode=0;          /* etcioad.txt：0=不强制，1..27=战役(显示编号) */

static float nowSec(void){
#ifdef _WIN32
    return (float)clock()/(float)CLOCKS_PER_SEC;
#else
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC,&ts);
    return ts.tv_sec + ts.tv_nsec/1.0e9f;
#endif
}
static unsigned short portOf(const struct sockaddr_in*a){ return ntohs(a->sin_port); }
static int sameAddr(const struct sockaddr_in*x,const struct sockaddr_in*y)
{ return x->sin_addr.s_addr==y->sin_addr.s_addr && x->sin_port==y->sin_port; }

static Peer* findPeer(const struct sockaddr_in*a)
{ for(int i=0;i<gPN;i++) if(gP[i].used && sameAddr(&gP[i].a,a)) return &gP[i]; return 0; }

static Peer* touchPeer(const struct sockaddr_in*a)
{
    Peer*p=findPeer(a);
    if(p){ p->last=nowSec(); return p; }
    if(gPN>=KA_MAXPEER){
        // recycle the stalest slot
        int worst=0;
        for(int i=1;i<gPN;i++) if(gP[i].last<gP[worst].last) worst=i;
        p=&gP[worst];
    } else { p=&gP[gPN++]; }
    memset(p,0,sizeof(*p));
    p->a=*a; p->used=1; p->last=nowSec(); p->id=gPN;
    p->natPort=portOf(a);
    const char*ip=inet_ntoa(a->sin_addr);
    if(ip){ strncpy(p->ip,ip,sizeof(p->ip)-1); p->ip[sizeof(p->ip)-1]=0; }
    return p;
}

static void prunePeers(float t)
{
    for(int i=0;i<gPN;i++)
        if(gP[i].used && t-gP[i].last>KA_EXPIRE)
        { printf("[离线] %s:%u 超过 %.0f 秒无心跳，已移除\n",
                 gP[i].ip,gP[i].natPort,KA_EXPIRE); gP[i].used=0; }
}

static int onlineCount(void){ int n=0; for(int i=0;i<gPN;i++) if(gP[i].used)n++; return n; }

/* 用信封把 payload 发往某一个端点（sport 记为发送方 NAT 端口） */
static void sendEnv(int sock,const struct sockaddr_in*dst,
                    unsigned short sport,unsigned short dport,
                    const char*data,int len)
{
    unsigned char b[KA_CAP+8];
    if(len>KA_CAP)len=KA_CAP;
    b[0]='K';
    b[1]=(unsigned char)(sport&255); b[2]=(unsigned char)(sport>>8);
    b[3]=(unsigned char)(dport&255); b[4]=(unsigned char)(dport>>8);
    memcpy(b+5,data,len);
    sendto(sock,(const char*)b,len+5,0,(const struct sockaddr*)dst,sizeof(*dst));
}

/* 广播 payload 给除发送者外的所有在线端点 */
static void broadcast(int sock,const struct sockaddr_in*from,
                      unsigned short sport,const char*data,int len)
{
    for(int i=0;i<gPN;i++)
        if(gP[i].used && !sameAddr(&gP[i].a,from))
            sendEnv(sock,&gP[i].a,sport,0xFFFF,data,len);
}

static void loadForceMode(void)
{
    FILE*f=fopen("etcioad.txt","r");
    if(!f){ gForceMode=0; return; }
    int v=0; if(fscanf(f,"%d",&v)!=1) v=0; fclose(f);
    gForceMode=(v>=1 && v<=27)?v:0;     // 显示编号 1..27
}

/* 若启用了强制模式，把 "SKG|<n>" 的 n 改成 gForceMode-1（原地改写）。返回长度 */
static int applyForceMode(char*buf,int len)
{
    if(gForceMode<=0||len<4||strncmp(buf,"SKG|",4)!=0) return len;
    int n=snprintf(buf,KA_CAP,"SKG|%d",gForceMode-1);
    return n>0&&n<=KA_CAP?n:len;
}

int main(int argc,char**argv)
{
    unsigned short listenPort=24463;
    if(argc>=2){ int v=atoi(argv[1]); if(v>0&&v<=65535)listenPort=(unsigned short)v; }

#if defined(_WIN32)
    // Chinese log text is UTF-8 in the binary; a default cmd/GBK code page renders
    // it as mojibake. Switch both consoles to UTF-8 before printing anything.
    SetConsoleOutputCP(65001);
    SetConsoleCP(65001);
#endif
#if defined(_WIN32)
    WSADATA w; if(WSAStartup(MAKEWORD(2,2),&w)!=0){ printf("WSAStartup 失败\n"); return 1; }
#endif
    loadForceMode();

    int s=(int)socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);
    if(s<0){ printf("socket 创建失败\n"); return 1; }
    int yes=1; setsockopt(s,SOL_SOCKET,SO_REUSEADDR,(const char*)&yes,sizeof(yes));
    struct sockaddr_in bindAddr; memset(&bindAddr,0,sizeof(bindAddr));
    bindAddr.sin_family=AF_INET; bindAddr.sin_addr.s_addr=htonl(INADDR_ANY);
    bindAddr.sin_port=htons(listenPort);
    if(bind(s,(struct sockaddr*)&bindAddr,sizeof(bindAddr))<0)
    { printf("绑定 UDP %u 失败（端口被占用？）\n",listenPort); KA_CLOSE(s); return 1; }

#if defined(_WIN32)
    DWORD nb=0; ioctlsocket(s,FIONBIO,&nb);   /* Kanye 用阻塞式，简单稳定 */
    DWORD to=1000; setsockopt(s,SOL_SOCKET,SO_RCVTIMEO,(const char*)&to,sizeof(to));
#else
    struct timeval to; to.tv_sec=1; to.tv_usec=0;
    setsockopt(s,SOL_SOCKET,SO_RCVTIMEO,&to,sizeof(to));
#endif

    printf("============================================================\n");
    printf(" Kanye 中继服务器 · 长空·1951 (J20-Skies1951)\n");
    printf(" 监听 UDP 0.0.0.0:%u\n",listenPort);
    printf(" 强制战役 etcioad.txt = %s\n", gForceMode? "(已设置)":"(关闭，由主机选择)");
    if(gForceMode) printf("   -> 所有开局强制为第 %d 场战役\n",gForceMode);
    printf(" 用法：客户端“添加服务器”填 <本机公网IP/域名>:%u\n",listenPort);
    printf(" 搭配 frpc：frpc -c frpc.ini（隧道 UDP 本地端口 %u）\n",listenPort);
    printf("============================================================\n");
    fflush(stdout);

    char buf[KA_CAP+8];
    for(;;)
    {
        struct sockaddr_in from; socklen_t fl=sizeof(from);
        int n=recvfrom(s,buf,sizeof(buf)-1,0,(struct sockaddr*)&from,&fl);
        float t=nowSec();
        prunePeers(t);
        if(n<=0) continue;
        buf[n]=0;
        Peer*p=touchPeer(&from);
        int beforeOnline=onlineCount();

        if(n>=5 && buf[0]=='K')
        {
            // Host-authoritative game with <=4 peers and tiny packets: the robust
            // choice over frp is to fan EVERY envelope out to all other physical
            // peers (frp gives each remote player its own local flow/port, so each
            // physical peer is unique). We RE-STAMP sport with the sender's unique
            // physical port, never trust an embedded port (NAT games collide).
            unsigned short sport=p->natPort;
            char*pl=buf+5; int pln=n-5;
            if(strncmp(pl,"SKG|",4)==0){ int nl=applyForceMode(pl,pln); if(nl!=pln)pln=nl; }
            (void)0; // dport in the envelope is intentionally ignored (broadcast)
            broadcast(s,&from,sport,pl,pln);
        }
        else
        {
            // SKR 心跳：只回一个 SKO 在线确认（不广播），供客户端测延迟
            if(!strncmp(buf,"SKR",3))
            { sendEnv(s,&from,p->natPort,p->natPort,"SKO",3); continue; }
            // 裸发现/心跳包：注册后转发给其他全部端点
            int outLen=n;
            if(strncmp(buf,"SKG|",4)==0) outLen=applyForceMode(buf,n);
            if(!strncmp(buf,"SKD",3))
                printf("[发现] %s:%u 正在搜索主机（在线 %d）\n",p->ip,p->natPort,beforeOnline);
            else if(!strncmp(buf,"SKH",3))
                printf("[主机] %s:%u 宣告房间（在线 %d）\n",p->ip,p->natPort,beforeOnline);
            else if(!strncmp(buf,"SKJ",3))
                printf("[加入] %s:%u 请求加入主机（在线 %d）\n",p->ip,p->natPort,beforeOnline);
            else if(!strncmp(buf,"SKG",3))
                printf("[开局] %s:%u 开始战斗（在线 %d）\n",p->ip,p->natPort,beforeOnline);
            broadcast(s,&from,p->natPort,buf,outLen);
        }
        fflush(stdout);
    }
    KA_CLOSE(s);
#if defined(_WIN32)
    WSACleanup();
#endif
    return 0;
}
