// noise.h - Perlin(improved gradient) noise + fBm, 2D. Public domain style implementation.
#ifndef NOISE_H
#define NOISE_H

void  Noise_Seed(unsigned int seed);
float Noise_Grad2(int x, int y);                 // hashed gradient dot, integer lattice
float Noise_Perlin2(float x, float y);           // smoothed perlin, ~[-1,1]
float Noise_Fbm2(float x, float y, int oct, float lac, float gain); // fractal brownian
float Noise_Ridged2(float x, float y, int oct);  // ridged fbm for mountain crests

#endif
