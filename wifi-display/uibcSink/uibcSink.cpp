#define LOG_TAG "uibcSink"


#undef NDEBUG 
#define LOG_NDEBUG   0
#define LOG_NIDEBUG  0
#define LOG_NDDEBUG 0
#define LOG_NEDEBUG 0
#include <utils/Log.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <dirent.h>
#include <netinet/ether.h>
#include <netdb.h>
#include <linux/if_packet.h>
#include <netinet/if_ether.h>
#include <linux/if_arp.h>
#include <net/if.h>
#include <netutils/ifc.h>
#include <media/stagefright/foundation/hexdump.h>
#include <media/stagefright/foundation/AMessage.h>
#include <android/input.h>
#include <android/sensor.h>
#include "uibcSink.h"

namespace android {

int stopIssued = 0;
pthread_mutex_t stopMutex;

int getStopIssued(void) {
  int ret = 0;
  pthread_mutex_lock(&stopMutex);
  ret = stopIssued;
  pthread_mutex_unlock(&stopMutex);
  return ret;
}

void setStopIssued(int val) {
  pthread_mutex_lock(&stopMutex);
  stopIssued = val;
  pthread_mutex_unlock(&stopMutex);
}


void  uibcSink::startThread(void)
{
    uibcSinkPtr t = &uibcSink::threadLoop;
    PthreadPtr p = *(PthreadPtr*)&t;
    pthread_t    tid;
    if(pthread_create(&tid, 0, p, this) == 0){
        pthread_detach(tid);
    }
}




status_t uibcSink::start(const char *host,unsigned port)
{
	ALOGI("uibcSink::start.");
    setStopIssued(0);
    memset(remoteHost, 0, sizeof(remoteHost));
    strcpy(remoteHost,host);
    this->port = port;
    startThread();

    /*
    usleep(1000000);
    do{
        if(connectUIBC(remoteHost,port) != OK)
            usleep(500000);
        else
            break;

    }while(1);
    */
	return OK;
	
}

void uibcSink::stop()
{
    close(mSocket);
    setStopIssued(1);
    
}

void uibcSink::sendTouchEvent(int type, int x, int y)
{
    ssize_t n; 
    ssize_t size;
    unsigned char buf[32];

    buf[0] = 0;
    buf[1] = 0; //0:generic , 1:HIDC 
    //set packet length
    buf[2] = 0;
    buf[3] = 13;
    //set UIBC input body
    buf[4] = type;
    //body length
    buf[5] = 0;
    buf[6] = 6;
    //describe
    buf[7] = 1; //one pointer , single-touch
    buf[8] = 0; //id
    //x
    buf[9] = x >> 8;
    buf[10] = x;
    //y
    buf[11] = y >> 8;
    buf[12] = y;

    size = 13;
    size_t sent = 0; 
    while(sent < size) { 
        n = send(mSocket, buf+sent, size-sent,0); 
        if(n < 0 && errno != EWOULDBLOCK){
            ALOGI("sendTouchEvent failed.");
            break; 
        }

        sent += n; 
    }
    //ALOGI("sendTouchEvent:send %d bytes.", n);
    

}

void uibcSink::sendKeyEvent(int type, int keyCode1, int keyCode2)
{
    ssize_t n; 
    ssize_t size;
    unsigned char buf[32];
    int genericPacketLen,uibcBodyLen;

    genericPacketLen = 5;
    uibcBodyLen = genericPacketLen + 7;
    
    buf[0] = 0x00;
    buf[1] = 0x00; //0:generic , 1:HIDC 
    //set packet length
    buf[2] = (uibcBodyLen >> 8) & 0xFF; //Length(16 bits)
    buf[3] = uibcBodyLen & 0xFF; //Length(16 bits)
    //set UIBC input body
    buf[4] = type & 0xFF;
    //body length
    buf[5] = (genericPacketLen >> 8) & 0xFF; // Length, 2 octets
    buf[6] = genericPacketLen & 0xFF; // Length, 2 octets
    buf[7] = 0x00; // reserved

    buf[8] = (keyCode1 >> 8) & 0xFF ;
    buf[9] = keyCode1 & 0xFF;

    buf[10] = (keyCode2 >> 8) & 0xFF ;
    buf[11] = keyCode2 & 0xFF;

    size = 12;
    size_t sent = 0; 
    while(sent < size) { 
        n = send(mSocket, buf+sent, size-sent,0); 
        if(n < 0 && errno != EWOULDBLOCK){
            ALOGI("sendKeyEvent failed.");
            break; 
        }

        sent += n; 
    }
    //ALOGI("sendTouchEvent:send %d bytes.", n);
    

}

status_t uibcSink::connectUIBC(const char *host,unsigned port)
{
    
    status_t err = OK;
    int res;
   
    ALOGI("uibcSink::connectUIBC:remote host=%s,port=%d.",host,port);

    mSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (mSocket < 0) {
        err = -errno;
        ALOGE("uibcSink socket failed.");
        return -1;
    }
    
    const int yes = 1;
    res = setsockopt(mSocket, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    if (res < 0) {
        err = -errno;
        ALOGE("uibcSink:setsockopt failed.");
        return -1;
    }
        
    struct sockaddr_in server;
    server.sin_addr.s_addr = inet_addr(host);
    server.sin_family = AF_INET;
    server.sin_port = htons(port);
 
    //Connect to remote server
    if (connect(mSocket , (struct sockaddr *)&server , sizeof(server)) < 0)
    {
        ALOGE("uibcSink connect failed.");
        return -1;
    }

    err = MakeSocketNonBlocking(mSocket);
    if (err != OK) {
        err = -errno;
        ALOGE("uibcSink MakeSocketNonBlocking failed.");
        return -1;
    }
    
    ALOGI("UIBC connected.");
  
    return OK;
    
}

void* uibcSink::threadLoop(void)
{
	ALOGI("uibcSink::threadLoop:+");
	
    JNIEnv *env = NULL;
    int isAttached = 0;
    
    if(mJvm){
        if(mJvm->AttachCurrentThread(&env, NULL) < 0){
            ALOGE("uibcSink::threadLoop:thread can not attach current thread." );
            return 0;
        }
        isAttached = 1;
    }

    usleep(500000);
	do{
        if(connectUIBC(remoteHost,port) != OK)
            usleep(500000);
        else
            break;

    }while(getStopIssued() == 0);

    if(isAttached){
        mJvm->DetachCurrentThread();
    }
    
    ALOGI("uibcSink::threadLoop--");
    pthread_exit((void *)0);
    return 0;
}


status_t uibcSink::MakeSocketNonBlocking(int s) {
    int flags = fcntl(s, F_GETFL, 0);
    if (flags < 0) {
        flags = 0;
    }

    int res = fcntl(s, F_SETFL, flags | O_NONBLOCK);
    if (res < 0) {
        return -errno;
    }

    return OK;
}


} // namespace android