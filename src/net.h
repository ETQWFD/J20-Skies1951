// net.h - tiny cross-platform, non-blocking UDP layer for LAN play.
// POSIX sockets on Linux/Android, Winsock2 on Windows. One socket per process.
#ifndef NET_H
#define NET_H

#include <stdint.h>

#define NET_PORT      55191
#define NET_MAXPKT    1400
#define NET_MAXPEERS  4

// portable endpoint (contents are platform-neutral; net.c maps to sockaddr)
typedef struct {
    uint32_t addr;   // network byte order IPv4
    uint16_t port;   // host byte order
    char     ip[32]; // dotted text for display
} NetAddr;

int   Net_Init(void);                 // call once (WSAStartup on Windows)
void  Net_Shutdown(void);
int   Net_Host(void);                 // bind 0.0.0.0:NETPORT, allow broadcast
int   Net_Client(void);               // bind ephemeral local port, allow broadcast
int   Net_IsOpen(void);
void  Net_Close(void);

int   Net_Poll(NetAddr*from,char*buf,int cap);   // non-blocking receive; returns bytes, 0 if none
void  Net_Send(const NetAddr*to,const char*buf,int len);
void  Net_Broadcast(const char*buf,int len);     // send to 255.255.255.255:NETPORT
int   Net_AddrEq(const NetAddr*a,const NetAddr*b);

// ---- optional public relay (frp/tunnel) support ---------------------------
// When a relay is set, discovery/keepalive is also sent to the relay's public
// UDP endpoint; a standalone Kanye relay fans every game packet out to all
// other registered players, so hosts/clients behind different NATs can play
// without being on the same LAN. Endpoints register via "SKR" heartbeats.
void  Net_SetRelay(const char* ip, int port);   // dotted text + host-order port
void  Net_SetRelayAddr(const NetAddr* a);        // pre-resolved endpoint
void  Net_ClearRelay(void);
int   Net_HasRelay(void);
void  Net_RelayTick(float dt);                  // call each frame; emits SKR
void  Net_RelayPing(void);                     // send one raw SKR now (RTT probe)
int   Net_ParseRelayAddr(const char* text, NetAddr* out); // "1.2.3.4:55191"

#endif
