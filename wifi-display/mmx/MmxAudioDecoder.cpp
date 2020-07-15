#define LOG_TAG "MmxAudioDecoder"

#undef NDEBUG 
#define LOG_NDEBUG   0 
#define LOG_NIDEBUG  0 
#define LOG_NDDEBUG  0
#define LOG_NEDEBUG  0

#include <utils/Log.h>
#include <sys/types.h>
#include <media/stagefright/foundation/hexdump.h>
#include <media/stagefright/foundation/ADebug.h>
#include <jni.h>

#include "MmxAudioDecoder.h"
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
    UINT32          continue_current_slice;
    UINT32          pes_len;
    UINT32          pes_curr_byte_count;
    UINT32          last_seq_num;
    sSLICE_CHAIN    slice_chain;

} sDECODER_CBLK;

// Decoder control block.
static sDECODER_CBLK f_cblk;



static sSX_DESC * slice_get()
{
    sSX_DESC *head;


    head = f_cblk.slice_chain.head;

    f_cblk.slice_chain.head = NULL;
    f_cblk.slice_chain.tail = NULL;

    return head;
}


static void slice_drop()
{
    sSX_DESC  *curr;
    sSX_DESC  *next;


    // Drop slice.
    MmxUtil::sx_desc_put2(f_cblk.slice_chain.head);

    // Reset head and tail.
    f_cblk.slice_chain.head = NULL;
    f_cblk.slice_chain.tail = NULL;

    //ALOGI("audio slice dropped.");
}


static void audio_decoder_slice_dump(sSX_DESC *slice_head)
{
    MmxUtil::sx_pipe_put(SX_VRDMA_AUDIO_SLICE, slice_head);
    //ALOGI("audio_decoder_slice_dump.");
}





static void slice_pkt_add(
    sSX_DESC   *desc
    )
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

//    ALOGI("slice_pkt_add() invoked\n");
}


static UINT8 slice_start_find(
    sSX_DESC   *desc,
    UINT32     *pes_payload_size
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

        if(PUSI_GET(ts->hdr) && (pid == 0x1100))
        {
            curr_ptr = &ts->payload.payload[0];


            UINT8 adaptation_field_len = 0;
            if(afc & 0x02) {
                // adaption field is present.
                // determine how many bytes to skip.
                adaptation_field_len = 1 + *curr_ptr;
            }
            // Skip 'adaptation field length' field.
            curr_ptr += adaptation_field_len;
            curr_ptr += sizeof(sPES);
            sPES_EXT *pes_ext = (sPES_EXT *) curr_ptr;
            curr_ptr += 4;
            //PES header
            //  4  :       2        :   2   :     1     : variable len : 4     : LPCM audio data 
            //start: data len behind: config: header len:              :private:
            unsigned char pesHeaderLen = *curr_ptr + 7; //2+1+variable len+4

            UINT16  len = ntohs(pes_ext->length) - pesHeaderLen; //hdcp pes header length behind length byte
            if(len > 0){
                *pes_payload_size = len;
            }else {
                *pes_payload_size = 0;
            }
            return 1;
        }

        curr_ptr += sizeof(sMPEG2_TS);
        bytes_left -= sizeof(sMPEG2_TS);

    } while (bytes_left > 0);

    return 0;
}
static UINT32 pes_payload_size(sSX_DESC *desc)
{
    UINT8  *curr_ptr;
    UINT32  bytes_left;
    UINT32  afc;
    UINT32  pid;
    UINT32  pes_byte_count;
    UINT32  payload_size;


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

    

    pes_byte_count = 0;
    
    do {
        sMPEG2_TS *ts = (sMPEG2_TS *) curr_ptr;

        afc = AFC_GET(ts->hdr);
        pid = PID_GET(ts->hdr);

        if(pid == 0x1100) {
            
            UINT32  pes_header_len = 0;
            UINT8   stuffing = 0;
            if(afc & 0x02){
                stuffing  = 1 + ts->payload.payload[0];
                //ALOGI("afc:stuffing=%d.",stuffing);
            }
            if(PUSI_GET(ts->hdr)){
                stuffing += 8; //pes header len byte 
                //5:1+4, 1:len byte, 4:hdcp audio pes private data
                pes_header_len = ts->payload.payload[stuffing] + 5; 

                //ALOGI("PUSI_GET:pes_header_len=%d.",pes_header_len);
            } 

            payload_size = (sizeof(sMPEG2_TS_PAYLOAD) - stuffing - pes_header_len);
            pes_byte_count += payload_size;
            

            //ALOGI("payload_size = %d\n", payload_size);
        }

        curr_ptr += sizeof(sMPEG2_TS);
        bytes_left -= sizeof(sMPEG2_TS);

    } while (bytes_left > 0);

    return pes_byte_count;
}

MmxAudioDecoder::~MmxAudioDecoder()
{
    //ALOGI("~MmxAudioDecoder:");
    
}


status_t MmxAudioDecoder::startService()
{
    //ALOGI("start: 5");
    
    f_cblk.look_for_new_slice = 1;
    //run("MmxAudioDecoder");
    startThread();
    
    return OK;
    
}
status_t MmxAudioDecoder::stopService()
{
    //ALOGI("stop:");
    
    return OK;
    
}

void  MmxAudioDecoder::startThread(void)
{
    MmxAudioDecoderPtr t = &MmxAudioDecoder::threadLoop;
    PthreadPtr p = *(PthreadPtr*)&t;
    pthread_t    tid;
    if(pthread_create(&tid, 0, p, this) == 0){
        pthread_detach(tid);
    }
}

void* MmxAudioDecoder::threadLoop(void)
{
	
    JNIEnv *env = NULL;
    int isAttached = 0;

    if(mJvm){
        if(mJvm->AttachCurrentThread(&env, NULL) < 0){
            ALOGE("MmxAudioDecoder::threadLoop:thread can not attach current thread." );
            return NULL;
        }
        isAttached = 1;
    }
    
    androidSetThreadPriority(0, AUDIO_DECODER_THREAD_PRIORITY);

    sSX_DESC  *desc;
    UINT8      *curr_ptr;
    UINT32      bytes_left;
    sMPEG2_TS  *ts;

//    UINT32  last_seq_num = 0;
    UINT8   eos;


    while(getStopIssued() == 0)
    {
        do
        {
            desc = (sSX_DESC*)MmxUtil::sx_pipe_get(SX_VRDMA_AUDIO);
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

            
            //ALOGI("audio packet:");
            //hexdump(curr_ptr+sizeof(sRTP_HDR),48);

            if(f_cblk.look_for_new_slice)
            {
                if(slice_start_find(desc, &f_cblk.pes_len))
                {
                    

                    f_cblk.look_for_new_slice = 0;

                    f_cblk.continue_current_slice = 1;

                    f_cblk.pes_curr_byte_count = 0;

                    f_cblk.last_seq_num = ntohs(rtp_hdr->sequence_num) - 1;
                }
            }

            if(f_cblk.continue_current_slice)
            {
                if(ntohs(rtp_hdr->sequence_num) != (f_cblk.last_seq_num + 1))
                {
//                    ALOGI("(audio_decoder): rtp seq num = %d, last_seq_num = %d\n",
//                            ntohs(rtp_hdr->sequence_num),
//                            f_cblk.last_seq_num);
                    
                    slice_drop();

                    f_cblk.continue_current_slice = 0;
                    f_cblk.look_for_new_slice = 1;
                }
                else
                {
                    //ALOGI("audio:got packet 4.\n");
                    f_cblk.last_seq_num = ntohs(rtp_hdr->sequence_num);

                    UINT32 pes_size = pes_payload_size(desc);

                    //ALOGI("pes_size=%d.",pes_size);
//                    assert(pes_size == 1920);

//                    ALOGI("(audio_decoder): PES payload size = %d\n", pes_size);

                    f_cblk.pes_curr_byte_count += pes_size;

                    //CHECK(f_cblk.pes_curr_byte_count <= f_cblk.pes_len);

//                    assert(0);

                    slice_pkt_add(desc);

                    //ALOGI("pes_size:%d,f_cblk:pes_curr_byte_count=%d,pes_len=%d.",pes_size,
                    //       f_cblk.pes_curr_byte_count,f_cblk.pes_len);

                    if(f_cblk.pes_curr_byte_count == f_cblk.pes_len)
                    {
                        //ALOGI("(audio_decoder): Slice end found!\n");

                        f_cblk.continue_current_slice = 0;
                        f_cblk.look_for_new_slice = 1;
                        audio_decoder_slice_dump(slice_get());
                    }
                }
            }

        } while (getStopIssued() == 0);


//        ALOGI("(audio_decoder): sleep!\n");

        usleep(500);
    }

    
    if(isAttached){
        mJvm->DetachCurrentThread();
    }
    pthread_exit((void *)0);
    
    return 0;
}


} // namespace android