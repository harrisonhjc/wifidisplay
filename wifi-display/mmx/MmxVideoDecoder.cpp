#define LOG_TAG "MmxVideoDecoder"

#undef NDEBUG 
#define LOG_NDEBUG   0   //LOGV
#define LOG_NIDEBUG  0   //LOGI
#define LOG_NDDEBUG 0    //LOGD
#define LOG_NEDEBUG 0    //LOGD

#include <utils/Log.h>
#include <sys/types.h>
#include <media/stagefright/foundation/hexdump.h>
#include <media/stagefright/foundation/ADebug.h>
#include <jni.h>
#include "MmxVideoDecoder.h"
#include "MmxUtil.h"




namespace android {

extern int getStopIssued(void);
extern JavaVM *mJvm;

typedef struct
{
    sSX_DESC   *head;
    sSX_DESC   *tail;

} sSLICE_CHAIN;

// Decoder control block
typedef struct
{
    pthread_t       decoder_thread;
    UINT32          look_for_new_slice;
    UINT32          pes_payload_curr_len;
    UINT32          last_cc;
    UINT32          slice_pkt_count;
    UINT64          slice_pts_ms;
    UINT64          slice_count;
    UINT16          last_seq_num;
    sSLICE_CHAIN    slice_chain;

} sDECODER_CBLK;

static sDECODER_CBLK f_cblk;



static sSX_DESC * slice_get()
{
    sSX_DESC *head;


    head = f_cblk.slice_chain.head;

    f_cblk.slice_chain.head = NULL;
    f_cblk.slice_chain.tail = NULL;

//    printf("slice_get() invoked\n");

    return head;
}


/**
 * Dumps a slice onto the queue.
 *
 * @param pts_ms
 * @param slice_head
 */static void video_decoder_slice_dump(UINT64 pts_ms, sSX_DESC *slice_head)
{
    sSLICE_HDR *hdr = (sSLICE_HDR *)malloc(sizeof(sSLICE_HDR));
    CHECK(hdr != NULL);

    hdr->type       = SLICE_TYPE_SLICE;
    hdr->timestamp  = pts_ms;

    sSX_DESC *new_desc = (sSX_DESC *)MmxUtil::sx_desc_get();

    new_desc->data = (unsigned char*) hdr;
    new_desc->data_len = sizeof(sSLICE_HDR);

    new_desc->next = slice_head;

    MmxUtil::sx_pipe_put(SX_VRDMA_SLICE, new_desc);

}


/**
 * Drops a given slice.
 */
static void slice_drop()
{
    sSX_DESC  *curr;
    sSX_DESC  *next;


    // Drop slice.
    MmxUtil::sx_desc_put2(f_cblk.slice_chain.head);

    // Reset head and tail.
    f_cblk.slice_chain.head = NULL;
    f_cblk.slice_chain.tail = NULL;

    ALOGI("video slice_dropped invoked.");
}


/**
 * Add a packet to a slice chain.
 *
 * @param desc
 */
static void slice_pkt_add(sSX_DESC *desc)
{
    CHECK(desc != NULL);


    if(f_cblk.slice_chain.head == NULL)
    {
        f_cblk.slice_chain.head = desc;
        f_cblk.slice_chain.tail = desc;

        return;
    }

    // Append to tail.
    f_cblk.slice_chain.tail->next = desc;

    // Update tail.
    f_cblk.slice_chain.tail = desc;
}


/**
 * Interpret mpeg 2 transport stream data.
 *
 * @param desc
 * @param pes_payload_size
 * @param cc_start
 * @param cc_end
 */
static void m2ts_data_interpret(
    sSX_DESC   *desc,
    UINT32     *pes_payload_size,
    UINT8      *cc_start,
    UINT8      *cc_end
    )
{
    UINT8  *curr_ptr;
    UINT32  bytes_left;
    UINT32  afc;
    UINT32  pid;
    UINT32  pes_byte_count;
    UINT32  payload_size;
    UINT8   cc;


    // Get current.
    curr_ptr = desc->data;

    // Get data left.
    bytes_left = desc->data_len;
    CHECK(bytes_left > sizeof(sRTP_HDR));

    // Get RTP header.
    // sRTP_HDR *rtp_hdr = (sRTP_HDR *) curr_ptr;

    // Get TS header.
    curr_ptr += sizeof(sRTP_HDR);

    // Get TS bytes left.
    bytes_left -= sizeof(sRTP_HDR);
    CHECK((bytes_left % sizeof(sMPEG2_TS)) == 0);

    sMPEG2_TS *ts = (sMPEG2_TS *) curr_ptr;

    UINT8   first_chunk = 1;

    pes_byte_count = 0;
    do
    {
        sMPEG2_TS *ts = (sMPEG2_TS *) curr_ptr;
        afc = AFC_GET(ts->hdr);
        pid = PID_GET(ts->hdr);

        if(pid == 0x1011)
        {
            cc = CC_GET(ts->hdr);
            if(first_chunk)
            {
                *cc_start = cc;

                first_chunk = 0;
            }

            if(PUSI_GET(ts->hdr))
            {
                // CHECK(afc == 0x03);

                //            printf("### 0x%x 0x%x 0x%x 0x%x\n",
                //                   ts->payload.payload[14],
                //                   ts->payload.payload[15],
                //                   ts->payload.payload[16],
                //                   ts->payload.payload[17]);

                payload_size = (sizeof(sMPEG2_TS_PAYLOAD) - 14);
            }
            else
            {
                if(afc == 0x01)
                {
                    payload_size = sizeof(sMPEG2_TS_PAYLOAD);
                }
                else if(afc == 0x03)
                {
                    payload_size = sizeof(sMPEG2_TS_PAYLOAD) - 1 - ts->payload.payload[0];
                }
                else
                {
                    CHECK(0);
                }
            }

            pes_byte_count += payload_size;
        }

        bytes_left -= sizeof(sMPEG2_TS);
        curr_ptr += sizeof(sMPEG2_TS);

    } while (bytes_left > 0);

    *cc_end = cc;
    *pes_payload_size = pes_byte_count;
}
/**
 * Find the slice start.
 *
 * @param desc
 * @param pes_payload_size
 * @param pts_ms
 * @return
 */
static UINT8 slice_start_find(
    sSX_DESC   *desc,
    UINT32     *pes_payload_size,
    UINT64     *pts_ms
    )
{
    UINT8      *curr_ptr;
    UINT32      bytes_left;
    sMPEG2_TS  *ts;
    UINT32      pid;
    UINT32      afc;


    // Get current.
    curr_ptr = desc->data;

    // Get data left.
    bytes_left = desc->data_len;
    CHECK(bytes_left > sizeof(sRTP_HDR));

    // Get RTP header.
    sRTP_HDR *rtp_hdr = (sRTP_HDR *) curr_ptr;

    // Get TS header.
    curr_ptr += sizeof(sRTP_HDR);

    // Get TS bytes left.
    bytes_left -= sizeof(sRTP_HDR);
    CHECK((bytes_left % sizeof(sMPEG2_TS)) == 0);

    do
    {
        sMPEG2_TS *ts = (sMPEG2_TS *) curr_ptr;

        afc = AFC_GET(ts->hdr);
        pid = PID_GET(ts->hdr);

        if(PUSI_GET(ts->hdr) && (pid == 0x1011)) {
            
            curr_ptr = &ts->payload.payload[0];
            UINT8 adaptation_field_len = 0;
            if(afc & 0x02) {
                // adaption field is present.
                // determine how many bytes to skip.
              adaptation_field_len = 1 + *curr_ptr;
            }

            // Skip 'adaptation field length' field.
            curr_ptr += adaptation_field_len;

            // Skip adaptation field length.
            curr_ptr += sizeof(sPES);

            sPES_EXT *pes_ext = (sPES_EXT *) curr_ptr;

            UINT16  len = ntohs(pes_ext->length);
            if(len > 0)
            {
                *pes_payload_size = len - 8;
            }
            else
            {
                *pes_payload_size = 0;
            }

            curr_ptr += sizeof(sPES_EXT);

            if(*curr_ptr != 0x05)
            {
                printf("0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x\n",
                       ((UINT8 *) ts)[0],
                       ((UINT8 *) ts)[1],
                       ((UINT8 *) ts)[2],
                       ((UINT8 *) ts)[3],
                       ((UINT8 *) ts)[4],
                       ((UINT8 *) ts)[5],
                       ((UINT8 *) ts)[6],
                       ((UINT8 *) ts)[7]);

                printf("0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x\n",
                       ((UINT8 *) ts)[8],
                       ((UINT8 *) ts)[9],
                       ((UINT8 *) ts)[10],
                       ((UINT8 *) ts)[11],
                       ((UINT8 *) ts)[12],
                       ((UINT8 *) ts)[13],
                       ((UINT8 *) ts)[14],
                       ((UINT8 *) ts)[15]);

                printf("*curr_ptr = 0x%x, delta = %u\n",
                       *curr_ptr,
                       (UINT8 *)curr_ptr - (UINT8 *) ts);

                // Consistency check.
                //CHECK(0);
            }

            curr_ptr++;

            UINT32  i;
            UINT64  pts;
            pts = 0;
            for(i = 0; i < 5; i++)
            {
                pts = ((pts << 8) | curr_ptr[i]);
            }

            UINT64 pts_hz;
            UINT64 mask;

            pts_hz = 0;

            mask = 0x0007;
            pts_hz |= (pts & (mask << 33)) >> 3;

            mask = 0x7fff;
            pts_hz |= (pts & (mask << 17)) >> 2;

            mask = 0x7fff;
            pts_hz |= (pts & (mask << 1)) >> 1;

            // Convert to ms.
            *pts_ms = pts_hz / 90;

            return 1;
        }

        curr_ptr += sizeof(sMPEG2_TS);
        bytes_left -= sizeof(sMPEG2_TS);

    } while (bytes_left > 0);

    return 0;
}

MmxVideoDecoder::~MmxVideoDecoder()
{
    //ALOGI("~MmxVideoDecoder:");
    
}


status_t MmxVideoDecoder::startService()
{
    //ALOGI("start: 3");
    
    f_cblk.look_for_new_slice = 1;
    //run("MmxVideoDecoder");
    startThread();
    return OK;
    
}
status_t MmxVideoDecoder::stopService()
{
    //ALOGI("stop:");
    return OK;
}

void  MmxVideoDecoder::startThread(void)
{
    MmxVideoDecoderPtr t = &MmxVideoDecoder::threadLoop;
    PthreadPtr p = *(PthreadPtr*)&t;
    pthread_t    tid;
    if(pthread_create(&tid, 0, p, this) == 0){
        pthread_detach(tid);
    }
}

void* MmxVideoDecoder::threadLoop(void)
{
	ALOGV("MmxVideoDecoder::threadLoop++");
    
    JNIEnv *env = NULL;
    int isAttached = 0;

    if(mJvm){
        if(mJvm->AttachCurrentThread(&env, NULL) < 0){
            ALOGE("MmxVideoDecoder::threadLoop:thread can not attach current thread." );
            return NULL;
        }
        isAttached = 1;
    }
	
    androidSetThreadPriority(0, VIDEO_DECODER_THREAD_PRIORITY);

    sSX_DESC  *desc;
    sSX_DESC  *slice_start;
    sSX_DESC  *slice_end;
    UINT8      *curr_ptr;
    UINT32      bytes_left;
    sMPEG2_TS  *ts;
    UINT32      pes_payload_len;
    UINT8       slice_start_found;
    UINT8       cc_start;
    UINT8       cc_end;
    UINT32      pes_payload_size;


    while(getStopIssued() == 0)
    {
        do
        {
            desc = (sSX_DESC*)MmxUtil::sx_pipe_get(SX_VRDMA_VIDEO_PKT_QUEUE);
            if(desc == NULL)
            {
                break;
            }


            // Get current.
            curr_ptr = desc->data;

            // Get data left.
            bytes_left = desc->data_len;
            CHECK(bytes_left > sizeof(sRTP_HDR));

            // Get RTP header.
            sRTP_HDR *rtp_hdr = (sRTP_HDR *) curr_ptr;

            // Get TS header.
            curr_ptr += sizeof(sRTP_HDR);
            ts = (sMPEG2_TS *) curr_ptr;

            // Always Look for slice start.
            UINT64  pts_ms;
            slice_start_found = slice_start_find(desc, &pes_payload_len, &pts_ms);

            if(f_cblk.look_for_new_slice)
            {
                if(!slice_start_found)
                {
                    // Looking for a new slice but didn't find a slice start.
                    //
                    // Retry with the next packet.

                    goto cleanup;
                }

                // Found new slice. Keep going.
                f_cblk.look_for_new_slice = 0;
            }

            if(slice_start_found)
            {
//                printf("New slice: RTP seq = %u, pts = %llu\n",
//                        pts_ms,
//                        ntohs(rtp_hdr->sequence_num));

                if(f_cblk.slice_count == 0)
                {
                    f_cblk.slice_pts_ms = pts_ms;
                }

                if(f_cblk.slice_pkt_count > 0)
                {
//                    printf("slice pkt count = %u, pes_payload_size = %u\n",
//                           f_cblk.slice_pkt_count,
//                           f_cblk.pes_payload_curr_len);

                    // If we already have a chain and found a slice start,
                    // dump the slice.
                    video_decoder_slice_dump(f_cblk.slice_pts_ms, slice_get());

                    // Reset slice packet count.
                    f_cblk.slice_pkt_count      = 0;
                    f_cblk.pes_payload_curr_len = 0;
                    f_cblk.slice_pts_ms         = pts_ms;
                }

                f_cblk.slice_count++;
            }

            // Interpret packet.
            m2ts_data_interpret(desc, &pes_payload_size, &cc_start, &cc_end);

            f_cblk.pes_payload_curr_len += pes_payload_size;

            if(f_cblk.slice_pkt_count == 0)
            {
                // If first packet, just cache the last CC.
                f_cblk.last_cc = cc_end;
            }
            else
            {
                // If not first packet, check for continuity.
                if(((f_cblk.last_cc + 1) & 0x0F) == cc_start)
                {
                    // Passes continuity test.

                    f_cblk.last_cc = cc_end;
                }
                else
                {
                    // Fails continuity test.

                    ALOGI("video_decoder: last rtp seq num = %d\n", f_cblk.last_seq_num);
                    ALOGI("video_decoder: curr rtp seq num = %d\n", ntohs(rtp_hdr->sequence_num));

                    ALOGI("video_decoder: Packet loss detected! last_cc = %u, start_cc = %d\n",
                            f_cblk.last_cc, cc_start);

                    slice_drop();

                    f_cblk.look_for_new_slice = 1;

                    f_cblk.slice_pkt_count = 0;

                    goto cleanup;
                }
            }

            slice_pkt_add(desc);

//            sx_pipe_put(SX_VRDMA_SLICE, desc);

            f_cblk.slice_pkt_count++;

//            printf("(video_decoder): ### 4, slice_pkt_count = %d, curr len = %d, total len = %d\n",
//                    f_cblk.slice_pkt_count,
//                    f_cblk.pes_payload_curr_len,
//                    f_cblk.pes_payload_len);

            cleanup:

            f_cblk.last_seq_num = ntohs(rtp_hdr->sequence_num);

        } while(getStopIssued() == 0);

        usleep(500); //500
    }
    
    if(isAttached){
        mJvm->DetachCurrentThread();
    }
    pthread_exit((void *)0);
    
    return 0;
}


} // namespace android