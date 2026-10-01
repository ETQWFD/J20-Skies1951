// ui_native.c - see ui_native.h
#include "ui_native.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#if defined(PLATFORM_ANDROID)

#include <jni.h>
#include <android_native_app_glue.h>
#include <pthread.h>

struct android_app *GetAndroidApp(void);   // exported by raylib's android backend

#define IME_RING 256
static volatile int sHead=0, sTail=0;
static int sRing[IME_RING];
static volatile int sBack=0, sEnter=0;
static pthread_mutex_t sMtx=PTHREAD_MUTEX_INITIALIZER;
static char sFilesDir[260]={0};

// Called from the Android UI thread by NativeLoader's TextWatcher / editor action.
JNIEXPORT void JNICALL
Java_com_changkong1951_skies_NativeLoader_nativeImeChar(JNIEnv* e, jclass c, jint cp)
{
    (void)e;(void)c;
    pthread_mutex_lock(&sMtx);
    int nt=(sTail+1)%IME_RING;
    if(nt!=sHead){ sRing[sTail]=cp; sTail=nt; }
    pthread_mutex_unlock(&sMtx);
}
JNIEXPORT void JNICALL
Java_com_changkong1951_skies_NativeLoader_nativeImeKey(JNIEnv* e, jclass c, jint key)
{
    (void)e;(void)c;
    pthread_mutex_lock(&sMtx);
    if(key==8) sBack++;
    else if(key==10||key==66||key==13) sEnter++;
    pthread_mutex_unlock(&sMtx);
}

typedef struct { jmethodID open,close,setText,text,clipGet,clipSet,filesDir; } MIds;
static MIds sM; static int sReady=0;

static JNIEnv* attach(JavaVM**vmOut)
{
    struct android_app* a=GetAndroidApp();
    if(!a||!a->activity||!a->activity->vm) return 0;
    JavaVM* vm=a->activity->vm; *vmOut=vm;
    JNIEnv* env=0;
    if((*vm)->GetEnv(vm,(void**)&env,JNI_VERSION_1_4)==JNI_OK) return env;
    if((*vm)->AttachCurrentThread(vm,&env,0)!=JNI_OK) return 0;
    return env;
}
// returns env; *attached set if this call attached (must detach). clazz is
// the NativeLoader instance (a global ref owned by native_app_glue).
static JNIEnv* beginCall(JavaVM**vm,int*attached,jobject*act,jclass*cls)
{
    struct android_app* a=GetAndroidApp();
    if(!a||!a->activity||!a->activity->vm) return 0;
    JavaVM* v=a->activity->vm; *vm=v;
    JNIEnv* env=0; jint st=(*v)->GetEnv(v,(void**)&env,JNI_VERSION_1_4);
    if(st==JNI_EDETACHED){ if((*v)->AttachCurrentThread(v,&env,0)!=JNI_OK) return 0; *attached=1; }
    else if(st!=JNI_OK) return 0;
    jobject activity=a->activity->clazz;
    jclass c=(*env)->GetObjectClass(env,activity);
    if(!c){ if(*attached)(*v)->DetachCurrentThread(v); return 0; }
    if(!sReady){
        sM.open   =(*env)->GetMethodID(env,c,"imeOpen","(Ljava/lang/String;)V");
        sM.close  =(*env)->GetMethodID(env,c,"imeClose","()V");
        sM.setText=(*env)->GetMethodID(env,c,"imeSet","(Ljava/lang/String;)V");
        sM.text   =(*env)->GetMethodID(env,c,"imeText","()Ljava/lang/String;");
        sM.clipGet=(*env)->GetMethodID(env,c,"clipGet","()Ljava/lang/String;");
        sM.clipSet=(*env)->GetMethodID(env,c,"clipSet","(Ljava/lang/String;)V");
        sM.filesDir=(*env)->GetMethodID(env,c,"cfgDir","()Ljava/lang/String;");
        sReady=1;
    }
    *act=activity; *cls=c;
    return env;
}
static void endCall(JavaVM*vm,int attached){ if(attached)(*vm)->DetachCurrentThread(vm); }

static void callVoidStr(jmethodID m,const char*str)
{
    JavaVM*vm; int at=0; jobject act; jclass cls;
    JNIEnv* env=beginCall(&vm,&at,&act,&cls); if(!env||!m){ if(env&&cls)(*env)->DeleteLocalRef(env,cls); endCall(vm,at); return; }
    jstring js=(*env)->NewStringUTF(env,str?str:"");
    (*env)->CallVoidMethod(env,act,m,js);
    (*env)->DeleteLocalRef(env,js); (*env)->DeleteLocalRef(env,cls); endCall(vm,at);
}
static const char* callGetStr(jmethodID m,char*dst,int cap)
{
    JavaVM*vm; int at=0; jobject act; jclass cls;
    JNIEnv* env=beginCall(&vm,&at,&act,&cls);
    if(!env||!m){ if(env&&cls)(*env)->DeleteLocalRef(env,cls); endCall(vm,at); return dst?dst:""; }
    jstring js=(jstring)(*env)->CallObjectMethod(env,act,m);
    if(dst) dst[0]=0;
    if(js){ const char* cs=(*env)->GetStringUTFChars(env,js,0);
            if(cs&&dst){ strncpy(dst,cs,cap-1); dst[cap-1]=0; (*env)->ReleaseStringUTFChars(env,js,cs); }
            (*env)->DeleteLocalRef(env,js); }
    (*env)->DeleteLocalRef(env,cls); endCall(vm,at);
    return dst?dst:"";
}

void Skies_ConfigPath(char*out,int cap,const char*name)
{
    if(sFilesDir[0]==0) callGetStr(sM.filesDir,sFilesDir,sizeof sFilesDir);
    if(sFilesDir[0]) snprintf(out,cap,"%s/%s",sFilesDir,name);
    else snprintf(out,cap,"%s",name);
}
void Skies_IME_Open(const char*cur){ callVoidStr(sM.open,cur); }
void Skies_IME_Close(void)
{ JavaVM*vm; int at=0; jobject act; jclass cls;
  JNIEnv* env=beginCall(&vm,&at,&act,&cls); if(env&&sM.close)(*env)->CallVoidMethod(env,act,sM.close);
  if(env&&cls)(*env)->DeleteLocalRef(env,cls); endCall(vm,at); }
void Skies_IME_SetText(const char*t){ callVoidStr(sM.setText,t); }

int Skies_IME_Char(void)
{
    int cp=0;
    pthread_mutex_lock(&sMtx);
    if(sHead!=sTail){ cp=sRing[sHead]; sHead=(sHead+1)%IME_RING; }
    pthread_mutex_unlock(&sMtx);
    return cp;
}
int Skies_IME_BackspacePressed(void)
{   int v=0; pthread_mutex_lock(&sMtx); v=sBack; sBack=0; pthread_mutex_unlock(&sMtx); return v; }
int Skies_IME_EnterPressed(void)
{   int v=0; pthread_mutex_lock(&sMtx); v=sEnter; sEnter=0; pthread_mutex_unlock(&sMtx); return v; }

static char sClip[1024];
const char* Skies_ClipGet(void){ return callGetStr(sM.clipGet,sClip,sizeof sClip); }
void Skies_ClipSet(const char*t){ callVoidStr(sM.clipSet,t); }

#else  // ---------------- desktop ----------------

#include "raylib.h"

void  Skies_ConfigPath(char*out,int cap,const char*name){ snprintf(out,cap,"%s",name); }
void  Skies_IME_Open(const char*c){(void)c;}
void  Skies_IME_Close(void){}
void  Skies_IME_SetText(const char*t){(void)t;}
int   Skies_IME_Char(void){ return 0; }
int   Skies_IME_BackspacePressed(void){ return 0; }
int   Skies_IME_EnterPressed(void){ return 0; }
static char sClipDesk[1024];
const char* Skies_ClipGet(void)
{ const char*t=GetClipboardText(); sClipDesk[0]=0; if(t) strncpy(sClipDesk,t,sizeof sClipDesk-1); return sClipDesk; }
void Skies_ClipSet(const char*t){ if(t) SetClipboardText(t); }

#endif
