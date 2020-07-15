#ifndef MMX_UTIL_H_
#define MMX_UTIL_H_

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
#include <semaphore.h>

#define UINT8   unsigned char
#define UINT16  unsigned short
#define UINT32  unsigned int
#define INT32  signed int
#define UINT64  unsigned long long
#define INT64   signed long long

#define DATA_RX_THREAD_PRIORITY             ANDROID_PRIORITY_DISPLAY     
#define M2TS_PKT_PROCESS_THREAD_PRIORITY    ANDROID_PRIORITY_NORMAL     
#define VIDEO_DECODER_THREAD_PRIORITY       ANDROID_PRIORITY_NORMAL     
#define VIDEO_SCHEDULER_THREAD_PRIORITY     ANDROID_PRIORITY_DISPLAY
#define PCR_THREAD_PRIORITY                 ANDROID_PRIORITY_NORMAL     
#define AUDIO_DECODER_THREAD_PRIORITY       ANDROID_PRIORITY_NORMAL     
#define AUDIO_SCHEDULER_THREAD_PRIORITY     ANDROID_PRIORITY_NORMAL     
#define SYS_THREAD_PRIORITY                 ANDROID_PRIORITY_NORMAL     

#define SX_SYSTEM_DELAY_MS                0 //(100)
//#define SX_SYSTEM_AUDIO_SOURCE_DELAY_MS (20) //(400)

#define SLICE_TYPE_PCR      0
#define SLICE_TYPE_SLICE    1

#define SX_VRDMA_PKT_QUEUE           0
#define SX_VRDMA_VIDEO_PKT_QUEUE     1
#define SX_VRDMA_SLICE               2
#define SX_VRDMA_SLICE_READY         3
#define SX_VRDMA_PCR                 4
#define SX_VRDMA_AUDIO               5
#define SX_VRDMA_AUDIO_SLICE         6
#define SX_VRDMA_AUDIO_SLICE_READY   7
#define SX_VRDMA_MAX                 8

#define SX_QUEUE    void *

#define PID_GET(hdr)                    ((((hdr).tei_pusi_tp_pid1 & 0x1F) << 8) | (hdr).pid2)
#define CC_GET(hdr)                     ((hdr).tsc_afc_cc & 0x0F)
#define PUSI_GET(hdr)                   (((hdr).tei_pusi_tp_pid1 & 0x40) >> 6)
#define AFC_GET(hdr)                    (((hdr).tsc_afc_cc & 0x30) >> 4)

typedef struct sNODE
{
    struct sNODE   *next;
    void           *data;

} sNODE;

typedef struct
{
    sNODE          *head;
    sNODE          *tail;
    pthread_mutex_t lock;
    sem_t           sem;
    unsigned int    len;
    UINT32          high_water_mark;

} sQUEUE;

typedef struct sSX_DESC{
    UINT8              *data;
    UINT32              data_len;
    struct sSX_DESC    *next;

} sSX_DESC;

typedef struct
{
    SX_QUEUE    queue[SX_VRDMA_MAX];

} sVRDMA_CBLK;

typedef struct
{
    UINT8   version_p_x_cc;
    UINT8   m_pt;
    UINT16  sequence_num;
    UINT32  timestamp;
    UINT32  ssrc_id;

} sRTP_HDR;

typedef struct
{
    UINT8   sync_byte;          ///< Sync byte
    UINT8   tei_pusi_tp_pid1;   ///< Transport Error Indicate (1 bit)
                                ///< Payload Unit Start Indicator (1 bit)
                                ///< Transport Priority (1 bit)
                                ///< PID (first half, 5 bit)
    UINT8   pid2;               ///< PID (second half, 8 bit)
    UINT8   tsc_afc_cc;         ///< Transport scrambling control (2 bits)
                                ///< Adaption Field Control (2 bits)
                                ///< Continuity Counter (4 bits)

} sMPEG2_TS_HDR;

// --------------------------------------------------------
// sMPEG2_TS_PAYLOAD
//      MPEG2 Transport Stream Payload
//
typedef struct
{
    UINT8   payload[184];

} sMPEG2_TS_PAYLOAD;


// --------------------------------------------------------
// sMPEG2_TS
//      MPEG2 Transport Stream
//
typedef struct
{
    sMPEG2_TS_HDR       hdr;
    sMPEG2_TS_PAYLOAD   payload;

} sMPEG2_TS;


typedef struct
{
    UINT8   prefix1;
    UINT8   prefix2;
    UINT8   prefix3;
    UINT8   stream;

} sPES;


typedef struct
{
    UINT16  length;
    UINT8   flag1;
    UINT8   flag2;

} sPES_EXT;


typedef struct
{
    UINT8   hdr_len;

} sPES_EXT2;

typedef struct
{
    UINT8   type;
    UINT8   rsvd[3];
    UINT64  timestamp;

} sSLICE_HDR;

namespace android {

struct MmxUtil
{
    

    static sSX_DESC * sx_desc_get();
    static void sx_desc_put(sSX_DESC  *desc);
    static void sx_desc_put2(sSX_DESC  *desc);

    static void sx_pipe_init();
    static void sx_pipe_put(UINT32 index, void *data);
    static void* sx_pipe_get(UINT32  index);
    static unsigned int sx_pipe_len_get(UINT32  index);

    static SX_QUEUE sx_queue_create();
    static void sx_queue_destroy(SX_QUEUE queue_id);
    static void sx_queue_push(SX_QUEUE queue_id, void *data);
    static void* sx_queue_pull(SX_QUEUE queue_id);
    static unsigned int sx_queue_len_get(SX_QUEUE queue_id);

    
};

}  // namespace android

#endif  // MMX_UTIL_H_
