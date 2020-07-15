#define LOG_TAG "MmxPcr"
/*
#undef NDEBUG 
#define LOG_NDEBUG   0   //LOGV
#define LOG_NIDEBUG  0   //LOGI
#define LOG_NDDEBUG 0    //LOGD
#define LOG_NEDEBUG 0    //LOGD
*/
#include <utils/Log.h>
#include <sys/types.h>
#include <media/stagefright/foundation/hexdump.h>
#include <media/stagefright/foundation/ADebug.h>
#include <jni.h>

#include "MmxPcr.h"
#include "MmxUtil.h"
#include "MmxVideoScheduler.h"



namespace android {

extern int getStopIssued(void);
extern JavaVM  *mJvm;

MmxPcr::~MmxPcr()
{
    //ALOGI("~MmxPcr:");
    
}


status_t MmxPcr::startService()
{
    //run("MmxPcr");
    startThread();
    return OK;
    
}
status_t MmxPcr::stopService()
{
    //ALOGI("stop:");
    
    return OK;
    
}

/**
 * Get current time on the sink.
 *
 * @return
 */
static UINT64 sink_time_get()
{
    struct timeval  curr_time;


    // Get current time.
    gettimeofday(&curr_time, NULL);

    UINT64 temp = curr_time.tv_sec * 1000 + curr_time.tv_usec / 1000;

    return temp;
}

void  MmxPcr::startThread(void)
{
    MmxPcrPtr t = &MmxPcr::threadLoop;
    PthreadPtr p = *(PthreadPtr*)&t;
    pthread_t    tid;
    if(pthread_create(&tid, 0, p, this) == 0){
        pthread_detach(tid);
    }
}

void* MmxPcr::threadLoop(void)
{
	
    JNIEnv *env = NULL;
    int isAttached = 0;
    if(mJvm){
        if(mJvm->AttachCurrentThread(&env, NULL) < 0){
            ALOGE("MmxPcr::threadLoop:thread can not attach current thread." );
            return NULL;
        }
        isAttached = 1;
    }
    androidSetThreadPriority(0, VIDEO_SCHEDULER_THREAD_PRIORITY);

    sSX_DESC   *desc;
    UINT64      pcr_time;
    UINT64      pcr_received_time;
    UINT64      curr_time;


    while(getStopIssued() == 0)
    {
        desc = (sSX_DESC*)MmxUtil::sx_pipe_get(SX_VRDMA_PCR);
        if(desc != NULL) {
            sSLICE_HDR *hdr = (sSLICE_HDR *) desc->data;
            // Update PCR time.
            pcr_time = hdr->timestamp;
            MmxUtil::sx_desc_put(desc);
            // Cache received time.
            pcr_received_time = sink_time_get();
        }
        curr_time = sink_time_get();
        pthread_mutex_lock(&sched_f_cblk.lock);
        sched_f_cblk.curr_time = pcr_time + (curr_time - pcr_received_time);// - SX_SYSTEM_DELAY_MS;
        pthread_mutex_unlock(&sched_f_cblk.lock);
        usleep(500);
    }
    
    if(isAttached){
        mJvm->DetachCurrentThread();
    }
    pthread_exit((void *)0);
    return 0;
}


} // namespace android