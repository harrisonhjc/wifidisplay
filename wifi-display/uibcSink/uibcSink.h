#ifndef UIBC_SINK_H_
#define UIBC_SINK_H_

#include <arpa/inet.h>
#include <fcntl.h>
#include <linux/tcp.h>
#include <net/if.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <utils/Thread.h>
#include <netinet/in.h>
#include <jni.h>


namespace android {

struct uibcSink : public RefBase
{

    typedef  void* (uibcSink::*uibcSinkPtr)(void);
    typedef  void* (*PthreadPtr)(void*);
    void  startThread(void);
    void* threadLoop(void);

    void sendKeyEvent(int type, int keyCode1, int keyCode2);
    void sendTouchEvent(int type, int x, int y);
    status_t start(const char *host, unsigned port);
    void stop();
    status_t connectUIBC(const char *host, unsigned port);
    
    int mSocket;
    char remoteHost[127];
    unsigned int port;
       
    JavaVM  *mJvm;
    void setJvm(JavaVM *jvm){
        mJvm = jvm;
    }

    status_t MakeSocketNonBlocking(int s);
    
    
};

}  // namespace android

#endif  // UIBC_SINK_H_
