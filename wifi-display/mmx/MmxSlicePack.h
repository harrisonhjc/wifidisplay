#ifndef MMX_SLICE_PACK_H_
#define MMX_SLICE_PACK_H_

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
#include "MmxUtil.h"

namespace android {

struct MmxSlicePack : public RefBase
{
    
    typedef  void* (MmxSlicePack::*MmxSlicePackPtr)(void);
    typedef  void* (*PthreadPtr)(void*);
    void  startThread(void);
    void* threadLoop(void);

    virtual ~MmxSlicePack();
    status_t startService();
    status_t stopService();
    
    void video_scheduler_slice_dump(sSX_DESC *slice_head);
    void decryptPES(unsigned char* pData, UINT32 dataLen, 
                      unsigned char streamCtr[4],unsigned char inputCtr[8]);
    void getCtr(unsigned char privateData[16], unsigned char streamCtr[4], unsigned char inputCtr[8]);
    void setkeyPES(unsigned char* key1, unsigned char* key2, unsigned char* lc);
    
    pthread_t    tid;
    unsigned char Ks[16];
    unsigned char riv[8];
    unsigned char lc[16];
    unsigned char keyPES[16];
    
};

}  // namespace android

#endif  // MMX_SLICE_PACK_H_
