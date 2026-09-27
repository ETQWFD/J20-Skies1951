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

#endif
