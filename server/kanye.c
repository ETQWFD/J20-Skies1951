/*
 * Kanye — 长空·1951 (J20-Skies1951) 专用 UDP 中继 / 内网穿透搭档
 * ----------------------------------------------------------------------------
 * 纯 C、无第三方依赖。运行在你自己的公网服务器（或 frpc 隧道背后）上，为不同
 * NAT 后的玩家提供“星型”UDP 中继，让“添加服务器→双击加入”跨网联机可用。
 *
 * 启动：
 *   Windows : Kanye.exe 24463 & frpc -c frpc.ini
 *   Linux/Android(Termux) : ./Kanye 24463 & frpc -c ./frpc.ini
 *     （24463 是中继监听的 UDP 端口，可改成任意端口，要与 frpc.ini 一致）
 *
 * 为什么按“会话 ID”而不是 IP:端口 路由：
 *   frp 常把多个远端玩家的 UDP 复用到同一条 frpc↔frps 流上，Kanye 看到的物理
 *   源地址可能完全相同（端口塌缩）；NAT/重连还会让源端口漂移。因此每个游戏
 *   socket 启动时随机生成一个 15 位会话 ID，放在报文内层，Kanye 只按会话 ID
 *   识别玩家、记录其“最新物理地址”，彻底免疫端口塌缩和漫游。
 *
 * 同目录 etcioad.txt：只写一个数字（默认 1）。Kanye 会把主机开局广播
 *   "SKG|<战役编号>" 强制改成这个编号，方便固定开某一场战役（1..27 显示）。
 *
 * 报文（与游戏 net.c 完全对应）：
 *   客户端 -> Kanye : 'K' 0x02 [srcSess hi lo] [dstSess hi lo] <payload>
 *                    dstSess=0xFFFF 表示广播给其它全部会话
 *   Kanye -> 客户端 : 'K' 0x02 [srcSess hi lo] <payload>
 *   裸包 "SKR"       : 仅用于在线/延迟探测，Kanye 原样回 "SKO"（不转发）
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
#define KA_MAXSESS  64
#define KA_EXPIRE   30.0     /* 秒：30 秒没动静视为离线 */
#define KA_VER      0x02
#define DST_BCAST   0xFFFF

typedef struct {
    int  used;
    unsigned short sess;
    struct sockaddr_in a;   /* 最新物理地址（每次报文刷新，支持 frp/漫游） */
    float last;
    char ip[32];
} Sess;

static Sess gS[KA_MAXSESS];
static int  gSN=0;
static int  gForceMode=0;   /* etcioad.txt：0=不强制，1..27=战役(显示编号) */

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
static void fmtIp(char*ip,const struct sockaddr_in*a)
{ const char*s=inet_ntoa(a->sin_addr); if(s){ strncpy(ip,s,31); ip[31]=0; } else ip[0]=0; }

/* find session by id (regardless of address) */
static Sess* findSess(unsigned short sess)
{ for(int i=0;i<gSN;i++) if(gS[i].used && gS[i].sess==sess) return &gS[i]; return 0; }

/* register/refresh (sess -> current address). Sessions are independent even if
 * two players temporarily share one physical address (frp UDP multiplexing); we
 * never evict one id for another, we only refresh that id's latest address. */
static Sess* touchSess(unsigned short sess,const struct sockaddr_in*from)
{
    float t=nowSec();
    Sess*p=findSess(sess);
    if(p)
    {   if(!sameAddr(&p->a,from))
        {   char oip[32],nip[32]; fmtIp(oip,&p->a); fmtIp(nip,from);
            printf("[漫游] 会话 %u 地址 %s:%u -> %s:%u\n",
                   sess,oip,portOf(&p->a),nip,portOf(from));
            p->a=*from; }
        p->last=t; return p;
    }
    if(gSN<KA_MAXSESS){ p=&gS[gSN++]; }
    else
    {   int worst=0;
        for(int i=1;i<gSN;i++) if(gS[i].last<gS[worst].last) worst=i;
        p=&gS[worst];
    }
    memset(p,0,sizeof(*p));
    p->used=1; p->sess=sess; p->a=*from; p->last=t; fmtIp(p->ip,from);
    return p;
}

static void pruneSess(float t)
{
    for(int i=0;i<gSN;i++)
        if(gS[i].used && t-gS[i].last>KA_EXPIRE)
        { printf("[离线] 会话 %u %s:%u 超过 %.0f 秒无报文，已移除\n",
                 gS[i].sess,gS[i].ip,portOf(&gS[i].a),KA_EXPIRE);
          gS[i].used=0; }
}
static int onlineCount(void){ int n=0; for(int i=0;i<gSN;i++) if(gS[i].used)n++; return n; }

/* relay->client frame: 'K' VER [srcSess hi lo] <payload>  (5-byte header) */
static void forwardTo(int sock,const Sess*dst,unsigned short srcSess,
                      const char*data,int len)
{
    unsigned char b[KA_CAP+8];
    if(len>KA_CAP)len=KA_CAP;
    b[0]='K'; b[1]=KA_VER;
    b[2]=(unsigned char)(srcSess>>8); b[3]=(unsigned char)(srcSess&255);
    memcpy(b+4,data,len);
    sendto(sock,(const char*)b,len+4,0,(const struct sockaddr*)&dst->a,sizeof(dst->a));
}

static void loadForceMode(void)
{
    FILE*f=fopen("etcioad.txt","r");
    if(!f){ gForceMode=0; return; }
    int v=0; if(fscanf(f,"%d",&v)!=1) v=0; fclose(f);
    gForceMode=(v>=1 && v<=27)?v:0;
}
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
    SetConsoleOutputCP(65001);
    SetConsoleCP(65001);
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
    DWORD nb=0; ioctlsocket(s,FIONBIO,&nb);   /* 阻塞式，简单稳定 */
    DWORD to=1000; setsockopt(s,SOL_SOCKET,SO_RCVTIMEO,(const char*)&to,sizeof(to));
#else
    struct timeval to; to.tv_sec=1; to.tv_usec=0;
    setsockopt(s,SOL_SOCKET,SO_RCVTIMEO,&to,sizeof(to));
#endif

    printf("============================================================\n");
    printf(" Kanye 中继服务器 · 长空·1951 (J20-Skies1951)  [会话ID路由 v2]\n");
    printf(" 监听 UDP 0.0.0.0:%u\n",listenPort);
    printf(" 强制战役 etcioad.txt = %s\n", gForceMode? "(已设置)":"(关闭，由主机选择)");
    if(gForceMode) printf("   -> 所有开局强制为第 %d 场战役\n",gForceMode);
    printf(" 用法：客户端“添加服务器”填 <本机公网IP/域名>:%u\n",listenPort);
    printf(" 搭配 frpc：frpc -c frpc.ini（隧道 UDP 本地端口 %u）\n",listenPort);
    printf("============================================================\n");
    fflush(stdout);

    unsigned char buf[KA_CAP+16];
    for(;;)
    {
        struct sockaddr_in from; socklen_t fl=sizeof(from);
        int n=recvfrom(s,(char*)buf,sizeof(buf)-1,0,(struct sockaddr*)&from,&fl);
        float t=nowSec();
        pruneSess(t);
        if(n<=0) continue;

        if(n>=6 && buf[0]=='K' && buf[1]==KA_VER)
        {
            unsigned short srcSess=(unsigned short)((buf[2]<<8)|buf[3]);
            unsigned short dstSess=(unsigned short)((buf[4]<<8)|buf[5]);
            char*pl=(char*)buf+6; int pln=n-6;
            if(srcSess==0||srcSess==DST_BCAST||pln<=0) continue;
            int before=onlineCount();
            Sess*sp=touchSess(srcSess,&from); (void)sp;
            if(strncmp(pl,"SKG|",4)==0){ int nl=applyForceMode(pl,pln); pln=nl; }

            if(pln>=3&&!strncmp(pl,"SKD",3))
                printf("[发现] 会话 %u 搜索主机（在线 %d）\n",srcSess,before);
            else if(pln>=3&&!strncmp(pl,"SKJ",3))
                printf("[加入] 会话 %u 请求加入（在线 %d）\n",srcSess,before);
            else if(pln>=3&&!strncmp(pl,"SKG",3))
                printf("[开局] 会话 %u 开始战斗（在线 %d）\n",srcSess,before);

            if(dstSess==DST_BCAST)
            {   // at most one packet per distinct physical address (frp may give
                // several sessions the same return address).
                struct sockaddr_in done[KA_MAXSESS]; int dn=0;
                for(int i=0;i<gSN;i++) if(gS[i].used && gS[i].sess!=srcSess)
                {   int dup=0;
                    for(int j=0;j<dn;j++) if(sameAddr(&done[j],&gS[i].a)){dup=1;break;}
                    if(!dup){ forwardTo(s,&gS[i],srcSess,pl,pln);
                              if(dn<KA_MAXSESS) done[dn++]=gS[i].a; }
            }   }
            else
            {   Sess*d=findSess(dstSess);
                if(d) forwardTo(s,d,srcSess,pl,pln); }
        }
        else
        {
            // bare packet: only SKR ping is handled (answer SKO, do not forward)
            if(n==3 && !strncmp((const char*)buf,"SKR",3))
            { char ip[32]; fmtIp(ip,&from); (void)ip;
              sendto(s,"SKO",3,0,(struct sockaddr*)&from,sizeof(from)); }
        }
        fflush(stdout);
    }
    KA_CLOSE(s);
#if defined(_WIN32)
    WSACleanup();
#endif
    return 0;
}
