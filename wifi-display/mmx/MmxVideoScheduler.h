#ifndef MMX_VIDEO_SCHEDULER_H_
#define MMX_VIDEO_SCHEDULER_H_

#include <gui/Surface.h>
#include <media/stagefright/foundation/AHandler.h>

#include "MmxUtil.h"


namespace android {

typedef struct
{
    pthread_t       video_scheduler_thread;
    pthread_t       slice_packing_thread;
    pthread_t       pcr_update_thread;
    UINT64          curr_time;
    pthread_mutex_t lock;

} sMGMT_VIDEO_SCHEDULER_CBLK;

extern sMGMT_VIDEO_SCHEDULER_CBLK sched_f_cblk;

struct MmxVideoScheduler : public RefBase
{
    
    typedef  void* (MmxVideoScheduler::*MmxVideoSchedulerPtr)(void);
    typedef  void* (*PthreadPtr)(void*);
    void  startThread(void);
    void* threadLoop(void);
    
    void setScale(int);
    virtual ~MmxVideoScheduler();
    status_t init();
    status_t startService(ANativeWindow* window);
    status_t stopService();
    void setkeyPES(unsigned char* Ks, unsigned char* riv, unsigned char* lc);
    ANativeWindow* mNativeWindow;
    int mScale;

    
};

}  // namespace android

#endif  // MMX_VIDEO_SCHEDULER_H_
