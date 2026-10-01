#ifndef GUNS_DATA_H
#define GUNS_DATA_H
#include <stdint.h>

typedef struct { int vcount,icount,use32,tex; const float*pos; const float*nrm; const float*uv; const uint16_t*idx16; const uint32_t*idx32; } GSubData;
typedef struct { int nsub,ntex; const GSubData*sub; const unsigned char*const*tex; const int*texlen; } GGunData;
extern const GGunData G_RIFLE,G_AKM,G_KNIFE;
#endif
