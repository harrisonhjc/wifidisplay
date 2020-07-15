#ifndef AVFormatSource_H_
#define AVFormatSource_H_

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

#include <gui/Surface.h>
#include <media/stagefright/foundation/AHandler.h>
#include <media/stagefright/MediaSource.h>
#include <media/stagefright/MetaData.h>
#include <media/stagefright/OMXCodec.h>
#include <media/stagefright/MediaBufferGroup.h>
#include "MmxUtil.h"

namespace android {

typedef struct 
{
  unsigned char* data;
  unsigned int size;
  int64_t pts;


} AVPacket;

class AVFormatSource : public MediaSource {
  public:
    AVFormatSource();

    virtual status_t read(MediaBuffer **buffer, const MediaSource::ReadOptions *options);
    virtual sp<MetaData> getFormat() { return mFormat; }
    virtual status_t start(MetaData *params) { return OK; }
    virtual status_t stop() { return OK; }

    status_t av_read_frame(AVPacket *pPacket);
    void av_free_packet(AVPacket *pPacket);

  protected:
    virtual ~AVFormatSource();

  private:
    int frameWidth;
    int frameHeight;
    MediaBufferGroup mGroup;
    sp<MetaData> mFormat;
    int mVideoIndex;
};


}  // namespace android

#endif  // AVFormatSource_H_
