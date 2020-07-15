#define LOG_TAG "MmxVideoScheduler"

#undef NDEBUG 
#define LOG_NDEBUG   0 
#define LOG_NIDEBUG  0 
#define LOG_NDDEBUG 0
#define LOG_NEDEBUG 0 

#include <utils/Log.h>
#include <sys/types.h>
#include <media/stagefright/foundation/hexdump.h>
#include <media/stagefright/foundation/ADebug.h>
#include <gui/Surface.h>
#include <gui/SurfaceComposerClient.h>
#include <ui/DisplayInfo.h>
#include <gui/ISurfaceComposerClient.h>
#include <gui/ISurfaceComposer.h>
#include <media/stagefright/OMXClient.h>
#include <media/stagefright/FileSource.h>
#include <media/stagefright/MediaExtractor.h>

#include <ui/FramebufferNativeWindow.h>
#include <ui/GraphicBufferMapper.h>
#include <jni.h>

#include "MmxVideoScheduler.h"
#include "MmxVideoRenderer.h"
#include "MmxPcr.h"
#include "MmxSlicePack.h"
#include "AVFormatSource.h"

namespace android {

extern int getStopIssued(void);
extern JavaVM *mJvm;

sp<MmxPcr> mPcr;
sp<MmxSlicePack> mPack;
sMGMT_VIDEO_SCHEDULER_CBLK sched_f_cblk;
pthread_mutex_t scaleMutex;
int scaleIssued = 0;
int getScaleIssued(void) {
  int ret = 0;
  pthread_mutex_lock(&scaleMutex);
  ret = scaleIssued;
  pthread_mutex_unlock(&scaleMutex);
  return ret;
}
void setScaleIssued(int val) {
  pthread_mutex_lock(&scaleMutex);
  scaleIssued = val;
  pthread_mutex_unlock(&scaleMutex);
}

UINT64 estimated_source_time_get()
{
    UINT64  time;


    pthread_mutex_lock(&sched_f_cblk.lock);

    time = sched_f_cblk.curr_time;

    pthread_mutex_unlock(&sched_f_cblk.lock);

    return time;
}


MmxVideoScheduler::~MmxVideoScheduler()
{
    //ALOGI("~MmxVideoScheduler:");
    
}

status_t MmxVideoScheduler::init()
{
    mScale = NATIVE_WINDOW_SCALING_MODE_SCALE_CROP;
    pthread_mutex_init(&sched_f_cblk.lock, NULL);
    
    //mNativeWindow = window;
    //startThread();

    mmx_video_sink_init();

    mPcr = new MmxPcr;
    mPack = new MmxSlicePack;
    if(mPcr == NULL || mPack == NULL)
        return -1;

    mPcr->startService();
    mPack->startService();
    return OK;
    
}

status_t MmxVideoScheduler::startService(ANativeWindow* window)
{
    //ALOGI("start: 4");
    //pthread_mutex_init(&sched_f_cblk.lock, NULL);
    mNativeWindow = window;
    startThread();
/*
    mmx_video_sink_init();
    mPcr = new MmxPcr;
    mPack = new MmxSlicePack;
    if(mPcr == NULL || mPack == NULL)
        return -1;

    mPcr->startService();
    mPack->startService();
*/
    return OK;
    
}
status_t MmxVideoScheduler::stopService()
{
    //ALOGI("stop:");
    
    mPcr->stopService();
    mPack->stopService();
    return OK;
    
}

void MmxVideoScheduler::setkeyPES(unsigned char* ks, unsigned char* riv, unsigned char* lc)
{
    mPack->setkeyPES(ks, riv,lc);
    
}

void  MmxVideoScheduler::startThread(void)
{
    MmxVideoSchedulerPtr t = &MmxVideoScheduler::threadLoop;
    PthreadPtr p = *(PthreadPtr*)&t;
    pthread_t    tid;
    if(pthread_create(&tid, 0, p, this) == 0){
        pthread_detach(tid);
    }
}

void MmxVideoScheduler::setScale(int type)
{/*
    if(mScale == NATIVE_WINDOW_SCALING_MODE_SCALE_CROP ||
       mScale == NATIVE_WINDOW_SCALING_MODE_SCALE_TO_WINDOW){
        mScale = type;
        
    }
    */
}

void* MmxVideoScheduler::threadLoop(void)
{
    ALOGV("MmxVideoScheduler::threadLoop++");

    JNIEnv *env = NULL;
    int isAttached = 0;

    if(mJvm){
        if(mJvm->AttachCurrentThread(&env, NULL) < 0){
            ALOGE("MmxVideoScheduler::threadLoop:thread can not attach current thread." );
            return NULL;
        }
        isAttached = 1;
    }

    status_t err;
    androidSetThreadPriority(0, VIDEO_SCHEDULER_THREAD_PRIORITY);

    native_window_set_scaling_mode(mNativeWindow, mScale);
    sp<MediaSource> mVideoSource = new AVFormatSource();
    OMXClient mClient;
    mClient.connect();
    sp<MediaSource> mVideoDecoder = OMXCodec::Create(mClient.interface(),
                                                     mVideoSource->getFormat(),
                                                     false,
                                                     mVideoSource,
                                                     NULL,
                                                     0,
                                                     mNativeWindow);
    CHECK(mVideoDecoder != NULL);
    mVideoDecoder->start();
    do {
        
        MediaBuffer *mVideoBuffer;
        MediaSource::ReadOptions options;
        status_t err = mVideoDecoder->read(&mVideoBuffer, &options);
        if (err == OK) {
            if (mVideoBuffer->range_length() > 0) {
                // If video frame availabe, render it to mNativeWindow
                sp<MetaData> metaData = mVideoBuffer->meta_data();
                err = mNativeWindow->queueBuffer(mNativeWindow,
                                                 mVideoBuffer->graphicBuffer().get(), -1);
                if(err == 0){
                    metaData->setInt32(kKeyRendered, 1);
                }
            }
            mVideoBuffer->release();
        }
    } while(getStopIssued() == 0);

    mPcr->stopService();
    mPack->stopService();
    mVideoDecoder->stop();
    mVideoDecoder.clear();
    mVideoSource.clear();
    mClient.disconnect();
    
    pthread_join(mPack->tid,NULL);
    //wait mPack finished , then clear resource.
    mmx_video_sink_release();
    
    if(isAttached){
        mJvm->DetachCurrentThread();
    }
    pthread_exit((void *)0);
    return 0;
}
// Play local video file -> works.
/*
bool MmxVideoScheduler::threadLoop()
{
    ALOGI("threadLoop:+ 4");

    status_t err;

    androidSetThreadPriority(0, VIDEO_SCHEDULER_THREAD_PRIORITY);

    // At first, get an ANativeWindow from somewhere
    sp<ANativeWindow> mNativeWindow = createNativeWindow();
    CHECK(mNativeWindow != NULL);
    ALOGI("mNativeWindow");

    native_window_set_scaling_mode(mNativeWindow.get(),
                                   NATIVE_WINDOW_SCALING_MODE_SCALE_TO_WINDOW);
    // Initialize the AVFormatSource from a video file
    //sp<MediaSource> mVideoSource = new AVFormatSource();

    sp<FileSource> dataSource = new FileSource("/sdcard/h264");
    CHECK(dataSource != NULL);
    ALOGI("dataSource");

    sp<MediaExtractor> extractor = MediaExtractor::Create(dataSource);
    CHECK(extractor != NULL);
    ALOGI("extractor");

    sp<MediaSource> mVideoSource = extractor->getTrack(1);
    CHECK(mVideoSource != NULL);
    ALOGI("mVideoSource");
    // Once we get an MediaSource, we can encapsulate it with the OMXCodec now
    OMXClient mClient;
    mClient.connect();
    sp<MediaSource> mVideoDecoder = OMXCodec::Create(mClient.interface(),
                                                     mVideoSource->getFormat(),
                                                     false,
                                                     mVideoSource,
                                                     NULL,
                                                     0,
                                                     mNativeWindow);
    CHECK(mVideoDecoder != NULL);

    mVideoDecoder->start();

    ALOGI("mVideoDecoder->start");
    // Just loop to read decoded frames from mVideoDecoder
    do {
        MediaBuffer *mVideoBuffer;
        MediaSource::ReadOptions options;
        status_t err = mVideoDecoder->read(&mVideoBuffer, &options);
        if (err == OK) {
            if (mVideoBuffer->range_length() > 0) {
                ALOGI("mVideoDecoder->read:len=%d.", mVideoBuffer->range_length());
                // If video frame availabe, render it to mNativeWindow
                sp<MetaData> metaData = mVideoBuffer->meta_data();
                int64_t timeUs = 0;
                metaData->findInt64(kKeyTime, &timeUs);
                native_window_set_buffers_timestamp(mNativeWindow.get(), timeUs * 1000);
                err = mNativeWindow->queueBuffer(mNativeWindow.get(),
                                                 mVideoBuffer->graphicBuffer().get(), -1);
                if (err != 0) {
                    ALOGE("queueBuffer failed with error %s (%d)", strerror(-err), -err);
                    
                }else{
                    metaData->setInt32(kKeyRendered, 1);
                }
            }
            mVideoBuffer->release();
        }
    } while(1);

    // Finally release the resources
    mVideoSource.clear();
    mVideoDecoder->stop();
    mVideoDecoder.clear();
    mClient.disconnect();

    ALOGI("threadLoop:-");
    return false;
}
*/
/*
bool MmxVideoScheduler::threadLoop()
{
	ALOGI("threadLoop:+ 4");
	
    androidSetThreadPriority(0, VIDEO_SCHEDULER_THREAD_PRIORITY);

    sSX_DESC   *desc;
    UINT64      slice_present_time;
    sSX_DESC *curr;
    sSX_DESC *next;

    desc = NULL;
    while(1){
        
        while(1){
            // Get slice.
            if(desc == NULL){
                //printf("video_scheduler_thread:#1.\n");
                desc = (sSX_DESC*)MmxUtil::sx_pipe_get(SX_VRDMA_SLICE_READY);

                if(desc == NULL){
                    usleep(500); //500
                     //printf("3");
                    continue;
                    //goto next_iter;
                }
                sSLICE_HDR *hdr = (sSLICE_HDR *) desc->data;
                // Get PTS.
                slice_present_time = ((sSLICE_HDR *) desc->data)->timestamp;
                //printf("video_scheduler_thread:#3.\n");
            }
            
            curr = desc->next;
            CHECK(curr);
            UINT64  estimated_source_time = estimated_source_time_get();
            UINT8 present = (estimated_source_time > slice_present_time) ? 1 : 0;
            
            //UINT64  estimated_source_time = estimated_source_time_get() ;
            //UINT64  diff = estimated_source_time - slice_present_time;
            //UINT8 present = (diff < 300) ? 1 : 0;

            if(!present){
                do{
                    next = curr->next;
                    //free(curr);
                    curr = next;
                }while(curr != NULL);
                //free(desc->data);
                //free(desc);
                continue;
            }
            
            mmx_video_sink_buf_set((sDECODER_HW_BUFFER *)curr->data);
            
            do{
               
                next = curr->next;
                free(curr);
                curr = next;
               
            } while (curr != NULL);
            
            free(desc->data);
            
            free(desc);
            
            desc = NULL;
            
            break;
        }

    }
    
    ALOGI("threadLoop:-");
    return false;
}
*/


} // namespace android