#include <jni.h>
#include <string.h>
#include <cstring>

#define LOG_NDEBUG 0
#define LOG_TAG "AmmboxJNI"
#include <utils/Log.h>
#include <android/log.h>
#include "sink/WifiDisplaySink.h"
#include <binder/ProcessState.h>
#include <binder/IServiceManager.h>
#include <gui/SurfaceComposerClient.h>
#include <media/AudioSystem.h>
#include <media/IMediaPlayerService.h>
#include <media/IRemoteDisplay.h>
#include <media/IRemoteDisplayClient.h>
#include <media/stagefright/DataSource.h>
#include <media/stagefright/foundation/ADebug.h>
#include <media/stagefright/foundation/AMessage.h>
#include <jni.h>
#include <android/native_window_jni.h>
#include <pthread.h>
#include <stdlib.h>  
#include <fcntl.h>  

namespace android {

struct tSinkParam {
 char ip[128];
 int port;
 ANativeWindow* window;
 char deviceName[128];
};

static unsigned int sinkId;
int kWhatStop = 2;
int kWhatWfdCmd = 3;
int kWhatWfdShow = 5;
int kWhatWfdSuspend = 6;
int kWhatWfdSetScale = 7;
int kWhatWfdTouchEvent = 8;
int kWhatWfdSetAudioPath = 11;
int kWhatWfdSetVolume = 12;
int kWhatWfdKeyEvent = 13;

static pthread_t threadId;
static tSinkParam  sinkParam;
static ANativeWindow* window = NULL;

static JavaVM *jvm = 0;
static jobject activity = 0;


static int stopIssued = 0;
static pthread_mutex_t stopMutex;
static int getStopIssued(void) {
  int ret = 0;
  pthread_mutex_lock(&stopMutex);
  ret = stopIssued;
  pthread_mutex_unlock(&stopMutex);
  return ret;
}

static void setStopIssued(int val) {
  pthread_mutex_lock(&stopMutex);
  stopIssued = val;
  pthread_mutex_unlock(&stopMutex);
}

void *sinkProc(void* arg)
{
    
    ALOGI("JNI:Ammbox thread ++++.");

    JNIEnv *env;
    int isAttached;

    isAttached = 0;
    env = NULL;
    
    if(jvm){
        
        if(jvm->AttachCurrentThread(&env, NULL) < 0){
            ALOGE("JNI:thread can not attach current thread." );
            return NULL;
        }

        isAttached = 1;
    }


    tSinkParam* sinkParam = (tSinkParam*)arg;

    char *ip = (char*)&sinkParam->ip[0];
    ANativeWindow* window = sinkParam->window;
    char *name = (char*)&sinkParam->deviceName[0];
    int port = sinkParam->port;
    
    
    sp<ALooper> looper;
    sp<WifiDisplaySink> sink;
    
    sp<ANetworkSession> session = new ANetworkSession;
    session->start();
    
    sink = new WifiDisplaySink(session);
    //if(sink != NULL)
    //    ALOGD("sink created.");

    looper = new ALooper;
    //if(looper != NULL)
    //    ALOGD("looper created.");

    looper->registerHandler(sink);
    
    
    sink->start(ip, port, window, name, jvm);
    
    sinkId = sink->id();
    //env->ReleaseStringUTFChars(ipaddr, ip);
    
    looper->start(false, false, ANDROID_PRIORITY_HIGHEST-10);

    int state = 0;
    while(1){
        usleep(50000);
        state = sink->getState();
        //ALOGI("JNI:state=%d.",state);
        if(state == 6) //disconnected
            break;
    }
    
    looper->unregisterHandler(sink->id());
    
    //looper->stop();
    
    sink->stop();
    
    /*
    ALOGD("ANativeWindow_lock ...");
    ANativeWindow_Buffer buffer;
    int lockResult = -1;
    int32_t w = ANativeWindow_getWidth(window);
    int32_t h = ANativeWindow_getHeight(window);
    lockResult = ANativeWindow_lock(window, &buffer, NULL);
    //ANativeWindow_setBuffersGeometry(window, w, h, WINDOW_FORMAT_RGBA_8888);
    if(lockResult == 0){
        ALOGD("ANativeWindow_lock in");
        memset(buffer.bits, 0,  w * h * 2);
        ANativeWindow_unlockAndPost(window);
    }
    */
    //ANativeWindow_release(window);
    //window = NULL;
    

    if(isAttached){
        jvm->DetachCurrentThread();
    }
    ALOGI("JNI:Ammbox thread --.");
    return (void*)NULL;
    
}

extern "C" void Java_com_antec_smartlink_miracast_AmmboxService_ammboxWfdSink(JNIEnv* env, jobject thiz,
                                                                 jstring ipaddr, jint port,jobject surface,jstring deviceName)
{
    ALOGI("JNI:ammboxWfdSink ++++");
    
    window = NULL;
    const char *ip = env->GetStringUTFChars(ipaddr, NULL);
    //window = ANativeWindow_fromSurface(env, surface);
    //if(window != NULL)
    //    ALOGD("window created.");

    const char *name = env->GetStringUTFChars(deviceName, NULL);
    memcpy(sinkParam.ip, ip, strlen(ip));
    sinkParam.window = window;
    sinkParam.port = port;
    memcpy(sinkParam.deviceName, name, strlen(name));
    setStopIssued(0);
    pthread_create(&threadId, NULL, &sinkProc, (void *)&sinkParam);
    env->ReleaseStringUTFChars(ipaddr, ip);
    env->ReleaseStringUTFChars(deviceName, name);
    
    
    //ALOGD("JNI:ammboxWfdSink --");
    
}

extern "C" void Java_com_antec_smartlink_miracast_AmmboxService_ammboxWfdSinkStop(JNIEnv* env, jobject thiz)
{
    ALOGI("JNI:ammboxWfdSinkStop ++");
    //void *ret;
    //setStopIssued(1);
    //pthread_join(threadId, &ret);
    sp<AMessage> msg = new AMessage(kWhatStop, sinkId);
    msg->post();
    
    ALOGI("JNI:ammboxWfdSinkStop --");
    
}

extern "C" void Java_com_antec_smartlink_miracast_AmmboxService_ammboxWfdSinkSuspend(JNIEnv* env, jobject thiz)
{
    ALOGI("JNI:ammboxWfdSinkSuspend ++");
        
    sp<AMessage> msg = new AMessage(kWhatWfdSuspend, sinkId);
    msg->post();
    
    
}


extern "C" void Java_com_antec_smartlink_miracast_AmmboxService_ammboxWfdSinkSetAudioPath(JNIEnv* env, jobject thiz,jint type)
{
    ALOGI("JNI:ammboxWfdSinkSetAudioPath ++");
        
    sp<AMessage> msg = new AMessage(kWhatWfdSetAudioPath, sinkId);
    msg->setInt32("audiopath", type);
    msg->post();
    
    
}

extern "C" void Java_com_antec_smartlink_miracast_AmmboxService_ammboxWfdSinkSetVolume(JNIEnv* env, jobject thiz,jint vol)
{
    ALOGI("JNI:ammboxWfdSinkSetVolume ++");
        
    sp<AMessage> msg = new AMessage(kWhatWfdSetVolume, sinkId);
    msg->setInt32("volume", vol);
    msg->post();
    
    
}

extern "C" void Java_com_antec_smartlink_miracast_AmmboxService_ammboxWfdSinkSetScale(JNIEnv* env, jobject thiz,jint type)
{
    ALOGI("JNI:ammboxWfdSinkSetScale ++");
        
    sp<AMessage> msg = new AMessage(kWhatWfdSetScale, sinkId);
    msg->setInt32("scale", type);
    msg->post();
    
    
}

extern "C" void Java_com_antec_smartlink_miracast_AmmboxService_ammboxWfdSinkCmd(JNIEnv* env, jobject thiz,jint cmd)
{
    sp<AMessage> msg = new AMessage(kWhatWfdCmd, sinkId);
    msg->setInt32("cmd", cmd);
    msg->post();
}

extern "C" void Java_com_antec_smartlink_miracast_AmmboxService_ammboxWfdSinkShow(JNIEnv* env, jobject thiz,jobject surface)
{
    ALOGI("JNI:ammboxWfdSinkShow ++");
    
    do{
        window = ANativeWindow_fromSurface(env, surface);
        usleep(5000);
        //ALOGI("JNI:ammboxWfdSinkShow : window=NULL.");
    } while(window == NULL);
    
    ALOGI("JNI:ammboxWfdSinkShow : window created.");

    sp<AMessage> msg = new AMessage(kWhatWfdShow, sinkId);
    msg->setPointer("window", (void*)window);
    msg->post();
}

extern "C" void Java_com_antec_smartlink_miracast_AmmboxService_ammboxWfdSinkTouchEvent(JNIEnv* env, jobject thiz,
                                                                             jint type, jint x, jint y, jint w, jint h)
{
    sp<AMessage> msg = new AMessage(kWhatWfdTouchEvent, sinkId);
    msg->setInt32("type", type);
    msg->setInt32("x", x);
    msg->setInt32("y", y);
    msg->setInt32("w", w);
    msg->setInt32("h", h);
    //ALOGI("JNI:TouchEvent:%d:%d:%d:%d",x,y,w,h);
    msg->post();
}

extern "C" void Java_com_antec_smartlink_miracast_AmmboxService_ammboxWfdSinkKeyEvent(JNIEnv* env, jobject thiz,
                                                                             jint type, jint keyCode1,jint keyCode2)
{
    sp<AMessage> msg = new AMessage(kWhatWfdKeyEvent, sinkId);
    msg->setInt32("type", type);
    msg->setInt32("keycode1", keyCode1);
    msg->setInt32("keycode2", keyCode2);
    //ALOGI("JNI:TouchEvent:%d:%d:%d:%d",x,y,w,h);
    msg->post();
}


extern "C" int JNI_OnLoad(JavaVM* vm, void* reserved)
{
    ALOGI("JNI_OnLoad++");
    JNIEnv *env;
    if(vm->GetEnv((void **)&env, JNI_VERSION_1_6) != JNI_OK){
        ALOGE( "Unable to get the env at JNI_OnLoad" );
        return -1;
    }
    
    jvm = vm;
    return JNI_VERSION_1_6;
    }


}  // namespace android

