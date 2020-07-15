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
//#define LOG_NDEBUG 0
#define LOG_TAG "ATSParser"
#include <utils/Log.h>

#include "ATSParser.h"

#include "AnotherPacketSource.h"
#include "ESQueue.h"
#include "include/avc_utils.h"

#include <media/stagefright/foundation/ABitReader.h>
#include <media/stagefright/foundation/ABuffer.h>
#include <media/stagefright/foundation/ADebug.h>
#include <media/stagefright/foundation/AMessage.h>
#include <media/stagefright/foundation/hexdump.h>
#include <media/stagefright/MediaDefs.h>
#include <media/stagefright/MediaErrors.h>
#include <media/stagefright/MetaData.h>
#include <media/stagefright/Utils.h>
#include <media/IStreamSource.h>
#include <utils/KeyedVector.h>

//hdcp:
#include <openssl/ssl.h>
#include <openssl/evp.h>
#include <openssl/bio.h>
#include <openssl/err.h>
#include <openssl/aes.h>
#include <openssl/buffer.h>
#include <openssl/engine.h>


namespace android {

//hdcp:
static bool setkeyPES();

// I want the expression "y" evaluated even if verbose logging is off.
#define MY_LOGV(x, y) \
    do { unsigned tmp = y; ALOGV(x, tmp); } while (0)

static const size_t kTSPacketSize = 188;

struct ATSParser::Program : public RefBase {
    Program(ATSParser *parser, unsigned programNumber, unsigned programMapPID);

    bool parsePSISection(
            unsigned pid, ABitReader *br, status_t *err);

    bool parsePID(
            unsigned pid, unsigned continuity_counter,
            unsigned payload_unit_start_indicator,
            ABitReader *br, status_t *err, bool onlypts = false);

    void signalDiscontinuity(
            DiscontinuityType type, const sp<AMessage> &extra);

    void signalEOS(status_t finalResult);

    sp<MediaSource> getSource(SourceType type);

    int64_t convertPTSToTimestamp(uint64_t PTS);

    bool PTSTimeDeltaEstablished() const {
        return mFirstPTSValid;
    }

    unsigned number() const { return mProgramNumber; }

    void updateProgramMapPID(unsigned programMapPID) {
        mProgramMapPID = programMapPID;
    }

    unsigned programMapPID() const {
        return mProgramMapPID;
    }

    uint32_t parserFlags() const {
        return mParser->mFlags;
    }

    void setFirstPts(uint64_t PTS) {
       mParser->setFirstPts(PTS);
    }

    void setLastPts(uint64_t PTS) {
       mParser->setLastPts(PTS);
    }

    void setDurationPID(unsigned pid) {
       mParser->setDurationPID(pid);
    }

private:
    ATSParser *mParser;
    unsigned mProgramNumber;
    unsigned mProgramMapPID;
    bool     mTransition;
    unsigned mNextVidPid;
    KeyedVector<unsigned, sp<Stream> > mStreams;
    bool mFirstPTSValid;
    uint64_t mFirstPTS;

    status_t parseProgramMap(ABitReader *br);

    DISALLOW_EVIL_CONSTRUCTORS(Program);
};

struct ATSParser::Stream : public RefBase {
    Stream(Program *program,
           unsigned elementaryPID,
           unsigned streamType,
           unsigned PCR_PID);

    unsigned type() const { return mStreamType; }
    unsigned pid() const { return mElementaryPID; }
    void setPID(unsigned pid) { mElementaryPID = pid; }

    status_t parse(
            unsigned continuity_counter,
            unsigned payload_unit_start_indicator,
            ABitReader *br, bool onlypts = false);

    void signalDiscontinuity(
            DiscontinuityType type, const sp<AMessage> &extra);

    void signalEOS(status_t finalResult);

    sp<MediaSource> getSource(SourceType type);

protected:
    virtual ~Stream();

private:
    Program *mProgram;
    unsigned mElementaryPID;
    unsigned mStreamType;
    unsigned mPCR_PID;
    int32_t mExpectedContinuityCounter;
    uint64_t firstsPTS;
    uint64_t lastsPTS;

    sp<ABuffer> mBuffer;
    sp<AnotherPacketSource> mSource;
    bool mPayloadStarted;

    uint64_t mPrevPTS;

    ElementaryStreamQueue *mQueue;

    status_t flush(bool onlypts = false);
    status_t parsePES(ABitReader *br, bool onlypts = false);

    void onPayloadData(
            unsigned PTS_DTS_flags, uint64_t PTS, uint64_t DTS,
            const uint8_t *data, size_t size);

    void extractAACFrames(const sp<ABuffer> &buffer);

    bool isAudio() const;
    bool isVideo() const;

    DISALLOW_EVIL_CONSTRUCTORS(Stream);
};

struct ATSParser::PSISection : public RefBase {
    PSISection();

    status_t append(const void *data, size_t size);
    void clear();

    bool isComplete() const;
    bool isEmpty() const;

    const uint8_t *data() const;
    size_t size() const;

protected:
    virtual ~PSISection();

private:
    sp<ABuffer> mBuffer;

    DISALLOW_EVIL_CONSTRUCTORS(PSISection);
};

////////////////////////////////////////////////////////////////////////////////

ATSParser::Program::Program(
        ATSParser *parser, unsigned programNumber, unsigned programMapPID)
    : mParser(parser),
      mProgramNumber(programNumber),
      mProgramMapPID(programMapPID),
      mFirstPTSValid(false),
      mFirstPTS(0),
      mTransition(false),
      mNextVidPid(0) {
    ALOGV("new program number %u", programNumber);
}

bool ATSParser::Program::parsePSISection(
        unsigned pid, ABitReader *br, status_t *err) {
    *err = OK;

    if (pid != mProgramMapPID) {
        return false;
    }

    *err = parseProgramMap(br);

    return true;
}

bool ATSParser::Program::parsePID(
        unsigned pid, unsigned continuity_counter,
        unsigned payload_unit_start_indicator,
        ABitReader *br, status_t *err, bool onlypts) {
    *err = OK;

    ssize_t index = mStreams.indexOfKey(pid);
    if (index < 0) {
        if (onlypts)
           *err = ERROR_MALFORMED;
        return false;
    } else if (mTransition && (mNextVidPid == pid)) {
          sp<AMessage> extra;
          ALOGE("Sending discontinuity for mpeg2 stream for ISDBT mode format change");

          mStreams.editValueAt(index)->signalDiscontinuity(
                                      DISCONTINUITY_FORMATCHANGE, extra);

          mStreams.editValueAt(index)->setPID(mNextVidPid);
          mTransition = false;
    }
    // If the control comes here and if check is for duration
    // lock the PID as we need to find the PTS for same PID
    // from end of file

    *err = mStreams.editValueAt(index)->parse(
            continuity_counter, payload_unit_start_indicator, br, onlypts);

    return true;
}

void ATSParser::Program::signalDiscontinuity(
        DiscontinuityType type, const sp<AMessage> &extra) {
    int64_t mediaTimeUs;
    if ((type & DISCONTINUITY_TIME)
            && extra != NULL
            && extra->findInt64(
                IStreamListener::kKeyMediaTimeUs, &mediaTimeUs)) {
        mFirstPTSValid = false;
    }

    for (size_t i = 0; i < mStreams.size(); ++i) {
        mStreams.editValueAt(i)->signalDiscontinuity(type, extra);
    }
}

void ATSParser::Program::signalEOS(status_t finalResult) {
    for (size_t i = 0; i < mStreams.size(); ++i) {
        mStreams.editValueAt(i)->signalEOS(finalResult);
    }
}

struct StreamInfo {
    unsigned mType;
    unsigned mPID;
};

status_t ATSParser::Program::parseProgramMap(ABitReader *br) {
    unsigned table_id = br->getBits(8);
    ALOGV("  table_id = %u", table_id);
    if (table_id != 0x02u) {
        // This happens in typical DVB-T streams where PMT spans across multiple
        // TS packets,ignoring as it does not harm
        ALOGV("Bogus PMT packet, ignoring");
        return OK;
    }

    unsigned section_syntax_indicator = br->getBits(1);
    ALOGV("  section_syntax_indicator = %u", section_syntax_indicator);
    CHECK_EQ(section_syntax_indicator, 1u);

    CHECK_EQ(br->getBits(1), 0u);
    MY_LOGV("  reserved = %u", br->getBits(2));

    unsigned section_length = br->getBits(12);
    ALOGV("  section_length = %u", section_length);
    CHECK_EQ(section_length & 0xc00, 0u);
    CHECK_LE(section_length, 1021u);

    MY_LOGV("  program_number = %u", br->getBits(16));
    MY_LOGV("  reserved = %u", br->getBits(2));
    MY_LOGV("  version_number = %u", br->getBits(5));
    MY_LOGV("  current_next_indicator = %u", br->getBits(1));
    MY_LOGV("  section_number = %u", br->getBits(8));
    MY_LOGV("  last_section_number = %u", br->getBits(8));
    MY_LOGV("  reserved = %u", br->getBits(3));

    unsigned PCR_PID = br->getBits(13);
    ALOGV("  PCR_PID = 0x%04x", PCR_PID);

    MY_LOGV("  reserved = %u", br->getBits(4));

    unsigned program_info_length = br->getBits(12);
    ALOGV("  program_info_length = %u", program_info_length);
    CHECK_EQ(program_info_length & 0xc00, 0u);

    br->skipBits(program_info_length * 8);  // skip descriptors

    Vector<StreamInfo> infos;

    // infoBytesRemaining is the number of bytes that make up the
    // variable length section of ES_infos. It does not include the
    // final CRC.
    size_t infoBytesRemaining = section_length - 9 - program_info_length - 4;

    if (infoBytesRemaining > (br->numBitsLeft() / 8)) {
        ALOGV("TBD: Warning! the PMT continues in next ts packet");
        infoBytesRemaining = (br->numBitsLeft() / 8);
    }
#define MY_CHECK_EQ(a, b) \
    if ((a) != (b)) { \
        ALOGV("Warning: PMT error, skipping"); \
        break; \
    }

#define MY_CHECK_GE(a, b) \
    if ((a) < (b)) { \
        ALOGV("Warning: PMT error, skipping"); \
        break; \
    }

    while (infoBytesRemaining > 0) {
        MY_CHECK_GE(infoBytesRemaining, 5u);

        unsigned streamType = br->getBits(8);
        ALOGV("    stream_type = 0x%02x", streamType);

        MY_LOGV("    reserved = %u", br->getBits(3));

        unsigned elementaryPID = br->getBits(13);
        ALOGV("    elementary_PID = 0x%04x", elementaryPID);

        MY_LOGV("    reserved = %u", br->getBits(4));

        unsigned ES_info_length = br->getBits(12);
        ALOGV("    ES_info_length = %u", ES_info_length);
        MY_CHECK_EQ(ES_info_length & 0xc00, 0u);

        MY_CHECK_GE(infoBytesRemaining - 5, ES_info_length);

#if 0
        br->skipBits(ES_info_length * 8);  // skip descriptors
#else
        unsigned info_bytes_remaining = ES_info_length;
        /* if the length of descriptor is more than data available
        * ignore this descriptor , typically happens for ISDBT/DVB-T streams where
        * PMT spans across multiple ts packets
        * reference links:
        * http://gstreamer.freedesktop.org/data/coverage/lcov
        * /gst-plugins-bad/gst/mpegdemux/gstmpegdesc.c.gcov.html
        * https://source.ridgerun.net/svn/leopardboarddm365/sdk
        * /trunk/fs/apps/live555/src/liveMedia/MPEG2IndexFromTransportStream.cpp
        */
        if (ES_info_length * 8 <= br->numBitsLeft()) {
            while (info_bytes_remaining >= 2) {
                MY_LOGV("      tag = 0x%02x", br->getBits(8));

                unsigned descLength = br->getBits(8);
                ALOGV("      len = %u", descLength);

                MY_CHECK_GE(info_bytes_remaining, 2 + descLength);

                br->skipBits(descLength * 8);

                info_bytes_remaining -= descLength + 2;
            }
            MY_CHECK_EQ(info_bytes_remaining, 0u);
        }
        else {
            br->skipBits(br->numBitsLeft());
        }
#endif

        StreamInfo info;
        info.mType = streamType;
        info.mPID = elementaryPID;
        infos.push(info);

        infoBytesRemaining -= 5 + ES_info_length;
    }

    if (infoBytesRemaining != 0u) {
        ALOGV("Ignoring trailing bits in PMT ES descriptors");
        br->skipBits(br->numBitsLeft());
    }
    // Again for ISDBT/DVB-T Streams where PMT in this packet is incomplete,
    // ignore CRC in such a case
    if (br->numBitsLeft() == 32)
        MY_LOGV("  CRC = 0x%08x", br->getBits(32));
    else
        ALOGV("No CRC found in PMT, br->numBitsLeft() is %d", br->numBitsLeft());

    bool PIDsChanged = false;
    for (size_t i = 0; i < infos.size(); ++i) {
        StreamInfo &info = infos.editItemAt(i);

        ssize_t index = mStreams.indexOfKey(info.mPID);

        if (index >= 0 && mStreams.editValueAt(index)->type() != info.mType) {
            ALOGI("uh oh. stream PIDs have changed.");
            PIDsChanged = true;
            break;
        }

        if (index < 0) {
            // New PID came, first check if we already have a stream handling the
            // same type, send EOS for the old stream as currently we
            // are not handling format/resolution change. TBD
            for (size_t i = 0; i < mStreams.size(); ++i) {
                if(info.mType == mStreams.editValueAt(i)->type()) {
                    sp<AMessage> extra;
                    ALOGV(" FATAL: Stream data type was already handled");
                    // signal discontinuity for old stream if of type mpeg2 video
                    if (((info.mType == 0x01) || (info.mType == 0x02) || (info.mType == 0x1b)) &&
                        ((mStreams.editValueAt(i)->type() == 0x01) ||
                         (mStreams.editValueAt(i)->type() == 0x02) ||
                         (mStreams.editValueAt(i)->type() == 0x1b))) {
                         // Handle the new PID with same stream-source
                         mStreams.add(info.mPID, mStreams.editValueAt(i));
                         mTransition = true;
                         mNextVidPid = info.mPID;
                    } else if ((info.mType == 0x0f) && (mStreams.editValueAt(i)->type() == 0x0f)) {
                         sp<Stream> newstream = mStreams.valueAt(i);
                         mStreams.removeItem(mStreams.editValueAt(i)->pid());
                         mStreams.add(info.mPID, newstream);
                         newstream->setPID(info.mPID);
                    }
                }
            }

        }
    }

    if (PIDsChanged) {
#if 0
        ALOGI("before:");
        for (size_t i = 0; i < mStreams.size(); ++i) {
            sp<Stream> stream = mStreams.editValueAt(i);

            ALOGI("PID 0x%08x => type 0x%02x", stream->pid(), stream->type());
        }

        ALOGI("after:");
        for (size_t i = 0; i < infos.size(); ++i) {
            StreamInfo &info = infos.editItemAt(i);

            ALOGI("PID 0x%08x => type 0x%02x", info.mPID, info.mType);
        }
#endif

        // The only case we can recover from is if we have two streams
        // and they switched PIDs.

        bool success = false;

        if (mStreams.size() == 2 && infos.size() == 2) {
            const StreamInfo &info1 = infos.itemAt(0);
            const StreamInfo &info2 = infos.itemAt(1);

            sp<Stream> s1 = mStreams.editValueAt(0);
            sp<Stream> s2 = mStreams.editValueAt(1);

            bool caseA =
                info1.mPID == s1->pid() && info1.mType == s2->type()
                    && info2.mPID == s2->pid() && info2.mType == s1->type();

            bool caseB =
                info1.mPID == s2->pid() && info1.mType == s1->type()
                    && info2.mPID == s1->pid() && info2.mType == s2->type();

            if (caseA || caseB) {
                unsigned pid1 = s1->pid();
                unsigned pid2 = s2->pid();
                s1->setPID(pid2);
                s2->setPID(pid1);

                mStreams.clear();
                mStreams.add(s1->pid(), s1);
                mStreams.add(s2->pid(), s2);

                success = true;
            }
        }

        if (!success) {
            ALOGI("Stream PIDs changed and we cannot recover.");
            return ERROR_MALFORMED;
        }
    }

    for (size_t i = 0; i < infos.size(); ++i) {
        StreamInfo &info = infos.editItemAt(i);

        ssize_t index = mStreams.indexOfKey(info.mPID);

        if (index < 0) {
            sp<Stream> stream = new Stream(
                    this, info.mPID, info.mType, PCR_PID);

            mStreams.add(info.mPID, stream);
        }
    }

    return OK;
}

sp<MediaSource> ATSParser::Program::getSource(SourceType type) {
    size_t index = (type == AUDIO) ? 0 : 0;

    for (size_t i = 0; i < mStreams.size(); ++i) {
        sp<MediaSource> source = mStreams.editValueAt(i)->getSource(type);
        if (source != NULL) {
            if (index == 0) {
                return source;
            }
            --index;
        }
    }

    return NULL;
}

int64_t ATSParser::Program::convertPTSToTimestamp(uint64_t PTS) {
    if (!(mParser->mFlags & TS_TIMESTAMPS_ARE_ABSOLUTE)) {
        if (!mFirstPTSValid) {
            mFirstPTSValid = true;
            mFirstPTS = PTS;
            PTS = 0;
        } else if (PTS < mFirstPTS) {
            PTS = 0;
        } else {
            PTS -= mFirstPTS;
        }
    }

    int64_t timeUs = (PTS * 100) / 9;

    if (mParser->mAbsoluteTimeAnchorUs >= 0ll) {
        timeUs += mParser->mAbsoluteTimeAnchorUs;
    }

    return timeUs;
}

////////////////////////////////////////////////////////////////////////////////

ATSParser::Stream::Stream(
        Program *program,
        unsigned elementaryPID,
        unsigned streamType,
        unsigned PCR_PID)
    : mProgram(program),
      mElementaryPID(elementaryPID),
      mStreamType(streamType),
      mPCR_PID(PCR_PID),
      mExpectedContinuityCounter(-1),
      mPayloadStarted(false),
      mPrevPTS(0),
      mQueue(NULL),
      firstsPTS(0),
      lastsPTS(0) {
    switch (mStreamType) {
        case STREAMTYPE_H264:
            mQueue = new ElementaryStreamQueue(
                    ElementaryStreamQueue::H264,
                    (mProgram->parserFlags() & ALIGNED_VIDEO_DATA)
                        ? ElementaryStreamQueue::kFlag_AlignedData : 0);
            break;
        case STREAMTYPE_MPEG2_AUDIO_ADTS:
            mQueue = new ElementaryStreamQueue(ElementaryStreamQueue::AAC);
            break;
        case STREAMTYPE_MPEG1_AUDIO:
        case STREAMTYPE_MPEG2_AUDIO:
            mQueue = new ElementaryStreamQueue(
                    ElementaryStreamQueue::MPEG_AUDIO);
            break;

        case STREAMTYPE_MPEG1_VIDEO:
        case STREAMTYPE_MPEG2_VIDEO:
            mQueue = new ElementaryStreamQueue(
                    ElementaryStreamQueue::MPEG_VIDEO);
            break;

        case STREAMTYPE_MPEG4_VIDEO:
            mQueue = new ElementaryStreamQueue(
                    ElementaryStreamQueue::MPEG4_VIDEO);
            break;

        case STREAMTYPE_PCM_AUDIO:
            mQueue = new ElementaryStreamQueue(
                    ElementaryStreamQueue::PCM_AUDIO);
            break;

        case STREAMTYPE_AUDIO_AC3:
        case STREAMTYPE_AUDIO_EAC3:
            mQueue = new ElementaryStreamQueue(
                    ElementaryStreamQueue::AC3_AUDIO);
            break;

        case STREAMTYPE_AUDIO_LPCM:
            mQueue = new ElementaryStreamQueue(
                    ElementaryStreamQueue::LPCM_AUDIO);
            break;

        case STREAMTYPE_VC1_VIDEO:
            mQueue = new ElementaryStreamQueue(
                    ElementaryStreamQueue::VC1_VIDEO);
            break;

        default:
            break;
    }

    ALOGV("new stream PID 0x%02x, type 0x%02x", elementaryPID, streamType);

    if (mQueue != NULL) {
        mBuffer = new ABuffer(192 * 1024);
        mBuffer->setRange(0, 0);
    }

    //hdcp:
    if(true == setkeyPES())
        ALOGI("ATSParser::Stream:setkeyPES:OK.");
    else
        ALOGI("ATSParser::Stream:setkeyPES:failed.");
}

ATSParser::Stream::~Stream() {
    delete mQueue;
    mQueue = NULL;
}

status_t ATSParser::Stream::parse(
        unsigned continuity_counter,
        unsigned payload_unit_start_indicator, ABitReader *br, bool onlypts) {
    

    if (mQueue == NULL) {
        if (!onlypts) {
            return OK;
        } else {
            ALOGV("mQueue is NULL, probably unsupported track in stream");
            return ERROR_MALFORMED;
        }
    }

    // Accumulate the buffer for finding PTS first
    if (onlypts) {
        size_t payloadSizeBits = br->numBitsLeft();
        CHECK_EQ(payloadSizeBits % 8, 0u);

        size_t neededSize = mBuffer->size() + payloadSizeBits / 8;
        if (mBuffer->capacity() < neededSize) {
            // Increment in multiples of 64K.
            neededSize = (neededSize + 65535) & ~65535;
            ALOGI("resizing buffer to %d bytes", neededSize);
            sp<ABuffer> newBuffer = new ABuffer(neededSize);
            memcpy(newBuffer->data(), mBuffer->data(), mBuffer->size());
            newBuffer->setRange(0, mBuffer->size());
            mBuffer = newBuffer;
        }

        memcpy(mBuffer->data() + mBuffer->size(), br->data(), payloadSizeBits / 8);
        mBuffer->setRange(0, mBuffer->size() + payloadSizeBits / 8);

        if (payload_unit_start_indicator) {
            status_t err = flush(onlypts);
            return err;
        } else {
            // discard the buffer as it may screw during reverse read
            mBuffer->setRange(0,0);
            return ERROR_MALFORMED;
        }
    }

    if (mExpectedContinuityCounter >= 0
            && (unsigned)mExpectedContinuityCounter != continuity_counter) {
        ALOGI("discontinuity on stream pid 0x%04x", mElementaryPID);

        mPayloadStarted = false;
        mBuffer->setRange(0, 0);
        mExpectedContinuityCounter = -1;

        return OK;
    }

    mExpectedContinuityCounter = (continuity_counter + 1) & 0x0f;

    if (payload_unit_start_indicator) {
        if (mPayloadStarted) {
            // Otherwise we run the danger of receiving the trailing bytes
            // of a PES packet that we never saw the start of and assuming
            // we have a a complete PES packet.

            status_t err = flush();

            if (err != OK) {
                return err;
            }
        }

        mPayloadStarted = true;
    }

    if (!mPayloadStarted) {
        return OK;
    }

    size_t payloadSizeBits = br->numBitsLeft();
    CHECK_EQ(payloadSizeBits % 8, 0u);

    size_t neededSize = mBuffer->size() + payloadSizeBits / 8;
    if (mBuffer->capacity() < neededSize) {
        // Increment in multiples of 64K.
        neededSize = (neededSize + 65535) & ~65535;

        ALOGI("resizing buffer to %d bytes", neededSize);

        sp<ABuffer> newBuffer = new ABuffer(neededSize);
        memcpy(newBuffer->data(), mBuffer->data(), mBuffer->size());
        newBuffer->setRange(0, mBuffer->size());
        mBuffer = newBuffer;
    }

    memcpy(mBuffer->data() + mBuffer->size(), br->data(), payloadSizeBits / 8);
    mBuffer->setRange(0, mBuffer->size() + payloadSizeBits / 8);

    return OK;
}

bool ATSParser::Stream::isVideo() const {
    switch (mStreamType) {
        case STREAMTYPE_H264:
        case STREAMTYPE_MPEG1_VIDEO:
        case STREAMTYPE_MPEG2_VIDEO:
        case STREAMTYPE_MPEG4_VIDEO:
        case STREAMTYPE_VC1_VIDEO:
            return true;

        default:
            return false;
    }
}

bool ATSParser::Stream::isAudio() const {
    switch (mStreamType) {
        case STREAMTYPE_MPEG1_AUDIO:
        case STREAMTYPE_MPEG2_AUDIO:
        case STREAMTYPE_MPEG2_AUDIO_ADTS:
        case STREAMTYPE_PCM_AUDIO:
        case STREAMTYPE_AUDIO_AC3:
        case STREAMTYPE_AUDIO_EAC3:
        case STREAMTYPE_AUDIO_LPCM:

            return true;

        default:
            return false;
    }
}

void ATSParser::Stream::signalDiscontinuity(
        DiscontinuityType type, const sp<AMessage> &extra) {
    mExpectedContinuityCounter = -1;

    if (mQueue == NULL) {
        return;
    }

    mPayloadStarted = false;
    mBuffer->setRange(0, 0);

    bool clearFormat = false;
    int32_t formatChange = 0;
    bool bClearQueue = true;
    if (isAudio()) {
        if (type & DISCONTINUITY_AUDIO_FORMAT) {
            clearFormat = true;
        }
    } else {
        if (type & DISCONTINUITY_VIDEO_FORMAT) {
            clearFormat = true;
        }
    }

    if ((type & DISCONTINUITY_FORMATCHANGE) && extra != NULL)
    {
        extra->findInt32(
                IStreamListener::kKeyDiscontinuityMask,
                (int32_t *)&formatChange);
        formatChange = formatChange & ATSParser::DISCONTINUITY_FORMATCHANGE;
        bClearQueue = ((formatChange != ATSParser::DISCONTINUITY_FORMATCHANGE) &&
                       (formatChange != ATSParser::DISCONTINUITY_VIDEO_FORMAT));
        LOGI("TS: IStreamListener::kKeyDiscontinuityMask %d clear %d type 0x%x \n",
            formatChange, (int)bClearQueue, type);
    }
    if (bClearQueue)
    {
        LOGD("%s: mQueue->clear type 0x%x \n", __FUNCTION__, type);
        mQueue->clear(clearFormat);
    }

    if (type & DISCONTINUITY_TIME) {
        uint64_t resumeAtPTS;
        if (extra != NULL
                && extra->findInt64(
                    IStreamListener::kKeyResumeAtPTS,
                    (int64_t *)&resumeAtPTS)) {
            int64_t resumeAtMediaTimeUs =
                mProgram->convertPTSToTimestamp(resumeAtPTS);

            extra->setInt64("resume-at-mediatimeUs", resumeAtMediaTimeUs);
        }
        if (mSource != NULL) {
            mSource->queueDiscontinuity(type, extra);
        }
    }
    else if (mSource != NULL && clearFormat) {
        mSource->queueDiscontinuity(type, extra);
    }
}

void ATSParser::Stream::signalEOS(status_t finalResult) {
    if (mSource != NULL) {
        mSource->signalEOS(finalResult);
    }
}

//hdcp:
unsigned char bufferDES[65535];
unsigned char Ks[16];
unsigned char riv[8];
unsigned char lc[16];
unsigned char keyPES[16];
EVP_CIPHER_CTX ctx;

static bool setkeyPES()
{
    unsigned char buffer[40];
    FILE *fp;

    fp = fopen("/data/data/com.antec.smartlink.miracast/tough","rb");
    if(fp == NULL){
        ALOGI("setkeyPES:failed.");
        return false;
    }

    fread(Ks,sizeof(Ks),1,fp);
    fread(riv,sizeof(riv),1,fp);
    fread(lc,sizeof(lc),1,fp);
    for(int i=0;i<16;i++){
        keyPES[i] = Ks[i] ^ lc[i];
    }
    fclose(fp);

    EVP_CIPHER_CTX_init(&ctx);

    ALOGI("setkeyPES:succeeded.");
    return true;
    
}

static void getCtr(unsigned char privateData[16], unsigned char streamCtr[4], unsigned char inputCtr[8])
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
static void decryptPES(unsigned char* pData,
                  unsigned int dataLen, 
                  unsigned char streamCtr[4],
                  unsigned char inputCtr[8])
{
    
    //EVP_CIPHER_CTX ctx;
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

    //EVP_CIPHER_CTX_init(&ctx);
        
    ctr = (inputCtr[0] << 56) |
        (inputCtr[1] << 48) |
        (inputCtr[2] << 40) |
        (inputCtr[3] <<  32) |
        (inputCtr[4] <<  24) |
        (inputCtr[5] <<  16) |
        (inputCtr[6] <<  8) |
        (inputCtr[7]);


    unsigned int offset = 0;
    unsigned int inLen;
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

    //ret = EVP_CIPHER_CTX_cleanup(&ctx);
    //CHECK(ret == 1);

    
    
}
/////////////////////////////////////////////////////////////////////////////

status_t ATSParser::Stream::parsePES(ABitReader *br, bool onlypts) {
    //harrison
    //ALOGI("ATSParser::Stream::parsePES++");
    //hexdump(br->data(),48);
    


    unsigned packet_startcode_prefix = br->getBits(24);

    ALOGV("packet_startcode_prefix = 0x%08x", packet_startcode_prefix);

    if (packet_startcode_prefix != 1) {
        ALOGV("Supposedly payload_unit_start=1 unit does not start "
             "with startcode.");

        return ERROR_MALFORMED;
    }

    CHECK_EQ(packet_startcode_prefix, 0x000001u);

    unsigned stream_id = br->getBits(8);
    ALOGV("stream_id = 0x%02x", stream_id);

    unsigned PES_packet_length = br->getBits(16);
    ALOGV("PES_packet_length = %u", PES_packet_length);

    if (stream_id != 0xbc  // program_stream_map
            && stream_id != 0xbe  // padding_stream
            && stream_id != 0xbf  // private_stream_2
            && stream_id != 0xf0  // ECM
            && stream_id != 0xf1  // EMM
            && stream_id != 0xff  // program_stream_directory
            && stream_id != 0xf2  // DSMCC
            && stream_id != 0xf8) {  // H.222.1 type E
        CHECK_EQ(br->getBits(2), 2u);

        MY_LOGV("PES_scrambling_control = %u", br->getBits(2));
        MY_LOGV("PES_priority = %u", br->getBits(1));
        MY_LOGV("data_alignment_indicator = %u", br->getBits(1));
        MY_LOGV("copyright = %u", br->getBits(1));
        MY_LOGV("original_or_copy = %u", br->getBits(1));

        unsigned PTS_DTS_flags = br->getBits(2);
        ALOGV("PTS_DTS_flags = %u", PTS_DTS_flags);

        unsigned ESCR_flag = br->getBits(1);
        ALOGV("ESCR_flag = %u", ESCR_flag);

        unsigned ES_rate_flag = br->getBits(1);
        ALOGV("ES_rate_flag = %u", ES_rate_flag);

        unsigned DSM_trick_mode_flag = br->getBits(1);
        ALOGV("DSM_trick_mode_flag = %u", DSM_trick_mode_flag);

        unsigned additional_copy_info_flag = br->getBits(1);
        ALOGV("additional_copy_info_flag = %u", additional_copy_info_flag);

        MY_LOGV("PES_CRC_flag = %u", br->getBits(1));
        MY_LOGV("PES_extension_flag = %u", br->getBits(1));

        unsigned PES_header_data_length = br->getBits(8);
        
        //ALOGI("PES_header_data_length = %u", PES_header_data_length);

        unsigned optional_bytes_remaining = PES_header_data_length;

        uint64_t PTS = 0, DTS = 0;

        if (PTS_DTS_flags == 2 || PTS_DTS_flags == 3) {
            CHECK_GE(optional_bytes_remaining, 5u);

            CHECK_EQ(br->getBits(4), PTS_DTS_flags);

            PTS = ((uint64_t)br->getBits(3)) << 30;
            CHECK_EQ(br->getBits(1), 1u);
            PTS |= ((uint64_t)br->getBits(15)) << 15;
            CHECK_EQ(br->getBits(1), 1u);
            PTS |= br->getBits(15);
            CHECK_EQ(br->getBits(1), 1u);

            ALOGV("PTS = 0x%016llx (%.2f)", PTS, PTS / 90000.0);
            if (onlypts) {
                if (PTS > 0 && firstsPTS == 0) {
                    firstsPTS = PTS;
                    mProgram->setFirstPts(PTS);
                    mProgram->setDurationPID(pid());
                    ALOGV("firstPTS = %.2f secs for PID %d", PTS / 90000.0f, pid());
                    return OK;
                } else if (PTS > 0 && lastsPTS == 0) {
                    lastsPTS = PTS;
                    mProgram->setLastPts(PTS);
                    ALOGV("lastPTS = %.2f secs for PID %d", PTS / 90000.0f, pid());
                    return OK;
                }
                // if control comes here while checking duration,
                // we should return error
                return ERROR_MALFORMED;
            }

            optional_bytes_remaining -= 5;

            if (PTS_DTS_flags == 3) {
                CHECK_GE(optional_bytes_remaining, 5u);

                CHECK_EQ(br->getBits(4), 1u);

                DTS = ((uint64_t)br->getBits(3)) << 30;
                CHECK_EQ(br->getBits(1), 1u);
                DTS |= ((uint64_t)br->getBits(15)) << 15;
                CHECK_EQ(br->getBits(1), 1u);
                DTS |= br->getBits(15);
                CHECK_EQ(br->getBits(1), 1u);

                ALOGV("DTS = %llu", DTS);

                optional_bytes_remaining -= 5;
            }
        }

        if (ESCR_flag) {
            CHECK_GE(optional_bytes_remaining, 6u);

            br->getBits(2);

            uint64_t ESCR = ((uint64_t)br->getBits(3)) << 30;
            CHECK_EQ(br->getBits(1), 1u);
            ESCR |= ((uint64_t)br->getBits(15)) << 15;
            CHECK_EQ(br->getBits(1), 1u);
            ESCR |= br->getBits(15);
            CHECK_EQ(br->getBits(1), 1u);

            ALOGV("ESCR = %llu", ESCR);
            MY_LOGV("ESCR_extension = %u", br->getBits(9));

            CHECK_EQ(br->getBits(1), 1u);

            optional_bytes_remaining -= 6;
        }

        if (ES_rate_flag) {
            CHECK_GE(optional_bytes_remaining, 3u);

            CHECK_EQ(br->getBits(1), 1u);
            MY_LOGV("ES_rate = %u", br->getBits(22));
            CHECK_EQ(br->getBits(1), 1u);

            optional_bytes_remaining -= 3;
        }

        //hdcp:get private data
        unsigned char *private_data;
        unsigned char streamCtr[4];
        unsigned char inputCtr[8];
        if(optional_bytes_remaining > 16){
            //ALOGI("optional_bytes_remaining = %u", optional_bytes_remaining);
            br->getBits(8);
            optional_bytes_remaining--;
            private_data = (unsigned char*)br->data();
            getCtr(private_data,streamCtr,inputCtr);

            //ALOGI("ATSParser::Stream::private data:");
            //hexdump(private_data,16);
            //hexdump(streamCtr,4);
            //hexdump(inputCtr,8);
        }
        //////////////////////////////////


        br->skipBits(optional_bytes_remaining * 8);

        // ES data follows.

        if (PES_packet_length != 0) {
            CHECK_GE(PES_packet_length, PES_header_data_length + 3);

            unsigned dataLength =
                PES_packet_length - 3 - PES_header_data_length;

            if (br->numBitsLeft() < dataLength * 8) {
                ALOGE("PES packet does not carry enough data to contain "
                     "payload. (numBitsLeft = %d, required = %d)",
                     br->numBitsLeft(), dataLength * 8);
                //Skip this packet
                br->skipBits(br->numBitsLeft());
                return OK;
            }

            //CHECK_GE(br->numBitsLeft(), dataLength * 8);
            if(br->numBitsLeft() < (dataLength * 8))
            {
                //Skip this packet
                br->skipBits(br->numBitsLeft());
                ALOGE("ATSParser:br->numBitsLeft() < dataLength * 8.");
                return OK;
            }
            
            //hdcp:
            if(optional_bytes_remaining >= 16){
                //ALOGI("ATSParser:optional_bytes_remaining >=16:dataLength=%d.",dataLength);
                if(dataLength < 65535){
                    memcpy(bufferDES,br->data(), dataLength);
                    decryptPES(bufferDES, dataLength, streamCtr, inputCtr);
                    onPayloadData(PTS_DTS_flags, PTS, DTS, bufferDES, dataLength);
                    
                }
            }else{
                onPayloadData(PTS_DTS_flags, PTS, DTS, br->data(), dataLength);
                //ALOGI("ATSParser:optional_bytes_remaining < 16.");
            }

            br->skipBits(dataLength * 8);
        } else {
            onPayloadData(
                    PTS_DTS_flags, PTS, DTS,
                    br->data(), br->numBitsLeft() / 8);

            size_t payloadSizeBits = br->numBitsLeft();
            CHECK_EQ(payloadSizeBits % 8, 0u);

            ALOGV("There's %d bytes of payload.", payloadSizeBits / 8);
        }
    } else if (stream_id == 0xbe) {  // padding_stream
        CHECK_NE(PES_packet_length, 0u);
        br->skipBits(PES_packet_length * 8);
    } else {
        CHECK_NE(PES_packet_length, 0u);
        br->skipBits(PES_packet_length * 8);
    }

    if (onlypts)
        return ERROR_MALFORMED;

    return OK;
}

status_t ATSParser::Stream::flush(bool onlypts) {
    if (mBuffer->size() == 0) {
        return OK;
    }
    uint8_t *ptr = mBuffer->data();
    status_t err = OK;

    ALOGV("flushing stream 0x%04x size = %d", mElementaryPID, mBuffer->size());

    ABitReader br(mBuffer->data(), mBuffer->size());
    // For dvb support check if the stream is actual PES or
    // some other DVB stream messages like DBB/DSM-CC
    if (ptr[0] == 0x00 && ptr[1] == 0x00 && ptr[2] == 0x01) {
        err = parsePES(&br, onlypts);
    }
    else {
        ALOGV("Skipping DVB specific messages now");
        br.skipBits(mBuffer->size() * 8);
        if (onlypts)
            err = ERROR_MALFORMED;
    }

    mBuffer->setRange(0, 0);

    return err;
}

void ATSParser::Stream::onPayloadData(
        unsigned PTS_DTS_flags, uint64_t PTS, uint64_t DTS,
        const uint8_t *data, size_t size) {
#if 0
    ALOGI("payload streamType 0x%02x, PTS = 0x%016llx, dPTS = %lld",
          mStreamType,
          PTS,
          (int64_t)PTS - mPrevPTS);
    mPrevPTS = PTS;
#endif


    ALOGV("onPayloadData mStreamType=0x%02x", mStreamType);

    int64_t timeUs = 0ll;  // no presentation timestamp available.
    if (PTS_DTS_flags == 2 || PTS_DTS_flags == 3) {
        timeUs = mProgram->convertPTSToTimestamp(PTS);
    }

    status_t err = mQueue->appendData(data, size, timeUs);

    if (err != OK) {
        return;
    }

    sp<ABuffer> accessUnit;
    while ((accessUnit = mQueue->dequeueAccessUnit()) != NULL) {
        if (mSource == NULL) {
            sp<MetaData> meta = mQueue->getFormat();

            if (meta != NULL) {
                ALOGV("Stream PID 0x%08x of type 0x%02x now has data.",
                     mElementaryPID, mStreamType);

                mSource = new AnotherPacketSource(meta);
                mSource->queueAccessUnit(accessUnit);
            }
        } else if (mQueue->getFormat() != NULL) {
            // After a discontinuity we invalidate the queue's format
            // and won't enqueue any access units to the source until
            // the queue has reestablished the new format.

            if (mSource->getFormat() == NULL) {
                mSource->setFormat(mQueue->getFormat());
            }
            mSource->queueAccessUnit(accessUnit);
        }
    }
}

sp<MediaSource> ATSParser::Stream::getSource(SourceType type) {
    switch (type) {
        case VIDEO:
        {
            if (isVideo()) {
                return mSource;
            }
            break;
        }

        case AUDIO:
        {
            if (isAudio()) {
                return mSource;
            }
            break;
        }

        default:
            break;
    }

    return NULL;
}

////////////////////////////////////////////////////////////////////////////////

ATSParser::ATSParser(uint32_t flags)
    : mFlags(flags),
      mAbsoluteTimeAnchorUs(-1ll),
      mNumTSPacketsParsed(0),
      mNumPCRs(0),
      firstPTS(0),
      lastPTS(0),
      mDurationPID(0)  {
    mPSISections.add(0 /* PID */, new PSISection);
	tsPacketLen = kTSPacketSize;
}

ATSParser::~ATSParser() {
}

void ATSParser::SetTsPacketLength(ssize_t len) {
    tsPacketLen = len;
}

ssize_t ATSParser::GetTsPacketLength() {
    return tsPacketLen;
}

bool ATSParser::computePts(const void *data, size_t size) {
    bool status = false;
    ABitReader br((const uint8_t *)data, GetTsPacketLength());
    br.skipBits(32);
    if (parseTS(&br, true) == OK) {
       status = true;
    }
    return status;
}

uint64_t ATSParser::computeDuration() {
    return ((lastPTS - firstPTS) * 100/9);
}

status_t ATSParser::feedTSPacket(const void *data, size_t size) {
    CHECK_EQ(size, GetTsPacketLength());

    ABitReader br((const uint8_t *)data, GetTsPacketLength());
    if (GetTsPacketLength() == 192) {
        ALOGV("Blue Ray/M2TS content");
        br.skipBits(32);
    }
    return parseTS(&br);
}

void ATSParser::signalDiscontinuity(
        DiscontinuityType type, const sp<AMessage> &extra) {
    int64_t mediaTimeUs;
    if ((type & DISCONTINUITY_TIME)
            && extra != NULL
            && extra->findInt64(
                IStreamListener::kKeyMediaTimeUs, &mediaTimeUs)) {
        mAbsoluteTimeAnchorUs = mediaTimeUs;
    } else if (type == DISCONTINUITY_ABSOLUTE_TIME) {
        int64_t timeUs;
        CHECK(extra->findInt64("timeUs", &timeUs));

        CHECK(mPrograms.empty());
        mAbsoluteTimeAnchorUs = timeUs;
        return;
    }

    for (size_t i = 0; i < mPrograms.size(); ++i) {
        mPrograms.editItemAt(i)->signalDiscontinuity(type, extra);
    }
}

void ATSParser::signalEOS(status_t finalResult) {
    CHECK_NE(finalResult, (status_t)OK);

    for (size_t i = 0; i < mPrograms.size(); ++i) {
        mPrograms.editItemAt(i)->signalEOS(finalResult);
    }
}

void ATSParser::parseProgramAssociationTable(ABitReader *br) {
    unsigned table_id = br->getBits(8);
    ALOGV("  table_id = %u", table_id);
    CHECK_EQ(table_id, 0x00u);

    unsigned section_syntax_indictor = br->getBits(1);
    ALOGV("  section_syntax_indictor = %u", section_syntax_indictor);
    CHECK_EQ(section_syntax_indictor, 1u);

    CHECK_EQ(br->getBits(1), 0u);
    MY_LOGV("  reserved = %u", br->getBits(2));

    unsigned section_length = br->getBits(12);
    ALOGV("  section_length = %u", section_length);
    CHECK_EQ(section_length & 0xc00, 0u);

    MY_LOGV("  transport_stream_id = %u", br->getBits(16));
    MY_LOGV("  reserved = %u", br->getBits(2));
    MY_LOGV("  version_number = %u", br->getBits(5));
    MY_LOGV("  current_next_indicator = %u", br->getBits(1));
    MY_LOGV("  section_number = %u", br->getBits(8));
    MY_LOGV("  last_section_number = %u", br->getBits(8));

    size_t numProgramBytes = (section_length - 5 /* header */ - 4 /* crc */);
    CHECK_EQ((numProgramBytes % 4), 0u);

    for (size_t i = 0; i < numProgramBytes / 4; ++i) {
        unsigned program_number = br->getBits(16);
        ALOGV("    program_number = %u", program_number);

        MY_LOGV("    reserved = %u", br->getBits(3));

        if (program_number == 0) {
            MY_LOGV("    network_PID = 0x%04x", br->getBits(13));
        } else {
            unsigned programMapPID = br->getBits(13);

            ALOGV("    program_map_PID = 0x%04x", programMapPID);

            bool found = false;
            for (size_t index = 0; index < mPrograms.size(); ++index) {
                const sp<Program> &program = mPrograms.itemAt(index);

                if (program->number() == program_number) {
                    program->updateProgramMapPID(programMapPID);
                    found = true;
                    break;
                }
            }

            if (!found) {
                mPrograms.push(
                        new Program(this, program_number, programMapPID));
            }

            if (mPSISections.indexOfKey(programMapPID) < 0) {
                mPSISections.add(programMapPID, new PSISection);
            }
        }
    }

    MY_LOGV("  CRC = 0x%08x", br->getBits(32));
}

status_t ATSParser::parsePID(
        ABitReader *br, unsigned PID,
        unsigned continuity_counter,
        unsigned payload_unit_start_indicator, bool onlypts) {
    ssize_t sectionIndex = mPSISections.indexOfKey(PID);

    if (sectionIndex >= 0) {
        const sp<PSISection> &section = mPSISections.valueAt(sectionIndex);

        if (payload_unit_start_indicator) {
            //Typical streams for ISDBT/DVB have multiple PAT packets,
            //the followng change is needed for supporting those use-cases

            if (PID != 0) {
                CHECK(section->isEmpty());
            }
            unsigned skip = br->getBits(8);
            br->skipBits(skip * 8);
        }


        CHECK((br->numBitsLeft() % 8) == 0);
        status_t err = section->append(br->data(), br->numBitsLeft() / 8);

        if (err != OK) {
            return err;
        }

        if (!section->isComplete()) {
            return OK;
        }

        ABitReader sectionBits(section->data(), section->size());

        if (PID == 0) {
            parseProgramAssociationTable(&sectionBits);
        } else {
            bool handled = false;
            for (size_t i = 0; i < mPrograms.size(); ++i) {
                status_t err;
                if (!mPrograms.editItemAt(i)->parsePSISection(
                            PID, &sectionBits, &err)) {
                    continue;
                }

                if (err != OK) {
                    return err;
                }

                handled = true;
                break;
            }

            if (!handled) {
                mPSISections.removeItem(PID);
            }
        }

        section->clear();

        if (onlypts)
           return ERROR_MALFORMED;

        return OK;
    }

    bool handled = false;
    for (size_t i = 0; i < mPrograms.size(); ++i) {
        status_t err;
        if (mPrograms.editItemAt(i)->parsePID(
                    PID, continuity_counter, payload_unit_start_indicator,
                    br, &err, onlypts)) {
            if (err != OK) {
                return err;
            }

            handled = true;
            break;
        }
    }

    if (onlypts && !handled)
       return ERROR_MALFORMED;

    if (!handled) {
        ALOGV("PID 0x%04x not handled.", PID);
    }

    return OK;
}

void ATSParser::parseAdaptationField(ABitReader *br, unsigned PID) {
    unsigned adaptation_field_length = br->getBits(8);

    if (adaptation_field_length > 0) {
        unsigned discontinuity_indicator = br->getBits(1);

        if (discontinuity_indicator) {
            ALOGV("PID 0x%04x: discontinuity_indicator = 1 (!!!)", PID);
        }

        br->skipBits(2);
        unsigned PCR_flag = br->getBits(1);

        size_t numBitsRead = 4;

        if (PCR_flag) {
            br->skipBits(4);
            uint64_t PCR_base = br->getBits(32);
            PCR_base = (PCR_base << 1) | br->getBits(1);

            br->skipBits(6);
            unsigned PCR_ext = br->getBits(9);

            // The number of bytes from the start of the current
            // MPEG2 transport stream packet up and including
            // the final byte of this PCR_ext field.
            size_t byteOffsetFromStartOfTSPacket =
                (GetTsPacketLength() - br->numBitsLeft() / 8);

            uint64_t PCR = PCR_base * 300 + PCR_ext;

            ALOGV("PID 0x%04x: PCR = 0x%016llx (%.2f)",
                  PID, PCR, PCR / 27E6);

            // The number of bytes received by this parser up to and
            // including the final byte of this PCR_ext field.
            size_t byteOffsetFromStart =
                mNumTSPacketsParsed * GetTsPacketLength() + byteOffsetFromStartOfTSPacket;

            for (size_t i = 0; i < mPrograms.size(); ++i) {
                updatePCR(PID, PCR, byteOffsetFromStart);
            }

            numBitsRead += 52;
        }

        CHECK_GE(adaptation_field_length * 8, numBitsRead);

        br->skipBits(adaptation_field_length * 8 - numBitsRead);
    }
}

status_t ATSParser::parseTS(ABitReader *br, bool onlypts) {
    ALOGV("---");

    unsigned sync_byte = br->getBits(8);
    CHECK_EQ(sync_byte, 0x47u);

    unsigned transport_error_indicator = br->getBits(1);
    if (transport_error_indicator) {
        ALOGI("transport_error_indicator is set, ignoring packet");
        br->skipBits(br->numBitsLeft());

        if (onlypts)
           return ERROR_MALFORMED;

        return OK;
    }

    unsigned payload_unit_start_indicator = br->getBits(1);
    ALOGV("payload_unit_start_indicator = %u", payload_unit_start_indicator);

    MY_LOGV("transport_priority = %u", br->getBits(1));

    unsigned PID = br->getBits(13);
    ALOGV("PID = 0x%04x", PID);
    if ((PID == 0x1FFF) && (!onlypts)) {
        ALOGV("Found NULL Packet, Ignoring");
        return OK;
    }

    if (onlypts && (mDurationPID != 0) && (mDurationPID != PID)) {
        // We already have identified a PES with a PID to supply first PTS
        // so ignore other PID's for duration
        return ERROR_MALFORMED;
    }

    MY_LOGV("transport_scrambling_control = %u", br->getBits(2));

    unsigned adaptation_field_control = br->getBits(2);
    ALOGV("adaptation_field_control = %u", adaptation_field_control);

    unsigned continuity_counter = br->getBits(4);
    ALOGV("PID = 0x%04x, continuity_counter = %u", PID, continuity_counter);

    // ALOGI("PID = 0x%04x, continuity_counter = %u", PID, continuity_counter);

    if (adaptation_field_control == 2 || adaptation_field_control == 3) {
        parseAdaptationField(br, PID);
    }

    status_t err = OK;

    if (adaptation_field_control == 1 || adaptation_field_control == 3) {
        err = parsePID(
                br, PID, continuity_counter, payload_unit_start_indicator, onlypts);
    }

    ++mNumTSPacketsParsed;

    if ((adaptation_field_control == 2) && onlypts)
        return ERROR_MALFORMED;

    return err;
}

sp<MediaSource> ATSParser::getSource(SourceType type) {
    int which = -1;  // any

    for (size_t i = 0; i < mPrograms.size(); ++i) {
        const sp<Program> &program = mPrograms.editItemAt(i);

        if (which >= 0 && (int)program->number() != which) {
            continue;
        }

        sp<MediaSource> source = program->getSource(type);

        if (source != NULL) {
            return source;
        }
    }

    return NULL;
}

bool ATSParser::PTSTimeDeltaEstablished() {
    if (mPrograms.isEmpty()) {
        return false;
    }

    return mPrograms.editItemAt(0)->PTSTimeDeltaEstablished();
}

void ATSParser::updatePCR(
        unsigned PID, uint64_t PCR, size_t byteOffsetFromStart) {
    ALOGV("PCR 0x%016llx @ %d", PCR, byteOffsetFromStart);

    if (mNumPCRs == 2) {
        mPCR[0] = mPCR[1];
        mPCRBytes[0] = mPCRBytes[1];
        mSystemTimeUs[0] = mSystemTimeUs[1];
        mNumPCRs = 1;
    }

    mPCR[mNumPCRs] = PCR;
    mPCRBytes[mNumPCRs] = byteOffsetFromStart;
    mSystemTimeUs[mNumPCRs] = ALooper::GetNowUs();

    ++mNumPCRs;

    if (mNumPCRs == 2) {
        double transportRate =
            (mPCRBytes[1] - mPCRBytes[0]) * 27E6 / (mPCR[1] - mPCR[0]);

        ALOGV("transportRate = %.2f bytes/sec", transportRate);
    }
}

////////////////////////////////////////////////////////////////////////////////

ATSParser::PSISection::PSISection() {
}

ATSParser::PSISection::~PSISection() {
}

status_t ATSParser::PSISection::append(const void *data, size_t size) {
    if (mBuffer == NULL || mBuffer->size() + size > mBuffer->capacity()) {
        size_t newCapacity =
            (mBuffer == NULL) ? size : mBuffer->capacity() + size;

        newCapacity = (newCapacity + 1023) & ~1023;

        sp<ABuffer> newBuffer = new ABuffer(newCapacity);

        if (mBuffer != NULL) {
            memcpy(newBuffer->data(), mBuffer->data(), mBuffer->size());
            newBuffer->setRange(0, mBuffer->size());
        } else {
            newBuffer->setRange(0, 0);
        }

        mBuffer = newBuffer;
    }

    memcpy(mBuffer->data() + mBuffer->size(), data, size);
    mBuffer->setRange(0, mBuffer->size() + size);

    return OK;
}

void ATSParser::PSISection::clear() {
    if (mBuffer != NULL) {
        mBuffer->setRange(0, 0);
    }
}

bool ATSParser::PSISection::isComplete() const {
    if (mBuffer == NULL || mBuffer->size() < 3) {
        return false;
    }

    unsigned sectionLength = U16_AT(mBuffer->data() + 1) & 0xfff;
    return mBuffer->size() >= sectionLength + 3;
}

bool ATSParser::PSISection::isEmpty() const {
    return mBuffer == NULL || mBuffer->size() == 0;
}

const uint8_t *ATSParser::PSISection::data() const {
    return mBuffer == NULL ? NULL : mBuffer->data();
}

size_t ATSParser::PSISection::size() const {
    return mBuffer == NULL ? 0 : mBuffer->size();
}

}  // namespace android
