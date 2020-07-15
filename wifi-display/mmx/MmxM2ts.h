#ifndef MMX_M2TS_H_
#define MMX_M2TS_H_

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


#define LEN_RTP_HEADER      12

namespace android {

struct MmxM2ts : public RefBase
{
	typedef  void* (MmxM2ts::*MmxM2tsPtr)(void);
    typedef  void* (*PthreadPtr)(void*);
    void  startThread(void);
    void* threadLoop(void);

    virtual ~MmxM2ts();
    status_t startService();
    status_t stopService();
    
    
    
};

}  // namespace android

#endif  // MMX_M2TS_H_
