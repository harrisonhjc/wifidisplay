#ifndef _MMX_AUDIO_RENDERERAAC_H
#define _MMX_AUDIO_RENDERERAAC_H

#include "MmxUtil.h"
#include <SLES/OpenSLES.h>
#include <SLES/OpenSLES_Android.h>

namespace android {

typedef struct
{
	unsigned int    buffer_len;
    unsigned char   *buffer;
    
    
} sDECODER_AUDIO_HW_BUFFER;



struct MmxAudioRenderer : public RefBase
{
	typedef  void* (MmxAudioRenderer::*MmxAudioRendererPtr)(void);
    typedef  void* (*PthreadPtr)(void*);
    void  startThread(void);
    void* threadLoop(void);

    virtual ~MmxAudioRenderer();
    status_t startService();
    status_t stopService();
    
	
	void audio_sink_init();
	void audio_sink_release();
	sDECODER_AUDIO_HW_BUFFER* audio_sink_buffer_get();
	void setMute(bool);


	static void createEngine();
	static void createBufferQueueAudioPlayer();
	static void bqPlayerCallback(SLAndroidSimpleBufferQueueItf bq, void *context);
	static void shutdownAudioPlayer();
    
};

} // namespace android

#endif   //_MMX_AUDIO_RENDERERAAC_H
