#define LOG_TAG "MmxRecv"

#undef NDEBUG 
#define LOG_NDEBUG   0  
#define LOG_NIDEBUG  0 
#define LOG_NDDEBUG 0  
#define LOG_NEDEBUG 0 

#include <utils/Log.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h>

#include "MmxRecv.h"
#include <media/stagefright/foundation/hexdump.h>
#include "MmxUtil.h"



namespace android {

extern int getStopIssued(void);
JavaVM  *mJvm;

void MmxRecv::setJvm(JavaVM *jvm){
        mJvm = jvm;
}

void MmxRecv::setScale(int type){
    if(mVideoScheduler != NULL){
        mVideoScheduler->setScale(type);
    }
}

status_t MmxRecv::init(ANativeWindow* window)
{
    MmxUtil::sx_pipe_init();
    resourceInit();
    
    mM2ts = new MmxM2ts;
    mVideoDecoder = new MmxVideoDecoder;
    mVideoScheduler = new MmxVideoScheduler;
    mAudioDecoder = new MmxAudioDecoder;
    mAudioScheduler = new MmxAudioScheduler;

    if(mM2ts == NULL ||
       mVideoDecoder == NULL ||
       mVideoScheduler == NULL ||
       mAudioDecoder == NULL ||
       mAudioScheduler == NULL )
        return -1;


    mM2ts->startService();
    mVideoDecoder->startService();
    //mVideoScheduler->startService(window);
    mAudioDecoder->startService();
    mAudioScheduler->startService();
    mVideoScheduler->init();
    
	return OK;
}

status_t MmxRecv::startService(ANativeWindow* window)
{
    //run("MmxRecv");
    if(mVideoScheduler == NULL)
        return -1;

    mVideoScheduler->startService(window);
    startThread();
    return OK;
    
}

status_t MmxRecv::resourceInit()
{
    pkt_chunk.head = (char*)malloc(PKT_CHUNK_NUM * sizeof(sPI_PORTAL_PKT));
    if(pkt_chunk.head == NULL)
        return -1;

    pkt_chunk.curr_ptr = pkt_chunk.head;
    pkt_chunk.pkt_ptr = pkt_chunk.head;
    pkt_chunk.total_read = 0;
    pkt_chunk.max_len = PKT_CHUNK_NUM * sizeof(sPI_PORTAL_PKT);
    pkt_chunk.pkt_len = sizeof(sPI_PORTAL_PKT);
    return OK;

}
void MmxRecv::resourceRelease()
{
    if(NULL != pkt_chunk.head){
        free(pkt_chunk.head);
        pkt_chunk.head = NULL;
    }
}


status_t MmxRecv::stopService()
{
    
    
    mAudioScheduler->stopService();
    mAudioDecoder->stopService();
    mVideoScheduler->stopService();
    mVideoDecoder->stopService();
    mM2ts->stopService();
    
    
    //ALOGI("stop:--");
    return OK;
    
}

void  MmxRecv::startThread(void)
{
    MmxRecvPtr t = &MmxRecv::threadLoop;
    PthreadPtr p = *(PthreadPtr*)&t;
    pthread_t    tid;
    if(pthread_create(&tid, 0, p, this) == 0){
        pthread_detach(tid);
    }
}

void* MmxRecv::threadLoop(void)
{
	
	
    JNIEnv *env = NULL;
    int isAttached = 0;

    if(mJvm){
        if(mJvm->AttachCurrentThread(&env, NULL) < 0){
            ALOGE("MmxRecv::threadLoop:thread can not attach current thread." );
            return NULL;
        }
        isAttached = 1;
    }

	status_t err = OK;
    int res;
    
    ssize_t n;
    ssize_t totalReadBytes = 0;
    struct sockaddr_in remoteAddr;
    socklen_t remoteAddrLen = sizeof(remoteAddr);
    sPI_PORTAL_PKT  *pkt;
    unsigned int    pkt_len;


    androidSetThreadPriority(0, DATA_RX_THREAD_PRIORITY);
    
    do{
        pkt_len = sizeof(sPI_PORTAL_PKT);
        pkt = (sPI_PORTAL_PKT*)pkt_chunk.pkt_ptr;
        
        n = recvfrom(mSockRtp, (void*)pkt, pkt_len, 0,
                     (struct sockaddr *)&remoteAddr, &remoteAddrLen);
        if(n < LEN_RTP_HEADER){
            //ALOGI("MmxRecv:no pkt.");
            
            continue;
        }
                
        pkt_len = n;
        pkt_chunk.curr_ptr += pkt_chunk.pkt_len;
        pkt_chunk.total_read += pkt_chunk.pkt_len;
        //put to queue
        sSX_DESC *desc  = (sSX_DESC*)MmxUtil::sx_desc_get();
        desc->data      = (unsigned char*)(pkt);
        desc->data_len  = pkt_len;
        MmxUtil::sx_pipe_put(SX_VRDMA_PKT_QUEUE, desc);
        pkt_chunk.pkt_ptr += pkt_chunk.pkt_len;
        

        //check if buffer is full.
        if((pkt_chunk.total_read + pkt_chunk.pkt_len) > pkt_chunk.max_len)
        {
            pkt_chunk.curr_ptr = pkt_chunk.head;
            pkt_chunk.pkt_ptr = pkt_chunk.head;
            pkt_chunk.total_read = 0;
        }
    }while(getStopIssued() == 0);
    resourceRelease();
    close(mSockRtp);
    close(mSockRtcp);

    
    if(isAttached){
        mJvm->DetachCurrentThread();
    }
    pthread_exit((void *)0);
    
    return 0;
}

void MmxRecv::setMute(bool status)
{
    mAudioScheduler->setMute(status);
}

void MmxRecv::setkeyPES(unsigned char* Ks, unsigned char* riv, unsigned char* lc)
{
    mVideoScheduler->setkeyPES(Ks, riv, lc);
    mAudioScheduler->setkeyPES(Ks, riv, lc);
}


status_t MmxRecv::createRtp()
{
    
    status_t err = OK;
    int res;
    int size = 32*1024*1024;
    
    
    mSockRtp = socket(AF_INET, SOCK_DGRAM, 0);
    if (mSockRtp < 0) {
        err = -errno;
        ALOGE("MmxRecv:createRtp:socket failed.");
        return false;
    }
    const int yes = 1;
    res = setsockopt(mSockRtp, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    if (res < 0) {
        err = -errno;
        ALOGE("MmxRecv:createRtp::setsockopt failed.");
        return false;
    }

    res = setsockopt(mSockRtp, SOL_SOCKET, SO_RCVBUF, &size, sizeof(size));
    if (res < 0) {
        err = -errno;
        ALOGE("MmxRecv:createRtp: failed.");
        return false;
    }
    
    int lowdelay = IPTOS_LOWDELAY;
    if (setsockopt(mSockRtp, IPPROTO_IP, IP_TOS, (void *)&lowdelay, 
                     sizeof(lowdelay)) < 0)
        ALOGE("MmxRecv:createRtp: IPTOS_LOWDELAY: %.100s", strerror(errno));
    
    int throughput = IPTOS_THROUGHPUT;
      if (setsockopt(mSockRtp, IPPROTO_IP, IP_TOS, (void *)&throughput, 
                     sizeof(throughput)) < 0)
        ALOGE("MmxRecv:createRtp: IPTOS_THROUGHPUT: %.100s", strerror(errno));

    err = MakeSocketNonBlocking(mSockRtp);
    if (err != OK) {
        err = -errno;
        ALOGE("MmxRecv:createRtp:MakeSocketNonBlocking failed.");
        return false;
    }
  
  
    shutdown(mSockRtp,SHUT_WR);
    unsigned localPort = 15550;
    struct sockaddr_in addr;
    memset(addr.sin_zero, 0, sizeof(addr.sin_zero));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(localPort);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    //addr.sin_addr.s_addr = inet_addr(localHost);
    res = bind(mSockRtp, (const struct sockaddr *)&addr, sizeof(addr));
    if(res < 0){
        close(mSockRtp);
        err = -errno;
        ALOGE("MmxRecv:createRtp:bind failed.");
        return false;
    }
   
    return OK;
    
}

status_t MmxRecv::createRtcp()
{
    
    status_t err = OK;
    int res;
    int size = 256 * 1024;
   
    
    mSockRtcp = socket(AF_INET, SOCK_DGRAM, 0);
    if (mSockRtcp < 0) {
        err = -errno;
        ALOGE("MmxRecv::createRtcp:socket failed.");
        return false;
    }
    res = setsockopt(mSockRtcp, SOL_SOCKET, SO_RCVBUF, &size, sizeof(size));
    if (res < 0) {
        err = -errno;
        ALOGE("MmxRecv::createRtcp:setsockopt failed.");
        return false;
    }
    const int yes = 1;
    res = setsockopt(mSockRtcp, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    if (res < 0) {
        err = -errno;
        ALOGE("MmxRecv::createRtcp::setsockopt failed.");
        return false;
    }

    res = setsockopt(mSockRtcp, SOL_SOCKET, SO_SNDBUF, &size, sizeof(size));
    if (res < 0) {
        err = -errno;
        ALOGE("MmxRecv::createRtcp:setsockopt failed.");
        return false;
    }
    err = MakeSocketNonBlocking(mSockRtcp);
    if (err != OK) {
        err = -errno;
        ALOGE("MmxRecv::createRtcp:MakeSocketNonBlocking failed.");
        return false;
    }
    localPort = 15550;
    struct sockaddr_in addr;
    memset(addr.sin_zero, 0, sizeof(addr.sin_zero));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(localPort+1);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    res = bind(mSockRtcp, (const struct sockaddr *)&addr, sizeof(addr));
    if(res < 0){
        close(mSockRtcp);
        err = -errno;
        ALOGE("MmxRecv::createRtcp:bind failed.");
        return false;
    }
    
    return OK;
    
}

status_t MmxRecv::connectRemote(const char *remoteHost, int32_t remoteRtpPort, int32_t remoteRctpPort)
{
    Mutex::Autolock autoLock(mLock);
  
    
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
                        mSockRtp,
                        (const struct sockaddr *)&remoteAddr,
                        sizeof(remoteAddr));

        if (res < 0) {
            err = -errno;
        }
        
        remoteAddr.sin_port = htons(remoteRctpPort);
        res = connect(
                        mSockRtcp,
                        (const struct sockaddr *)&remoteAddr,
                        sizeof(remoteAddr));

        if (res < 0) {
            err = -errno;
        }
        
    }
    return err;
    
}
status_t MmxRecv::MakeSocketNonBlocking(int s) {
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