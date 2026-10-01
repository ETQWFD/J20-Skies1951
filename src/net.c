// net.c - tiny cross-platform, non-blocking UDP layer for LAN play.
#include "net.h"

#if defined(_WIN32)
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #pragma comment(lib,"ws2_32.lib")
  typedef int socklen_t;
  #define NET_CLOSESOCK closesocket
  #define NET_EINTR     WSAEINTR
#else
  #include <sys/socket.h>
  #include <sys/types.h>
  #include <netinet/in.h>
  #include <arpa/inet.h>
  #include <netdb.h>
  #include <unistd.h>
  #include <fcntl.h>
  #include <errno.h>
  #include <string.h>
  #define NET_CLOSESOCK close
#endif

static int   gSock=-1;
static int   gWsa=0;
static NetAddr gPeer[NET_MAXPEERS];
static int   gPeerN=0;

// public relay (Kanye) state
static int   gRelay=0;
static struct sockaddr_in gRelayAddr;
static float gRelayHb=0.0f;
static uint16_t gLocalPort=0;

int Net_AddrEq(const NetAddr*a,const NetAddr*b)
{ return a->addr==b->addr && a->port==b->port; }

int Net_Init(void)
{
#if defined(_WIN32)
    if(!gWsa){ WSADATA w; if(WSAStartup(MAKEWORD(2,2),&w)!=0) return 0; gWsa=1; }
#endif
    return 1;
}

void Net_Shutdown(void)
{
    Net_Close();
#if defined(_WIN32)
    if(gWsa){ WSACleanup(); gWsa=0; }
#endif
}

static int makeNonBlock(int s)
{
#if defined(_WIN32)
    unsigned long nb=1; return ioctlsocket(s,FIONBIO,&nb)==0;
#else
    int fl=fcntl(s,F_GETFL,0); return fcntl(s,F_SETFL,fl|O_NONBLOCK)>=0;
#endif
}

static int openSock(int bindWellKnown)
{
    if(gSock>=0) NET_CLOSESOCK(gSock);
    int s=(int)socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);
    if(s<0) return 0;
    int yes=1;
    setsockopt(s,SOL_SOCKET,SO_REUSEADDR,(const char*)&yes,sizeof(yes));
#ifdef SO_REUSEPORT
    setsockopt(s,SOL_SOCKET,SO_REUSEPORT,(const char*)&yes,sizeof(yes));
#endif
    int bcast=1;
    setsockopt(s,SOL_SOCKET,SO_BROADCAST,(const char*)&bcast,sizeof(bcast));

    struct sockaddr_in a; memset(&a,0,sizeof(a));
    a.sin_family=AF_INET; a.sin_addr.s_addr=htonl(INADDR_ANY);
    a.sin_port=htons(bindWellKnown?NET_PORT:0);
    if(bind(s,(struct sockaddr*)&a,sizeof(a))<0){ NET_CLOSESOCK(s); return 0; }
    makeNonBlock(s);
    { struct sockaddr_in la; socklen_t ll=sizeof(la);
      if(getsockname(s,(struct sockaddr*)&la,&ll)==0) gLocalPort=ntohs(la.sin_port); }
    gSock=s; gPeerN=0;
    return 1;
}

int Net_Host(void){ Net_Init(); return openSock(1); }
int Net_Client(void){ Net_Init(); return openSock(0); }
int Net_IsOpen(void){ return gSock>=0; }

void Net_Close(void)
{
    if(gSock>=0){ NET_CLOSESOCK(gSock); gSock=-1; }
    gPeerN=0; gRelay=0; gRelayHb=0.0f;
}

static void fillAddr(NetAddr*na,const struct sockaddr_in*a)
{
    na->addr=ntohl(a->sin_addr.s_addr);
    na->port=ntohs(a->sin_port);
    const char*ip=inet_ntoa(a->sin_addr);
    int i=0; if(ip){ for(;ip[i]&&i<31;i++)na->ip[i]=ip[i]; na->ip[i]=0; }
}

int Net_Poll(NetAddr*from,char*buf,int cap)
{
    if(gSock<0) return 0;
    struct sockaddr_in sa; socklen_t sl=sizeof(sa);
    static char raw[NET_MAXPKT+8];
    int n=recvfrom(gSock,raw,sizeof(raw)-1,0,(struct sockaddr*)&sa,&sl);
    if(n<=0) return 0;
    // relay envelope: 'K'[sport u16][dport u16][payload]; sport is the sender's
    // public NAT port observed/embedded by the Kanye relay and is what makes each
    // peer distinct even though every packet physically arrives from the relay.
    if(gRelay && n>=5 && raw[0]=='K' &&
       sa.sin_addr.s_addr==gRelayAddr.sin_addr.s_addr)
    {
        int sport=(unsigned char)raw[1]|((unsigned char)raw[2]<<8);
        int payload=n-5;
        if(payload<=0) return 0;
        if(from){
            memset(from,0,sizeof(*from));
            from->addr=ntohl(gRelayAddr.sin_addr.s_addr); // virtual addr on relay IP
            from->port=(sport>0)?sport:ntohs(sa.sin_port);
            const char*ip=inet_ntoa(gRelayAddr.sin_addr);
            int i=0; if(ip){ for(;ip[i]&&i<31;i++)from->ip[i]=ip[i]; from->ip[i]=0; }
        }
        if(payload>cap)payload=cap;
        memcpy(buf,raw+5,payload);
        return payload;
    }
    if(n>cap)n=cap;
    memcpy(buf,raw,n);
    if(from) fillAddr(from,&sa);
    return n;
}

static void toSock(struct sockaddr_in*sa,const NetAddr*na)
{
    memset(sa,0,sizeof(*sa));
    sa->sin_family=AF_INET;
    sa->sin_addr.s_addr=htonl(na->addr);
    sa->sin_port=htons(na->port);
}

void Net_Send(const NetAddr*to,const char*buf,int len)
{
    if(gSock<0||!to) return;
    struct sockaddr_in sa; toSock(&sa,to);
    // destination lives on the relay (virtual addr on relay IP with a NAT port
    // other than the relay's own listen port): tunnel it in a 'K' envelope.
    if(gRelay && sa.sin_addr.s_addr==gRelayAddr.sin_addr.s_addr &&
       ntohs(gRelayAddr.sin_port)>0 && sa.sin_port!=gRelayAddr.sin_port)
    {
        unsigned char env[NET_MAXPKT+8];
        env[0]='K';
        env[1]=(unsigned char)(gLocalPort&255); env[2]=(unsigned char)(gLocalPort>>8);
        unsigned int dp=ntohs(sa.sin_port);
        env[3]=(unsigned char)(dp&255); env[4]=(unsigned char)(dp>>8);
        int m=len; if(m>NET_MAXPKT)m=NET_MAXPKT;
        memcpy(env+5,buf,m);
        sendto(gSock,(const char*)env,m+5,0,(struct sockaddr*)&gRelayAddr,sizeof(gRelayAddr));
        return;
    }
    sendto(gSock,buf,len,0,(struct sockaddr*)&sa,sizeof(sa));
}

void Net_Broadcast(const char*buf,int len)
{
    if(gSock<0) return;
    struct sockaddr_in sa; memset(&sa,0,sizeof(sa));
    sa.sin_family=AF_INET; sa.sin_port=htons(NET_PORT);
    sa.sin_addr.s_addr=inet_addr("255.255.255.255");
    sendto(gSock,buf,len,0,(struct sockaddr*)&sa,sizeof(sa));
    if(gRelay) sendto(gSock,buf,len,0,(struct sockaddr*)&gRelayAddr,sizeof(gRelayAddr));
}

static int resolveIPv4(const char* host, unsigned int* outAddr)
{
    struct in_addr a4;
    if(inet_pton(AF_INET,host,&a4)==1){ *outAddr=a4.s_addr; return 1; }
    struct addrinfo hints,*res=0; memset(&hints,0,sizeof hints);
    hints.ai_family=AF_INET; hints.ai_socktype=SOCK_DGRAM;
    if(getaddrinfo(host,0,&hints,&res)==0 && res)
    {   struct sockaddr_in* sin=(struct sockaddr_in*)res->ai_addr;
        *outAddr=sin->sin_addr.s_addr; freeaddrinfo(res); return 1; }
    return 0;
}

int Net_ParseRelayAddr(const char* text, NetAddr* out)
{
    if(!text||!out) return 0;
    char tmp[96]; int i=0;
    for(; text[i] && i<94; i++) tmp[i]=text[i];
    tmp[i]=0;
    char* colon=strrchr(tmp,':');
    char* host=tmp; int port=NET_PORT;
    if(colon){ *colon=0; host=tmp; port=atoi(colon+1); if(port<=0||port>65535)port=NET_PORT; }
    // tolerate bracketed [host]
    int hl=(int)strlen(host);
    if(hl>2 && host[0]=='[' && host[hl-1]==']'){ host++; host[hl-2]=0; }
    unsigned int a=0;
    if(!resolveIPv4(host,&a)) return 0;
    memset(out,0,sizeof(*out));
    out->addr=ntohl(a); out->port=(uint16_t)port;
    return 1;
}

void Net_SetRelay(const char* ip, int port)
{
    Net_Init();
    unsigned int a=0;
    if(!ip || !resolveIPv4(ip,&a)){ gRelay=0; return; }
    memset(&gRelayAddr,0,sizeof gRelayAddr);
    gRelayAddr.sin_family=AF_INET; gRelayAddr.sin_addr.s_addr=a;
    gRelayAddr.sin_port=htons((uint16_t)port);
    gRelay=1; gRelayHb=0.0f;
}
void Net_SetRelayAddr(const NetAddr* a)
{
    if(!a) { gRelay=0; return; }
    Net_Init();
    memset(&gRelayAddr,0,sizeof gRelayAddr);
    gRelayAddr.sin_family=AF_INET;
    gRelayAddr.sin_addr.s_addr=htonl(a->addr);
    gRelayAddr.sin_port=htons(a->port);
    gRelay=1; gRelayHb=0.0f;
}
void Net_ClearRelay(void){ gRelay=0; }
int  Net_HasRelay(void){ return gRelay; }
void Net_RelayTick(float dt)
{
    if(!gRelay || gSock<0) return;
    gRelayHb-=dt;
    if(gRelayHb<=0.0f)
    {   gRelayHb=2.0f;                 // (re)register our NAT mapping on the relay
        sendto(gSock,"SKR",3,0,(struct sockaddr*)&gRelayAddr,sizeof(gRelayAddr)); }
}
