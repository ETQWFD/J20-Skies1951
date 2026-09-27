// coop.h - LAN cooperative session: lobby/discovery + session globals.
#ifndef COOP_H
#define COOP_H

#include "net.h"

extern int gCoopRole;      // 0 none/single-player, 1 host, 2 client
extern int gCoopId;        // host = 0, joined clients assigned 1..3
extern int gCoopScenario;  // 0 ridge, 1 Chosin night
extern NetAddr gCoopHost;  // client -> host endpoint (valid when role==2)

// run the LAN lobby loop. returns:
//   0 = back to main menu, 1 = connected (role/id/scenario filled)
int Coop_Lobby(void);

// host: receive discovery / join / heartbeat, maintain peer table
void Coop_HostPoll(float dt);
// client: broadcast discovery, return number of discovered hosts found
typedef struct {
    NetAddr addr; char name[40]; int scenario, players; float pingMs; float lastSeen;
} CoopRoom;
int  Coop_BeginSearch(void);
int  Coop_PollRooms(CoopRoom*out, int cap, float dt);   // fills rooms, returns count
int  Coop_Join(const NetAddr*host);                     // send join; assigns id
int  Coop_JoinStatus(void);                             // 1 accepted,-1 waiting,0 failed
int  Coop_PeerCount(void);

#endif
