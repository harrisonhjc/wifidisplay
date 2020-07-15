#define LOG_TAG "MmxAudioScheduler"

#undef NDEBUG 
#define LOG_NDEBUG   0 
#define LOG_NIDEBUG  0 
#define LOG_NDDEBUG 0  
#define LOG_NEDEBUG 0 

#include <utils/Log.h>
#include <sys/types.h>
#include <media/stagefright/foundation/hexdump.h>
#include <media/stagefright/foundation/ADebug.h>
#include <openssl/ssl.h>
#include <openssl/evp.h>
#include <openssl/bio.h>
#include <openssl/err.h>
#include <openssl/aes.h>
#include <openssl/buffer.h>
#include <openssl/engine.h>
#include <jni.h>
#include "MmxAudioScheduler.h"
#include "MmxUtil.h"




namespace android {

extern int getStopIssued(void);
extern JavaVM  *mJvm;

//#define WRITE_TO_FILE_AUDIO  1
#if WRITE_TO_FILE_AUDIO
    static FILE * write_ptr;
#endif

typedef enum
{
    STATE_INACTIVE,
    STATE_ACTIVE

} eSTATE;

// Decoder control block
typedef struct
{
    pthread_t   audio_scheduler_thread;
    eSTATE      state;

} sMGMT_AUDIO_SCHEDULER_CBLK;


// ========================================================
// Private Variables & Functions
// ========================================================

// Decoder control block.
static sMGMT_AUDIO_SCHEDULER_CBLK f_cblk;

unsigned int sx_audio_sink_ms_left_get(void)
{
    //unsigned int samples_left ;

    //unsigned int ms_left = (samples_left/2)*1000/48000;

    return 120;
}

void sx_audio_sink_playback_speed_reset()
{

}

void sx_audio_sink_playback_speed_inc( )
{

}

void sx_audio_sink_playback_speed_dec()
{

}

static UINT32 audio_total_remaining_ms(
    void
    )
{

    UINT32  data_left = MmxUtil::sx_pipe_len_get(SX_VRDMA_AUDIO_SLICE) * 10;
    UINT32  queued_left = sx_audio_sink_ms_left_get();

//    printf("total left = %u, data left = %u, queued left = %u\n",
//            data_left + queued_left,
//            data_left,
//            queued_left);

    return (data_left + queued_left);
}


static void inline audio_endianness_convert(
    UINT16 *temp,
    UINT32  samples
    )
{
    UINT32 i;


    for(i = 0; i < samples; i++)
    {
        temp[i] = ntohs(temp[i]);
    }
}

void MmxAudioScheduler::setMute(bool status)
{
    mAudioRenderer.setMute(status);
}

void MmxAudioScheduler::setkeyPES(unsigned char* key1, unsigned char* key2, unsigned char* key3)
{
    memcpy(Ks,key1,sizeof(Ks));
    memcpy(riv,key2,sizeof(riv));
    memcpy(lc,key3,sizeof(lc));
    for(int i=0;i<16;i++){
        keyPES[i] = Ks[i] ^ lc[i];
    }
       
}
/*  private data structure
PES_private_data() {
reserved_bits
streamCtr[31..30]
marker_bit
streamCtr[29..15]
marker_bit
streamCtr[14..0]
marker_bit
reserved_bits
inputCtr[63..60]
marker_bit
inputCtr[59..45]
marker_bit
inputCtr[44..30]
marker_bit
inputCtr[29..15]
marker_bit
inputCtr[14..0]
marker_bit
} */
void MmxAudioScheduler::getCtr(unsigned char privateData[16], unsigned char streamCtr[4], unsigned char inputCtr[8])
{
    streamCtr[0] = (privateData[1] & 0x06) << 5;
    streamCtr[0] |= privateData[2] >> 2;   
    streamCtr[1] = (privateData[2] << 6);
    streamCtr[1] |= privateData[3] >> 2;
    streamCtr[2] = (privateData[3] & 0x02) << 6;
    streamCtr[2] |= privateData[4] >> 1;   
    streamCtr[3] = privateData[4] << 7;
    streamCtr[3] |= privateData[5] >> 1;   

    inputCtr[0] = (privateData[7] & 0x1e) << 3;
    inputCtr[0] |= privateData[8] >> 4;
    inputCtr[1] = privateData[8] << 4;
    inputCtr[1] |= privateData[9] >> 4;
    inputCtr[2] = (privateData[9] &0x0e) << 4;
    inputCtr[2] |= privateData[10] >> 3;
    inputCtr[3] = privateData[10] << 5;
    inputCtr[3] |= privateData[11] >> 3;
    inputCtr[4] = (privateData[11]&0x06) << 5;
    inputCtr[4] |= privateData[12] >> 2;
    inputCtr[5] = privateData[12] << 6;
    inputCtr[5] |= privateData[13] >> 2;
    inputCtr[6] = (privateData[13]&0x02) << 6;
    inputCtr[6] |= privateData[14] >> 1;
    inputCtr[7] = privateData[14] << 7;
    inputCtr[7] |= privateData[15] >> 1;
    
   

}

void MmxAudioScheduler::decryptPES(unsigned char* pData, UINT32 dataLen, 
                      unsigned char streamCtr[4],unsigned char inputCtr[8])
{
    
    EVP_CIPHER_CTX ctx;
    
    unsigned char iv[16];
    
    unsigned char tmp[100];
    unsigned char Riv[8];
    unsigned char outData[16];
   
    unsigned long long ctr;
    int ret;
    int outl;
    
    //streamCtr XORed with the least significant 32-bits of riv.
    memcpy(Riv,riv,sizeof(Riv));
    for(int i=0;i<4;i++){
        Riv[4+i] ^= streamCtr[i];
    }
    memcpy(iv,Riv,8);
    memcpy(iv+8,inputCtr,8);

    EVP_CIPHER_CTX_init(&ctx);
        
    ctr = (inputCtr[0] << 56) |
        (inputCtr[1] << 48) |
        (inputCtr[2] << 40) |
        (inputCtr[3] <<  32) |
        (inputCtr[4] <<  24) |
        (inputCtr[5] <<  16) |
        (inputCtr[6] <<  8) |
        (inputCtr[7]);

    
    UINT32 offset = 0;
    UINT32 inLen;
    do{
        ret = EVP_EncryptInit_ex(&ctx, EVP_aes_128_ctr(), NULL, keyPES, iv);
        //CHECK(ret == 1);
        inLen = 16;
        if( (inLen+offset) > dataLen)
            inLen = dataLen - offset;
        ret = EVP_EncryptUpdate(&ctx, pData+offset, &outl, pData+offset, inLen);
        //CHECK(ret == 1);
        //CHECK(outl == inLen);
        ret = EVP_EncryptFinal_ex(&ctx, tmp, &outl);
        //CHECK(ret == 1);
        //CHECK(outl == 0);
    
        ctr++;
        iv[8] = ctr >> 56;
        iv[9] = ctr >> 48;
        iv[10] = ctr >> 40;
        iv[11] = ctr >> 32;
        iv[12] = ctr >> 24;
        iv[13] = ctr >> 16;
        iv[14] = ctr >> 8;
        iv[15] = ctr ;

        offset += inLen;

    } while(offset < dataLen);

    ret = EVP_CIPHER_CTX_cleanup(&ctx);
    CHECK(ret == 1);
    
}

void MmxAudioScheduler::audio_scheduler_slice_dump(sSX_DESC  *slice_head)
{
    UINT8      *curr_ptr;
    UINT32      bytes_left;
    UINT32      afc;
    UINT32      pid;
    UINT32      pes_byte_count;
    UINT32      payload_size;
    UINT32      start_offset;
    UINT32      copy_index;
    UINT32      samples_left;
    UINT32      ms_left;
    sSX_DESC   *desc;
    unsigned char streamCtr[4];
    unsigned char inputCtr[8];
   /*
    static UINT8   playback_speed = 1;

    ms_left = audio_total_remaining_ms();
    if(ms_left > (100 + SX_SYSTEM_DELAY_MS))
    {
        if(playback_speed != 2)
        {
            sx_audio_sink_playback_speed_inc();

            playback_speed = 2;
        }
    }
    else if(ms_left < (50 + SX_SYSTEM_DELAY_MS))
    {
        if(playback_speed != 0)
        {
            sx_audio_sink_playback_speed_dec();

            playback_speed = 0;
        }
    }
    else
    {
        if(playback_speed != 1)
        {
            sx_audio_sink_playback_speed_reset();

            playback_speed = 1;
        }
    }
    */
    sSX_DESC *hw_desc = MmxUtil::sx_desc_get();
    // Set this as another chain.
    sSX_DESC *hw_desc_head = hw_desc;
    //get buffer
    sDECODER_AUDIO_HW_BUFFER *hw_buf = mAudioRenderer.audio_sink_buffer_get();
    CHECK(hw_buf != NULL);
    // Set payload.
    hw_desc->data = (UINT8 *) hw_buf;

    
    bool hdcp2Used = false;
    copy_index = 0;
    desc = slice_head;
    do {
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
            unsigned char private_data[16];
            unsigned char* privatePtr;

            sMPEG2_TS *ts = (sMPEG2_TS *) curr_ptr;
            
            afc = AFC_GET(ts->hdr);
            pid = PID_GET(ts->hdr);

            if(pid == 0x1100) {
                
                UINT32  pes_header_len = 0;
                UINT8 stuffing = 0;
                if(afc & 0x02){
                    stuffing = 1 + ts->payload.payload[0];
                    start_offset = stuffing;
                }

                if(PUSI_GET(ts->hdr)){
                    
                    if(ts->payload.payload[start_offset+4] == 0x07 &&
                       ts->payload.payload[start_offset+5] ==  0x9f){
                        //check wifi-display spec 1.1 page 136.
                        hdcp2Used = true;
                    }

                    stuffing += 8; //pes header len byte 
                    pes_header_len = ts->payload.payload[stuffing];
                    start_offset = (stuffing + pes_header_len + 1);

                    if(hdcp2Used){
                        //PES private data:get decrypt counter
                        privatePtr = &ts->payload.payload[start_offset-18];
                        memcpy(private_data,privatePtr,sizeof(private_data));
                        getCtr(private_data,streamCtr,inputCtr);
                    }/*
                    else{
                        //payload private data , check wifi display spec 1.1 p.136
                        start_offset += 4;     
                    }*/
                    
                    
                    //ALOGI("header:");
                    //hexdump(&ts->payload.payload[0],48);

                }
                payload_size = sizeof(sMPEG2_TS_PAYLOAD) - start_offset;
                memcpy(&hw_buf->buffer[copy_index],
                        &ts->payload.payload[start_offset],
                        payload_size);
                copy_index += payload_size;

                //ALOGI("audio pkt:");
                //hexdump(&ts->payload.payload[start_offset], 32);         
            }

            curr_ptr += sizeof(sMPEG2_TS);
            bytes_left -= sizeof(sMPEG2_TS);

        } while (bytes_left > 0);

        desc = desc->next;

    } while (desc != NULL);

    hw_buf->buffer_len = copy_index;
    if(hdcp2Used){
        //ALOGI("hdcp2 used:");
        //decrypt PES payload
        decryptPES(hw_buf->buffer, hw_buf->buffer_len, streamCtr, inputCtr);
    }else{
        //ALOGI("hdcp2 not used:");
        audio_endianness_convert((UINT16 *)hw_buf->buffer, 960); // 1920/2
    }
    
#if WRITE_TO_FILE_AUDIO
    /*
    unsigned char adts_header[7];
    adts_header[0] = 0xff;;
    adts_header[1] = 0xf1;;
    adts_header[2] = 0x4c;;
    adts_header[3] = 0x80;
    adts_header[4] = hw_buf->buffer_len >> 3;
    adts_header[5] = hw_buf->buffer_len & 0x0007;
    adts_header[5] <<= 5 ;
    adts_header[5] |= 0x1F ;
    adts_header[6] = 0xfc;
    fwrite(adts_header, 7, 1, write_ptr);
    */
    fwrite(hw_buf->buffer, hw_buf->buffer_len, 1, write_ptr);
#endif
    
    //ALOGI("audio pkt:len=%d.",hw_buf->buffer_len);
    //hexdump(hw_buf->buffer, 48); 
    

    //MmxUtil::sx_desc_put2(slice_head->next);
    // Set slice.
    //ALOGI("AUDIO: put SX_VRDMA_AUDIO_SLICE_READY.");
    slice_head->next = hw_desc_head;
    MmxUtil::sx_pipe_put(SX_VRDMA_AUDIO_SLICE_READY, slice_head);
    
    // Push to decoder hardware.
    //sx_audio_sink_buffer_set(buf, copy_index);
}

MmxAudioScheduler::~MmxAudioScheduler()
{
    //ALOGI("~MmxAudioScheduler:");
    
}


status_t MmxAudioScheduler::startService()
{

#if WRITE_TO_FILE_AUDIO
    write_ptr = fopen("/data/audio_sink.pcm", "wb");
#endif

    
    
    mAudioRenderer.startService();
    //run("MmxAudioScheduler");
    startThread();
    
    return OK;
    
}
status_t MmxAudioScheduler::stopService()
{
    //ALOGI("stop:");
    
    mAudioRenderer.stopService();
    return OK;
    
}

void  MmxAudioScheduler::startThread(void)
{
    MmxAudioSchedulerPtr t = &MmxAudioScheduler::threadLoop;
    PthreadPtr p = *(PthreadPtr*)&t;
    pthread_t    tid;
    if(pthread_create(&tid, 0, p, this) == 0){
        pthread_detach(tid);
    }
}

void* MmxAudioScheduler::threadLoop(void)
{
	
    JNIEnv *env = NULL;
    int isAttached = 0;
    if(mJvm){
        if(mJvm->AttachCurrentThread(&env, NULL) < 0){
            ALOGE("MmxAudioScheduler::threadLoop:thread can not attach current thread." );
            return NULL;
        }
        isAttached = 1;
    }

    androidSetThreadPriority(0, AUDIO_SCHEDULER_THREAD_PRIORITY);

    sSX_DESC   *desc;


    do {
        
        desc = (sSX_DESC*)MmxUtil::sx_pipe_get(SX_VRDMA_AUDIO_SLICE);
        if(desc == NULL){
            usleep(500);
            continue;
        }else{
            audio_scheduler_slice_dump(desc);
        }

    } while(getStopIssued() == 0);
    
    
    if(isAttached){
        mJvm->DetachCurrentThread();
    }
    pthread_exit((void *)0);

    return 0;
}


} // namespace android