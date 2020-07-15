#define LOG_TAG "ContentRecv"
/*
#undef NDEBUG 
#define LOG_NDEBUG   0
#define LOG_NIDEBUG  0 
#define LOG_NDDEBUG 0
#define LOG_NEDEBUG 0 
*/

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
#include <sys/types.h>
#include <media/stagefright/foundation/hexdump.h>
#include <media/stagefright/foundation/ADebug.h>
#include <OMXAL/OpenMAXAL.h>
#include <OMXAL/OpenMAXAL_Android.h>
#include <gui/SurfaceComposerClient.h>
#include <gui/ISurfaceComposer.h>
#include <ui/DisplayInfo.h>
#include <binder/IMemory.h>
#include <binder/IServiceManager.h>
#include <gui/SurfaceComposerClient.h>
#include <media/IMediaPlayerService.h>
#include <media/IStreamSource.h>
#include <gui/ISurfaceComposer.h>
#include <gui/ISurfaceComposerClient.h>
#include <media/stagefright/foundation/hexdump.h>
#include <jni.h>
#include <android/native_window_jni.h>
#include <media/stagefright/foundation/AMessage.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "ContentRecv.h"
#include "mmx/MmxRecv.h"


namespace android {

sp<MmxRecv> mhdcpRecv;
int sockRtp;
int sockRctp;
int sockStatus;
sp<SurfaceComposerClient> mComposerClient;
sp<SurfaceControl> mSurfaceControl;
sp<Surface> mSurface;
// engine interfaces
 XAObjectItf engineObject = NULL;
 XAEngineItf engineEngine = NULL;

// output mix interfaces
 XAObjectItf outputMixObject = NULL;

// streaming media player interfaces
 XAObjectItf                    playerObj = NULL;
 XAPlayItf                      playerPlayItf = NULL;
 XAAndroidBufferQueueItf        playerBQItf = NULL;
 XAStreamInformationItf         playerStreamInfoItf = NULL;
 XAVolumeItf                    playerVolItf = NULL;
 XAVideoPostProcessingItf       playerVideoPostProcessingItf = NULL;

// number of required interfaces for the MediaPlayer creation
#define NB_MAXAL_INTERFACES 3 // XAAndroidBufferQueueItf, XAStreamInformationItf and XAPlayItf

// video sink for the player
 ANativeWindow* theNativeWindow;

// number of buffers in our buffer queue, an arbitrary number
#define NB_BUFFERS 4 //8

// we're streaming MPEG-2 transport stream data, operate on transport stream block size
#define MPEG2_TS_PACKET_SIZE 188

// number of MPEG-2 transport stream blocks per buffer, an arbitrary number
#define PACKETS_PER_BUFFER 20 //8

// determines how much memory we're dedicating to memory caching
#define BUFFER_SIZE (PACKETS_PER_BUFFER*MPEG2_TS_PACKET_SIZE)

// where we cache in memory the data to play
// note this memory is re-used by the buffer queue callback
 typedef struct _BufBlock{
    int length;
    unsigned char data[BUFFER_SIZE];
 } tBufBlock;
 
 tBufBlock bufBlocks[NB_BUFFERS];

// constant to identify a buffer context which is the end of the stream to decode
 const int kEosBufferCntxt = 1980; // a magic value we can compare against

// For mutual exclusion between callback thread and application thread(s).
// The mutex protects reachedEof, discontinuity,
// The condition is signalled when a discontinuity is acknowledged.

 pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
 pthread_cond_t cond = PTHREAD_COND_INITIALIZER;

 //pthread_mutex_t mutexStop;

// whether a discontinuity is in progress
bool discontinuity = false;
struct ifaddrs *get_interface(const char *name, sa_family_t family);
void freeifaddrs(struct ifaddrs *ifa);
int getifaddrs(struct ifaddrs **ifap);
unsigned int wfdsinkId;

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

void ContentRecv::setJvm(JavaVM *jvm){
        mhdcpRecv->setJvm(jvm);
}

void ContentRecv::setScale(int type){
    if(hdcpEnabled){
        mhdcpRecv->setScale(type);
    }
}

void ContentRecv::setAudioPath(int type){
    if(playerVolItf != NULL){
        ALOGI("ContentRecv::setAudioPath:%d.", type);
        int streamid = 0 - type*2875;
        (*playerVolItf)->SetVolumeLevel(playerVolItf , streamid);
    }
    
}

void ContentRecv::setVolume(int vol){
    if(playerVolItf != NULL){
        ALOGI("ContentRecv::setVolume:%d.", vol);
        (*playerVolItf)->SetVolumeLevel(playerVolItf , (0-vol*100));
    }
    
}



ContentRecv::ContentRecv(unsigned int id)
{
    hdcpEnabled = false;
    wfdsinkId = id;
    ALOGI("ContentRecv:id=%d.",wfdsinkId);
    
}

ContentRecv::~ContentRecv()
{
    //ALOGI("~ContentRecv:");
    //stopAll = true;
}

status_t ContentRecv::init(ANativeWindow* window)
{
	//ALOGI("init:");
    setStopIssued(0);
    localPort = 15550;
    createRtpConnection();
    createRtcpConnection();
    
    if(hdcpEnabled){
        
        //mhdcpRecv->init(window);
        
    }else{
        
        theNativeWindow = window;
        createEngine();
        streamingPlayer = false;
    }
    
	return OK;
}

//HDCP functions
///////////////////////////////////////////////////
void ContentRecv::enableHdcp(bool status)
{
   // hdcpEnabled = status;
   // if(mhdcpRecv == NULL)
   //     mhdcpRecv = new MmxRecv;
    
}
status_t ContentRecv::initHdcp(ANativeWindow* window)
{
    /*
    setStopIssued(0);
    localPort = 15550;
    createRtpConnection();
    createRtcpConnection();
    mhdcpRecv->init(window);
    hdcpInited = true;
    return OK;
    */
    init(window);
    return OK;
}
void ContentRecv::setkeyPES(unsigned char* Ks, unsigned char* riv, unsigned char* lc)
{
    /*
    memcpy(this->Ks,Ks,sizeof(this->Ks));
    memcpy(this->riv,riv,sizeof(this->riv));
    memcpy(this->lc,lc,sizeof(this->lc));
    mhdcpRecv->setkeyPES(Ks,riv,lc);
    */
    
    ALOGI("ContentRecv::setkeyPES");
    FILE *fp;
    fp = fopen("/data/data/com.antec.smartlink.miracast/tough","wb");
    if(fp == NULL)
        return;

    fwrite(Ks, 16, 1, fp);
    fwrite(riv, 8, 1,fp);
    fwrite(lc, 16, 1, fp);
    fclose(fp);
    chmod("/data/data/com.antec.smartlink.miracast/tough",
           S_IRWXU|S_IRWXG|S_IROTH|S_IWOTH);
}
//////////////////////////////////////////////////////////////

status_t ContentRecv::startService(ANativeWindow* window)
{
    if(hdcpEnabled){
        if(hdcpInited == false){
            initHdcp(window);
            mhdcpRecv->setkeyPES(Ks,riv,lc);
        }
        mhdcpRecv->startService(window);
    
    }else{
        //init(window);
        if(streamingPlayer == true)
            return OK;

        theNativeWindow = window;
        createStreamingMediaPlayer(false);
        streamingPlayer = true;
    }
    
    return OK;
    
}
status_t ContentRecv::resumeService(ANativeWindow* window)
{
    if(hdcpEnabled){
        if(hdcpInited == false){
            initHdcp(window);
            mhdcpRecv->setkeyPES(Ks,riv,lc);
        }
        mhdcpRecv->startService(window);
    
    }else{
        
        if(streamingPlayer == true){
            //return OK;
            //stop audio play
            setStopIssued(1);
            XAresult res;
            //stop playing
            res = (*playerPlayItf)->SetPlayState(playerPlayItf,  XA_PLAYSTATE_PAUSED);
            CHECK(XA_RESULT_SUCCESS == res);
            res = (*playerPlayItf)->SetPlayState(playerPlayItf,  XA_PLAYSTATE_STOPPED);
            CHECK(XA_RESULT_SUCCESS == res);
            //sendStatusReport("SUSPEND\n");
            shutdownMediaPlayer();
            shutdownMediaEngine();
            streamingPlayer = false;
            close(sockRtp);
            close(sockRctp);
        }

        setStopIssued(0);
        localPort = 15550;
        createRtpConnection();
        createRtcpConnection();
        theNativeWindow = window;
        createEngine();
        createStreamingMediaPlayer(false);
        streamingPlayer = true;
        /*
        if(playerVolItf != NULL){
            (*playerVolItf)->SetVolumeLevel(playerVolItf, -10);
            
        }
        
        usleep(50000);
        if(playerVolItf != NULL){
            (*playerVolItf)->SetVolumeLevel(playerVolItf, -3);
            
        }
        */
    }
    
    return OK;
    
}


status_t ContentRecv::suspendService()
{
    if(hdcpEnabled){
        //sendStatusReport("SUSPEND\n");
        mhdcpRecv->stopService();
        setStopIssued(1);
        hdcpInited = false;
    
    }else{
        setStopIssued(1);
        XAresult res;
        //stop playing
        res = (*playerPlayItf)->SetPlayState(playerPlayItf,  XA_PLAYSTATE_PAUSED);
        CHECK(XA_RESULT_SUCCESS == res);
        res = (*playerPlayItf)->SetPlayState(playerPlayItf,  XA_PLAYSTATE_STOPPED);
        CHECK(XA_RESULT_SUCCESS == res);
        //sendStatusReport("SUSPEND\n");
        shutdownMediaPlayer();
        shutdownMediaEngine();
        streamingPlayer = false;
        close(sockRtp);
        close(sockRctp);

        //connect audio only
        setStopIssued(0);
        localPort = 15550;
        createRtpConnection();
        createRtcpConnection();
        createEngine();
        createStreamingMediaPlayer(true);
        streamingPlayer = true;
    }
    return OK;
}

status_t ContentRecv::stopService()
{
    if(hdcpEnabled){
        //sendStatusReport("SHUTDOWN\n");
        mhdcpRecv->stopService();
        setStopIssued(1);
    
    }else{
        setStopIssued(1);
        XAresult res;
        //stop playing
        res = (*playerPlayItf)->SetPlayState(playerPlayItf,  XA_PLAYSTATE_PAUSED);
        CHECK(XA_RESULT_SUCCESS == res);
        res = (*playerPlayItf)->SetPlayState(playerPlayItf,  XA_PLAYSTATE_STOPPED);
        CHECK(XA_RESULT_SUCCESS == res);
        shutdownMediaPlayer();
        shutdownMediaEngine();
        //sendStatusReport("ABORT\n");
        streamingPlayer = false;
        shutdown(sockStatus,SHUT_RDWR);
        close(sockRtp);
        close(sockRctp);
        close(sockStatus);
    }

    return OK;
}

status_t ContentRecv::resourceInit()
{
    return OK;

}



void ContentRecv::initStatusReport()
{
    createStatusReportConnection();    
}



status_t ContentRecv::createStatusReportConnection()
{
    int portno, n;
    struct sockaddr_in serveraddr;
    

    sockStatus = socket(AF_INET, SOCK_STREAM, 0);
    if (sockStatus < 0){ 
        ALOGE("status socket failed.");
        return false;
    }

    serveraddr.sin_addr.s_addr = inet_addr("127.0.0.1");
    serveraddr.sin_family = AF_INET;
    serveraddr.sin_port = htons(45346);

    if(connect(sockStatus, (struct sockaddr *)&serveraddr, sizeof(serveraddr)) < 0){
        ALOGE("status connection failed.");
        return false;
    } 
    //ALOGI("status connection succeeded.");

    return OK;
    
}

status_t ContentRecv::sendStatusReport(const char* buf)
{
   
    int n;
    n = write(sockStatus, buf, strlen(buf));
    if (n < 0) {
        ALOGE("status write failed.");
        return false;
    }
    //ALOGI("Status:%s.", buf);
    return OK;
      
}

status_t ContentRecv::createRtpConnection()
{
    
    status_t err = OK;
    int res;
    int size = 32*1024*1024;
    
    if(hdcpEnabled){
        mhdcpRecv->createRtp();
        return OK;
    }

    sockRtp = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockRtp < 0) {
        err = -errno;
        ALOGE("socket failed.");
        return false;
    }
    const int yes = 1;
    res = setsockopt(sockRtp, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    if (res < 0) {
        err = -errno;
        ALOGE("createRtpConnection:setsockopt failed.");
        return false;
    }

    res = setsockopt(sockRtp, SOL_SOCKET, SO_RCVBUF, &size, sizeof(size));
    if (res < 0) {
        err = -errno;
        ALOGE("setsockopt failed.");
        return false;
    }
    /*
    res = setsockopt(sockRtp, SOL_SOCKET, SO_SNDBUF, &size, sizeof(size));
    if (res < 0) {
        err = -errno;
        ALOGE("setsockopt failed.");
        return false;
    }
    */

    int lowdelay = IPTOS_LOWDELAY;
    if (setsockopt(sockRtp, IPPROTO_IP, IP_TOS, (void *)&lowdelay, 
                     sizeof(lowdelay)) < 0)
        ALOGE("setsockopt IPTOS_LOWDELAY: %.100s", strerror(errno));
    
    int throughput = IPTOS_THROUGHPUT;
      if (setsockopt(sockRtp, IPPROTO_IP, IP_TOS, (void *)&throughput, 
                     sizeof(throughput)) < 0)
        ALOGE("setsockopt IPTOS_THROUGHPUT: %.100s", strerror(errno));

    err = MakeSocketNonBlocking(sockRtp);
    if (err != OK) {
        err = -errno;
        ALOGE("MakeSocketNonBlocking failed.");
        return false;
    }
  
/*
    struct ifreq ifr;
    struct ifaddrs *addrs, *iap;
    struct sockaddr_in *sa;
    char localHost[32];
    
    getifaddrs(&addrs);
    for (iap = addrs; iap != NULL; iap = iap->ifa_next) {
        if (iap->ifa_addr && (iap->ifa_flags & IFF_UP) && iap->ifa_addr->sa_family == AF_INET) {
            sa = (struct sockaddr_in *)(iap->ifa_addr);
            inet_ntop(iap->ifa_addr->sa_family, (void *)&(sa->sin_addr), localHost, sizeof(localHost));
            ALOGI("if:%s.", localHost);
            if(strcmp("127.0.0.1", localHost)){
                ALOGI("if:name:%s,ip=%s.", iap->ifa_name, localHost);
                break;
            }
        }
    }
    freeifaddrs(addrs);
*/
    
    shutdown(sockRtp,SHUT_WR);

    struct sockaddr_in addr;
    memset(addr.sin_zero, 0, sizeof(addr.sin_zero));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(localPort);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    //addr.sin_addr.s_addr = inet_addr(localHost);
    res = bind(sockRtp, (const struct sockaddr *)&addr, sizeof(addr));
    if(res < 0){
        close(sockRtp);
        err = -errno;
        ALOGE("bind failed.");
        return false;
    }
   
    return OK;
    
}

status_t ContentRecv::createRtcpConnection()
{
    
    status_t err = OK;
    int res;
    int size = 256 * 1024;
   
    if(hdcpEnabled){
        mhdcpRecv->createRtcp();
        return OK;
    }

    sockRctp = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockRctp < 0) {
        err = -errno;
        ALOGE("socket failed.");
        return false;
    }
    res = setsockopt(sockRctp, SOL_SOCKET, SO_RCVBUF, &size, sizeof(size));
    if (res < 0) {
        err = -errno;
        ALOGE("setsockopt failed.");
        return false;
    }
    const int yes = 1;
    res = setsockopt(sockRctp, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    if (res < 0) {
        err = -errno;
        ALOGE("createRtcpConnection:setsockopt failed.");
        return false;
    }

    res = setsockopt(sockRctp, SOL_SOCKET, SO_SNDBUF, &size, sizeof(size));
    if (res < 0) {
        err = -errno;
        ALOGE("setsockopt failed.");
        return false;
    }
    err = MakeSocketNonBlocking(sockRctp);
    if (err != OK) {
        err = -errno;
        ALOGE("MakeSocketNonBlocking failed.");
        return false;
    }

    struct sockaddr_in addr;
    memset(addr.sin_zero, 0, sizeof(addr.sin_zero));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(localPort+1);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    res = bind(sockRctp, (const struct sockaddr *)&addr, sizeof(addr));
    if(res < 0){
        close(sockRctp);
        err = -errno;
        ALOGE("bind failed.");
        return false;
    }
    
    return OK;
    
}

status_t ContentRecv::connectRemote(const char *remoteHost, int32_t remoteRtpPort, int32_t remoteRctpPort)
{
    Mutex::Autolock autoLock(mLock);
  
    if(hdcpEnabled){
        mhdcpRecv->connectRemote(remoteHost,remoteRtpPort,remoteRctpPort);
        return OK;
    }

    status_t err = OK;
    struct sockaddr_in remoteAddr;
    memset(remoteAddr.sin_zero, 0, sizeof(remoteAddr.sin_zero));
    remoteAddr.sin_family = AF_INET;
    remoteAddr.sin_port = htons(remoteRtpPort);
    struct hostent *ent = gethostbyname(remoteHost);
    if (ent == NULL) {
        err = -h_errno;
    } else {
        remoteAddr.sin_addr.s_addr = *(in_addr_t *)ent->h_addr;
        int res = connect(
                        sockRtp,
                        (const struct sockaddr *)&remoteAddr,
                        sizeof(remoteAddr));

        if (res < 0) {
            err = -errno;
        }
        
        remoteAddr.sin_port = htons(remoteRctpPort);
        res = connect(
                        sockRctp,
                        (const struct sockaddr *)&remoteAddr,
                        sizeof(remoteAddr));

        if (res < 0) {
            err = -errno;
        }
        
    }
    return err;
    
}

status_t ContentRecv::MakeSocketNonBlocking(int s) {
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

/*
int ContentRecv::deQueuePacket(void* pBufTarget, int lenBufTarget)
{
    ssize_t n = 0;
    struct sockaddr_in remoteAddr;
    socklen_t remoteAddrLen = sizeof(remoteAddr);

    void* pData = pBufTarget;
    do {
        n = recvfrom(sockRtp, (void*)pData, lenBufTarget, 0,
                    (struct sockaddr *)&remoteAddr, &remoteAddrLen);
    } while (n < LEN_RTP_HEADER);
    
    return n;
}
*/

int ContentRecv::deQueuePacket(void* pBufTarget, int lenBufTarget)
{
    ssize_t n = 0;
    ssize_t total = 0;
    struct sockaddr_in remoteAddr;
    socklen_t remoteAddrLen = sizeof(remoteAddr);
    
    struct timeval start, end;
    long secs_used , micros_used;

    //gettimeofday(&start, NULL);
    
    //read 2 packets 
    void* pData = pBufTarget;
    
    do {
        n = recvfrom(sockRtp, (void*)pData, lenBufTarget, 0,
                    (struct sockaddr *)&remoteAddr, &remoteAddrLen);
        
        
    } while( (n < LEN_RTP_HEADER) && (getStopIssued() != 1));
    
    
    total = n;
    pData += n;
    
    if(getStopIssued() == 1){
        return -1;
    }
    char readBuf[1400];
    do {
        n = recvfrom(sockRtp, (void*)readBuf, 1400, 0,
                    (struct sockaddr *)&remoteAddr, &remoteAddrLen);
        
    } while ( (n < LEN_RTP_HEADER)  && (getStopIssued() != 1) );
   
    total += n-LEN_RTP_HEADER;
    memcpy(pData, readBuf + LEN_RTP_HEADER, n-LEN_RTP_HEADER);
    

    //gettimeofday(&end, NULL);
    //secs_used=(end.tv_sec - start.tv_sec);
    //micros_used= ((secs_used*1000000) + end.tv_usec) - (start.tv_usec);
    //ALOGI("dQ:%ld us.", micros_used);
    return total;
}


// Enqueue the initial buffers, and optionally signal a discontinuity in the first buffer
bool ContentRecv::enqueueInitialBuffers(bool discontinuity)
{

    /* Fill our cache.
     * We want to read whole packets (integral multiples of MPEG2_TS_PACKET_SIZE).
     * fread returns units of "elements" not bytes, so we ask for 1-byte elements
     * and then check that the number of elements is a multiple of the packet size.
     */
    XAresult res;
    size_t bytesRead;
    int i;
    unsigned char* pBuf;
    /*
    for(i = 0; i < NB_BUFFERS; i++){
        bytesRead = deQueuePacket(&bufBlocks[i].data, BUFFER_SIZE);
        if(bytesRead <= 0) {
          return false;
        }else{
            bufBlocks[i].length = bytesRead - LEN_RTP_HEADER;
        }
    }
    for (i = 0; i < NB_BUFFERS; i++) {
        pBuf = bufBlocks[i].data;
        res = (*playerBQItf)->Enqueue(playerBQItf, NULL ,
                                      pBuf + LEN_RTP_HEADER,
                                      bufBlocks[i].length,
                                      NULL, 0);
        CHECK(XA_RESULT_SUCCESS == res);
    }
*/    
    for(i = 0; i < NB_BUFFERS; i++){
        bytesRead = deQueuePacket(&bufBlocks[i].data, BUFFER_SIZE);
        bufBlocks[i].length = bytesRead - LEN_RTP_HEADER;
        pBuf = bufBlocks[i].data;
        (*playerBQItf)->Enqueue(playerBQItf, NULL ,
                                      pBuf + LEN_RTP_HEADER,
                                      bufBlocks[i].length,
                                      NULL, 0);
        
    }


    return true;
}


// AndroidBufferQueueItf callback to supply MPEG-2 TS packets to the media player
XAresult ContentRecv::AndroidBufferQueueCallback(
        XAAndroidBufferQueueItf caller,
        void *pCallbackContext,        /* input */
        void *pBufferContext,          /* input */
        void *pBufferData,             /* input */
        XAuint32 dataSize,             /* input */
        XAuint32 dataUsed,             /* input */
        const XAAndroidBufferItem *pItems,/* input */
        XAuint32 itemsLength           /* input */)
{
    XAresult res;
    int ok;
    size_t bytesRead;
    int i;
    unsigned char* pData = NULL;
    unsigned char* pDataBlock = NULL; 
    
    /*
    //ALOGI("AndroidBufferQueueCallback ++");
    // pCallbackContext was specified as NULL at RegisterCallback and is unused here
    CHECK(NULL == pCallbackContext);

    // note there is never any contention on this mutex unless a discontinuity request is active
    ok = pthread_mutex_lock(&mutex);
    CHECK(0 == ok);

    // was a discontinuity requested?
    if (discontinuity) {
        // clear the buffer queue
        res = (*playerBQItf)->Clear(playerBQItf);
        CHECK(XA_RESULT_SUCCESS == res);
        // Enqueue the initial buffers, with a discontinuity indicator on first buffer
        (void) enqueueInitialBuffers(true);
        // acknowledge the discontinuity request
        discontinuity = false;
        ok = pthread_cond_signal(&cond);
        CHECK(0 == ok);
        goto exit;
    }

    if ((pBufferData == NULL) && (pBufferContext != NULL)) {
        const int processedCommand = *(int *)pBufferContext;
        if (kEosBufferCntxt == processedCommand) {
            LOGV("EOS was processed\n");
            // our buffer with the EOS message has been consumed
            CHECK(0 == dataSize);
            goto exit;
        }
    }
    
    // pBufferData is a pointer to a buffer that we previously Enqueued
    CHECK((dataSize > 0) && ((dataSize % MPEG2_TS_PACKET_SIZE) == 0));
    */
    //find the buffer that queued in  enqueueInitialBuffers
    for(i = 0; i < NB_BUFFERS; i++){
        pData = bufBlocks[i].data;
        pData += LEN_RTP_HEADER;
        if(pData == pBufferData)
            break;
    }
    
    //CHECK(pData != NULL);
    pDataBlock = bufBlocks[i].data;
    bytesRead = deQueuePacket(pDataBlock, BUFFER_SIZE);
    if (bytesRead > 0) {
        bytesRead -= LEN_RTP_HEADER;
        //if ((bytesRead % MPEG2_TS_PACKET_SIZE) != 0) {
        //    LOGV("Dropping last packet because it is not whole");
        //}
        //size_t packetsRead = bytesRead / MPEG2_TS_PACKET_SIZE;
        //size_t bufferSize = packetsRead * MPEG2_TS_PACKET_SIZE;
        res = (*caller)->Enqueue(caller, NULL /*pBufferContext*/,
                pBufferData /*pData*/,
                bytesRead, //bufferSize /*dataLength*/,
                NULL /*pMsg*/,
                0 /*msgLength*/);
        //CHECK(XA_RESULT_SUCCESS == res);
    }/* 
    else {
        // EOF or I/O error, signal EOS
        XAAndroidBufferItem msgEos[1];
        msgEos[0].itemKey = XA_ANDROID_ITEMKEY_EOS;
        msgEos[0].itemSize = 0;
        // EOS message has no parameters, so the total size of the message is the size of the key
        //   plus the size if itemSize, both XAuint32
        res = (*caller)->Enqueue(caller, (void *)&kEosBufferCntxt ,//
                NULL, //pData
                0, //dataLength
                msgEos, //
                sizeof(XAuint32)*2); //msgLength
        CHECK(XA_RESULT_SUCCESS == res);
        
    }*/

exit:
    //ok = pthread_mutex_unlock(&mutex);
    //CHECK(0 == ok);
    return XA_RESULT_SUCCESS;
}


// callback invoked whenever there is new or changed stream information
void ContentRecv::StreamChangeCallback(XAStreamInformationItf caller,
        XAuint32 eventId,
        XAuint32 streamIndex,
        void * pEventData,
        void * pContext )
{
    //ALOGI("StreamChangeCallback called for stream %u", streamIndex);
    //androidSetThreadPriority(0,ANDROID_PRIORITY_HIGHEST);
    // pContext was specified as NULL at RegisterStreamChangeCallback and is unused here
    CHECK(NULL == pContext);
    switch (eventId) {
      case XA_STREAMCBEVENT_PROPERTYCHANGE: {
        /** From spec 1.0.1:
            "This event indicates that stream property change has occurred.
            The streamIndex parameter identifies the stream with the property change.
            The pEventData parameter for this event is not used and shall be ignored."
         */

        XAresult res;
        XAuint32 domain;
        res = (*caller)->QueryStreamType(caller, streamIndex, &domain);
        CHECK(XA_RESULT_SUCCESS == res);
        switch (domain) {
          case XA_DOMAINTYPE_VIDEO: {
            XAVideoStreamInformation videoInfo;
            res = (*caller)->QueryStreamInformation(caller, streamIndex, &videoInfo);
            CHECK(XA_RESULT_SUCCESS == res);
            
            int kWhatVideoResolution = 9;
            sp<AMessage> msg = new AMessage(kWhatVideoResolution, wfdsinkId);
            msg->setInt32("w", videoInfo.width);
            msg->setInt32("h", videoInfo.height);
            msg->post();
            //ALOGI("Found video size %u x %u, codec ID=%u, frameRate=%u, bitRate=%u, duration=%u ms",
            //            videoInfo.width, videoInfo.height, videoInfo.codecId, videoInfo.frameRate,
            //            videoInfo.bitRate, videoInfo.duration);
          } break;
          default:
            ALOGE("Unexpected domain %u\n", domain);
            break;
        }
      } break;
      default:
        ALOGE("Unexpected stream event ID %u\n", eventId);
        break;
    }
}
/*
ANativeWindow* ContentRecv::createNativeWindow()
{
    mComposerClient = new SurfaceComposerClient;
    CHECK_EQ(mComposerClient->initCheck(), (status_t)OK);
    
    DisplayInfo info;
    sp<IBinder> display = SurfaceComposerClient::getBuiltInDisplay(ISurfaceComposer::eDisplayIdMain);
    SurfaceComposerClient::getDisplayInfo(display, &info);
    ssize_t displayWidth = info.w;
    ssize_t displayHeight = info.h;
    ALOGI("createNativeWindow:Display:[%d, %d]", displayWidth, displayHeight);

    mSurfaceControl =
        mComposerClient->createSurface(
                String8("A Sink Surface"),
                displayWidth,
                displayHeight,
                PIXEL_FORMAT_RGB_565,
                0);

    CHECK(mSurfaceControl != NULL);
    CHECK(mSurfaceControl->isValid());


    SurfaceComposerClient::openGlobalTransaction();
    
    Rect layerStackRect(0,0,displayWidth, displayHeight);
    Rect displayRect(0,0,displayWidth, displayHeight);
    mComposerClient->setDisplayProjection(display,
                                            0, // 0 degree rotation
                                            layerStackRect,
                                            displayRect);

    CHECK_EQ(mSurfaceControl->setLayer(INT_MAX), (status_t)OK);
    CHECK_EQ(mSurfaceControl->show(), (status_t)OK);
    SurfaceComposerClient::closeGlobalTransaction();
    mSurface = mSurfaceControl->getSurface();
    CHECK(mSurface != NULL);
    ANativeWindow* window = mSurface.get();
    return window;
  
}
*/
// create the engine and output mix objects
void ContentRecv::createEngine()
{
    XAresult res;

    
    // create engine
    res = xaCreateEngine(&engineObject, 0, NULL, 0, NULL, NULL);
    CHECK(XA_RESULT_SUCCESS == res);

    // realize the engine
    res = (*engineObject)->Realize(engineObject, XA_BOOLEAN_FALSE);
    CHECK(XA_RESULT_SUCCESS == res);

    // get the engine interface, which is needed in order to create other objects
    res = (*engineObject)->GetInterface(engineObject, XA_IID_ENGINE, &engineEngine);
    CHECK(XA_RESULT_SUCCESS == res);

    // create output mix
    res = (*engineEngine)->CreateOutputMix(engineEngine, &outputMixObject, 0, NULL, NULL);
    CHECK(XA_RESULT_SUCCESS == res);

    // realize the output mix
    res = (*outputMixObject)->Realize(outputMixObject, XA_BOOLEAN_FALSE);
    CHECK(XA_RESULT_SUCCESS == res);



}

// create streaming media player
bool ContentRecv::createStreamingMediaPlayer(bool audioOnly)
{
    XAresult res;

    // configure data source
    XADataLocator_AndroidBufferQueue loc_abq = { XA_DATALOCATOR_ANDROIDBUFFERQUEUE, NB_BUFFERS };
    XADataFormat_MIME format_mime = {
            XA_DATAFORMAT_MIME, XA_ANDROID_MIME_MP2TS, XA_CONTAINERTYPE_MPEG_TS };
    XADataSource dataSrc = {&loc_abq, &format_mime};

    // configure audio sink
    XADataLocator_OutputMix loc_outmix = { XA_DATALOCATOR_OUTPUTMIX, outputMixObject };
    XADataSink audioSnk = { &loc_outmix, NULL };

    // configure image video sink
    XADataLocator_NativeDisplay loc_nd = {
            XA_DATALOCATOR_NATIVEDISPLAY,        // locatorType
            // the video sink must be an ANativeWindow created from a Surface or SurfaceTexture
            (void*)theNativeWindow,              // hWindow
            // must be NULL
            NULL                                 // hDisplay
    };
    XADataSink imageVideoSink = {&loc_nd, NULL};

    // declare interfaces to use
    XAboolean     required[NB_MAXAL_INTERFACES]
                           = {XA_BOOLEAN_TRUE, XA_BOOLEAN_TRUE,           XA_BOOLEAN_TRUE};
    XAInterfaceID iidArray[NB_MAXAL_INTERFACES]
                           = {XA_IID_PLAY,     XA_IID_ANDROIDBUFFERQUEUESOURCE,
                                               XA_IID_STREAMINFORMATION};

    if(audioOnly == true){
        res = (*engineEngine)->CreateMediaPlayer(engineEngine, &playerObj, &dataSrc,
                NULL, &audioSnk,
                NULL,
                NULL, NULL,
                NB_MAXAL_INTERFACES /*XAuint32 numInterfaces*/,
                iidArray /*const XAInterfaceID *pInterfaceIds*/,
                required /*const XAboolean *pInterfaceRequired*/);
        CHECK(XA_RESULT_SUCCESS == res);
    }else{
        res = (*engineEngine)->CreateMediaPlayer(engineEngine, &playerObj, &dataSrc,
                NULL, &audioSnk,
                &imageVideoSink,
                NULL, NULL,
                NB_MAXAL_INTERFACES /*XAuint32 numInterfaces*/,
                iidArray /*const XAInterfaceID *pInterfaceIds*/,
                required /*const XAboolean *pInterfaceRequired*/);
        CHECK(XA_RESULT_SUCCESS == res);
    }

    // realize the player
    res = (*playerObj)->Realize(playerObj, XA_BOOLEAN_FALSE);
    CHECK(XA_RESULT_SUCCESS == res);

    // get the play interface
    res = (*playerObj)->GetInterface(playerObj, XA_IID_PLAY, &playerPlayItf);
    CHECK(XA_RESULT_SUCCESS == res);

    if(audioOnly == false){
        // get the stream information interface (for video size)
        res = (*playerObj)->GetInterface(playerObj, XA_IID_STREAMINFORMATION, &playerStreamInfoItf);
        CHECK(XA_RESULT_SUCCESS == res);
    }

    // get the volume interface
    res = (*playerObj)->GetInterface(playerObj, XA_IID_VOLUME, &playerVolItf);
    CHECK(XA_RESULT_SUCCESS == res);

    // get the Android buffer queue interface
    res = (*playerObj)->GetInterface(playerObj, XA_IID_ANDROIDBUFFERQUEUESOURCE, &playerBQItf);
    CHECK(XA_RESULT_SUCCESS == res);

    // get the video post processing interface
    //res = (*playerObj)->GetInterface(playerObj, XA_IID_VIDEOPOSTPROCESSING, &playerVideoPostProcessingItf);
    //CHECK(XA_RESULT_SUCCESS == res);

    // specify which events we want to be notified of
    res = (*playerBQItf)->SetCallbackEventsMask(playerBQItf, XA_ANDROIDBUFFERQUEUEEVENT_PROCESSED);
    CHECK(XA_RESULT_SUCCESS == res);

    // register the callback from which OpenMAX AL can retrieve the data to play
    res = (*playerBQItf)->RegisterCallback(playerBQItf, AndroidBufferQueueCallback, NULL);
    CHECK(XA_RESULT_SUCCESS == res);

    if(audioOnly == false){
        // we want to be notified of the video size once it's found, so we register a callback for that
        res = (*playerStreamInfoItf)->RegisterStreamChangeCallback(playerStreamInfoItf, StreamChangeCallback, NULL);
        CHECK(XA_RESULT_SUCCESS == res);
    }

    // enqueue the initial buffers
    if (!enqueueInitialBuffers(false)) {
        return false;
    }

    // prepare the player
    //res = (*playerPlayItf)->SetPlayState(playerPlayItf, XA_PLAYSTATE_PAUSED);
    //CHECK(XA_RESULT_SUCCESS == res);

    // set the volume
    res = (*playerVolItf)->SetVolumeLevel(playerVolItf, 0);
    CHECK(XA_RESULT_SUCCESS == res);
    
    //res = (*playerVideoPostProcessingItf)->SetScaleOptions(playerVideoPostProcessingItf,
    //                                                       XA_VIDEOSCALE_STRETCH,0x00000000,0);
    //CHECK(XA_RESULT_SUCCESS == res);

    

    // start the playback
    res = (*playerPlayItf)->SetPlayState(playerPlayItf, XA_PLAYSTATE_PLAYING);
    CHECK(XA_RESULT_SUCCESS == res);

    
    return true;
}



void ContentRecv::shutdownMediaPlayer()
{
    if (playerObj != NULL) {
        (*playerObj)->Destroy(playerObj);
        playerObj = NULL;
        playerPlayItf = NULL;
        playerBQItf = NULL;
        playerStreamInfoItf = NULL;
        playerVolItf = NULL;
    }
    
    if (outputMixObject != NULL) {
        (*outputMixObject)->Destroy(outputMixObject);
        outputMixObject = NULL;
    }

    
}

void ContentRecv::shutdownMediaEngine()
{
    
    if (engineObject != NULL) {
        (*engineObject)->Destroy(engineObject);
        engineObject = NULL;
        engineEngine = NULL;
    }

}

void ContentRecv::setMute(bool mute)
{
    XAresult res;

    if(hdcpEnabled){
        mhdcpRecv->setMute(mute);
    }else{
        if(playerVolItf == NULL)
            return;

        res = (*playerVolItf)->SetMute(playerVolItf, mute);
        CHECK(XA_RESULT_SUCCESS == res);
    }
    
    

    return;

}

struct ifaddrs *get_interface(const char *name, sa_family_t family)
{
    unsigned addr, flags;
    int masklen;
    struct ifaddrs *ifa;
    struct sockaddr_in *saddr = NULL;
    struct sockaddr_in *smask = NULL;
    struct sockaddr_ll *hwaddr = NULL;
    unsigned char hwbuf[ETH_ALEN];

    if (ifc_get_info(name, &addr, &masklen, &flags))
        return NULL;

    if ((family == AF_INET) && (addr == 0))
        return NULL;

    ifa = (struct ifaddrs *)malloc(sizeof(struct ifaddrs));
    if (!ifa)
        return NULL;
    memset(ifa, 0, sizeof(struct ifaddrs));

    ifa->ifa_name = (char*)malloc(strlen(name)+1);
    if (!ifa->ifa_name) {
        free(ifa);
        return NULL;
    }
    strcpy(ifa->ifa_name, name);
    ifa->ifa_flags = flags;

    if (family == AF_INET) {
        saddr = (struct sockaddr_in *)malloc(sizeof(struct sockaddr_in));
        if (saddr) {
            saddr->sin_addr.s_addr = addr;
            saddr->sin_family = family;
        }
        ifa->ifa_addr = (struct sockaddr *)saddr;

        if (masklen != 0) {
            smask = (struct sockaddr_in *)malloc(sizeof(struct sockaddr_in));
            if (smask) {
                smask->sin_addr.s_addr = prefixLengthToIpv4Netmask(masklen);
                smask->sin_family = family;
            }
        }
        ifa->ifa_netmask = (struct sockaddr *)smask;
    } else if (family == AF_PACKET) {
        if (!ifc_get_hwaddr(name, hwbuf)) {
            hwaddr = (sockaddr_ll*)malloc(sizeof(struct sockaddr_ll));
            if (hwaddr) {
                memset(hwaddr, 0, sizeof(struct sockaddr_ll));
                hwaddr->sll_family = family;
                /* hwaddr->sll_protocol = ETHERTYPE_IP; */
                hwaddr->sll_hatype = ARPHRD_ETHER;
                hwaddr->sll_halen = ETH_ALEN;
                memcpy(hwaddr->sll_addr, hwbuf, ETH_ALEN);
            }
        }
        ifa->ifa_addr = (struct sockaddr *)hwaddr;
        ifa->ifa_netmask = (struct sockaddr *)smask;
    }
    return ifa;
}

int getifaddrs(struct ifaddrs **ifap)
{
    DIR *d;
    struct dirent *de;
    struct ifaddrs *ifa;
    struct ifaddrs *ifah = NULL;

    if (!ifap)
        return -1;
    *ifap = NULL;

    if (ifc_init())
       return -1;

    d = opendir("/sys/class/net");
    if (d == 0)
        return -1;
    while ((de = readdir(d))) {
        if (de->d_name[0] == '.')
            continue;
        ifa = get_interface(de->d_name, AF_INET);
        if (ifa != NULL) {
            ifa->ifa_next = ifah;
            ifah = ifa;
        }
        ifa = get_interface(de->d_name, AF_PACKET);
        if (ifa != NULL) {
            ifa->ifa_next = ifah;
            ifah = ifa;
        }
    }
    *ifap = ifah;
    closedir(d);
    ifc_close();
    return 0;
}

void freeifaddrs(struct ifaddrs *ifa)
{
    struct ifaddrs *ifp;

    while (ifa) {
        ifp = ifa;
        free(ifp->ifa_name);
        if (ifp->ifa_addr)
            free(ifp->ifa_addr);
        if (ifp->ifa_netmask)
            free(ifp->ifa_netmask);
        ifa = ifa->ifa_next;
        free(ifp);
    }
}

} // namespace android