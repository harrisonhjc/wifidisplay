#ifndef MMX_VIDEO_DECODER_H_
#define MMX_VIDEO_DECODER_H_


#include <media/stagefright/foundation/AHandler.h>



namespace android {

struct MmxVideoDecoder : public RefBase
{
    
    typedef  void* (MmxVideoDecoder::*MmxVideoDecoderPtr)(void);
    typedef  void* (*PthreadPtr)(void*);
    void  startThread(void);
    void* threadLoop(void);

    virtual ~MmxVideoDecoder();
    status_t startService();
    status_t stopService();
        
};

}  // namespace android

#endif  // MMX_VIDEO_DECODER_H_
