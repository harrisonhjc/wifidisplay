#ifndef MMX_RECV_H_
#define MMX_RECV_H_

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
#include "MmxM2ts.h"
#include "MmxVideoDecoder.h"
#include "MmxVideoScheduler.h"
#include "MmxAudioDecoder.h"
#include "MmxAudioScheduler.h"
#include <jni.h>

#define PKT_CHUNK_NUM       32000
#define PKT_HDR_SIZE           (sizeof(unsigned int))
#define PKT_PAYLOAD_SIZE_MAX   (1472 - PKT_HDR_SIZE)


typedef struct
{
    unsigned int    hdr;
    unsigned char   payload[PKT_PAYLOAD_SIZE_MAX];

} sPI_PORTAL_PKT;

typedef struct sPKT_CHUNK{  
    char *head;
    char *curr_ptr;
    char *pkt_ptr;
    unsigned int     total_read;
    unsigned int     max_len;
    unsigned int     pkt_len;
} sPKT_CHUNK;

namespace android {



struct MmxRecv : public RefBase
{
    
    typedef  void* (MmxRecv::*MmxRecvPtr)(void);
    typedef  void* (*PthreadPtr)(void*);
    void  startThread(void);
    void* threadLoop(void);

    char remoteHost[128];
    unsigned remotePort;
    char localHost[128];
    unsigned localPort;
    mutable Mutex mLock;
    int mSockRtp;
    int mSockRtcp;
    sPKT_CHUNK pkt_chunk;
    sp<MmxM2ts> mM2ts;
    sp<MmxVideoDecoder> mVideoDecoder;
    sp<MmxVideoScheduler> mVideoScheduler;
    sp<MmxAudioDecoder> mAudioDecoder;
    sp<MmxAudioScheduler> mAudioScheduler;
  
    void setScale(int);
    void setJvm(JavaVM *jvm);
    void setkeyPES(unsigned char* Ks, unsigned char* riv, unsigned char* lc);
    status_t init(ANativeWindow* window);
    status_t startService(ANativeWindow* window);
    status_t stopService();
    
    status_t resourceInit();
    void resourceRelease();
    void setMute(bool);
    status_t createRtp();
    status_t createRtcp();
    status_t connectRemote(const char *remoteHost, int32_t remoteRtpPort, int32_t remoteRctpPort);

private:
    
    status_t MakeSocketNonBlocking(int s);

    
};

}  // namespace android

#endif  // MMX_RECV_H_
