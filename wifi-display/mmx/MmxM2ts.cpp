#define LOG_TAG "MmxM2ts"
/*
#undef NDEBUG 
#define LOG_NDEBUG   0   //LOGV
#define LOG_NIDEBUG  0   //LOGI
#define LOG_NDDEBUG 0    //LOGD
#define LOG_NEDEBUG 0    //LOGD
*/
#include <utils/Log.h>
#include <sys/types.h>
#include <media/stagefright/foundation/hexdump.h>
#include <media/stagefright/foundation/ADebug.h>
#include <jni.h>

#include "MmxM2ts.h"
#include "MmxUtil.h"




namespace android {

extern int getStopIssued(void);
extern JavaVM  *mJvm;

typedef enum
{
    PKT_TYPE_AUDIO,
    PKT_TYPE_VIDEO,
    PKT_TYPE_NULL

} ePKT_TYPE;

static UINT64 pcr_get(sSX_DESC *desc);
static ePKT_TYPE pkt_type_get(sSX_DESC *desc);

MmxM2ts::~MmxM2ts()
{
    //ALOGI("~MmxM2ts:");
    
}


status_t MmxM2ts::startService()
{
    //run("MmxM2ts");
    startThread();
    return OK;
    
}
status_t MmxM2ts::stopService()
{
    //ALOGI("stop:");
    
    return OK;
    
}

void  MmxM2ts::startThread(void)
{
    MmxM2tsPtr t = &MmxM2ts::threadLoop;
    PthreadPtr p = *(PthreadPtr*)&t;
    pthread_t    tid;
    if(pthread_create(&tid, 0, p, this) == 0){
        pthread_detach(tid);
    }
}

void* MmxM2ts::threadLoop(void)
{
	//ALOGI("threadLoop:+ 2");
    JNIEnv *env = NULL;
    int isAttached = 0;
    if(mJvm){
        if(mJvm->AttachCurrentThread(&env, NULL) < 0){
            ALOGE("MmxRecv::threadLoop:thread can not attach current thread." );
            return NULL;
        }
        isAttached = 1;
    }
	
    androidSetThreadPriority(0, M2TS_PKT_PROCESS_THREAD_PRIORITY);

    SX_QUEUE   *queue;
    sSX_DESC   *desc;
    sSX_DESC   *h264_desc;
    sSX_DESC   *lpcm_desc;
    UINT32      bytes_left;


    while(getStopIssued() == 0)
    {
        do
        {
            desc = (sSX_DESC*)MmxUtil::sx_pipe_get(SX_VRDMA_PKT_QUEUE);
            if(desc == NULL)
            {
                break;
            }

            // Get data left.
            bytes_left = desc->data_len;
            CHECK(bytes_left > sizeof(sRTP_HDR));

            // ------------------------
            //  Get and push program reference time.
            // ------------------------

            UINT64 pcr_ms = pcr_get(desc);
            if(pcr_ms > 0)
            {
                sSX_DESC *new_desc = (sSX_DESC*)MmxUtil::sx_desc_get();

                sSLICE_HDR *hdr = (sSLICE_HDR*)malloc(sizeof(sSLICE_HDR));
                CHECK(hdr != NULL);

                hdr->type       = SLICE_TYPE_PCR;
                hdr->timestamp  = pcr_ms;

                new_desc->data = (unsigned char *) hdr;
                new_desc->data_len = sizeof(sSLICE_HDR);

                MmxUtil::sx_pipe_put(SX_VRDMA_PCR, new_desc);
            }

            // ------------------------
            //  Get and push media packet.
            // ------------------------

            ePKT_TYPE pkt_type = pkt_type_get(desc);
            switch(pkt_type)
            {
                case PKT_TYPE_VIDEO:
                {
                    MmxUtil::sx_pipe_put(SX_VRDMA_VIDEO_PKT_QUEUE, desc);
                    break;
                }
                case PKT_TYPE_AUDIO:
                {
                    
                    MmxUtil::sx_pipe_put(SX_VRDMA_AUDIO, desc);
                    break;
                }
                case PKT_TYPE_NULL:
                {
                    MmxUtil::sx_desc_put2(desc);

                    break;
                }
                default:
                {
                    CHECK(0);
                    break;
                }
            }

        } while(getStopIssued() == 0);

        usleep(500); //500
    }

    
    if(isAttached){
        mJvm->DetachCurrentThread();
    }
    pthread_exit((void *)0);
    
    return 0;
}

static UINT64 pcr_get(sSX_DESC   *desc)
{
    UINT8  *curr_ptr;
    UINT32  bytes_left;

    static UINT64   last_pcr_ms;


    // Get current.
    curr_ptr = desc->data;

    // Get data left.
    bytes_left = desc->data_len;
    assert(bytes_left > sizeof(sRTP_HDR));

    // Get TS header.
    curr_ptr += sizeof(sRTP_HDR);

    // Get TS bytes left.
    bytes_left -= sizeof(sRTP_HDR);
    assert((bytes_left % sizeof(sMPEG2_TS)) == 0);

    do
    {
        sMPEG2_TS *ts = (sMPEG2_TS *) curr_ptr;
        UINT16 pid = PID_GET(ts->hdr);

        if(pid == 0x1000)
        {
            curr_ptr += (sizeof(sMPEG2_TS_HDR) + 2);

            UINT64  pcr = 0;
            UINT32  i;
            for(i = 0; i < 6; i++)
            {
                pcr = ((pcr << 8) | curr_ptr[i]);
            }

//            printf("(pcr_get): curr_ptr: 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x\n",
//                    curr_ptr[0],
//                    curr_ptr[1],
//                    curr_ptr[2],
//                    curr_ptr[3],
//                    curr_ptr[4],
//                    curr_ptr[5]);
//
//            printf("(pcr_get): pcr: 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x\n",
//                    ((UINT8 *) &pcr)[0],
//                    ((UINT8 *) &pcr)[1],
//                    ((UINT8 *) &pcr)[2],
//                    ((UINT8 *) &pcr)[3],
//                    ((UINT8 *) &pcr)[4],
//                    ((UINT8 *) &pcr)[5]);

            UINT64 pcr_base = (pcr >> (9 + 6));

            UINT64 pcr_ext = pcr & (0x1FF);

            pcr = pcr_base * 300 + pcr_ext;

            UINT64  pcr_ms = pcr / 27000;

//            printf("(pcr_get): pcr = %llu, pcr_ms = %llu, delta = %u\n",
//                    pcr,
//                    pcr_ms,
//                    (UINT32) (pcr_ms - last_pcr_ms));

            last_pcr_ms = pcr_ms;

            return pcr_ms;
        }

        bytes_left -= sizeof(sMPEG2_TS);
        curr_ptr += sizeof(sMPEG2_TS);

    } while (bytes_left > 0);

    return 0;
}


static ePKT_TYPE pkt_type_get(sSX_DESC *desc)
{
    UINT8  *curr_ptr;
    UINT32  bytes_left;


    // Get current.
    curr_ptr = desc->data;

    // Get data left.
    bytes_left = desc->data_len;
    assert(bytes_left > sizeof(sRTP_HDR));

    // Get TS header.
    curr_ptr += sizeof(sRTP_HDR);

    // Get TS bytes left.
    bytes_left -= sizeof(sRTP_HDR);
    assert((bytes_left % sizeof(sMPEG2_TS)) == 0);

    // printf("(mgmt_m2ts): pkt_type_get() invoked!\n");

    do
    {
        sMPEG2_TS *ts = (sMPEG2_TS *) curr_ptr;
        UINT16 pid = PID_GET(ts->hdr);

#if 0
        printf("(mgmt_m2ts): 0x%x 0x%x 0x%x 0x%x\n",
                ts->hdr.sync_byte,
                ts->hdr.tei_pusi_tp_pid1,
                ts->hdr.pid2,
                ts->hdr.tsc_afc_cc);

        printf("(mgmt_m2ts): pid = 0x%.4x\n", pid);
#endif

        if(pid == 0x1011)
        {
            return PKT_TYPE_VIDEO;
        }
        else if(pid == 0x1100)
        {
            return PKT_TYPE_AUDIO;
        }

        bytes_left -= sizeof(sMPEG2_TS);
        curr_ptr += sizeof(sMPEG2_TS);

    } while (bytes_left > 0);

    return PKT_TYPE_NULL;
}

} // namespace android