#include <pthread.h>
#include <utils/Log.h>
#include <sys/types.h>
#include <jni.h>
#include <media/stagefright/foundation/hexdump.h>
#include <media/stagefright/foundation/ADebug.h>
#include "MmxVideoRenderer.h"


namespace android {

//#define WRITE_TO_FILE 1
#if WRITE_TO_FILE
static FILE * write_ptr;
#endif

static unsigned int first_packet = 1;
sDECODER_HW_BUFFER* phw_slice[DECODER_HW_PKT_NUM];
int index_slice = 0;

static void resource_init()
{
    int i;

    //ALOGI("sx_video_sink_init:resource_init.");

    for(i=0;i<DECODER_HW_PKT_NUM;i++){
        phw_slice[i] = (sDECODER_HW_BUFFER*)malloc(sizeof(sDECODER_HW_BUFFER));
        if(NULL != phw_slice[i])
            phw_slice[i]->buffer = (unsigned char*)malloc(DECODER_HW_PKT_SIZE);
    }

}

static void resource_release()
{
    int i;

    //ALOGI("sx_video_sink_init:resource_release.");

    for(i=0;i<DECODER_HW_PKT_NUM;i++){
        if(NULL != phw_slice[i]->buffer)
            free(phw_slice[i]->buffer);

        if(NULL != phw_slice[i])
            free(phw_slice[i]);
    }

}

void mmx_video_sink_init()
{
    //ALOGI("sx_video_sink_init:+.");

    resource_init();

#if WRITE_TO_FILE
    write_ptr = fopen("/sdcard/video_sink.h264", "wb");
    CHECK(write_ptr);
#endif


}
void mmx_video_sink_release()
{
    //ALOGI("sx_video_sink_init:sx_video_sink_release.");
	resource_release();
}

static int count_slice = 0;

sDECODER_HW_BUFFER * mmx_video_sink_buf_get()
{
    if(index_slice >= DECODER_HW_PKT_NUM)
        index_slice = 0;

    //ALOGI("count=%d,index=%d.\n", count_slice++, index_slice);
    return phw_slice[index_slice++];

}



void mmx_video_sink_buf_set(sDECODER_HW_BUFFER *hw_buf)
{
#if WRITE_TO_FILE
	fwrite(hw_buf->buffer, hw_buf->buffer_len, 1, write_ptr);
#endif

    /*
    int rc;
    OMX_ERRORTYPE err = OMX_ErrorNone;
    OMX_BUFFERHEADERTYPE *pBuf = pBufferIn;

    memcpy(pBuf->pBuffer, hw_buf->buffer, hw_buf->buffer_len);
    pBuf->nFilledLen = hw_buf->buffer_len;
    pBuf->nAllocLen = hw_buf->buffer_len;
    rc = OMX_EmptyThisBuffer(decILComp->handle, pBuf);
    */
}


} // namespace android