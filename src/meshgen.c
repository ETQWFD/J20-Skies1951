// meshgen.c - hand-built indexed primitives, single GPU upload (proven path)
#include "meshgen.h"
#include <stdlib.h>
#include <math.h>

#ifndef PI
#define PI 3.14159265358979323846f
#endif

typedef struct {
    float *p,*n,*t; unsigned char*c; unsigned short*idx;
    int nv,ni,capv,capi;
} B;

static void bgrow(B*b)
{
    if(b->nv < b->capv) return;
    b->capv = b->capv? b->capv*2 : 64;
    b->p=(float*)realloc(b->p,b->capv*3*sizeof(float));
    b->n=(float*)realloc(b->n,b->capv*3*sizeof(float));
    b->t=(float*)realloc(b->t,b->capv*2*sizeof(float));
    b->c=(unsigned char*)realloc(b->c,b->capv*4);
}
static int vtx(B*b,float px,float py,float pz,float nx,float ny,float nz,float u,float v)
{
    bgrow(b); int i=b->nv++;
    b->p[i*3]=px;b->p[i*3+1]=py;b->p[i*3+2]=pz;
    b->n[i*3]=nx;b->n[i*3+1]=ny;b->n[i*3+2]=nz;
    b->t[i*2]=u;b->t[i*2+1]=v;
    b->c[i*4]=255;b->c[i*4+1]=255;b->c[i*4+2]=255;b->c[i*4+3]=255;
    return i;
}
static void tri(B*b,int x,int y,int z)
{
    if(b->ni+3 > b->capi){ b->capi=b->capi?b->capi*2:96; b->idx=(unsigned short*)realloc(b->idx,b->capi*sizeof(unsigned short)); }
    b->idx[b->ni++]= (unsigned short)x; b->idx[b->ni++]=(unsigned short)y; b->idx[b->ni++]=(unsigned short)z;
}
static void quad(B*b,int a,int bb,int c,int d){ tri(b,a,bb,c); tri(b,a,c,d); }

static void face(B*b,float cx,float cy,float cz,
                 float ux,float uy,float uz,float hu,
                 float vx,float vy,float vz,float hv,
                 float nx,float ny,float nz)
{
    int a=vtx(b,cx-ux*hu-vx*hv,cy-uy*hu-vy*hv,cz-uz*hu-vz*hv,nx,ny,nz,0,0);
    int c2=vtx(b,cx+ux*hu-vx*hv,cy+uy*hu-vy*hv,cz+uz*hu-vz*hv,nx,ny,nz,1,0);
    int d=vtx(b,cx+ux*hu+vx*hv,cy+uy*hu+vy*hv,cz+uz*hu+vz*hv,nx,ny,nz,1,1);
    int e=vtx(b,cx-ux*hu+vx*hv,cy-uy*hu+vy*hv,cz-uz*hu+vz*hv,nx,ny,nz,0,1);
    quad(b,a,c2,d,e);
}

Mesh MG_Box(float w,float h,float d)
{
    B b={0}; float hx=w*0.5f,hy=h*0.5f,hz=d*0.5f;
    face(&b, hx,0,0, 0,1,0,hy, 0,0,1,hz, 1,0,0);
    face(&b,-hx,0,0, 0,1,0,hy, 0,0,-1,hz,-1,0,0);
    face(&b,0, hy,0, 1,0,0,hx, 0,0,-1,hz, 0,1,0);
    face(&b,0,-hy,0, 1,0,0,hx, 0,0,1,hz, 0,-1,0);
    face(&b,0,0, hz, 1,0,0,hx, 0,1,0,hy, 0,0,1);
    face(&b,0,0,-hz,-1,0,0,hx, 0,1,0,hy, 0,0,-1);
    Mesh m={0};
    m.vertexCount=b.nv; m.triangleCount=b.ni/3;
    m.vertices=b.p; m.normals=b.n; m.texcoords=b.t; m.colors=b.c; m.indices=b.idx;
    UploadMesh(&m,false); return m;
}

Mesh MG_Cylinder(float r,float h,int seg)
{
    B b={0}; float y0=-h*0.5f,y1=h*0.5f;
    int*bot=(int*)malloc(seg*sizeof(int)),*top=(int*)malloc(seg*sizeof(int));
    for(int i=0;i<seg;i++)
    {
        float a=(float)i/seg*2*PI, x=cosf(a),z=sinf(a);
        bot[i]=vtx(&b,x*r,y0,z*r,x,0,z,(float)i/seg,0);
        top[i]=vtx(&b,x*r,y1,z*r,x,0,z,(float)i/seg,1);
    }
    for(int i=0;i<seg;i++){ int j=(i+1)%seg; quad(&b,bot[i],top[i],top[j],bot[j]); }
    int ct=vtx(&b,0,y1,0,0,1,0,0.5f,0.5f), cb=vtx(&b,0,y0,0,0,-1,0,0.5f,0.5f);
    for(int i=0;i<seg;i++){ int j=(i+1)%seg; tri(&b,ct,top[j],top[i]); tri(&b,cb,bot[i],bot[j]); }
    free(bot);free(top);
    Mesh m={0}; m.vertexCount=b.nv; m.triangleCount=b.ni/3;
    m.vertices=b.p;m.normals=b.n;m.texcoords=b.t;m.colors=b.c;m.indices=b.idx;
    UploadMesh(&m,false); return m;
}

Mesh MG_Cone(float r,float h,int seg)
{
    B b={0}; float y0=-h*0.5f,y1=h*0.5f;
    int*ring=(int*)malloc(seg*sizeof(int));
    for(int i=0;i<seg;i++){ float a=(float)i/seg*2*PI,x=cosf(a),z=sinf(a);
        float nl=sqrtf(x*x+z*z+ (r/h)*(r/h));
        ring[i]=vtx(&b,x*r,y0,z*r,x/nl, (r/h)/nl, z/nl,(float)i/seg,0); }
    int apex=vtx(&b,0,y1,0,0,1,0,0.5f,0.5f), cb=vtx(&b,0,y0,0,0,-1,0,0.5f,0.5f);
    for(int i=0;i<seg;i++){ int j=(i+1)%seg; tri(&b,apex,ring[j],ring[i]); tri(&b,cb,ring[i],ring[j]); }
    free(ring);
    Mesh m={0}; m.vertexCount=b.nv; m.triangleCount=b.ni/3;
    m.vertices=b.p;m.normals=b.n;m.texcoords=b.t;m.colors=b.c;m.indices=b.idx;
    UploadMesh(&m,false); return m;
}

Mesh MG_Sphere(float rad,int rings,int slices)
{
    B b={0};
    for(int j=0;j<=rings;j++)
    {
        float phi=(-PI*0.5f)+(PI)*(float)j/rings;
        float cp=cosf(phi),sp=sinf(phi);
        for(int i=0;i<=slices;i++)
        {
            float th=(float)i/slices*2*PI;
            float x=cp*cosf(th),y=sp,z=cp*sinf(th);
            vtx(&b,x*rad,y*rad,z*rad,x,y,z,(float)i/slices,(float)j/rings);
        }
    }
    for(int j=0;j<rings;j++)
        for(int i=0;i<slices;i++)
        {
            int a=j*(slices+1)+i, bb=(j+1)*(slices+1)+i, c=bb+1, d=a+1;
            quad(&b,a,bb,c,d);
        }
    Mesh m={0}; m.vertexCount=b.nv; m.triangleCount=b.ni/3;
    m.vertices=b.p;m.normals=b.n;m.texcoords=b.t;m.colors=b.c;m.indices=b.idx;
    UploadMesh(&m,false); return m;
}
