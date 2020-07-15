#ifndef HDCP_RECEIVER_H_
#define HDCP_RECEIVER_H_

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
#include "DeviceKeyset.h"

struct __attribute__((packed)) sCert{
    char id;
    char repeater;
    char Certrx[522];
};
struct __attribute__((packed)) sReceiverInfo{
    char id;
    short len;
    char version;
    short cap;
};

typedef struct sCert tCertrx; 
typedef struct sReceiverInfo tReceiverInfo; 

namespace android {



struct HdcpReceiver : public RefBase
{

    typedef  void* (HdcpReceiver::*HdcpReceiverPtr)(void);
    typedef  void* (*PthreadPtr)(void*);
    void  startThread(void);
    void* threadLoop(void);


    status_t start(const char *host, unsigned port);
    void stop();
    DeviceKeyset  mKeyset;
    virtual ~HdcpReceiver();

    int hdcpVersion;
    char localHost[128];
    unsigned port;
    int mSocket;
    mutable Mutex mLock;
    AKE_Transmitter_Info ake_transmitter_info;
    
    JavaVM  *mJvm;
    void setJvm(JavaVM *jvm){
        mJvm = jvm;
    }

    status_t on_AKE_Init(int fd, const char* buf);
    status_t on_LC_Init(int fd,const char* buf);
    status_t on_AKE_Transmitter_Info(int fd, const char* buf);
    status_t on_AKE_No_Stored_Km(int fd, const char* buf);
    status_t on_AKE_Stored_Km(int fd, const char* buf);
    status_t onSKE_Send_Eks(int fd, const char* buf);
    
    status_t AKE_Send_Cert(int fd);
    status_t AKE_Receiver_Info(int fd);
    status_t AKE_Send_Rrx(int fd);
    status_t AKE_Send_H_prime(int fd);
    status_t AKE_Send_Pairing_Info(int fd);
    status_t LC_Send_L_prime(int fd);
    status_t HandleHdcp(int clientSocket);
    status_t MakeSocketNonBlocking(int s);
    
    void setHdcpVersion(int ver);
    unsigned char* getKs();
    unsigned char* getRiv();
    unsigned char* getLc();
    
    typedef void (*callback_type)(void*, int);
    callback_type callback;
    void* user_data;
    void set_callback(callback_type cb, void* ud){
        
        callback = cb;
        user_data = ud;
    }
    void notifyAuthComplete(){ callback(user_data, 0); }
    
private:
    enum {
        kWhatStart,
        kWhatRTSPNotify,
        kWhatStop,
        kWhatWfdCmd,
        kWhatHdcpAuthOK,
    };
    
};

}  // namespace android

#endif  // HDCP_RECEIVER_H_
