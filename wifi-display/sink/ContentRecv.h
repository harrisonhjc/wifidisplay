#ifndef CONTENT_RECV_H_
#define CONTENT_RECV_H_

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
#include <OMXAL/OpenMAXAL.h>
#include <OMXAL/OpenMAXAL_Android.h>
//#include <android/native_window_jni.h>
#include <gui/Surface.h>
#include <media/stagefright/foundation/AHandler.h>
#include <jni.h>
#include <utils/RefBase.h>

#define LEN_RTP_HEADER      12


struct ifaddrs {
  struct ifaddrs  *ifa_next;
  char            *ifa_name;
  unsigned int     ifa_flags;
  struct sockaddr *ifa_addr;
  struct sockaddr *ifa_netmask;
  union {
    struct sockaddr *ifu_broadaddr;
    struct sockaddr *ifu_dstaddr;
  } ifa_ifu;
#define ifa_broadaddr ifa_ifu.ifu_broadaddr
#define ifa_dstaddr   ifa_ifu.ifu_dstaddr
  void            *ifa_data;
};

namespace android {

struct ContentRecv : public RefBase  // : public Thread
{
    ContentRecv(unsigned int);
    virtual ~ContentRecv();
    char remoteHost[128];
    unsigned remotePort;
    char localHost[128];
    unsigned localPort;
    mutable Mutex mLock;
    bool streamingPlayer;
    bool hdcpInited;
    

    void setScale(int);
    void setAudioPath(int);
    void setVolume(int);
    void setJvm(JavaVM *jvm);
    void setkeyPES(unsigned char*, unsigned char*, unsigned char*);
    void enableHdcp(bool);
    status_t init(ANativeWindow* window);
    status_t initHdcp(ANativeWindow* window);
    status_t startService(ANativeWindow* window);
    status_t resumeService(ANativeWindow* window);
    status_t stopService();
    status_t suspendService();
    status_t createRtpConnection();
    status_t createRtcpConnection();
    status_t connectRemote(const char *remoteHost, int32_t remoteRtpPort, int32_t remoteRtcpPort);
    
    status_t resourceInit();
    void initStatusReport();
    status_t createStatusReportConnection();
    status_t sendStatusReport(const char* buf);
    ////////////////////////////////
    static XAresult AndroidBufferQueueCallback(
        XAAndroidBufferQueueItf caller,
        void *pCallbackContext,        /* input */
        void *pBufferContext,          /* input */
        void *pBufferData,             /* input */
        XAuint32 dataSize,             /* input */
        XAuint32 dataUsed,             /* input */
        const XAAndroidBufferItem *pItems,/* input */
        XAuint32 itemsLength           /* input */);

    static bool enqueueInitialBuffers(
        bool discontinuity);

    static void StreamChangeCallback(
        XAStreamInformationItf caller,
        XAuint32 eventId,
        XAuint32 streamIndex,
        void * pEventData,
        void * pContext );

    static int deQueuePacket(
        void* pBufTarget,
        int lenBufTarget);

    static void createEngine();
    //static ANativeWindow* createNativeWindow();
    static bool createStreamingMediaPlayer(bool audioOnly);
    static void shutdownMediaPlayer();
    static void shutdownMediaEngine();
    void setMute(bool);
    //////////////////////////////////////

private:
    unsigned char Ks[16];
    unsigned char riv[8];
    unsigned char lc[16];
    bool hdcpEnabled;
    status_t MakeSocketNonBlocking(int s);

    
};

}  // namespace android

#endif  // CONTENT_RECV_H_
