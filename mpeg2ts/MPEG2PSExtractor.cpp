/*
 * Copyright (C) 2011 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

//#define LOG_NDEBUG 0
#define LOG_TAG "MPEG2PSExtractor"
#include <utils/Log.h>

#include "include/MPEG2PSExtractor.h"

#include "AnotherPacketSource.h"
#include "ESQueue.h"

#include <media/stagefright/foundation/ABitReader.h>
#include <media/stagefright/foundation/ABuffer.h>
#include <media/stagefright/foundation/ADebug.h>
#include <media/stagefright/foundation/AMessage.h>
#include <media/stagefright/foundation/hexdump.h>
#include <media/stagefright/DataSource.h>
#include <media/stagefright/MediaDefs.h>
#include <media/stagefright/MediaErrors.h>
#include <media/stagefright/MediaSource.h>
#include <media/stagefright/MetaData.h>
#include <media/stagefright/Utils.h>
#include <utils/String8.h>

#define MAX_FRAME_SERACH 3000

namespace android {

struct MPEG2PSExtractor::Track : public MediaSource {
    Track(MPEG2PSExtractor *extractor,
          unsigned stream_id, unsigned stream_type);

    virtual status_t start(MetaData *params);
    virtual status_t stop();
    virtual sp<MetaData> getFormat();

    virtual status_t read(
            MediaBuffer **buffer, const ReadOptions *options);

protected:
    virtual ~Track();

private:
    friend struct MPEG2PSExtractor;

    MPEG2PSExtractor *mExtractor;

    unsigned mStreamID;
    unsigned mStreamType;
    int64_t mTimeUs;
    int64_t mTrackFirstPTS;
    unsigned mTrackTypeID;
    int64_t mDuration;
    bool mSeekable;
    bool    mTrackFirstPTSAvailable;
    ElementaryStreamQueue *mQueue;
    sp<AnotherPacketSource> mSource;

    status_t flush();
    void setDuration(int64_t dur);
    status_t appendPESData(
            unsigned PTS_DTS_flags,
            uint64_t PTS, uint64_t DTS,
            const uint8_t *data, size_t size);

    DISALLOW_EVIL_CONSTRUCTORS(Track);
};

struct MPEG2PSExtractor::WrappedTrack : public MediaSource {
    WrappedTrack(const sp<MPEG2PSExtractor> &extractor, const sp<Track> &track);

    virtual status_t start(MetaData *params);
    virtual status_t stop();
    virtual sp<MetaData> getFormat();

    virtual status_t read(
            MediaBuffer **buffer, const ReadOptions *options);

protected:
    virtual ~WrappedTrack();

private:
    sp<MPEG2PSExtractor> mExtractor;
    sp<MPEG2PSExtractor::Track> mTrack;

    DISALLOW_EVIL_CONSTRUCTORS(WrappedTrack);
};

////////////////////////////////////////////////////////////////////////////////

MPEG2PSExtractor::MPEG2PSExtractor(const sp<DataSource> &source)
    : mDataSource(source),
      mOffset(0),
      mFinalResult(OK),
      mBuffer(new ABuffer(0)),
      mFirstPTS(0),
      mLastPTS(0),
      mIsVideo(false),
      mIsAudio(false),
      mSeeking(false),
      mScanning(true),
      mReverseScan(false),
      mpeg1Stream(false),
      mProgramStreamMapValid(false) {
    for (size_t i = 0; i < MAX_FRAME_SERACH; ++i) {
        if (feedMore() != OK) {
            break;
        }
        if (mIsVideo && mIsAudio) {
            break;
        }
    }

    // Remove all tracks that were unable to determine their format.
    for (size_t i = mTracks.size(); i-- > 0;) {
        if (mTracks.valueAt(i)->getFormat() == NULL) {
            mTracks.removeItemsAt(i);
        }
    }
    getDuration();
    mScanning = false;
}

MPEG2PSExtractor::~MPEG2PSExtractor() {
}

size_t MPEG2PSExtractor::countTracks() {
    return mTracks.size();
}

sp<MediaSource> MPEG2PSExtractor::getTrack(size_t index) {
    if (index >= mTracks.size()) {
        return NULL;
    }

    return new WrappedTrack(this, mTracks.valueAt(index));
}

sp<MetaData> MPEG2PSExtractor::getTrackMetaData(size_t index, uint32_t flags) {
    if (index >= mTracks.size()) {
        return NULL;
    }
//    return mTracks.valueAt(index)->getFormat();
    sp<MetaData> meta;
    if (index < mTracks.size()) {
        meta = mTracks.valueAt(index)->getFormat();
    }

    if (flags & kIncludeExtensiveMetaData) {
        const char *mime;
        CHECK(meta->findCString(kKeyMIMEType, &mime));
        if (!strncasecmp("video/", mime, 6)) {
            if(mDuration > (7 * 1000000))
                meta->setInt64( kKeyThumbnailTime, (7 * 1000000));
            else
                meta->setInt64( kKeyThumbnailTime, 0);
        }
    }
    return meta;
}

sp<MetaData> MPEG2PSExtractor::getMetaData() {
    sp<MetaData> meta = new MetaData;
    meta->setCString(kKeyMIMEType, MEDIA_MIMETYPE_CONTAINER_MPEG2PS);

    return meta;
}

void MPEG2PSExtractor::seekTo(int64_t seekTimeUs)
{
    Mutex::Autolock autoLock(mLock);
    off64_t offset =(filesize / (1.0 * mDuration)) * seekTimeUs;
    ALOGV("Seek to %lld and seekoffset %llx",seekTimeUs,offset);
    offset -= offset % 2048;
    ALOGV("offset after boundry correction %llx.. and now flushing", offset);
    for(size_t i=0;i<mTracks.size();i++)
    {
        mTracks.editValueAt(i)->flush();
    }
    mOffset = offset;
    mSeeking = true;
}

uint32_t MPEG2PSExtractor::flags() const {
    return (CAN_PAUSE | CAN_SEEK_FORWARD | CAN_SEEK_BACKWARD | CAN_SEEK);
}

status_t MPEG2PSExtractor::feedMore() {
    Mutex::Autolock autoLock(mLock);

    // How much data we're reading at a time
    static const size_t kChunkSize = 8192;

    for (;;) {
        status_t err = dequeueChunk();

        if (err == -EAGAIN && mFinalResult == OK) {
            memmove(mBuffer->base(), mBuffer->data(), mBuffer->size());
            mBuffer->setRange(0, mBuffer->size());

            if (mBuffer->size() + kChunkSize > mBuffer->capacity()) {
                size_t newCapacity = mBuffer->capacity() + kChunkSize;
                sp<ABuffer> newBuffer = new ABuffer(newCapacity);
                memcpy(newBuffer->data(), mBuffer->data(), mBuffer->size());
                newBuffer->setRange(0, mBuffer->size());
                mBuffer = newBuffer;
            }

            ssize_t n = mDataSource->readAt(
                    mOffset, mBuffer->data() + mBuffer->size(), kChunkSize);

            if (n < (ssize_t)kChunkSize) {
                mFinalResult = (n < 0) ? (status_t)n : ERROR_END_OF_STREAM;
                return mFinalResult;
            }

            mBuffer->setRange(mBuffer->offset(), mBuffer->size() + n);
            mOffset += n;
        } else if (err != OK) {
            mFinalResult = err;
            return err;
        } else {
            return OK;
        }
    }
}

status_t MPEG2PSExtractor::dequeueChunk() {
    if (mBuffer->size() < 4) {
        return -EAGAIN;
    }

    size_t offset = 0;
    uint8_t *data = mBuffer->data();
    ssize_t size = mBuffer->size();

    if (mSeeking == true) {
        while(offset + 4 < size) {
            if(!memcmp("\x00\x00\x01\xba", &data[offset], 4)) {
                mBuffer->setRange(offset, size-offset);
                mSeeking = false;
                break;
            } else {
                ++offset;
            }
        }

        if (mSeeking == true) {
            mBuffer->setRange(0, 0);
            return -EAGAIN;
        }
    }

    while(memcmp("\x00\x00\x01", mBuffer->data(), 3)) {
        if(memcmp("\x00", mBuffer->data(), 1)) { // padding bytes are all zeros
            // not padding byte; go to next chunk
            mBuffer->setRange(0, 0);
            return -EAGAIN;
        } else {
            // skip padding byte
            mBuffer->setRange(mBuffer->offset() + 1, mBuffer->size() - 1);
            if (mBuffer->size() < 4) {
                return -EAGAIN;
            }
        }
    }

    unsigned chunkType = mBuffer->data()[3];

    ssize_t res;

    switch (chunkType) {
        case 0xba:
        {
            res = dequeuePack();
            break;
        }

        case 0xbb:
        {
            res = dequeueSystemHeader();
            break;
        }

        default:
        {
            if(mpeg1Stream)
              res = dequeueMPEG1PES();
            else
            res = dequeuePES();
            break;
        }
    }

    if (res > 0) {
        if (mBuffer->size() < (size_t)res) {
            return -EAGAIN;
        }

        mBuffer->setRange(mBuffer->offset() + res, mBuffer->size() - res);
        res = OK;
    }

    return res;
}

ssize_t MPEG2PSExtractor::dequeuePack() {
    // 32 + 2 + 3 + 1 + 15 + 1 + 15+ 1 + 9 + 1 + 22 + 1 + 1 | +5    for mpeg2 Program stream
    //32 + 4 + 3 + 1+ 15 + 1 + 15 + 1 + 1 + 22 + 1                  for mpeg1 program stream

    if (mBuffer->size() < 14) {
        return -EAGAIN;
    }

    unsigned chunkType = mBuffer->data()[4];

    if ((chunkType >> 6) != 1) {
        mpeg1Stream = true;
        return 12;
    }

    unsigned pack_stuffing_length = mBuffer->data()[13] & 7;

    return pack_stuffing_length + 14;
}

ssize_t MPEG2PSExtractor::dequeueSystemHeader() {
    if (mBuffer->size() < 6) {
        return -EAGAIN;
    }

    unsigned header_length = U16_AT(mBuffer->data() + 4);

    return header_length + 6;
}

ssize_t MPEG2PSExtractor::dequeueMPEG1PES() {
    unsigned dataLength = 0;
    unsigned stream_type = 0;

    if (mBuffer->size() < 6) {
        return -EAGAIN;
    }

    unsigned PES_packet_length = U16_AT(mBuffer->data() + 4);

    size_t n = PES_packet_length + 6;

    if (PES_packet_length <= 0)
        return n;

    dataLength = PES_packet_length;

    if (mBuffer->size() < n) {
        return -EAGAIN;
    }

    ABitReader br(mBuffer->data(), n);

    unsigned packet_startcode_prefix = br.getBits(24);

    ALOGV("packet_startcode_prefix = 0x%08x", packet_startcode_prefix);

    if (packet_startcode_prefix != 1) {
        ALOGV("Supposedly payload_unit_start=1 unit does not start "
             "with startcode.");

        return ERROR_MALFORMED;
    }

    CHECK_EQ(packet_startcode_prefix, 0x000001u);

    unsigned stream_id = br.getBits(8);
    ALOGV("stream_id = 0x%02x", stream_id);

    if (stream_id == 0xe0)
         stream_type = 0x01;

    if (stream_id == 0xc0)
        stream_type = 0x03;

    if (stream_id == 0xbd)
        stream_type = 0x81;

    /* unsigned PES_packet_length = */br.getBits(16);

    if (stream_id != 0xbe  // padding_stream                              // packetlength - 2 - 5 -5-
            && stream_id != 0xbf  // private_stream_2
            && stream_id != 0xf0  // ECM
            && stream_id != 0xf1  // EMM
            && stream_id != 0xff  // program_stream_directory
            && stream_id != 0xf2  // DSMCC
            && stream_id != 0xf8) {  // H.222.1 type E

        unsigned nextbits = mBuffer->data()[6];

        uint32_t i = 6;
        while(nextbits == 0xff) {
            br.skipBits(8);
            dataLength = dataLength - 1;
            i++;
            nextbits = mBuffer->data()[i];
        }

        nextbits = nextbits & 0x40;

        if(nextbits == 0x40) {
            br.skipBits(2);
            unsigned STD_buffer_scale = br.getBits(1);
            unsigned STD_buffer_size = br.getBits(13);
            dataLength = dataLength - 2;
        }

        unsigned PTS_DTS_flags = br.getBits(4);

        uint64_t PTS = 0, DTS = 0;

        if (PTS_DTS_flags == 2 || PTS_DTS_flags == 3) {
            PTS = ((uint64_t)br.getBits(3)) << 30;
            CHECK_EQ(br.getBits(1), 1u);
            PTS |= ((uint64_t)br.getBits(15)) << 15;
            CHECK_EQ(br.getBits(1), 1u);
            PTS |= br.getBits(15);
            CHECK_EQ(br.getBits(1), 1u);
            dataLength = dataLength - 5;

            ALOGV("PTS = %llu", PTS);

            if (mScanning)
            {
                if (PTS > 0 && mFirstPTS == 0)
                    mFirstPTS = PTS;

                if (PTS > 0 && mLastPTS == 0)
                    mLastPTS = PTS;
             }
            if (PTS_DTS_flags == 3) {
                CHECK_EQ(br.getBits(4), 1u);
                DTS = ((uint64_t)br.getBits(3)) << 30;
                CHECK_EQ(br.getBits(1), 1u);
                DTS |= ((uint64_t)br.getBits(15)) << 15;
                CHECK_EQ(br.getBits(1), 1u);
                DTS |= br.getBits(15);
                CHECK_EQ(br.getBits(1), 1u);
                dataLength = dataLength - 5;
                ALOGV("DTS = %llu", DTS);
            }
        }  else {
            CHECK_EQ(br.getBits(4), 15u);
            dataLength = dataLength - 1;
        }

       if (br.numBitsLeft() < dataLength * 8) {
            ALOGE("PES packet does not carry enough data to contain "
                 "payload. (numBitsLeft = %d, required = %d)",
                 br.numBitsLeft(), dataLength * 8);

            return ERROR_MALFORMED;
        }

        ssize_t index = mTracks.indexOfKey(stream_id);
        if (index < 0 && mScanning) {
            unsigned streamType;

            mStreamTypeByESID.add(stream_id, stream_type);
            ssize_t streamTypeIndex;
            if ((streamTypeIndex =
                            mStreamTypeByESID.indexOfKey(stream_id)) >= 0) {
                streamType = mStreamTypeByESID.valueAt(streamTypeIndex);
            } else if ((stream_id & ~0x1f) == 0xc0) {
                // ISO/IEC 13818-3 or ISO/IEC 11172-3 or ISO/IEC 13818-7
                // or ISO/IEC 14496-3 audio
                streamType = ATSParser::STREAMTYPE_MPEG2_AUDIO;
            } else if ((stream_id & ~0x0f) == 0xe0) {
                // ISO/IEC 13818-2 or ISO/IEC 11172-2 or ISO/IEC 14496-2 video
                streamType = ATSParser::STREAMTYPE_MPEG2_VIDEO;
            } else if (stream_id  == 0xbd) {
                streamType = ATSParser::STREAMTYPE_AUDIO_AC3;
            } else {
                streamType = ATSParser::STREAMTYPE_RESERVED;
            }

            index = mTracks.add(
                    stream_id, new Track(this, stream_id, streamType));
        }

        status_t err = OK;

        if (index >= 0 && !mReverseScan) {
            err =
                mTracks.editValueAt(index)->appendPESData(
                    PTS_DTS_flags, PTS, DTS, br.data(), dataLength);
        }

        br.skipBits(dataLength * 8);

        if (err != OK) {
            return n;  // no sufficeient data in PES packet...going for next pack
        }
    } else if (stream_id == 0xbe) {  // padding_stream
        CHECK_NE(PES_packet_length, 0u);
        br.skipBits(PES_packet_length * 8);
    } else {
        CHECK_NE(PES_packet_length, 0u);
        br.skipBits(PES_packet_length * 8);
    }

    return n;
}

ssize_t MPEG2PSExtractor::dequeuePES() {
    if (mBuffer->size() < 6) {
        return -EAGAIN;
    }

    unsigned PES_packet_length = U16_AT(mBuffer->data() + 4);
    if (PES_packet_length == 0) {
        return -EAGAIN;
    }

    size_t n = PES_packet_length + 6;

    if (mBuffer->size() < n) {
        return -EAGAIN;
    }

    ABitReader br(mBuffer->data(), n);

    unsigned packet_startcode_prefix = br.getBits(24);

    ALOGV("packet_startcode_prefix = 0x%08x", packet_startcode_prefix);

    if (packet_startcode_prefix != 1) {
        ALOGV("Supposedly payload_unit_start=1 unit does not start "
             "with startcode.");

        return ERROR_MALFORMED;
    }

    CHECK_EQ(packet_startcode_prefix, 0x000001u);

    unsigned stream_id = br.getBits(8);
    ALOGV("stream_id = 0x%02x", stream_id);

    /* unsigned PES_packet_length = */br.getBits(16);

    if (stream_id == 0xbc) {
        // program_stream_map

        if (!mScanning) {
            return n;
        }

        mStreamTypeByESID.clear();

        /* unsigned current_next_indicator = */br.getBits(1);
        /* unsigned reserved = */br.getBits(2);
        /* unsigned program_stream_map_version = */br.getBits(5);
        /* unsigned reserved = */br.getBits(7);
        /* unsigned marker_bit = */br.getBits(1);
        unsigned program_stream_info_length = br.getBits(16);

        size_t offset = 0;
        while (offset < program_stream_info_length) {
            if (offset + 2 > program_stream_info_length) {
                return ERROR_MALFORMED;
            }

            unsigned descriptor_tag = br.getBits(8);
            unsigned descriptor_length = br.getBits(8);

            ALOGI("found descriptor tag 0x%02x of length %u",
                 descriptor_tag, descriptor_length);

            if (offset + 2 + descriptor_length > program_stream_info_length) {
                return ERROR_MALFORMED;
            }

            br.skipBits(8 * descriptor_length);

            offset += 2 + descriptor_length;
        }

        unsigned elementary_stream_map_length = br.getBits(16);

        offset = 0;
        while (offset < elementary_stream_map_length) {
            if (offset + 4 > elementary_stream_map_length) {
                return ERROR_MALFORMED;
            }

            unsigned stream_type = br.getBits(8);
            unsigned elementary_stream_id = br.getBits(8);

            ALOGI("elementary stream id 0x%02x has stream type 0x%02x",
                 elementary_stream_id, stream_type);

            mStreamTypeByESID.add(elementary_stream_id, stream_type);

            unsigned elementary_stream_info_length = br.getBits(16);

            if (offset + 4 + elementary_stream_info_length
                    > elementary_stream_map_length) {
                return ERROR_MALFORMED;
            }

            offset += 4 + elementary_stream_info_length;
        }

        /* unsigned CRC32 = */br.getBits(32);

        mProgramStreamMapValid = true;
    } else if (stream_id != 0xbe  // padding_stream
            && stream_id != 0xbf  // private_stream_2
            && stream_id != 0xf0  // ECM
            && stream_id != 0xf1  // EMM
            && stream_id != 0xff  // program_stream_directory
            && stream_id != 0xf2  // DSMCC
            && stream_id != 0xf8) {  // H.222.1 type E
        CHECK_EQ(br.getBits(2), 2u);

        /* unsigned PES_scrambling_control = */br.getBits(2);
        /* unsigned PES_priority = */br.getBits(1);
        /* unsigned data_alignment_indicator = */br.getBits(1);
        /* unsigned copyright = */br.getBits(1);
        /* unsigned original_or_copy = */br.getBits(1);

        unsigned PTS_DTS_flags = br.getBits(2);
        ALOGV("PTS_DTS_flags = %u", PTS_DTS_flags);

        unsigned ESCR_flag = br.getBits(1);
        ALOGV("ESCR_flag = %u", ESCR_flag);

        unsigned ES_rate_flag = br.getBits(1);
        ALOGV("ES_rate_flag = %u", ES_rate_flag);

        unsigned DSM_trick_mode_flag = br.getBits(1);
        ALOGV("DSM_trick_mode_flag = %u", DSM_trick_mode_flag);

        unsigned additional_copy_info_flag = br.getBits(1);
        ALOGV("additional_copy_info_flag = %u", additional_copy_info_flag);

        /* unsigned PES_CRC_flag = */br.getBits(1);
        /* PES_extension_flag = */br.getBits(1);

        unsigned PES_header_data_length = br.getBits(8);
        ALOGV("PES_header_data_length = %u", PES_header_data_length);

        unsigned optional_bytes_remaining = PES_header_data_length;

        uint64_t PTS = 0, DTS = 0;

        if (PTS_DTS_flags == 2 || PTS_DTS_flags == 3) {
            CHECK_GE(optional_bytes_remaining, 5u);

            CHECK_EQ(br.getBits(4), PTS_DTS_flags);

            PTS = ((uint64_t)br.getBits(3)) << 30;
            CHECK_EQ(br.getBits(1), 1u);
            PTS |= ((uint64_t)br.getBits(15)) << 15;
            CHECK_EQ(br.getBits(1), 1u);
            PTS |= br.getBits(15);
            CHECK_EQ(br.getBits(1), 1u);

            ALOGV("PTS = %llu", PTS);
            if (mScanning)
            {
                if (PTS > 0 && mFirstPTS == 0)
                    mFirstPTS = PTS;

                if (PTS > 0 && mLastPTS == 0)
                    mLastPTS = PTS;
             }
            // ALOGI("PTS = %.2f secs", PTS / 90000.0f);

            optional_bytes_remaining -= 5;

            if (PTS_DTS_flags == 3) {
                CHECK_GE(optional_bytes_remaining, 5u);

                CHECK_EQ(br.getBits(4), 1u);

                DTS = ((uint64_t)br.getBits(3)) << 30;
                CHECK_EQ(br.getBits(1), 1u);
                DTS |= ((uint64_t)br.getBits(15)) << 15;
                CHECK_EQ(br.getBits(1), 1u);
                DTS |= br.getBits(15);
                CHECK_EQ(br.getBits(1), 1u);

                ALOGV("DTS = %llu", DTS);

                optional_bytes_remaining -= 5;
            }
        }

        if (ESCR_flag) {
            CHECK_GE(optional_bytes_remaining, 6u);

            br.getBits(2);

            uint64_t ESCR = ((uint64_t)br.getBits(3)) << 30;
            CHECK_EQ(br.getBits(1), 1u);
            ESCR |= ((uint64_t)br.getBits(15)) << 15;
            CHECK_EQ(br.getBits(1), 1u);
            ESCR |= br.getBits(15);
            CHECK_EQ(br.getBits(1), 1u);

            ALOGV("ESCR = %llu", ESCR);
            /* unsigned ESCR_extension = */br.getBits(9);

            CHECK_EQ(br.getBits(1), 1u);

            optional_bytes_remaining -= 6;
        }

        if (ES_rate_flag) {
            CHECK_GE(optional_bytes_remaining, 3u);

            CHECK_EQ(br.getBits(1), 1u);
            /* unsigned ES_rate = */br.getBits(22);
            CHECK_EQ(br.getBits(1), 1u);

            optional_bytes_remaining -= 3;
        }

        br.skipBits(optional_bytes_remaining * 8);

        // ES data follows.

        CHECK_GE(PES_packet_length, PES_header_data_length + 3);

        unsigned dataLength =
            PES_packet_length - 3 - PES_header_data_length;

        if (br.numBitsLeft() < dataLength * 8) {
            ALOGE("PES packet does not carry enough data to contain "
                 "payload. (numBitsLeft = %d, required = %d)",
                 br.numBitsLeft(), dataLength * 8);

            return ERROR_MALFORMED;
        }

        CHECK_GE(br.numBitsLeft(), dataLength * 8);

        ssize_t index = mTracks.indexOfKey(stream_id);
        if (index < 0 && mScanning) {
            unsigned streamType;

            ssize_t streamTypeIndex;
            if (mProgramStreamMapValid
                    && (streamTypeIndex =
                            mStreamTypeByESID.indexOfKey(stream_id)) >= 0) {
                streamType = mStreamTypeByESID.valueAt(streamTypeIndex);
            } else if ((stream_id & ~0x1f) == 0xc0) {
                // ISO/IEC 13818-3 or ISO/IEC 11172-3 or ISO/IEC 13818-7
                // or ISO/IEC 14496-3 audio
                streamType = ATSParser::STREAMTYPE_MPEG2_AUDIO;
            } else if ((stream_id & ~0x0f) == 0xe0) {
                // ISO/IEC 13818-2 or ISO/IEC 11172-2 or ISO/IEC 14496-2 video
                streamType = ATSParser::STREAMTYPE_MPEG2_VIDEO;
            } else if (stream_id  == 0xbd) {
                streamType = ATSParser::STREAMTYPE_AUDIO_AC3;
            }else {
                streamType = ATSParser::STREAMTYPE_RESERVED;
            }

            index = mTracks.add(
                    stream_id, new Track(this, stream_id, streamType));
        }

        status_t err = OK;

        if (index >= 0 && !mReverseScan) {
            err =
                mTracks.editValueAt(index)->appendPESData(
                    PTS_DTS_flags, PTS, DTS, br.data(), dataLength);
        }

        br.skipBits(dataLength * 8);

        if (err != OK) {
            return err;
        }
    } else if (stream_id == 0xbe) {  // padding_stream
        CHECK_NE(PES_packet_length, 0u);
        br.skipBits(PES_packet_length * 8);
    } else {
        CHECK_NE(PES_packet_length, 0u);
        br.skipBits(PES_packet_length * 8);
    }

    return n;
}
status_t MPEG2PSExtractor::scanpacketHeader() {
    for(int i=0; i< mBuffer->size(); i++)
    {
        if (!memcmp("\x00\x00\x01\xba", mBuffer->data()+i, 4)) {
            ALOGV("GOT Packet Header");
            mBuffer->setRange(mBuffer->offset() + i, mBuffer->size()-i);
            return OK;
        }
    }
    return -EAGAIN;
}

ssize_t MPEG2PSExtractor::getDuration() {
    Mutex::Autolock autoLock(mLock);

    off64_t offset =0;
    bool invalidmedia=false;
    static const size_t kChunkSize = 4096;
    ssize_t res=-EAGAIN;

    sp<ABuffer> newBuffer = new ABuffer(kChunkSize);;
    sp<ABuffer> oldBuffer = mBuffer;

    ALOGV("First PTS = %lld", mFirstPTS);
    if(mDataSource->getSize(&filesize) == OK)
    {
        ALOGV("File Size = %lld", filesize);
    }
    mLastPTS = 0;
    mReverseScan = true;
    offset = (filesize - kChunkSize) & ~(kChunkSize-1);
    mBuffer = newBuffer;
    for(int i=0;i<500;i++)
    {
        if(mLastPTS != 0 && mLastPTS > mFirstPTS)
            break;
        if(res == OK)
           res = dequeueChunk();
        if(res == -EAGAIN || res == -11)
        {
            mBuffer->setRange(0,mBuffer->capacity());
            mDataSource->readAt(offset, mBuffer->data(),kChunkSize);
            offset = offset - kChunkSize;
            res = scanpacketHeader();
        }
    }

    ALOGV("Last PTS = %lld", mLastPTS);
    mReverseScan = false;
    if(!invalidmedia && (mLastPTS > mFirstPTS))
    {
        mDuration = (mLastPTS - mFirstPTS) * 100 / 9;
        ALOGV("Duration of File is %lld", mDuration);
    } else {
        mDuration = 0;
    }
    for(size_t i=0;i<mTracks.size();i++)
    {
        mTracks.editValueAt(i)->setDuration(mDuration);
    }
    mBuffer = oldBuffer;
    newBuffer->setRange(0,0);
    return OK;
}

////////////////////////////////////////////////////////////////////////////////

MPEG2PSExtractor::Track::Track(
        MPEG2PSExtractor *extractor, unsigned stream_id, unsigned stream_type)
    : mExtractor(extractor),
      mStreamID(stream_id),
      mTrackFirstPTS(0),
      mTrackTypeID(0),
      mTimeUs(0),
      mTrackFirstPTSAvailable(false),
      mStreamType(stream_type),
      mSeekable(true),
      mQueue(NULL) {
    bool supported = true;
    ElementaryStreamQueue::Mode mode;

    switch (mStreamType) {
        case ATSParser::STREAMTYPE_H264:
            mode = ElementaryStreamQueue::H264;
            break;
        case ATSParser::STREAMTYPE_MPEG2_AUDIO_ADTS:
            mode = ElementaryStreamQueue::AAC;
            mSeekable = false;
            break;
        case ATSParser::STREAMTYPE_MPEG1_AUDIO:
        case ATSParser::STREAMTYPE_MPEG2_AUDIO:
            mode = ElementaryStreamQueue::MPEG_AUDIO;
            mSeekable = false;
            break;
        case ATSParser::STREAMTYPE_AUDIO_AC3:
        case ATSParser::STREAMTYPE_AUDIO_EAC3:
            mode = ElementaryStreamQueue::AC3_AUDIO;
            mSeekable = false;
            break;

        case ATSParser::STREAMTYPE_MPEG1_VIDEO:
        case ATSParser::STREAMTYPE_MPEG2_VIDEO:
            mode = ElementaryStreamQueue::MPEG_VIDEO;
            break;

        case ATSParser::STREAMTYPE_MPEG4_VIDEO:
            mode = ElementaryStreamQueue::MPEG4_VIDEO;
            break;

        default:
            supported = false;
            break;
    }

    if (supported) {
        mQueue = new ElementaryStreamQueue(mode);
        if (mQueue != NULL)
            mQueue->setPsExtractor(true);
    } else {
        ALOGI("unsupported stream ID 0x%02x", stream_id);
    }
}

MPEG2PSExtractor::Track::~Track() {
    delete mQueue;
    mQueue = NULL;
}

status_t MPEG2PSExtractor::Track::start(MetaData *params) {
    if (mSource == NULL) {
        return NO_INIT;
    }

    return mSource->start(params);
}

status_t MPEG2PSExtractor::Track::stop() {
    if (mSource == NULL) {
        return NO_INIT;
    }

    return mSource->stop();
}

void MPEG2PSExtractor::Track::setDuration(int64_t dur) {
    mDuration = dur;
}
status_t MPEG2PSExtractor::Track::flush() {
    if (mSource == NULL) {
        return NO_INIT;
    }

    mQueue->clear(true);
    mExtractor->mBuffer->setRange(0, 0);

    mSource->queueDiscontinuity( ATSParser::DISCONTINUITY_TIME, NULL);
    return OK;
}

sp<MetaData> MPEG2PSExtractor::Track::getFormat() {
    if (mSource == NULL) {
        return NULL;
    }
    sp<MetaData> meta = mSource->getFormat();
    meta->setInt64(kKeyDuration, mDuration);
    return meta;
}

status_t MPEG2PSExtractor::Track::read(
        MediaBuffer **buffer, const ReadOptions *options) {
    if (mSource == NULL) {
        return NO_INIT;
    }

    int32_t maxSearch = 0;
    status_t finalResult;
    int64_t seekTimeUs;
    ReadOptions::SeekMode seekMode;

    if (mSeekable && options && options->getSeekTo(&seekTimeUs, &seekMode) && mDuration > 0) {
        mExtractor->seekTo(seekTimeUs);
    }

    while (!mSource->hasBufferAvailable(&finalResult)) {
        if (finalResult != OK) {
            return ERROR_END_OF_STREAM;
        }

        if (maxSearch > MAX_FRAME_SERACH)   // if stream is corrupted like video is for 20 sec in 4 min stream , it will search  whole file for video frame.
            return ERROR_END_OF_STREAM;    // so restricting it to a certain limit

        maxSearch++;
        status_t err = mExtractor->feedMore();

        if (err != OK) {
            mSource->signalEOS(err);
        }
    }

    return mSource->read(buffer, options);
}

status_t MPEG2PSExtractor::Track::appendPESData(
        unsigned PTS_DTS_flags,
        uint64_t PTS, uint64_t DTS,
        const uint8_t *data, size_t size) {
        status_t err;
    if (mQueue == NULL) {
        return OK;
    }

    if (PTS_DTS_flags == 2 || PTS_DTS_flags == 3) {
        mTimeUs = (PTS * 100) / 9;

        if (!mTrackFirstPTSAvailable || mTrackFirstPTS > mTimeUs) {
            mTrackFirstPTS = mTimeUs;
            mTrackFirstPTSAvailable = true;
        }

        mTimeUs = mTimeUs - mTrackFirstPTS;
    }

    if (mStreamType == ATSParser::STREAMTYPE_AUDIO_AC3 && size > 4) {
        unsigned trackTypeID = data[0];
        if (mTrackTypeID == 0) {
            mTrackTypeID = trackTypeID;
        }

        if (trackTypeID != mTrackTypeID) {
            return OK;
        }
        //Need to remove redundent data for partial frame appending
        err = mQueue->appendData(data + 4, size - 4, mTimeUs);
    } else {
        err = mQueue->appendData(data, size, mTimeUs);
    }

    if (err == ERROR_MALFORMED) {  // The packet may be a Invalid packet . Ignoring the error and going
        return OK;                 // to the next packet
    } else if (err != OK) {
        return err;
    }

    sp<ABuffer> accessUnit;
    while ((accessUnit = mQueue->dequeueAccessUnit()) != NULL) {
        if (mSource == NULL) {
            sp<MetaData> meta = mQueue->getFormat();

            if (meta != NULL) {
                const char *_mime;
                CHECK(meta->findCString(kKeyMIMEType, &_mime));
                String8 mime = String8(_mime);

                if (!strncasecmp(mime.string(), "video/", 6)) {
                    mExtractor->mIsVideo = true;
                } else if (!strncasecmp(mime.string(), "audio/", 6)) {
                    mExtractor->mIsAudio = true;
                }

                ALOGV("Stream ID 0x%02x now has data.", mStreamID);

                mSource = new AnotherPacketSource(meta);
                mSource->queueAccessUnit(accessUnit);
            }
        } else if (mQueue->getFormat() != NULL) {
            mSource->queueAccessUnit(accessUnit);
        }
    }

    return OK;
}

////////////////////////////////////////////////////////////////////////////////

MPEG2PSExtractor::WrappedTrack::WrappedTrack(
        const sp<MPEG2PSExtractor> &extractor, const sp<Track> &track)
    : mExtractor(extractor),
      mTrack(track) {
}

MPEG2PSExtractor::WrappedTrack::~WrappedTrack() {
}

status_t MPEG2PSExtractor::WrappedTrack::start(MetaData *params) {
    return mTrack->start(params);
}

status_t MPEG2PSExtractor::WrappedTrack::stop() {
    return mTrack->stop();
}

sp<MetaData> MPEG2PSExtractor::WrappedTrack::getFormat() {
    return mTrack->getFormat();
}

status_t MPEG2PSExtractor::WrappedTrack::read(
        MediaBuffer **buffer, const ReadOptions *options) {
    return mTrack->read(buffer, options);
}

////////////////////////////////////////////////////////////////////////////////

bool SniffMPEG2PS(
        const sp<DataSource> &source, String8 *mimeType, float *confidence,
        sp<AMessage> *) {
    uint8_t header[5];
    if (source->readAt(0, header, sizeof(header)) < (ssize_t)sizeof(header)) {
        return false;
    }

   if (memcmp("\x00\x00\x01\xba", header, 4)) {             //|| (header[4] >> 6) != 1
        return false;
    }

    *confidence = 0.25f;  // Slightly larger than .mp3 extractor's confidence

    mimeType->setTo(MEDIA_MIMETYPE_CONTAINER_MPEG2PS);

    return true;
}

}  // namespace android
