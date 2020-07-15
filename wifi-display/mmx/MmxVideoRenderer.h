#ifndef _MMX_VIDEO_RENDERER_H
#define _MMX_VIDEO_RENDERER_H



#define DECODER_HW_PKT_SIZE		     82994//(81*1024)
#define DECODER_HW_PKT_NUM			192

namespace android {
// This is an overlay of OMX_BUFFERHEADERTYPE to reduce dependency.
//
typedef struct sHW_CHUNK{
	unsigned char	*head;
	unsigned char	*pkt_ptr;
	unsigned int	 total_used;
	unsigned int	 max_len;

} sHW_CHUNK;



// This is an overlay of OMX_BUFFERHEADERTYPE to reduce dependency.
typedef struct
{
    unsigned int    reserve1;   // nSize
    unsigned int    reserved2;  // nVersion
    unsigned char   *buffer;     // pBuffer
    unsigned int    reserved3;  // nAllocLen
    unsigned int    buffer_len; // nFilledLen
    unsigned int    reserved4;  // nOffset
    unsigned int    reserved5;  // pAppPrivate
    unsigned int    reserved6;  // pPlatformPrivate;
    unsigned int    reserved7;  // pInputPortPrivate;
    unsigned int    reserved8;  // pOutputPortPrivate
    unsigned int    reserved9;  // hMarkTargetComponent
    unsigned int    reserved10; // pMarkData
    unsigned int    reserved11; // nTickCount
    unsigned int    reserved12; // nTimeStamp
    unsigned int    reserved13; // nFlags
    unsigned int    reserved14; // nOutputPortIndex
    unsigned int    reserved15; // nInputPortIndex

} sDECODER_HW_BUFFER;

extern void mmx_video_sink_init();
extern void mmx_video_sink_release();

extern sDECODER_HW_BUFFER * mmx_video_sink_buf_get();
extern void mmx_video_sink_buf_set(sDECODER_HW_BUFFER *decoder_hw_buf);

} // namespace android

#endif   //_MMX_VIDEO_RENDERER_H
