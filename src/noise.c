// noise.c
#include "noise.h"
#include <math.h>

static unsigned char perm[512];

static inline float fade(float t){ return t*t*t*(t*(t*6.0f-15.0f)+10.0f); }
static inline float lerp(float a,float b,float t){ return a+(b-a)*t; }

static float grad(int h, float x, float y)
{
    // 8 gradient directions
    static const float g[8][2] = {
        {1,1},{-1,1},{1,-1},{-1,-1},
        {1,0},{-1,0},{0,1},{0,-1}
    };
    h &= 7;
    return g[h][0]*x + g[h][1]*y;
}

void Noise_Seed(unsigned int seed)
{
    unsigned char p[256];
    for (int i=0;i<256;i++) p[i]=(unsigned char)i;
    // deterministic shuffle (xorshift PRNG)
    unsigned int s = seed ? seed : 1u;
    for (int i=255;i>0;i--)
    {
        s ^= s<<13; s ^= s>>17; s ^= s<<5;
        int j = (int)(s % (unsigned)(i+1));
        unsigned char t=p[i]; p[i]=p[j]; p[j]=t;
    }
    for (int i=0;i<512;i++) perm[i]=p[i&255];
}

float Noise_Perlin2(float x, float y)
{
    int X = (int)floorf(x) & 255;
    int Y = (int)floorf(y) & 255;
    x -= floorf(x);
    y -= floorf(y);
    float u = fade(x), v = fade(y);
    int aa = perm[perm[X  ]+Y  ];
    int ab = perm[perm[X  ]+Y+1];
    int ba = perm[perm[X+1]+Y  ];
    int bb = perm[perm[X+1]+Y+1];
    float x1 = lerp(grad(aa,x,   y),   grad(ba,x-1,y),   u);
    float x2 = lerp(grad(ab,x,   y-1), grad(bb,x-1,y-1), u);
    return lerp(x1,x2,v) * 1.4142f; // normalize approx to [-1,1]
}

float Noise_Fbm2(float x, float y, int oct, float lac, float gain)
{
    float amp=0.5f, freq=1.0f, sum=0.0f, norm=0.0f;
    for (int i=0;i<oct;i++)
    {
        sum += amp*Noise_Perlin2(x*freq, y*freq);
        norm += amp;
        amp *= gain;
        freq *= lac;
    }
    return sum/norm;
}

float Noise_Ridged2(float x, float y, int oct)
{
    float amp=0.5f, freq=1.0f, sum=0.0f, norm=0.0f;
    for (int i=0;i<oct;i++)
    {
        float n = 1.0f - fabsf(Noise_Perlin2(x*freq, y*freq));
        n *= n;
        sum += amp*n;
        norm += amp;
        amp *= 0.5f;
        freq *= 2.03f;
    }
    return (sum/norm)*2.0f-1.0f;
}
