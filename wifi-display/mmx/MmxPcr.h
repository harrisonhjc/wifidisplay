#ifndef MMX_PCR_H_
#define MMX_PCR_H_

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

struct MmxPcr : public RefBase
{
	typedef  void* (MmxPcr::*MmxPcrPtr)(void);
    typedef  void* (*PthreadPtr)(void*);
    void  startThread(void);
    void* threadLoop(void);

    virtual ~MmxPcr();
    status_t startService();
    status_t stopService();
    
    
};

}  // namespace android

#endif  // MMX_PCR_H_
