// bnet.h - compact binary protocol for host-authoritative ground co-op.
// All multi-byte values are little-endian; float32 is IEEE-754 (x86/arm LE).
#ifndef BNET_H
#define BNET_H

#include <stdint.h>

#define BNET_PORT   55191
#define BNET_NF     40     // must match ground.c NF
#define BNET_NP     12     // must match ground.c NP
#define BNET_MAXPLY 4      // host id 0 + clients id 1..3

// client -> host: this frame's intent
#pragma pack(push,1)
typedef struct {
    uint8_t  magic[3];    // 'S','K','I'
    uint8_t  id;          // assigned player id (1..3)
    uint32_t seq;
    float    yaw, pitch;
    int8_t   ax, ay;      // move stick -100..100 (strafe, forward)
    uint8_t  bits;        // see BN_B_*
    uint8_t  weapon;      // 0..3
} BnInput;
#pragma pack(pop)
#define BN_B_FIRE    1
#define BN_B_JUMP    2    // edge
#define BN_B_RELOAD  4    // edge
#define BN_B_ADS     8
#define BN_B_GRE     16   // edge
#define BN_B_SPRINT  32
#define BN_B_MELEE   64   // edge (punch/slash)

#pragma pack(push,1)
typedef struct { float x,z; int16_t angC; int8_t hp; uint8_t state; } BnSoldier; // state:1 alive,2 down
typedef struct { uint8_t id; float x,y,z; int16_t yawC; int8_t hp; uint8_t state; uint8_t weapon; } BnPlayer;
typedef struct {
    uint8_t magic[3];     // 'S','K','W'
    uint8_t flags;        // bit0 started
    uint8_t scenario;
    uint8_t win;          // 0 ongoing, 1 victory, 2 player dead
    uint8_t planted;      // flag planted ceremony
    int8_t  myHp;         // HP of the addressed client (its own avatar)
    int8_t  myState;      // own alive state
    uint8_t foeN, palN, plyN;
    int16_t foesAlive, palsAlive;
    BnSoldier foes[BNET_NF];
    BnSoldier pals[BNET_NP];
    BnPlayer  players[BNET_MAXPLY];
} BnWorld;
#pragma pack(pop)

int Bn_EncodeInput(char*out,const BnInput*in);               // returns bytes
int Bn_DecodeInput(const char*buf,int n,BnInput*out);        // 1 if valid
int Bn_EncodeWorld(char*out,int cap,const BnWorld*in);
int Bn_DecodeWorld(const char*buf,int n,BnWorld*out);

#endif
