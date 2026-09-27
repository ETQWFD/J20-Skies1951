// coop.h - LAN cooperative session: lobby/discovery + session globals.
#ifndef COOP_H
#define COOP_H

#include "net.h"

extern int gCoopRole;      // 0 none/single-player, 1 host, 2 client
extern int gCoopId;        // host = 0, joined clients assigned 1..3
extern int gCoopScenario;  // 0 ridge, 1 Chosin night
extern NetAddr gCoopHost;  // client -> host endpoint (valid when role==2)

// run the LAN lobby loop. Returns:
//   0 = back to main menu (socket closed),
//   1 = host starts battle, 2 = client told to start (socket kept open)
int Coop_Lobby(void);

// host: receive discovery / join / heartbeat, maintain peer table
void Coop_HostPoll(float dt);
typedef struct {
    NetAddr addr; char name[40]; int scenario, players; float pingMs; float lastSeen;
} CoopRoom;
int  Coop_BeginSearch(void);
int  Coop_PollRooms(CoopRoom*out, int cap, float dt);
int  Coop_Join(const NetAddr*host);
int  Coop_JoinStatus(void);                             // 1 accepted,-1 waiting,0 failed
int  Coop_PeerCount(void);

// battle-time accessors
const NetAddr* Coop_HostAddr(void);
int  Coop_PeerAddr(int id, NetAddr*out);
void Coop_HostStartBattle(void);
int  Coop_ClientStart(int*scenario);
void Coop_Reset(void);

#endif
