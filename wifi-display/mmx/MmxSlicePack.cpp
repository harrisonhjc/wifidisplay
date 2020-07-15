#define LOG_TAG "MmxSlicePack"
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
#include <openssl/ssl.h>
#include <openssl/evp.h>
#include <openssl/bio.h>
#include <openssl/err.h>
#include <openssl/aes.h>
#include <openssl/buffer.h>
#include <openssl/engine.h>
#include <jni.h>

#include "MmxSlicePack.h"
#include "MmxUtil.h"
#include "MmxVideoScheduler.h"
#include "MmxVideoRenderer.h"

namespace android {

extern int getStopIssued(void);
extern JavaVM  *mJvm;
//#define WRITE_TO_FILE 1
#if WRITE_TO_FILE
static FILE * write_ptr;
#endif

MmxSlicePack::~MmxSlicePack()
{
    //ALOGI("~MmxSlicePack:");
    
}


status_t MmxSlicePack::startService()
{
    
//#if WRITE_TO_FILE
//    write_ptr = fopen("/data/video_sink.h264", "wb");
//    CHECK(write_ptr);
//#endif

    //run("MmxSlicePack");
    startThread();
    
    return OK;
    
}
status_t MmxSlicePack::stopService()
{
    //ALOGI("stop:");
    return OK;
    
}
void MmxSlicePack::setkeyPES(unsigned char* key1, unsigned char* key2, unsigned char* key3)
{
    
    memcpy(Ks,key1,sizeof(Ks));
    memcpy(riv,key2,sizeof(riv));
    memcpy(lc,key3,sizeof(lc));
    for(int i=0;i<16;i++){
        keyPES[i] = Ks[i] ^ lc[i];
    }
    
    return;
    
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
void MmxSlicePack::getCtr(unsigned char privateData[16], unsigned char streamCtr[4], unsigned char inputCtr[8])
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
    
    /*
    UINT32 sCtr[3];
    UINT32 iCtr[5];
    ABitReader bitReader;

    initABitReader(&bitReader, privateData, 16);
    skipBits(&bitReader,13);
    sCtr[0] = getBits(&bitReader, 2);
    skipBits(&bitReader,1);
    sCtr[1] = getBits(&bitReader, 15);
    skipBits(&bitReader,1);
    sCtr[2] = getBits(&bitReader, 15);
    skipBits(&bitReader,12);
    iCtr[0] = getBits(&bitReader, 4);
    skipBits(&bitReader,1);
    iCtr[1] = getBits(&bitReader, 15);
    skipBits(&bitReader,1);
    iCtr[2] = getBits(&bitReader, 15);
    skipBits(&bitReader,1);
    iCtr[3] = getBits(&bitReader, 15);
    skipBits(&bitReader,1);
    iCtr[4] = getBits(&bitReader, 15);
    
    //sCtr:       0  :  1    :  2
    //split:      2  : 6-8-1   7-8
    //to  4 bytes : 2+6 8 1+7 8     
    //convert sCtr to streamCtr
    streamCtr[0] = sCtr[0];
    streamCtr[0] <<= 6;   
    streamCtr[0] |= (sCtr[0] >> 9);
    streamCtr[1]  = sCtr[1] >> 1;
    streamCtr[2] = sCtr[1] &0x00000001;
    streamCtr[2] << 7;
    streamCtr[2] |= (sCtr[2] >> 8);
    streamCtr[3] = sCtr[2] & 0x000000ff;

    //iCtr:       0  :  1    :  2     :   3   :   4 
    //split:      4  : 4-8-3 :  5+8+2 : 6+8+1 :  7+8 
    //to  8 bytes : 4+4 : 8 : 3+5 : 8 : 2+6 : 8 : 1+7 : 8   
    //convert iCtr to inputCtr
    inputCtr[0] = iCtr[0];
    inputCtr[0] <<= 4;
    inputCtr[0] |= (iCtr[0] >> 11);
    inputCtr[1] = (iCtr[1] >> 3);
    inputCtr[2] = iCtr[1] & 0x00000007;
    inputCtr[2] <<= 5;
    inputCtr[2] |= (iCtr[2] >> 10);
    inputCtr[3] = iCtr[2] >> 2;
    inputCtr[4] = iCtr[2] & 0x00000003;
    inputCtr[4] <<= 6;
    inputCtr[4] |= (iCtr[3] >> 9);
    inputCtr[5] = iCtr[3] >> 1;
    inputCtr[6] = iCtr[3] & 0x00000001;
    inputCtr[6] << 7;
    inputCtr[6] |= (iCtr[4] >> 8);
    inputCtr[7] = iCtr[4] & 0x000000ff;
    */
    

}

void MmxSlicePack::decryptPES(unsigned char* pData, UINT32 dataLen, 
                      unsigned char streamCtr[4],unsigned char inputCtr[8])
{
    
    EVP_CIPHER_CTX ctx;
    
    unsigned char iv[16];
    
    unsigned char tmp[100];
    unsigned char Riv[8];
    unsigned char outData[16];
   
    unsigned long long  ctr;
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

void MmxSlicePack::video_scheduler_slice_dump(sSX_DESC *slice_head)
{
    UINT8      *curr_ptr;
    UINT32      bytes_left;
    UINT32      afc;
    UINT32      pes_byte_count;
    UINT32      payload_size;
    UINT32      start_offset;
    UINT32      copy_index;
    UINT32      pid;
    sSX_DESC   *curr_desc;
    sSX_DESC   *head_desc;
    unsigned char streamCtr[4];
    unsigned char inputCtr[8];

    // Consistency check.
    CHECK(slice_head);

    // Get descriptor.
    sSX_DESC *hw_desc = MmxUtil::sx_desc_get();

    // Set this as another chain.
    sSX_DESC *hw_desc_head = hw_desc;

    // Get a hw buffer.
    sDECODER_HW_BUFFER * hw_buf = mmx_video_sink_buf_get();
    CHECK(hw_buf != NULL);


    // Set payload.
    hw_desc->data = (UINT8 *) hw_buf;

    // Get first descriptor.
    //
    // First descriptor holds the timestamp and will be skipped.
    curr_desc = slice_head;

    copy_index = 0;
    do  //desc chain loop (contains multi descs)
    {
        
        // Get next.
        curr_desc = curr_desc->next;

        // Get current.
        curr_ptr = curr_desc->data;

        // Get data left.
        bytes_left = curr_desc->data_len;
        CHECK(bytes_left > sizeof(sRTP_HDR));

        // Get RTP header.
        // sRTP_HDR *rtp_hdr = (sRTP_HDR *) curr_ptr;

        // Get TS header.
        curr_ptr += sizeof(sRTP_HDR);

        // Get TS bytes left.
        bytes_left -= sizeof(sRTP_HDR);
        CHECK((bytes_left % sizeof(sMPEG2_TS)) == 0);

#if WRITE_TO_FILE
  fwrite(curr_ptr, bytes_left, 1, write_ptr);
#endif

      do   //one desc(contains multi ts packets) loop
        {
            sMPEG2_TS *ts = (sMPEG2_TS *) curr_ptr;
            afc = AFC_GET(ts->hdr);
            pid = PID_GET(ts->hdr);
            pes_byte_count = 0;
            UINT32 pes_header_len; 
            unsigned char private_data[16];
            unsigned char* privatePtr;
            
            if(pid == 0x1011) {
                
                UINT8 stuffing = 0;
                if(afc & 0x02){
                    stuffing = 1 + ts->payload.payload[0];
                    
                }
                start_offset = stuffing;
                
                if(PUSI_GET(ts->hdr)) {
                    //9:4+4+1 : 4:PES start code,4:PES header length,1:next to PES payload
                    pes_header_len = ts->payload.payload[start_offset+8]; 
                    start_offset += (pes_header_len+9); 
                    privatePtr = &ts->payload.payload[start_offset-16];
                    memcpy(private_data,privatePtr,sizeof(private_data));
                    getCtr(private_data,streamCtr,inputCtr);
                    /*
                     ALOGI("PES private Data:");
                     hexdump(private_data,16);
                     ALOGI("streamCtr:");
                     hexdump(streamCtr,4);
                     ALOGI("inputCtr:");
                     hexdump(inputCtr,8);
                     */
                }//else{
                 //   ALOGI("No PUSI:");
                 //   hexdump(curr_ptr,188);
                //}
                

                payload_size = sizeof(sMPEG2_TS_PAYLOAD) - start_offset;

                if((copy_index + payload_size) > 81920) {
                    //printf("pld=%d ",copy_index + payload_size);
                    // If the hw buffer is full, just submit the current buffer.
                    hw_buf->buffer_len = copy_index;

                    // Get a new descriptor.
                    hw_desc->next = MmxUtil::sx_desc_get();

                    // Point to the new descriptor.
                    hw_desc = hw_desc->next;

                    // Get a new buffer.
                    hw_buf = mmx_video_sink_buf_get();
                    CHECK(hw_buf != NULL);

                    // Set new payload.
                    hw_desc->data = (UINT8 *) hw_buf;

                    // Reset index.
                    copy_index = 0;
                }

                memcpy(&hw_buf->buffer[copy_index],
                        &ts->payload.payload[start_offset],
                        payload_size);
                copy_index += payload_size;
            }

            curr_ptr += sizeof(sMPEG2_TS);
            bytes_left -= sizeof(sMPEG2_TS);

        } while (bytes_left > 0);

    } while (curr_desc->next != NULL);

    // Set length.
    hw_buf->buffer_len = copy_index;
    //decrypt PES payload
    //////////////////////////////////////////////////////////
    decryptPES(hw_buf->buffer,hw_buf->buffer_len,streamCtr,inputCtr);

    ///////////////////////////////////////////////////////////
    // Free the existing slice, minus the head (timestamp).
    MmxUtil::sx_desc_put2(slice_head->next);
    // Set slice.
    slice_head->next = hw_desc_head;
    MmxUtil::sx_pipe_put(SX_VRDMA_SLICE_READY, slice_head);
}

void  MmxSlicePack::startThread(void)
{
    MmxSlicePackPtr t = &MmxSlicePack::threadLoop;
    PthreadPtr p = *(PthreadPtr*)&t;
    
    if(pthread_create(&tid, 0, p, this) == 0){
        pthread_detach(tid);
    }
}

void* MmxSlicePack::threadLoop(void)
{
	
    JNIEnv *env = NULL;
    int isAttached = 0;
    if(mJvm){
        if(mJvm->AttachCurrentThread(&env, NULL) < 0){
            ALOGE("MmxSlicePack::threadLoop:thread can not attach current thread." );
            return NULL;
        }
        isAttached = 1;
    }
    androidSetThreadPriority(0, VIDEO_SCHEDULER_THREAD_PRIORITY);

    sSX_DESC   *desc;
    
    desc = NULL;
    while(getStopIssued() == 0)
    {
        UINT32 len = MmxUtil::sx_pipe_len_get(SX_VRDMA_SLICE_READY);
        if(len >= 10)
        {
            // More than enough. Try again next iteration.
            //printf("> ");
            usleep(500);
            continue;
        }
        //printf("len=%d.",len);
        UINT32  slices_to_dump = 10 - len;
        do
        {
            desc = (sSX_DESC*)MmxUtil::sx_pipe_get(SX_VRDMA_SLICE);
            if(desc == NULL){
                if(getStopIssued() == 1)
                    break;
                else
                    continue;
            }

            video_scheduler_slice_dump(desc);

            slices_to_dump--;

        } while (slices_to_dump > 0);

        //usleep(500);

    }
    
    if(isAttached){
        mJvm->DetachCurrentThread();
    }
    pthread_exit((void *)0);
    return 0;
}


} // namespace android