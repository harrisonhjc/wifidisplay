#define LOG_TAG "AVFormatSource"
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
#include <media/stagefright/MediaDefs.h>

#include "AVFormatSource.h"
#include "MmxVideoScheduler.h"
#include "MmxVideoRenderer.h"

namespace android {

extern int getStopIssued(void);

extern sMGMT_VIDEO_SCHEDULER_CBLK sched_f_cblk;
extern UINT64 estimated_source_time_get();


AVFormatSource::AVFormatSource()
{

  frameWidth = 1024;//1280;
  frameHeight = 600; //720;
  size_t bufferSize = (frameWidth * frameHeight * 3) / 2;
  mGroup.add_buffer(new MediaBuffer(bufferSize));
  mFormat = new MetaData;
  mFormat->setCString(kKeyMIMEType, MEDIA_MIMETYPE_VIDEO_AVC);
  mFormat->setInt32(kKeyWidth, frameWidth);
  mFormat->setInt32(kKeyHeight, frameHeight);

}

AVFormatSource::~AVFormatSource()
{
  
}

status_t AVFormatSource::av_read_frame(AVPacket *pPacket)
{
  sSX_DESC   *desc;
  UINT64      slice_present_time = 0;
  sSX_DESC *curr;
  sSX_DESC *next;

  
  desc = NULL;
  pPacket->size = 0;
  do{
    // Get slice.
    desc = (sSX_DESC*)MmxUtil::sx_pipe_get(SX_VRDMA_SLICE_READY);
    if(desc == NULL){
      usleep(500);
      continue;
    }
    sSLICE_HDR *hdr = (sSLICE_HDR *) desc->data;
    // Get PTS.
    //slice_present_time = ((sSLICE_HDR *) desc->data)->timestamp;
    
    curr = desc->next;
    ///CHECK(curr);
    /*
    UINT64  estimated_source_time = estimated_source_time_get();
    UINT8 present = (estimated_source_time > slice_present_time) ? 1 : 0;
    if(!present){
      ALOGI("no present.");
      free(curr);
      //free(desc->data);
      free(desc);
        continue;
    }*/

    
    sDECODER_HW_BUFFER *hw_buf = (sDECODER_HW_BUFFER*)curr->data;
    memcpy((unsigned char*)(pPacket->data), hw_buf->buffer, hw_buf->buffer_len);
    pPacket->size = hw_buf->buffer_len;
    pPacket->pts = ((sSLICE_HDR *) desc->data)->timestamp;
    

    //do{
    //    next = curr->next;
        free(curr);
    //    curr = next;
       
    //} while (curr != NULL);
    
    //free(desc->data);
    free(desc);
    break;
   
  } while(getStopIssued() == 0);

  return OK;
}

void AVFormatSource::av_free_packet(AVPacket *pPacket)
{
  if(pPacket != NULL){
    if(pPacket->data != NULL){
      free(pPacket->data);
    }
  }
  
}

status_t AVFormatSource::read(MediaBuffer **buffer, const MediaSource::ReadOptions *options)
{
  AVPacket packet;
  status_t ret;
  bool found = false;

  while (!found) {
    ret = mGroup.acquire_buffer(buffer);
    if (ret != OK)
      return -1;

    packet.data = (unsigned char*)(*buffer)->data();
    ret = av_read_frame(&packet);
    if (ret != OK)
      return ERROR_END_OF_STREAM;

    //memcpy((*buffer)->data(), packet.data, packet.size);
    (*buffer)->set_range(0, packet.size);
    (*buffer)->meta_data()->clear();
    (*buffer)->meta_data()->setInt32(kKeyIsSyncFrame, 1);
    (*buffer)->meta_data()->setInt64(kKeyTime, packet.pts);
    found = true;
    
    //av_free_packet(&packet);
  }

  return ret;
}

}  // namespace android

