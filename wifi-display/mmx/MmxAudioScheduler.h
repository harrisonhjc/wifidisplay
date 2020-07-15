#ifndef MMX_AUDIO_SCHEDULER_H_
#define MMX_AUDIO_SCHEDULER_H_

#include <gui/Surface.h>
#include <media/stagefright/foundation/AHandler.h>
#include <media/stagefright/MediaSource.h>
#include <media/stagefright/MetaData.h>
#include <media/stagefright/OMXCodec.h>
#include <media/stagefright/MediaBufferGroup.h>
#include "MmxUtil.h"
#include "MmxAudioRenderer.h"

namespace android {

struct MmxAudioScheduler : public RefBase
{
    typedef  void* (MmxAudioScheduler::*MmxAudioSchedulerPtr)(void);
    typedef  void* (*PthreadPtr)(void*);
    void  startThread(void);
    void* threadLoop(void);

    virtual ~MmxAudioScheduler();
    status_t startService();
    status_t stopService();
    void audio_scheduler_slice_dump(sSX_DESC  *slice_head);

    void decryptPES(unsigned char* pData, UINT32 dataLen, 
                      unsigned char streamCtr[4],unsigned char inputCtr[8]);
    void getCtr(unsigned char privateData[16], unsigned char streamCtr[4], unsigned char inputCtr[8]);
    void setkeyPES(unsigned char* key1, unsigned char* key2, unsigned char* lc);
    
     unsigned char Ks[16];
     unsigned char riv[8];
     unsigned char lc[16];
     unsigned char keyPES[16];
     MmxAudioRenderer mAudioRenderer;
     void setMute(bool);
};

}  // namespace android

#endif  // MMX_AUDIO_SCHEDULER_H_
