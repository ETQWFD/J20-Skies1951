// bnet.c - encode/decode for host-authoritative ground co-op.
#include "bnet.h"
#include <string.h>

static void w8(char**p,uint8_t v){ **(uint8_t**)p=v; (*p)++; }
static void w16(char**p,int16_t v){ uint8_t b[2]={(uint8_t)(v&255),(uint8_t)(v>>8)}; memcpy(*p,b,2); *p+=2; }
static void w32(char**p,uint32_t v){ uint8_t b[4]={(uint8_t)v,(uint8_t)(v>>8),(uint8_t)(v>>16),(uint8_t)(v>>24)}; memcpy(*p,b,4); *p+=4; }
static uint8_t  r8 (const char**p){ uint8_t v=*(const uint8_t*)*p; (*p)++; return v; }
static int16_t  r16(const char**p){ const uint8_t*q=(const uint8_t*)*p; int16_t v=(int16_t)(q[0]|(q[1]<<8)); *p+=2; return v; }
static uint32_t r32(const char**p){ const uint8_t*q=(const uint8_t*)*p; uint32_t v=(uint32_t)q[0]|((uint32_t)q[1]<<8)|((uint32_t)q[2]<<16)|((uint32_t)q[3]<<24); *p+=4; return v; }

static int16_t encAng(float a){ int v=(int)(a*1000.0f); if(v<-3141)v=-3141; if(v>3141)v=3141; return (int16_t)v; }
static float   decAng(int16_t c){ return c/1000.0f; }

int Bn_EncodeInput(char*out,const BnInput*in)
{
    char*p=out;
    w8(&p,'S'); w8(&p,'K'); w8(&p,'I'); w8(&p,in->id);
    w32(&p,in->seq);
    float f;
    f=in->yaw;   memcpy(p,&f,4); p+=4;
    f=in->pitch; memcpy(p,&f,4); p+=4;
    w8(&p,(uint8_t)in->ax); w8(&p,(uint8_t)in->ay);
    w8(&p,in->bits); w8(&p,in->weapon);
    return (int)(p-out);
}
int Bn_DecodeInput(const char*buf,int n,BnInput*out)
{
    if(n<(int)sizeof(BnInput)||buf[0]!='S'||buf[1]!='K'||buf[2]!='I')return 0;
    const char*p=buf+3;
    out->id=r8(&p);
    out->seq=r32(&p);
    memcpy(&out->yaw,p,4); p+=4;
    memcpy(&out->pitch,p,4); p+=4;
    out->ax=(int8_t)r8(&p); out->ay=(int8_t)r8(&p);
    out->bits=r8(&p); out->weapon=r8(&p);
    return out->id>=1 && out->id<BNET_MAXPLY;
}

static void putSoldier(char**p,const BnSoldier*s)
{
    float f;
    f=s->x; memcpy(*p,&f,4); *p+=4;
    f=s->z; memcpy(*p,&f,4); *p+=4;
    w16(p,s->angC); w8(p,(uint8_t)s->hp); w8(p,s->state);
}
static BnSoldier getSoldier(const char**p)
{
    BnSoldier s; memcpy(&s.x,*p,4); *p+=4; memcpy(&s.z,*p,4); *p+=4;
    s.angC=r16(p); s.hp=(int8_t)r8(p); s.state=r8(p); return s;
}
static void putPlayer(char**p,const BnPlayer*s)
{
    w8(p,s->id);
    float f; f=s->x;memcpy(*p,&f,4);*p+=4; f=s->y;memcpy(*p,&f,4);*p+=4; f=s->z;memcpy(*p,&f,4);*p+=4;
    w16(p,s->yawC); w8(p,(uint8_t)s->hp); w8(p,s->state); w8(p,s->weapon);
}
static BnPlayer getPlayer(const char**p)
{
    BnPlayer s; s.id=r8(p);
    memcpy(&s.x,*p,4);*p+=4; memcpy(&s.y,*p,4);*p+=4; memcpy(&s.z,*p,4);*p+=4;
    s.yawC=r16(p); s.hp=(int8_t)r8(p); s.state=r8(p); s.weapon=r8(p); return s;
}

int Bn_EncodeWorld(char*out,int cap,const BnWorld*in)
{
    char*p=out;
    w8(&p,'S');w8(&p,'K');w8(&p,'W');
    w8(&p,in->flags);w8(&p,in->scenario);w8(&p,in->win);w8(&p,in->planted);
    w8(&p,(uint8_t)in->myHp);w8(&p,(uint8_t)in->myState);
    w8(&p,in->foeN);w8(&p,in->palN);w8(&p,in->plyN);
    w16(&p,in->foesAlive);w16(&p,in->palsAlive);
    for(int i=0;i<BNET_NF;i++) putSoldier(&p,&in->foes[i]);
    for(int i=0;i<BNET_NP;i++) putSoldier(&p,&in->pals[i]);
    for(int i=0;i<BNET_MAXPLY;i++) putPlayer(&p,&in->players[i]);
    int used=(int)(p-out);
    return used<=cap?used:0;
}
int Bn_DecodeWorld(const char*buf,int n,BnWorld*out)
{
    int need=4+4+2+ BNET_NF*(4+4+2+1+1)+ BNET_NP*(4+4+2+1+1)+ BNET_MAXPLY*(1+12+2+1+1+1);
    if(n<need || buf[0]!='S'||buf[1]!='K'||buf[2]!='W')return 0;
    const char*p=buf+3;
    out->flags=r8(&p);out->scenario=r8(&p);out->win=r8(&p);out->planted=r8(&p);
    out->myHp=(int8_t)r8(&p);out->myState=r8(&p);
    out->foeN=r8(&p);out->palN=r8(&p);out->plyN=r8(&p);
    out->foesAlive=r16(&p);out->palsAlive=r16(&p);
    for(int i=0;i<BNET_NF;i++) out->foes[i]=getSoldier(&p);
    for(int i=0;i<BNET_NP;i++) out->pals[i]=getSoldier(&p);
    for(int i=0;i<BNET_MAXPLY;i++) out->players[i]=getPlayer(&p);
    return 1;
}
