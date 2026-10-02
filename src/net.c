// net.c - tiny cross-platform, non-blocking UDP layer for LAN + relay play.
//
// LAN: both host and client bind the well-known port NET_PORT (SO_REUSE* lets a
// later bind share it; falls back to an ephemeral port if busy). The host also
// PUSH-announces its room once a second, so discovery no longer depends on the
// client's limited-broadcast reaching the host (many Android hotspots / APs
// drop 255.255.255.255 one way). On POSIX we additionally enumerate every NIC's
// directed broadcast address (e.g. 192.168.43.255), which is what actually
// works on a phone hotspot.
//
// Remote relay (Kanye / frp): frp commonly multiplexes several remote players
// onto ONE local frpc flow, so the relay CANNOT trust the physical source
// address to tell players apart. Every relayed packet therefore carries an
// inner 15-bit SESSION id (one random id per socket). Kanye routes by session,
// not by IP:port, which is immune to source collapse and to NAT/frp roaming.
//   client -> relay : 'K' 0x02 [srcSess hi lo] [dstSess hi lo] payload (dst=FFFF broadcast)
//   relay -> client : 'K' 0x02 [srcSess hi lo] payload
#include "net.h"
#include <time.h>
#include <stdlib.h>
#include <stdio.h>

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
  #include <time.h>
  #if !defined(__EMSCRIPTEN__)
    #include <net/if.h>
    #include <ifaddrs.h>
  #endif
  #define NET_CLOSESOCK close
#endif

#define KA_VER     0x02
#define DST_BCAST  0xFFFF
#define REL_VMARK  0xF000   // virtual port block for relayed peers: 0xF000..0xF03F
#define REL_VMASK  0x003F

static int   gSock=-1;
static int   gWsa=0;

// public relay (Kanye) state
static int   gRelay=0;          // a relay endpoint is configured (may be pending DNS)
static int   gRelayResolved=0;  // configured endpoint resolved to a usable IPv4
static struct sockaddr_in gRelayAddr;
static char  gRelayHost[96]={0};// saved host: IPv4 literal OR domain (lazily resolved)
static uint16_t gRelayWPort=NET_PORT;
static float gRelayHb=0.0f;
static float gDnsRetry=0.0f;    // seconds until the next DNS attempt while unresolved
static uint16_t gLocalPort=0;
static uint16_t gSession=1;     // our own inner session id (1..0x7FFF)

static int relayOn(void){ return gRelay && gRelayResolved; }

// virtual relayed peers: a session learned from an incoming packet is mapped to
// a stable local handle; NetAddr.port = REL_VMARK | handle.
typedef struct { uint16_t sess; uint32_t rip; int used; } VPeer;
static VPeer gVP[NET_MAXPEERS+4];
static int   gVPN=0;

#if !defined(_WIN32) && !defined(__EMSCRIPTEN__)
static uint32_t gBcast[12]; static int gBcastN=0; static double gBcastT=-100.0;
#endif

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

static uint16_t makeSession(void)
{
    static uint32_t s=0;
    if(!s)
    {   s=(uint32_t)time(NULL)*2654435761u;
#if !defined(_WIN32)
        s ^= (uint32_t)getpid()*2246822519u;
#endif
        s ^= (uint32_t)gLocalPort*40503u + 0x9E3779B1u;
    }
    s ^= s<<13; s ^= s>>17; s ^= s<<5;
    uint16_t r=(uint16_t)(s & 0x7FFF);
    return r?r:1;
}

static int makeNonBlock(int s)
{
#if defined(_WIN32)
    unsigned long nb=1; return ioctlsocket(s,FIONBIO,&nb)==0;
#else
    int fl=fcntl(s,F_GETFL,0); return fcntl(s,F_SETFL,fl|O_NONBLOCK)>=0;
#endif
}

static int bindSock(int s,int bindWellKnown)
{
    struct sockaddr_in a; memset(&a,0,sizeof(a));
    a.sin_family=AF_INET; a.sin_addr.s_addr=htonl(INADDR_ANY);
    a.sin_port=htons(bindWellKnown?NET_PORT:0);
    if(bind(s,(struct sockaddr*)&a,sizeof(a))<0) return 0;
    makeNonBlock(s);
    { struct sockaddr_in la; socklen_t ll=sizeof(la);
      if(getsockname(s,(struct sockaddr*)&la,&ll)==0) gLocalPort=ntohs(la.sin_port); }
    return 1;
}

static int openSock(int wantWellKnown)
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

    gLocalPort=0;
    int ok=0;
    if(wantWellKnown)
    {   // share the discovery port; only fall back to ephemeral if it is busy
        // without REUSEPORT support (e.g. another local socket).
        ok=bindSock(s,1) || bindSock(s,0);
    }
    else ok=bindSock(s,0);
    if(!ok){ NET_CLOSESOCK(s); return 0; }
    gSock=s; gVPN=0; memset(gVP,0,sizeof(gVP));
    gSession=makeSession();
    return 1;
}

int Net_Host(void){ Net_Init(); return openSock(1); }
int Net_Client(void){ Net_Init(); return openSock(1); } // client listens on 55191 too
int Net_IsOpen(void){ return gSock>=0; }

void Net_Close(void)
{
    if(gSock>=0){ NET_CLOSESOCK(gSock); gSock=-1; }
    gVPN=0; gRelay=0; gRelayResolved=0; gRelayHost[0]=0; gRelayHb=0.0f;
}

static void putTextIp(char*dst,unsigned int netAddr)
{
    struct in_addr ia; ia.s_addr=netAddr;
    const char*ip=inet_ntoa(ia);
    int i=0; if(ip){ for(;ip[i]&&i<31;i++)dst[i]=ip[i]; dst[i]=0; } else dst[0]=0;
}

// ---- virtual session handles for relayed peers ----------------------------
static int vpHandle(uint32_t ripHostOrder,uint16_t sess)
{
    for(int i=0;i<gVPN;i++) if(gVP[i].used && gVP[i].rip==ripHostOrder && gVP[i].sess==sess)
        return i;
    if(gVPN<(int)(sizeof(gVP)/sizeof(gVP[0])))
    { int i=gVPN++; gVP[i].used=1; gVP[i].sess=sess; gVP[i].rip=ripHostOrder; return i; }
    return -1;
}

int Net_Poll(NetAddr*from,char*buf,int cap)
{
    if(gSock<0) return 0;
    struct sockaddr_in sa; socklen_t sl=sizeof(sa);
    static unsigned char raw[NET_MAXPKT+16];
    int n=recvfrom(gSock,(char*)raw,sizeof(raw)-1,0,(struct sockaddr*)&sa,&sl);
    if(n<=0) return 0;

    // relayed packet: 'K' VER [srcSess hi lo] payload   (5-byte header)
    if(relayOn() && n>=6 && raw[0]=='K' && raw[1]==KA_VER &&
       sa.sin_addr.s_addr==gRelayAddr.sin_addr.s_addr)
    {
        uint16_t srcSess=(uint16_t)((raw[2]<<8)|raw[3]);
        int payload=n-5;
        if(payload<=0 || srcSess==0 || srcSess==0xFFFF) return 0;
        uint32_t ripNet=gRelayAddr.sin_addr.s_addr;
        int h=vpHandle((uint32_t)ntohl(ripNet),srcSess);
        if(h<0) return 0;
        if(from){
            memset(from,0,sizeof(*from));
            from->addr=ntohl(ripNet);
            from->port=(uint16_t)(REL_VMARK|(h&REL_VMASK));
            putTextIp(from->ip,ripNet);
        }
        if(payload>cap) payload=cap;
        memcpy(buf,raw+5,payload);
        return payload;
    }
    if(n>cap) n=cap;
    memcpy(buf,raw,n);
    if(from)
    {
        memset(from,0,sizeof(*from));
        from->addr=ntohl(sa.sin_addr.s_addr);
        from->port=ntohs(sa.sin_port);
        putTextIp(from->ip,sa.sin_addr.s_addr);
    }
    return n;
}

static void u16be(unsigned char*p,uint16_t v){ p[0]=(unsigned char)(v>>8); p[1]=(unsigned char)(v&255); }

// 6-byte header to the relay: 'K' VER srcH srcL dstH dstL ; payload follows.
static void relaySend(uint16_t dstSess,const char*buf,int len)
{
    if(!relayOn()||gSock<0) return;
    unsigned char env[NET_MAXPKT+8];
    env[0]='K'; env[1]=KA_VER; u16be(env+2,gSession); u16be(env+4,dstSess);
    int m=len; if(m>NET_MAXPKT) m=NET_MAXPKT;
    memcpy(env+6,buf,m);
    sendto(gSock,(const char*)env,m+6,0,(struct sockaddr*)&gRelayAddr,sizeof(gRelayAddr));
}

void Net_Send(const NetAddr*to,const char*buf,int len)
{
    if(gSock<0||!to) return;
    struct sockaddr_in sa;
    memset(&sa,0,sizeof(sa));
    sa.sin_family=AF_INET;
    sa.sin_addr.s_addr=htonl(to->addr);
    sa.sin_port=htons(to->port);

    // virtual relayed peer handle? tunnel by inner session id.
    if(relayOn() && (to->port & 0xF000)==REL_VMARK &&
       sa.sin_addr.s_addr==gRelayAddr.sin_addr.s_addr)
    {
        int h=to->port & REL_VMASK;
        if(h<gVPN && gVP[h].used) relaySend(gVP[h].sess,buf,len);
        return;
    }
    sendto(gSock,buf,len,0,(struct sockaddr*)&sa,sizeof(sa));
}

#if !defined(_WIN32) && !defined(__EMSCRIPTEN__)
static void enumBcast(void)
{
    double now=0;
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC,&ts); now=ts.tv_sec+ts.tv_nsec/1e9;
    if(now-gBcastT<8.0) return;
    gBcastT=now; gBcastN=0;
    struct ifaddrs*ifa0=0;
    if(getifaddrs(&ifa0)!=0) return;
    for(struct ifaddrs*p=ifa0;p;p=p->ifa_next)
    {
        if(!p->ifa_addr||p->ifa_addr->sa_family!=AF_INET) continue;
        if(!p->ifa_broadaddr) continue;
        if(!(p->ifa_flags&IFF_UP)||(p->ifa_flags&IFF_LOOPBACK)||
           !(p->ifa_flags&IFF_BROADCAST)) continue;
        uint32_t b=((struct sockaddr_in*)p->ifa_broadaddr)->sin_addr.s_addr;
        int dup=0; for(int i=0;i<gBcastN;i++) if(gBcast[i]==b){dup=1;break;}
        if(!dup && gBcastN<(int)(sizeof(gBcast)/sizeof(gBcast[0])))
            gBcast[gBcastN++]=b;
    }
    freeifaddrs(ifa0);
}
#endif

void Net_Broadcast(const char*buf,int len)
{
    if(gSock<0) return;
    struct sockaddr_in sa; memset(&sa,0,sizeof(sa));
    sa.sin_family=AF_INET; sa.sin_port=htons(NET_PORT);

    // limited broadcast
    sa.sin_addr.s_addr=inet_addr("255.255.255.255");
    sendto(gSock,buf,len,0,(struct sockaddr*)&sa,sizeof(sa));
#if !defined(_WIN32) && !defined(__EMSCRIPTEN__)
    // one directed broadcast per NIC (this is what phone hotspots honour)
    enumBcast();
    for(int i=0;i<gBcastN;i++)
    { sa.sin_addr.s_addr=gBcast[i];
      sendto(gSock,buf,len,0,(struct sockaddr*)&sa,sizeof(sa)); }
#endif
    if(gRelay) relaySend(DST_BCAST,buf,len);
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
    char host[80]; int port=NET_PORT;
    if(!Net_ParseRelayText(text,host,sizeof host,&port)) return 0;
    unsigned int a=0;
    if(!resolveIPv4(host,&a)) return 0;   // this variant resolves immediately
    memset(out,0,sizeof(*out));
    out->addr=ntohl(a); out->port=(uint16_t)port;
    return 1;
}

// Trim whitespace, strip a leading scheme and a trailing path, then validate
// the host:port SHAPE only (no DNS). Host may be a dotted IPv4 or a domain.
int Net_ParseRelayText(const char* text, char* host, int hostCap, int* portOut)
{
    if(!text||!host||hostCap<=0) return 0;
    char tmp[100]; int n=0;
    for(const unsigned char*p=(const unsigned char*)text; *p && n<98; p++)
    { unsigned char c=*p; if(c!=' '&&c!='\t'&&c!='\n'&&c!='\r') tmp[n++]= (char)c; }
    tmp[n]=0;
    if(!n) return 0;
    // drop scheme: udp://, http://, frp:// ...
    char*p=tmp; char*sc=strstr(p,"://"); if(sc) p=sc+3;
    while(*p=='/') p++;
    // cut a trailing path / query
    char*slash=strpbrk(p,"/?"); if(slash)*slash=0;
    // split host:port (support [v6-literal] loosely, though play is IPv4)
    char*h=p; int port=NET_PORT;
    int hl=(int)strlen(h);
    if(hl>2 && h[0]=='[' && h[hl-1]==']'){ h++; h[hl-2]=0; }
    else { char*colon=strrchr(h,':');
           if(colon){ *colon=0; const char*pp=colon+1;
               for(const char*z=pp;*z;z++) if(*z<'0'||*z>'9'){ return 0; } // port must be digits
               int v=atoi(pp); if(v<=0||v>65535) return 0; port=v; } }
    // strip a leading @ (user@host) just in case
    char*at=strrchr(h,'@'); if(at) h=at+1;
    hl=(int)strlen(h);
    if(hl<1||hl>hostCap-1||hl>253) return 0;
    // split into labels
    char lab[64][64]; int nl=0, li=0;
    for(int i=0;i<=hl;i++){
        if(i==hl||h[i]=='.'){ if(li==0||li>63) return 0; lab[nl][li]=0; nl++; li=0; if(i==hl)break; }
        else { char c=h[i];
            if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='-')) return 0;
            if(li==0&&c=='-') return 0;               // no label starting with '-'
            if(li>0&&c=='-'&&(i+1==hl||h[i+1]=='.')) return 0; // no label ending with '-'
            if(li<63) lab[nl][li++]=c; }
    }
    int allNumeric=1, anyLetter=0;
    for(int i=0;i<nl;i++) for(int j=0;lab[i][j];j++){
        char c=lab[i][j];
        if((c>='a'&&c<='z')||(c>='A'&&c<='Z')){ allNumeric=0; anyLetter=1; } }
    if(allNumeric)
    {   if(nl!=4) return 0;
        for(int i=0;i<4;i++){ int v=0; for(int j=0;lab[i][j];j++) v=v*10+(lab[i][j]-'0');
            if(v>255) return 0; }
    }
    else
    {   if(nl<2||!anyLetter) return 0;
        int tldLetter=0; for(int j=0;lab[nl-1][j];j++){ char c=lab[nl-1][j];
            if((c>='a'&&c<='z')||(c>='A'&&c<='Z')) tldLetter=1; }
        if(!tldLetter) return 0;   // TLD can't be all digits
    }
    for(int i=0;i<hl;i++) host[i]=h[i]; host[hl]=0;
    if(portOut)*portOut=port;
    return 1;
}

static void applyResolved(unsigned int netAddr,uint16_t port)
{
    Net_Init();
    memset(&gRelayAddr,0,sizeof gRelayAddr);
    gRelayAddr.sin_family=AF_INET; gRelayAddr.sin_addr.s_addr=netAddr;
    gRelayAddr.sin_port=htons(port);
    gRelayResolved=1; gRelayHb=0.0f;
}
static void setRelayRaw(unsigned int netAddr,uint16_t port)
{
    Net_Init();
    gRelay=1; gRelayHost[0]=0; gDnsRetry=0.0f;
    applyResolved(netAddr,port);
}
void Net_SetRelay(const char* ip, int port)
{
    unsigned int a=0;
    if(!ip||!resolveIPv4(ip,&a)){ gRelay=0; gRelayResolved=0; return; }
    setRelayRaw(a,(uint16_t)port);
}
void Net_SetRelayAddr(const NetAddr* a)
{
    if(!a){ gRelay=0; gRelayResolved=0; return; }
    setRelayRaw(htonl((uint32_t)a->addr),a->port);
}

// Configure from a saved "host:port" string. Accepts domains; DNS is deferred to
// Net_RelayTick so adding/starting never blocks and tolerates brief offline.
void Net_SetRelayText(const char* text)
{
    char host[80]; int port=NET_PORT;
    if(!Net_ParseRelayText(text,host,sizeof host,&port)){ gRelay=0; gRelayResolved=0; return; }
    // Idempotent: callers (host lobby) re-apply every frame; don't reset DNS /
    // do a blocking getaddrinfo on every frame when the endpoint is unchanged.
    char want[100]; snprintf(want,sizeof want,"%s:%d",host,port);
    static char lastSet[100]={0};
    if(gRelay && gRelayResolved && strcmp(lastSet,want)==0) return;
    Net_Init();
    gRelay=1; gRelayResolved=0; gRelayHb=0.0f; gDnsRetry=0.0f;
    strncpy(gRelayHost,host,sizeof gRelayHost-1); gRelayHost[sizeof gRelayHost-1]=0;
    gRelayWPort=(uint16_t)port;
    strncpy(lastSet,want,sizeof lastSet-1); lastSet[sizeof lastSet-1]=0;
}
void Net_ClearRelay(void){ gRelay=0; gRelayResolved=0; gRelayHost[0]=0; }
int  Net_HasRelay(void){ return gRelay; }
int  Net_RelayReady(void){ return relayOn(); }

// raw heartbeat directly to the relay (Kanye answers a bare SKO for RTT ping)
static void relayRaw(const char*s,int n){
    if(!relayOn()||gSock<0) return;
    sendto(gSock,s,n,0,(struct sockaddr*)&gRelayAddr,sizeof(gRelayAddr));
}
void Net_RelayPing(void){ relayRaw("SKR",3); }
void Net_RelayTick(float dt)
{
    if(!gRelay || gSock<0) return;
    if(!gRelayResolved && gRelayHost[0])
    {
        gDnsRetry-=dt;
        if(gDnsRetry<=0.0f)
        {   unsigned int a=0;
            if(resolveIPv4(gRelayHost,&a)) applyResolved(a,gRelayWPort);
            else gDnsRetry=2.0f;   // retry every 2s; UI shows "解析/连接中"
        }
        return;
    }
    if(!gRelayResolved) return;
    gRelayHb-=dt;
    if(gRelayHb<=0){ gRelayHb=2.0f; relayRaw("SKR",3); }
}
