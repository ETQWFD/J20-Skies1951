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
    gSock=s; gPeerN=0;
    return 1;
}

int Net_Host(void){ Net_Init(); return openSock(1); }
int Net_Client(void){ Net_Init(); return openSock(0); }
int Net_IsOpen(void){ return gSock>=0; }

void Net_Close(void)
{
    if(gSock>=0){ NET_CLOSESOCK(gSock); gSock=-1; }
    gPeerN=0;
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
    int n=recvfrom(gSock,buf,cap>NET_MAXPKT?NET_MAXPKT:cap,0,(struct sockaddr*)&sa,&sl);
    if(n<=0) return 0;
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
    sendto(gSock,buf,len,0,(struct sockaddr*)&sa,sizeof(sa));
}

void Net_Broadcast(const char*buf,int len)
{
    if(gSock<0) return;
    struct sockaddr_in sa; memset(&sa,0,sizeof(sa));
    sa.sin_family=AF_INET; sa.sin_port=htons(NET_PORT);
    sa.sin_addr.s_addr=inet_addr("255.255.255.255");
    sendto(gSock,buf,len,0,(struct sockaddr*)&sa,sizeof(sa));
}
