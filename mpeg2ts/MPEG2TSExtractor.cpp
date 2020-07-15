/*
 * Copyright (C) 2010 The Android Open Source Project
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

#define LOG_NDEBUG 0
#define LOG_TAG "MPEG2TSExtractor"
#include <utils/Log.h>

#include "include/MPEG2TSExtractor.h"
#include "include/LiveSession.h"
#include "include/NuCachedSource2.h"

#include <media/stagefright/foundation/ADebug.h>
#include <media/stagefright/DataSource.h>
#include <media/stagefright/MediaDefs.h>
#include <media/stagefright/MediaErrors.h>
#include <media/stagefright/MediaSource.h>
#include <media/stagefright/MetaData.h>
#include <media/IStreamSource.h>
#include <utils/String8.h>

#include "AnotherPacketSource.h"
#include "ATSParser.h"

namespace android {

static const size_t NV_MAX_TS_READ_SIZE = 100;
static const size_t kMaxTSPacketSize = 192;
static const size_t kTSPacketSize = 188;

struct MPEG2TSSource : public MediaSource {
    MPEG2TSSource(
            const sp<MPEG2TSExtractor> &extractor,
            const sp<AnotherPacketSource> &impl,
            bool seekable);

    virtual status_t start(MetaData *params = NULL);
    virtual status_t stop();
    virtual sp<MetaData> getFormat();

    virtual status_t read(
            MediaBuffer **buffer, const ReadOptions *options = NULL);

private:
    sp<MPEG2TSExtractor> mExtractor;
    sp<AnotherPacketSource> mImpl;

    // If there are both audio and video streams, only the video stream
    // will be seekable, otherwise the single stream will be seekable.
    bool mSeekable;

    DISALLOW_EVIL_CONSTRUCTORS(MPEG2TSSource);
};

MPEG2TSSource::MPEG2TSSource(
        const sp<MPEG2TSExtractor> &extractor,
        const sp<AnotherPacketSource> &impl,
        bool seekable)
    : mExtractor(extractor),
      mImpl(impl),
      mSeekable(seekable) {
}

status_t MPEG2TSSource::start(MetaData *params) {
    return mImpl->start(params);
}

status_t MPEG2TSSource::stop() {
    return mImpl->stop();
}

sp<MetaData> MPEG2TSSource::getFormat() {
    sp<MetaData> meta = mImpl->getFormat();

    int64_t durationUs;
    if (mExtractor->mLiveSession != NULL
            && mExtractor->mLiveSession->getDuration(&durationUs) == OK) {
        meta->setInt64(kKeyDuration, durationUs);
    }

    if (mExtractor->IsM2TSMedia()) {
       meta->setInt64(kKeyDuration, mExtractor->GetM2TSDuration());
    }

    return meta;
}

status_t MPEG2TSSource::read(
        MediaBuffer **out, const ReadOptions *options) {
    *out = NULL;

    int64_t seekTimeUs = 0;
    ReadOptions::SeekMode seekMode;
    if (options && options->getSeekTo(&seekTimeUs, &seekMode) && (seekTimeUs == 0)) {
       // reset the position for thumbnail case
       mExtractor->mOffset = 0;
    }

    if (mSeekable && seekTimeUs > 0) {
        mExtractor->seekTo(seekTimeUs);
    }

    status_t finalResult;
    while (!mImpl->hasBufferAvailable(&finalResult)) {
        if (finalResult != OK) {
            return ERROR_END_OF_STREAM;
        }

        status_t err = mExtractor->feedMore();
        if (err != OK) {
            mImpl->signalEOS(err);
        }
    }

    return mImpl->read(out, options);
}

////////////////////////////////////////////////////////////////////////////////

MPEG2TSExtractor::MPEG2TSExtractor(const sp<DataSource> &source)
    : mDataSource(source),
      mParser(new ATSParser),
      mOffset(0),
      durationUs(-1),
      fileSize(0),
      isM2TSMedia(false) {
    init();
}

size_t MPEG2TSExtractor::countTracks() {
    return mSourceImpls.size();
}

sp<MediaSource> MPEG2TSExtractor::getTrack(size_t index) {
    if (index >= mSourceImpls.size()) {
        return NULL;
    }

    bool seekable = true;
    if (mSourceImpls.size() > 1) {
        CHECK_EQ(mSourceImpls.size(), 2u);

        sp<MetaData> meta = mSourceImpls.editItemAt(index)->getFormat();
        const char *mime;
        CHECK(meta->findCString(kKeyMIMEType, &mime));
        if ((!isM2TSMedia) ||
            (!strncasecmp("audio/", mime, 6))) {
            seekable = false;
        }
    }

    return new MPEG2TSSource(this, mSourceImpls.editItemAt(index), seekable);
}

sp<MetaData> MPEG2TSExtractor::getTrackMetaData(
        size_t index, uint32_t flags) {
    if (isM2TSMedia) {
        sp<MetaData> meta;
        if (index < mSourceImpls.size()) {
            meta = mSourceImpls.editItemAt(index)->getFormat();
        }
        // For LPCM Audio
        const char *mime;
        CHECK(meta->findCString(kKeyMIMEType, &mime));
        if (!strcasecmp(MEDIA_MIMETYPE_AUDIO_RAW, mime)) {
            ALOGE("RAW Audio Type, Setting track duration as %lld", durationUs);
            meta->setInt64(kKeyDuration, durationUs);
        }

        if (flags & kIncludeExtensiveMetaData) {
            const char *mime;
            CHECK(meta->findCString(kKeyMIMEType, &mime));
            if (!strncasecmp("video/", mime, 6)) {
                if (durationUs > (7 * 1000000)) {
                    meta->setInt64( kKeyThumbnailTime, (7 * 1000000));
                }
            }
        }
        return meta;
    }

    return index < mSourceImpls.size()
        ? mSourceImpls.editItemAt(index)->getFormat() : NULL;
}

sp<MetaData> MPEG2TSExtractor::getMetaData() {
    sp<MetaData> meta = new MetaData;
    meta->setCString(kKeyMIMEType, MEDIA_MIMETYPE_CONTAINER_MPEG2TS);

    return meta;
}

void MPEG2TSExtractor::init() {
    bool haveAudio = false;
    bool haveVideo = false;
    int numPacketsParsed = 0;

    if ((mParser->GetTsPacketLength() != kTSPacketSize ||  mParser->GetTsPacketLength() != kMaxTSPacketSize))
    {
        ALOGV("Setting the TS Packet Size");
        mParser->SetTsPacketLength(kTSPacketSize);
        char header[5];
        if (mDataSource->readAt( 0,  header, 5) == 5)
        //Check if it is m2ts or normal ts
        {
             if (*header == 0x47)
             {
                 mParser->SetTsPacketLength(kTSPacketSize);
             }
             else if (*(header + 4) == 0x47)
             {
                 mParser->SetTsPacketLength(kMaxTSPacketSize);
                 isM2TSMedia = true;
             }
        }
        ALOGV("Set the TS packet size as %d", mParser->GetTsPacketLength());
    }

    if (isM2TSMedia) {
        ALOGE("Duration of m2ts media is %lld", GetM2TSDuration());
    }

    bool isLocalPlayBack = mDataSource->flags() & DataSource::kIsLocalDataSource;
    uint8_t numPacketsToReadAtTime = isLocalPlayBack?NV_MAX_TS_READ_SIZE:1;

    while (feedMore() == OK) {
        ATSParser::SourceType type;
        if (haveAudio && haveVideo) {
            break;
        }
        if (!haveVideo) {
            sp<AnotherPacketSource> impl =
                (AnotherPacketSource *)mParser->getSource(
                        ATSParser::VIDEO).get();

            if (impl != NULL) {
                haveVideo = true;
                mSourceImpls.push(impl);
            }
        }

        if (!haveAudio) {
            sp<AnotherPacketSource> impl =
                (AnotherPacketSource *)mParser->getSource(
                        ATSParser::AUDIO).get();

            if (impl != NULL) {
                haveAudio = true;
                mSourceImpls.push(impl);
            }
        }

        // Number of ts packets to be parsed to determine type of streams
        // Assuming 25000 packets is sufficient
        numPacketsParsed += numPacketsToReadAtTime;
        if (numPacketsParsed > 25000) {
            break;
        }
    }

    ALOGI("haveAudio=%d, haveVideo=%d", haveAudio, haveVideo);
}

status_t MPEG2TSExtractor::feedMore() {
    Mutex::Autolock autoLock(mLock);

    status_t ret = OK;
    uint8_t packet[kMaxTSPacketSize * NV_MAX_TS_READ_SIZE];
    bool isLocalPlayBack = mDataSource->flags() & DataSource::kIsLocalDataSource;
    uint8_t numPacketsToReadAtTime = isLocalPlayBack?NV_MAX_TS_READ_SIZE:1;
    ssize_t n = mDataSource->readAt(mOffset, packet, mParser->GetTsPacketLength() * numPacketsToReadAtTime);

    if (n < (mParser->GetTsPacketLength() * numPacketsToReadAtTime)) {
         return (n < 0) ? (status_t)n : ERROR_END_OF_STREAM;
    }

    mOffset += n;
    off64_t offset = 0;
    ssize_t iter = n / mParser->GetTsPacketLength();
    while (iter) {
         ret = mParser->feedTSPacket(packet + offset, mParser->GetTsPacketLength());
         if (ret != OK)
           break;
         iter--;
         offset += mParser->GetTsPacketLength();
    }
    return ret;
}

void MPEG2TSExtractor::setLiveSession(const sp<LiveSession> &liveSession) {
    Mutex::Autolock autoLock(mLock);

    mLiveSession = liveSession;
}

void MPEG2TSExtractor::seekTo(int64_t seekTimeUs) {
    Mutex::Autolock autoLock(mLock);
    // M2TS seek
    if ((isM2TSMedia) && (mLiveSession == NULL)) {
         sp<AMessage> extra = new AMessage;
         extra->setInt64(IStreamListener::kKeyResumeAtPTS, (seekTimeUs/100) * 9);
         mParser->signalDiscontinuity(ATSParser::DISCONTINUITY_TIME, extra);
         // Now convert seekTo point to fileoffset and set mOffset to there
         ALOGV("In SeekTo fileSize: %lld seekTimeUs : %lld durationUs: %lld", fileSize, seekTimeUs, durationUs);
         off64_t seekOffset = (fileSize / (1.0 * durationUs)) * seekTimeUs;
         // Align the offset to 192
         ALOGV("Seek offset is %lld", seekOffset);
         seekOffset -= (seekOffset % mParser->GetTsPacketLength());
         ALOGV("Aligned Seek offset is %lld", seekOffset);
         mOffset = seekOffset;
         return;
    }

    if (mLiveSession == NULL) {
        return;
    }

    mLiveSession->seekTo(seekTimeUs);
}

uint32_t MPEG2TSExtractor::flags() const {
    Mutex::Autolock autoLock(mLock);

    uint32_t flags = CAN_PAUSE;

    if (((isM2TSMedia) && (mLiveSession == NULL)) ||
       (mLiveSession != NULL && mLiveSession->isSeekable())) {
        flags |= CAN_SEEK_FORWARD | CAN_SEEK_BACKWARD | CAN_SEEK;
    }

    return flags;
}

uint64_t MPEG2TSExtractor::GetM2TSDuration() {
    bool firstPesFound = false;
    bool lastPesFound  = false;
    int64_t offset = 0;
    unsigned residue = 0;
    bool invalidmedia = false;

    if (durationUs >= 0)
        return durationUs;

    if ((isM2TSMedia)&& (durationUs < 0)) {
        // M2TS/MTS format, compute duration
        uint8_t packet[kMaxTSPacketSize];
        // Try hitting the first PES packet first
        int numPacketsParsed = 0;
        while (!firstPesFound && numPacketsParsed < 25000) {
            ssize_t n = mDataSource->readAt(offset, packet, mParser->GetTsPacketLength());

            if (n < mParser->GetTsPacketLength()) {
                // Invalid packets in file, set duration as 0
                invalidmedia = true;
                break;
            }

            ++numPacketsParsed;
            offset += n;
            firstPesFound = mParser->computePts(packet, mParser->GetTsPacketLength());
        }
        // Go backwards from end for last PES PTS
        if (mDataSource->getSize(&fileSize) == OK) {
            ALOGV("M2TS file size is %lld bytes", fileSize);
            // Some m2ts files have truncated last frame so correct the last offset
            residue = fileSize % mParser->GetTsPacketLength();
            ALOGV("Warning M2TS file size is not multiple of 192 %d bytes", residue);
        }

        offset = (fileSize - residue) - mParser->GetTsPacketLength();
        fileSize -= residue;
        numPacketsParsed = 0;
        while (!lastPesFound && offset > 0 && numPacketsParsed < 25000) {
            ssize_t n = mDataSource->readAt(offset, packet, mParser->GetTsPacketLength());

            if (n < mParser->GetTsPacketLength()) {
                // Invalid packets in file, set duration as 0
                invalidmedia = true;
                break;
            }

            ++numPacketsParsed;
            offset -= n;
            lastPesFound = mParser->computePts(packet, mParser->GetTsPacketLength());
        }

        if (!invalidmedia && (firstPesFound && lastPesFound)) {
            durationUs = mParser->computeDuration();
            return durationUs;
        } else {
            return 0;
        }
    }
    return 0;
}

bool MPEG2TSExtractor::IsM2TSMedia() {
    return (isM2TSMedia);
}

////////////////////////////////////////////////////////////////////////////////

bool SniffMPEG2TS(
        const sp<DataSource> &source, String8 *mimeType, float *confidence,
        sp<AMessage> *) {
    bool success = false;
    size_t tspacketlen = kTSPacketSize;
    for (int i = 0; i < 5; ++i) {
        char header[5];
        //Check if it is m2ts or normal ts
        if (source->readAt(tspacketlen * i, header, 5) == 5) {
            if(*header == 0x47) {
                 ALOGV("Normal TS content keeping kTSPacketSize as %d", tspacketlen);
                 success = true;
            } else if(*(header + 4) == 0x47) {
                 tspacketlen = kMaxTSPacketSize;
                 ALOGV("M2TS Blųe Ray Content keeping kTSPacketSize as %d", tspacketlen);
                 success = true;
            } else {
                ALOGV("Unrecognised TS Content");
                return success;
            }
        } else {
            ALOGV("Unrecognised TS Content");
            return success;
        }
    }
    *confidence = 0.1f;
    mimeType->setTo(MEDIA_MIMETYPE_CONTAINER_MPEG2TS);

    return success;
}

}  // namespace android
