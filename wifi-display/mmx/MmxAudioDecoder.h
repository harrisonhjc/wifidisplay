#ifndef MMX_AUDIO_DECODER_H_
#define MMX_AUDIO_DECODER_H_

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
#include <gui/Surface.h>
#include <media/stagefright/foundation/AHandler.h>


namespace android {

struct MmxAudioDecoder : public RefBase
{
	typedef  void* (MmxAudioDecoder::*MmxAudioDecoderPtr)(void);
    typedef  void* (*PthreadPtr)(void*);
    void  startThread(void);
    void* threadLoop(void);

    virtual ~MmxAudioDecoder();
    status_t startService();
    status_t stopService();
    
};

}  // namespace android

#endif  // MMX_AUDIO_DECODER_H_
