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
#define LOG_TAG "ESQueue"
#include <media/stagefright/foundation/ADebug.h>

#include "ESQueue.h"

#include <media/stagefright/foundation/hexdump.h>
#include <media/stagefright/foundation/ABitReader.h>
#include <media/stagefright/foundation/ABuffer.h>
#include <media/stagefright/foundation/AMessage.h>
#include <media/stagefright/MediaErrors.h>
#include <media/stagefright/MediaDefs.h>
#include <media/stagefright/MetaData.h>
#include <media/stagefright/Utils.h>

#include "include/avc_utils.h"

#include <netinet/in.h>

namespace android {
static const struct {
    const uint32_t bitrate;
    const uint32_t framesize[3];
}framesize_table[38] =
{
    { 32,    { 64, 69, 96}},
    { 32,    { 64, 70, 96}},
    { 40,    { 80, 87, 120}},
    { 40,    { 80, 88, 120}},
    { 48,    { 96, 104, 144}},
    { 48,    { 96, 105, 144}},
    { 56,    { 112, 121, 168}},
    { 56,    { 112, 122, 168}},
    { 64,    { 128, 139, 192}},
    { 64,    { 128, 140, 192}},
    { 80,    { 160, 174, 240}},
    { 80,    { 160, 175, 240}},
    { 96,    { 192, 208, 288}},
    { 96,    { 192, 209, 288}},
    { 112,   { 224, 243, 336}},
    { 112,   { 224, 244, 336}},
    { 128,   { 256, 278, 384}},
    { 128,   { 256, 279, 384}},
    { 160,   { 320, 348, 480}},
    { 160,   { 320, 349, 480}},
    { 192,   { 384, 417, 576}},
    { 192,   { 384, 418, 576}},
    { 224,   { 448, 487, 672}},
    { 224,   { 448, 488, 672}},
    { 256,   { 512, 557, 768}},
    { 256,   { 512, 558, 768}},
    { 320,   { 640, 696, 960}},
    { 320,   { 640, 697, 960}},
    { 384,   { 768, 835, 1152}},
    { 384,   { 768, 836, 1152}},
    { 448,   { 896, 975, 1344}},
    { 448,   { 896, 976, 1344}},
    { 512,   { 1024, 1114, 1536}},
    { 512,   { 1024, 1115, 1536}},
    { 576,   { 1152, 1253, 1728}},
    { 576,   { 1152, 1254, 1728}},
    { 640,   { 1280, 1393, 1920}},
    { 640,   { 1280, 1394, 1920}}
};

static const uint32_t frame_rates[4] = { 48000, 44100, 32000, 0 };
static const uint32_t acmod_channels[8] = { 2, 1, 2, 3, 3, 4, 4, 5 };
static const uint32_t numblks[4] = { 1, 2, 3, 6 };
 
ElementaryStreamQueue::ElementaryStreamQueue(Mode mode, uint32_t flags)
    : mMode(mode),
      mFlags(flags),
      mvideoHeight(480),
      mvideoWidth(640),
      bseqHdrSent(false),
      mPsExtractor(false) {
}

sp<MetaData> ElementaryStreamQueue::getFormat() {
    return mFormat;
}

void ElementaryStreamQueue::clear(bool clearFormat) {
    if (mBuffer != NULL) {
        mBuffer->setRange(0, 0);
    }

    mRangeInfos.clear();

    if (clearFormat) {
        mFormat.clear();
    }
}

void ElementaryStreamQueue::setPsExtractor(bool isPsExtractor) {
    mPsExtractor = isPsExtractor;
}

static bool IsSeeminglyValidVC1SeqHeader(const uint8_t *ptr, size_t size, uint16_t &w, uint16_t& h) 
{
    if (size < 9)
        return false;

    //Check sequence start code
    if ((ptr[0] == 0x00) && (ptr[1] == 0x00) && (ptr[2] == 0x01) && (ptr[3] == 0x0F))
    {
        ptr += 4;
        ABitReader br(ptr, size -4);
        // Check profile and level
        uint8_t profile = br.getBits(2);
        uint8_t level = br.getBits(3);
        br.skipBits(11);
        uint16_t max_coded_width = br.getBits(12);
        uint16_t max_coded_height = br.getBits(12);
        uint16_t width = max_coded_width * 2 + 2;
        uint16_t height = max_coded_height * 2 + 2;
        if ((w != width) ||
           (h != height))
        {
            w = width;
            h = height;
        }
        ALOGV("profile is %d, level is %d width x height is %d x %d", profile, level, width, height);
        // As per BDAV spec, only Advanced profile level 2 and level 3 are valid
        if ((profile == 3) && ((level == 2) || (level == 3))) {
            return true;
        }
    }

    return false;
}

static bool IsSeeminglyValidMPEG2SeqHeader(const uint8_t *ptr, size_t size, uint16_t& width, uint16_t& height) {

    if (size < 7)
        return false;

    if ((ptr[0] == 0x00) && (ptr[1] == 0x00) && (ptr[2] == 0x01) && (ptr[3] == 0xB3))
    {
        uint16_t VideoWidth = (ptr[4] << 4)  | (ptr[5] & 0xF0);
        uint16_t VideoHeight = ((ptr[5] & 0x0F) << 8) | (ptr[6]);
        if ((width != VideoWidth) ||
           (height != VideoHeight))
        {
            width = VideoWidth;
            height = VideoHeight;
        }
        return true;
    }

    return false;
}

static bool ParseLPCMHeader(const uint8_t* data, size_t size, uint32_t* bps,
                          uint32_t* rate, uint32_t* ch) {
    ABitReader br(data, size);
    uint32_t hdr = br.getBits(32);
    switch(( hdr & 0xf000) >> 12)
    {
    case 1:
        *ch = 1;
        break;
    case 3:
        *ch = 2;
        break;
    case 4:
        *ch = 3;
        break;
    case 5:
        *ch = 3;
        break;
    case 6:
        *ch = 4;
        break;
    case 7:
        *ch = 4;
        break;
    case 8:
        *ch = 5;
        break;
    case 9:
        *ch = 6;
        break;
    case 10:
        *ch = 7;
        break;
    case 11:
        *ch = 8;
        break;

    default:
        return false;
    }

    switch((hdr >> 6) & 0x03)
    {
    case 1:
        *bps = 16;
        break;
    case 2: /* 20 bits but samples are stored on 24 bits */
    case 3: /* 24 bits */
        *bps = 24;
        break;
    default:
        return false;
    }

    switch((hdr >> 8) & 0x0f)
    {
    case 1:
        *rate = 48000;
        break;
    case 4:
        *rate = 96000;
        break;
    case 5:
        *rate = 192000;
        break;
    default:
        return false;
    }

    ALOGV("LPCM: Channnel count is %d, Sample Rate is %d, BPS is %d", *ch, *rate, *bps);
    return true;
}

static bool ParseAC3Frame(const uint8_t* data, size_t size, uint32_t* framesize,
                          uint32_t* rate, uint32_t* ch, uint32_t* blk,
                          uint32_t* sid) {
    if (size < 7)
        return false;

    ABitReader br(data, size);
    uint8_t lfe_on = 0;
    uint8_t acmod = 0;
    br.skipBits(32);
    uint8_t framescode = br.getBits(2);
    uint8_t framesizecode = br.getBits(6);

    if (framescode == 3 || framesizecode >= 38)
        return false;

    uint8_t bsid = br.getBits(5);
    br.skipBits(3); //bsmod
    acmod = br.getBits(3);

    if (bsid > 8) {
        ALOGV("Unexpected bsid");
        return false;
    } else if (bsid != 8 && bsid != 6)
        // Spec not clear
        ALOGV("Undefined bѕid, ignoring");

    if ((acmod & 0x01) && (acmod != 0x01))
        ALOGV("3 Front Channelѕ");
    if ((acmod & 0x04))
        ALOGV("Surround Channelѕ");
    if ((acmod == 0x02))
        ALOGV("Stereo Channelѕ");

    br.skipBits(2);

    lfe_on = br.getBits(1);

    ALOGV("AC3 Frame Params framesize is \t\t %d \n sample rate is \t\t %d \n"  \
         "number of channels \t\t %d \n num blocks \t\t %d \n sid \t\t %d",
         framesize_table[framesizecode].framesize[framescode] *2 , frame_rates[framescode],
         acmod_channels[acmod] + lfe_on, 6, 0);

    if(framesize)
        *framesize = framesize_table[framesizecode].framesize[framescode] * 2;

    if(rate)
        *rate = frame_rates[framescode];

    if(ch)
        *ch = acmod_channels[acmod] + lfe_on;

    if(blk)
        *blk = 6;

    if(sid)
       *sid = 0;

    return true;

}

static bool ParseEAC3Frame(const uint8_t* data, size_t size, uint32_t* framesize,
                          uint32_t* rate, uint32_t* ch, uint32_t* blk,
                          uint32_t* sid) {
    ABitReader br(data, size);
    uint8_t lfe_on = 0;
    uint8_t acmod = 0;
    uint8_t framescode2 = 0;
    uint8_t samplerate = 0;
    uint8_t lblk = 0;
    uint8_t numblkcode = 0;

    ALOGV("Parsing EAC3");

    br.skipBits(16);
    uint8_t strmtype = br.getBits(2);

    if (strmtype == 3) {
        ALOGV("Bad Streamtype in EAC3");
        return false;
    }

    uint8_t streamId = br.getBits(3);
    uint8_t lframesize = br.getBits(11);
    uint8_t framescode = br.getBits(2);
    if (framescode == 3) {
        framescode2 = br.getBits(2);
        if (framescode2 == 3) {
            ALOGV("Invalid framescode2 ");
            return false;
        }
        samplerate = frame_rates[framescode2] / 2;
        lblk = 6;
    } else {
        numblkcode = br.getBits(2);
        samplerate = frame_rates[framescode];
        lblk = numblks[numblkcode];
    }

    acmod = br.getBits(3);
    lfe_on = br.getBits(1);

    br.skipBits(5); //bsid

    ALOGV("EAC3 Frame Params framesize is \t\t %d \n sample rate is \t\t %d \n" \
         "number of channels \t\t %d \n num blocks \t\t %d \n sid \t\t %d",
         (lframesize + 1) * 2 , samplerate,
         acmod_channels[acmod] + lfe_on, lblk, (strmtype & 0x01) << 3 | streamId);

    if(framesize)
        *framesize = (lframesize + 1) * 2;

    if(rate)
        *rate = samplerate;

    if(ch)
        *ch = acmod_channels[acmod] + lfe_on;

    if(blk)
        *blk = lblk;

    if(sid)
       *sid = (strmtype & 0x01) << 3 | streamId;

    return true;
}

static bool IsSeeminglyValidAC3Header(const uint8_t *ptr, size_t size,
                      uint32_t* framesize, uint32_t* rate, uint32_t* ch,
                      uint32_t* blk, uint32_t* sid, bool& iseac3) {
    if (size < 6)
        return false;

    iseac3 = false;

    ABitReader bits(ptr, size);
    uint16_t sync = bits.getBits(16);
    if(sync != 0x0b77)
       return false;

    bits.skipBits(24);
    uint8_t bsid = bits.getBits(5);
    ALOGV(" AC3 bsid is %d", bsid);
    size_t offset = 0;
    uint8_t success = 0;

    while (offset < (size-7)) {
        if (bsid <= 10)
        {
            uint8_t *header = (uint8_t *)ptr + offset;
            //parse ac3 frame header
            if(!ParseAC3Frame(header, size, framesize, rate, ch, blk, sid))
                return false;

            LOGV("returning from the ac-3 parsing\n");
            if (*framesize == 0) {
                return false;
            } else {
                offset += *framesize;
                success++;
            }

            if (success == 3)
                break;
        }
        else if (bsid <= 16)
        {
            iseac3 = true;
            uint8_t *header = (uint8_t *)ptr + offset;
            //parse the eac3 frame header
            if(!ParseEAC3Frame(header, size, framesize, rate, ch, blk, sid))
                return false;

            if (*framesize == 0) {
                offset++;
                continue;
            } else {
                offset += *framesize;
                success++;
            }

            if (success == 3)
                break;
        } else {
            return false;
        }
    }
    return true;
}

static bool IsSeeminglyValidADTSHeader(const uint8_t *ptr, size_t size) {
    if (size < 3) {
        // Not enough data to verify header.
        return false;
    }

    if (ptr[0] != 0xff || (ptr[1] >> 4) != 0x0f) {
        return false;
    }

    unsigned layer = (ptr[1] >> 1) & 3;

    if (layer != 0) {
        return false;
    }

    unsigned ID = (ptr[1] >> 3) & 1;
    unsigned profile_ObjectType = ptr[2] >> 6;

    if (ID == 1 && profile_ObjectType == 3) {
        // MPEG-2 profile 3 is reserved.
        return false;
    }

    // Check the aac_frame_length also as some ts packets have no valid aac frames
    unsigned aac_frame_length = (((uint16_t)(ptr[3] & 0x3)) << 11)
            | (((uint16_t)ptr[4]) << 3) | (ptr[5] >> 5);
    if (aac_frame_length == 0)
       return false;

    return true;
}

static bool IsSeeminglyValidMPEGAudioHeader(const uint8_t *ptr, size_t size) {
    size_t frameSize = 0;
    int    samplerate = 0;
    int    channels = 0;
    int    bitrate = 0;
    int    num_out_samples = 0;
    size_t offset = 0;
    uint8_t success = 0;

    while (offset < (size - 4)) {
        uint32_t header = U32_AT(ptr + offset);
        bool ret = GetMPEGAudioFrameSize(header,
                                   &frameSize,
                                   &samplerate,
                                   &channels,
                                   &bitrate,
                                   &num_out_samples);
        if (!ret) {
            //hexdump(&header, 4); // keeping it, intentional
            ALOGV("Incorrect mp3 sync, resyncing");
            return false;
        }
        offset += frameSize;
        success++;
        if (success == 3) // 3 back to back frames is enough to trust
           break;
    }

    return true;
}

status_t ElementaryStreamQueue::appendData(
        const void *data, size_t size, int64_t timeUs) {
    if (mBuffer == NULL || mBuffer->size() == 0) {
        switch (mMode) {
            case H264:
            {
#if 0
                if (size < 4 || memcmp("\x00\x00\x00\x01", data, 4)) {
                    return ERROR_MALFORMED;
                }
#else
                uint8_t *ptr = (uint8_t *)data;

                ssize_t startOffset = -1;
                for (size_t i = 0; i + 3 < size; ++i) {
                    if (!memcmp("\x00\x00\x00\x01", &ptr[i], 4)) {
                        startOffset = i;
                        break;
                    }
                }

                if (startOffset < 0) {
                    return ERROR_MALFORMED;
                }

                if (startOffset > 0) {
                    ALOGI("found something resembling an H.264/MPEG syncword at "
                         "offset %ld",
                         startOffset);
                }

                data = &ptr[startOffset];
                size -= startOffset;
#endif
                break;
            }

            case MPEG_VIDEO:
            {
                uint8_t *ptr = (uint8_t *)data;
                ssize_t startOffset = -1;
                //Ignoring the error from the below function call currently as
                // nvidia mpeg2 decoder can discard video frames till it received a
                // valid sequence header
                if (!bseqHdrSent) {
                    for (size_t i = 0; i < size ; i++) {
                        if ((IsSeeminglyValidMPEG2SeqHeader(&ptr[i], size-i,mvideoWidth, mvideoHeight))) {
                            bseqHdrSent = true;
                            startOffset = i;
                           break;
                        }
                    }

                   if (!bseqHdrSent) {
                        return ERROR_MALFORMED;
                    }
                    data = &ptr[startOffset];
                    size -= startOffset;
                }

                break;
            }

            case MPEG4_VIDEO:
            {
#if 0
                if (size < 3 || memcmp("\x00\x00\x01", data, 3)) {
                    return ERROR_MALFORMED;
                }
#else
                uint8_t *ptr = (uint8_t *)data;

                ssize_t startOffset = -1;
                for (size_t i = 0; i + 2 < size; ++i) {
                    if (!memcmp("\x00\x00\x01", &ptr[i], 3)) {
                        startOffset = i;
                        break;
                    }
                }

                if (startOffset < 0) {
                    return ERROR_MALFORMED;
                }

                if (startOffset > 0) {
                    ALOGI("found something resembling an H.264/MPEG syncword at "
                         "offset %ld",
                         startOffset);
                }

                data = &ptr[startOffset];
                size -= startOffset;
#endif
                break;
            }

            case AAC:
            {
                uint8_t *ptr = (uint8_t *)data;

#if 0
                if (size < 2 || ptr[0] != 0xff || (ptr[1] >> 4) != 0x0f) {
                    return ERROR_MALFORMED;
                }
#else
                ssize_t startOffset = -1;
                for (size_t i = 0; i < size; ++i) {
                    if (IsSeeminglyValidADTSHeader(&ptr[i], size - i)) {
                        startOffset = i;
                        break;
                    }
                }

                if (startOffset < 0) {
                    return ERROR_MALFORMED;
                }

                if (startOffset > 0) {
                    ALOGI("found something resembling an AAC syncword at offset %ld",
                         startOffset);
                }

                data = &ptr[startOffset];
                size -= startOffset;
#endif
                break;
            }

            case MPEG_AUDIO:
            {
                uint8_t *ptr = (uint8_t *)data;

                ssize_t startOffset = -1;
                for (size_t i = 0; i < size; ++i) {
                    if (IsSeeminglyValidMPEGAudioHeader(&ptr[i], size - i)) {
                        startOffset = i;
                        break;
                    }
                }

                if (startOffset < 0) {
                    return ERROR_MALFORMED;
                }

                if (startOffset > 0) {
                    ALOGI("found something resembling an MPEG audio "
                         "syncword at offset %ld",
                         startOffset);
                }

                data = &ptr[startOffset];
                size -= startOffset;
                break;
            }

            case PCM_AUDIO:
            {
                break;
            }

            case AC3_AUDIO:
            {
                uint8_t *ptr = (uint8_t *)data;
                ALOGV("Parsing AC3 audio stream");
                ssize_t startOffset = -1;
                size_t frameSize;
                bool iseac3 = false;
                for (size_t i = 0; i < size; ++i) {
                    if (IsSeeminglyValidAC3Header(&ptr[i], size - i, &frameSize,
                                         NULL, NULL, NULL, NULL, iseac3)) {
                        startOffset = i;
                        break;
                    }
                }

                if (startOffset < 0) {
                    return ERROR_MALFORMED;
                }

                if (startOffset > 0) {
                    ALOGI("found something resembling an AC3 syncword at offset %ld",
                         startOffset);
                }
                data = &ptr[startOffset];
                size -= startOffset;
                break;
            }

            case LPCM_AUDIO:
            {
                uint8_t *ptr = (uint8_t *)data;
                ALOGV("Parsing LPCM audio stream");
                ssize_t startOffset = -1;
                for (size_t i = 0; i < size; ++i) {
                    uint32_t bps;
                    uint32_t rate;
                    uint32_t ch;
                    if (ParseLPCMHeader(&ptr[i], size - i, &bps, &rate, &ch)) {
                        startOffset = i;
                        break;
                    }
                }

                if (startOffset < 0) {
                    return ERROR_MALFORMED;
                }

                if (startOffset > 0) {
                    ALOGI("found something resembling an LPCM syncword at offset %ld",
                         startOffset);
                }
                data = &ptr[startOffset];
                size -= startOffset;
                break;
            }

            case VC1_VIDEO:
            {
                uint8_t *ptr = (uint8_t *)data;
                // Make sure we always start with a sequence header IDU
                if ((IsSeeminglyValidVC1SeqHeader(&ptr[0], size, mvideoWidth, mvideoHeight)))
                    bseqHdrSent = true;
                else if (!bseqHdrSent)
                    return ERROR_MALFORMED;

                break;
            }

            default:
                TRESPASS();
                break;
        }
    }

    size_t neededSize = (mBuffer == NULL ? 0 : mBuffer->size()) + size;
    if (mBuffer == NULL || neededSize > mBuffer->capacity()) {
        neededSize = (neededSize + 65535) & ~65535;

        ALOGV("resizing buffer to size %d", neededSize);

        sp<ABuffer> buffer = new ABuffer(neededSize);
        if (mBuffer != NULL) {
            memcpy(buffer->data(), mBuffer->data(), mBuffer->size());
            buffer->setRange(0, mBuffer->size());
        } else {
            buffer->setRange(0, 0);
        }

        mBuffer = buffer;
    }

    memcpy(mBuffer->data() + mBuffer->size(), data, size);
    mBuffer->setRange(0, mBuffer->size() + size);

    RangeInfo info;
    info.mLength = size;
    info.mTimestampUs = timeUs;
    mRangeInfos.push_back(info);

#if 0
    if (mMode == AAC) {
        ALOGI("size = %d, timeUs = %.2f secs", size, timeUs / 1E6);
        hexdump(data, size);
    }
#endif

    return OK;
}

sp<ABuffer> ElementaryStreamQueue::dequeueAccessUnit() {
    if ((mFlags & kFlag_AlignedData) && mMode == H264) {
        if (mRangeInfos.empty()) {
            return NULL;
        }

        RangeInfo info = *mRangeInfos.begin();
        mRangeInfos.erase(mRangeInfos.begin());

        sp<ABuffer> accessUnit = new ABuffer(info.mLength);
        memcpy(accessUnit->data(), mBuffer->data(), info.mLength);
        accessUnit->meta()->setInt64("timeUs", info.mTimestampUs);

        memmove(mBuffer->data(),
                mBuffer->data() + info.mLength,
                mBuffer->size() - info.mLength);

        mBuffer->setRange(0, mBuffer->size() - info.mLength);

        if (mFormat == NULL) {
            mFormat = MakeAVCCodecSpecificData(accessUnit);
        }

        return accessUnit;
    }

    switch (mMode) {
        case H264:
            return dequeueAccessUnitH264();
        case AAC:
            return dequeueAccessUnitAAC();
        case MPEG_VIDEO:
            return dequeueAccessUnitMPEGVideo();
        case MPEG4_VIDEO:
            return dequeueAccessUnitMPEG4Video();
        case PCM_AUDIO:
            return dequeueAccessUnitPCMAudio();
        case AC3_AUDIO:
            return dequeueAccessUnitAC3();
        case LPCM_AUDIO:
            return dequeueAccessUnitLPCM();
        case VC1_VIDEO:
            return dequeueAccessUnitVC1Video();

        default:
            CHECK_EQ((unsigned)mMode, (unsigned)MPEG_AUDIO);
            return dequeueAccessUnitMPEGAudio();
    }
}

sp<ABuffer> ElementaryStreamQueue::dequeueAccessUnitPCMAudio() {
    if (mBuffer->size() < 4) {
        return NULL;
    }

    ABitReader bits(mBuffer->data(), 4);
    CHECK_EQ(bits.getBits(8), 0xa0);
    unsigned numAUs = bits.getBits(8);
    bits.skipBits(8);
    unsigned quantization_word_length = bits.getBits(2);
    unsigned audio_sampling_frequency = bits.getBits(3);
    unsigned num_channels = bits.getBits(3);

    CHECK_EQ(audio_sampling_frequency, 2);  // 48kHz
    CHECK_EQ(num_channels, 1u);  // stereo!

    if (mFormat == NULL) {
        mFormat = new MetaData;
        mFormat->setCString(kKeyMIMEType, MEDIA_MIMETYPE_AUDIO_RAW);
        mFormat->setInt32(kKeyChannelCount, 2);
        mFormat->setInt32(kKeySampleRate, 48000);
    }

    static const size_t kFramesPerAU = 80;
    size_t frameSize = 2 /* numChannels */ * sizeof(int16_t);

    size_t payloadSize = numAUs * frameSize * kFramesPerAU;

    if (mBuffer->size() < 4 + payloadSize) {
        return NULL;
    }

    sp<ABuffer> accessUnit = new ABuffer(payloadSize);
    memcpy(accessUnit->data(), mBuffer->data() + 4, payloadSize);

    int64_t timeUs = fetchTimestamp(payloadSize + 4);
    CHECK_GE(timeUs, 0ll);
    accessUnit->meta()->setInt64("timeUs", timeUs);

    int16_t *ptr = (int16_t *)accessUnit->data();
    for (size_t i = 0; i < payloadSize / sizeof(int16_t); ++i) {
        ptr[i] = ntohs(ptr[i]);
    }

    memmove(
            mBuffer->data(),
            mBuffer->data() + 4 + payloadSize,
            mBuffer->size() - 4 - payloadSize);

    mBuffer->setRange(0, mBuffer->size() - 4 - payloadSize);

    return accessUnit;
}

sp<ABuffer> ElementaryStreamQueue::dequeueAccessUnitVC1Video() {
    size_t auSize = mBuffer->size();
    uint8_t *data = mBuffer->data();

    if(auSize == 0)
        return NULL;

     if (mFormat == NULL) {
        //Create codec spefic data
        mFormat = new MetaData;
        mFormat->setCString(kKeyMIMEType, MEDIA_MIMETYPE_VIDEO_WMV);
        ALOGE("Setting Mimetype as MEDIA_MIMETYPE_VIDEO_WMV");
        ALOGE("Width is %d, Height is %d", mvideoWidth, mvideoHeight);
        mFormat->setInt32(kKeyWidth, mvideoWidth);
        mFormat->setInt32(kKeyHeight, mvideoHeight);
    }

    int64_t timeUs = fetchTimestamp(auSize);
    sp<ABuffer> accessUnit = new ABuffer(auSize);
    memcpy(accessUnit->data(), mBuffer->data(), auSize);
    mBuffer->setRange(0, mBuffer->size() - auSize);

    if (timeUs >= 0) {
       accessUnit->meta()->setInt64("timeUs", timeUs);
    } else {
       ALOGW("no time for VC1 access unit");
    }

    return accessUnit;
}

sp<ABuffer> ElementaryStreamQueue::dequeueAccessUnitLPCM() {
    size_t auSize = mBuffer->size();
    uint32_t sample_rate = 0;
    uint32_t channels = 0;
    uint32_t bps = 0;

    if(auSize == 0)
        return NULL;

     if (mFormat == NULL) {
         //Create codec spefic data
        if (!ParseLPCMHeader(mBuffer->data(), mBuffer->size(),
                                   &bps, &sample_rate, &channels)) {
             //Discard this buffer and proceed
            ALOGV("Failed to parse the LPCM header");
            return NULL;
        }
        mFormat = new MetaData;
        mFormat->setCString(kKeyMIMEType, MEDIA_MIMETYPE_AUDIO_RAW);
        ALOGE("Setting Mimetype as MEDIA_MIME_TYPE_AUDIO_RAW");
        ALOGE("Setting SampleRate as %d and channels as %d", sample_rate, channels);
        mFormat->setInt32(kKeySampleRate, sample_rate);
        mFormat->setInt32(kKeyChannelCount, channels);
    }

    int64_t timeUs = fetchTimestamp(auSize);
    sp<ABuffer> accessUnitSwapped = new ABuffer(auSize - 4);
    // Swap and copy data, ignore first 4 byte header
    uint8_t* data = mBuffer->data() + 4;
    uint8_t tmp;
    for (int index = 0; index < (auSize - 4); index += 2) {
       tmp = data[index];
       data[index] = data[index + 1];
       data[index + 1] = tmp;
    }

    memcpy(accessUnitSwapped->data(), mBuffer->data() + 4, auSize - 4);
    mBuffer->setRange(0, mBuffer->size() - auSize);

    if (timeUs >= 0) {
       accessUnitSwapped->meta()->setInt64("timeUs", timeUs);
    } else {
       ALOGW("no time for LPCM access unit");
    }

    return accessUnitSwapped;
}

sp<ABuffer> ElementaryStreamQueue::dequeueAccessUnitAC3() {
    size_t auSize = mBuffer->size();
    uint32_t framesize = 0;
    uint32_t sample_rate = 0;
    uint32_t channels = 0;
    uint32_t numBlks = 0;
    uint32_t sid = 0;
    bool iseac3 = false;
    ssize_t pos = 0;
    const uint8_t *data = mBuffer->data();

    do
    {
        if(auSize == 0)
            return NULL;

        for(pos=0;pos<(ssize_t)auSize;pos++)
        {
            if (IsSeeminglyValidAC3Header(&data[pos], auSize-pos,
                                   &framesize, &sample_rate, &channels,
                                   &numBlks, &sid, iseac3)) {
                //Discard this buffer and proceed
                LOGV("Got the AC3 frame at pos %d", pos);
                break;
            }
        }
        if (mFormat == NULL) {
            mFormat = new MetaData;
            mFormat->setCString(kKeyMIMEType, MEDIA_MIMETYPE_AUDIO_AC3);
            LOGV("Setting Mimetype as MEDIA_MIME_TYPE_AUDIO_AC3");
            mFormat->setInt32(kKeySampleRate, sample_rate);
            mFormat->setInt32(kKeyChannelCount, channels);
        }

        if(auSize <= 0 || auSize < (framesize + pos) || framesize == 0) {
            return NULL;
        }

        auSize = auSize - framesize - pos;

        int64_t timeUs = fetchTimestamp(framesize);
        sp<ABuffer> accessUnit = new ABuffer(framesize);
        memcpy(accessUnit->data(), mBuffer->data()+ pos, framesize);
        memmove(mBuffer->data(), mBuffer->data()+ pos + framesize,
                mBuffer->size() - framesize - pos);
        mBuffer->setRange(0, mBuffer->size() - framesize - pos);
        if (timeUs >= 0) {
            accessUnit->meta()->setInt64("timeUs", timeUs);
        } else {
            LOGW("no time for AC3 access unit");
        }
        return accessUnit;
    } while (auSize < framesize);

    return NULL;
}

sp<ABuffer> ElementaryStreamQueue::dequeueAccessUnitAAC() {
    int64_t timeUs;

    size_t offset = 0;
    while (offset + 7 <= mBuffer->size()) {
        ABitReader bits(mBuffer->data() + offset, mBuffer->size() - offset);

        // adts_fixed_header

        if (bits.getBits(12) !=  0xfffu) {
            ALOGV("Bad ADTS sync header, skipping");
            // clear the buffer so that bogus aac packets are cleaned
            mBuffer->setRange(0, 0);
            return NULL;
        }

        bits.skipBits(3);  // ID, layer
        bool protection_absent = bits.getBits(1) != 0;

        if (mFormat == NULL) {
            unsigned profile = bits.getBits(2);
            CHECK_NE(profile, 3u);
            unsigned sampling_freq_index = bits.getBits(4);
            bits.getBits(1);  // private_bit
            unsigned channel_configuration = bits.getBits(3);
            //CHECK_NE(channel_configuration, 0u);
            if (channel_configuration == 0)
                ALOGE("Invalid channel configuration, ignoring for decoder to decide");
            bits.skipBits(2);  // original_copy, home

            mFormat = MakeAACCodecSpecificData(
                    profile, sampling_freq_index, channel_configuration);

            mFormat->setInt32(kKeyIsADTS, true);

            int32_t sampleRate;
            int32_t numChannels;
            // Lets rely on decoder to find the stream configuration
            mFormat->findInt32(kKeySampleRate, &sampleRate);
            mFormat->findInt32(kKeyChannelCount, &numChannels);

            ALOGI("found AAC codec config (%d Hz, %d channels)",
                 sampleRate, numChannels);
        } else {
            // profile_ObjectType, sampling_frequency_index, private_bits,
            // channel_configuration, original_copy, home
            bits.skipBits(12);
        }

        // adts_variable_header

        // copyright_identification_bit, copyright_identification_start
        bits.skipBits(2);

        unsigned aac_frame_length = bits.getBits(13);

        bits.skipBits(11);  // adts_buffer_fullness

        unsigned number_of_raw_data_blocks_in_frame = bits.getBits(2);

        if (number_of_raw_data_blocks_in_frame != 0) {
            // To be implemented.
            TRESPASS();
        }

        if (offset + aac_frame_length > mBuffer->size()) {
            break;
        }

        size_t headerSize = protection_absent ? 7 : 9;

        int64_t tmpUs = fetchTimestamp(aac_frame_length);
        CHECK_GE(tmpUs, 0ll);

        if (offset == 0) {
            timeUs = tmpUs;
        }

        offset += aac_frame_length;
    }

    if (offset == 0) {
        return NULL;
    }

    sp<ABuffer> accessUnit = new ABuffer(offset);
    memcpy(accessUnit->data(), mBuffer->data(), offset);

    memmove(mBuffer->data(), mBuffer->data() + offset,
            mBuffer->size() - offset);
    mBuffer->setRange(0, mBuffer->size() - offset);

    accessUnit->meta()->setInt64("timeUs", timeUs);

    return accessUnit;
}

int64_t ElementaryStreamQueue::fetchTimestamp(size_t size) {
    int64_t timeUs = -1;
    bool first = true;

    while (size > 0) {
        CHECK(!mRangeInfos.empty());

        RangeInfo *info = &*mRangeInfos.begin();

        if (first) {
            timeUs = info->mTimestampUs;
            first = false;
        }

        if (info->mLength > size) {
            info->mLength -= size;

            if (first) {
                info->mTimestampUs = -1;
            }

            size = 0;
        } else {
            size -= info->mLength;

            mRangeInfos.erase(mRangeInfos.begin());
            info = NULL;
        }
    }

    if (timeUs == 0ll) {
        ALOGV("Returning 0 timestamp");
    }

    return timeUs;
}

struct NALPosition {
    size_t nalOffset;
    size_t nalSize;
};

sp<ABuffer> ElementaryStreamQueue::dequeueAccessUnitH264() {
    const uint8_t *data = mBuffer->data();

    size_t size = mBuffer->size();
    Vector<NALPosition> nals;

    size_t totalSize = 0;

    status_t err;
    const uint8_t *nalStart;
    size_t nalSize;
    bool foundSlice = false;
    while ((err = getNextNALUnit(&data, &size, &nalStart, &nalSize)) == OK) {
        CHECK_GT(nalSize, 0u);

        unsigned nalType = nalStart[0] & 0x1f;
        bool flush = false;

        if (!bseqHdrSent && (nalType == 5  || nalType == 1)) {
            bseqHdrSent = true;
        }

        if (bseqHdrSent && (nalType == 1 || nalType == 5)) {
            if (foundSlice) {
                ABitReader br(nalStart + 1, nalSize);
                unsigned first_mb_in_slice = parseUE(&br);

                if (first_mb_in_slice == 0) {
                    // This slice starts a new frame.

                    flush = true;
                }
            }

            foundSlice = true;
        } else if ((nalType == 9 || nalType == 7) && foundSlice) {
            // Access unit delimiter and SPS will be associated with the
            // next frame.

            flush = true;
        }

        if (flush) {
            // The access unit will contain all nal units up to, but excluding
            // the current one, separated by 0x00 0x00 0x00 0x01 startcodes.

            size_t auSize = 4 * nals.size() + totalSize;
            sp<ABuffer> accessUnit = new ABuffer(auSize);

#if !LOG_NDEBUG
            AString out;
#endif

            size_t dstOffset = 0;
            for (size_t i = 0; i < nals.size(); ++i) {
                const NALPosition &pos = nals.itemAt(i);

                unsigned nalType = mBuffer->data()[pos.nalOffset] & 0x1f;

#if !LOG_NDEBUG
                char tmp[128];
                sprintf(tmp, "0x%02x", nalType);
                if (i > 0) {
                    out.append(", ");
                }
                out.append(tmp);
#endif

                memcpy(accessUnit->data() + dstOffset, "\x00\x00\x00\x01", 4);

                memcpy(accessUnit->data() + dstOffset + 4,
                       mBuffer->data() + pos.nalOffset,
                       pos.nalSize);

                dstOffset += pos.nalSize + 4;
            }

            //ALOGV("accessUnit contains nal types %s", out.c_str());

            const NALPosition &pos = nals.itemAt(nals.size() - 1);
            size_t nextScan = pos.nalOffset + pos.nalSize;

            memmove(mBuffer->data(),
                    mBuffer->data() + nextScan,
                    mBuffer->size() - nextScan);

            mBuffer->setRange(0, mBuffer->size() - nextScan);

            int64_t timeUs = fetchTimestamp(nextScan);
            CHECK_GE(timeUs, 0ll);

            accessUnit->meta()->setInt64("timeUs", timeUs);

            if (mFormat == NULL) {
                mFormat = MakeAVCCodecSpecificData(accessUnit);
                if(mFormat  != NULL){
                    int32_t width, height;

                    mFormat->findInt32(kKeyWidth, &width);
                    mFormat->findInt32(kKeyHeight, &height);

                    ALOGV("accessUnit:w:h=%d:%d.",width,height);

                    if((width <= 0) || (height<= 0)){
                        mFormat->setInt32(kKeyWidth, 640);
                        mFormat->setInt32(kKeyHeight, 480);
                    }
                }
            }

            return accessUnit;
        }

        NALPosition pos;
        pos.nalOffset = nalStart - mBuffer->data();
        pos.nalSize = nalSize;

        nals.push(pos);

        totalSize += nalSize;
    }
    CHECK_EQ(err, (status_t)-EAGAIN);

    return NULL;
}

sp<ABuffer> ElementaryStreamQueue::dequeueAccessUnitMPEGAudio() {
    const uint8_t *data = mBuffer->data();
    size_t size = mBuffer->size();

    if (size < 4) {
        return NULL;
    }

    uint32_t header = U32_AT(data);

    size_t frameSize;
    int samplingRate, numChannels, bitrate, numSamples;

    if (!GetMPEGAudioFrameSize(
                header, &frameSize, &samplingRate, &numChannels,
                &bitrate, &numSamples)) {
       // ignore the complete buffer and force resync
       ALOGE("Lost MPEG Audio sync");
       mBuffer->setRange(0,0);
       return NULL;
    }

    if (size < frameSize) {
        return NULL;
    }

    unsigned layer = 4 - ((header >> 17) & 3);

    sp<ABuffer> accessUnit = new ABuffer(frameSize);
    memcpy(accessUnit->data(), data, frameSize);

    memmove(mBuffer->data(),
            mBuffer->data() + frameSize,
            mBuffer->size() - frameSize);

    mBuffer->setRange(0, mBuffer->size() - frameSize);

    int64_t timeUs = fetchTimestamp(frameSize);
    CHECK_GE(timeUs, 0ll);

    accessUnit->meta()->setInt64("timeUs", timeUs);

    if (mFormat == NULL) {
        mFormat = new MetaData;

        switch (layer) {
            case 1:
                mFormat->setCString(
                        kKeyMIMEType, MEDIA_MIMETYPE_AUDIO_MPEG_LAYER_I);
                break;
            case 2:
                mFormat->setCString(
                        kKeyMIMEType, MEDIA_MIMETYPE_AUDIO_MPEG_LAYER_II);
                break;
            case 3:
                mFormat->setCString(
                        kKeyMIMEType, MEDIA_MIMETYPE_AUDIO_MPEG);
                break;
            default:
                TRESPASS();
        }

        mFormat->setInt32(kKeySampleRate, samplingRate);
        mFormat->setInt32(kKeyChannelCount, numChannels);
    }

    return accessUnit;
}

static void EncodeSize14(uint8_t **_ptr, size_t size) {
    CHECK_LE(size, 0x3fff);

    uint8_t *ptr = *_ptr;

    *ptr++ = 0x80 | (size >> 7);
    *ptr++ = size & 0x7f;

    *_ptr = ptr;
}

static sp<ABuffer> MakeMPEGVideoESDS(const sp<ABuffer> &csd) {
    sp<ABuffer> esds = new ABuffer(csd->size() + 25);

    uint8_t *ptr = esds->data();
    *ptr++ = 0x03;
    EncodeSize14(&ptr, 22 + csd->size());

    *ptr++ = 0x00;  // ES_ID
    *ptr++ = 0x00;

    *ptr++ = 0x00;  // streamDependenceFlag, URL_Flag, OCRstreamFlag

    *ptr++ = 0x04;
    EncodeSize14(&ptr, 16 + csd->size());

    *ptr++ = 0x40;  // Audio ISO/IEC 14496-3

    for (size_t i = 0; i < 12; ++i) {
        *ptr++ = 0x00;
    }

    *ptr++ = 0x05;
    EncodeSize14(&ptr, csd->size());

    memcpy(ptr, csd->data(), csd->size());

    return esds;
}

sp<ABuffer> ElementaryStreamQueue::dequeueAccessUnitMPEGVideo() {
    size_t auSize = mBuffer->size();

    if (mFormat == NULL) {
        //Create codec spefic data
        mFormat = new MetaData;
        mFormat->setCString(kKeyMIMEType, MEDIA_MIMETYPE_VIDEO_MPEG2);
        ALOGV("Setting Mimetype as MEDIA_MIME_TYPE_VIDEO_MPEG2");
        ALOGV("Width is %d, Height is %d", mvideoWidth, mvideoHeight);
        mFormat->setInt32(kKeyWidth, mvideoWidth);
        mFormat->setInt32(kKeyHeight, mvideoHeight);

        if (mPsExtractor)
            mFormat->setInt32(kKeyPSExtractor, mPsExtractor);
    }

    if (auSize == 0)
        return NULL;

    int64_t timeUs = fetchTimestamp(auSize);
    sp<ABuffer> accessUnit = new ABuffer(auSize);
    memcpy(accessUnit->data(), mBuffer->data(), auSize);
    mBuffer->setRange(0, 0);

    if (timeUs >= 0) {
        accessUnit->meta()->setInt64("timeUs", timeUs);
    } else {
        ALOGW("no time for MPEG2 access unit");
    }
    return accessUnit;
}

static ssize_t getNextChunkSize(
        const uint8_t *data, size_t size) {
    static const char kStartCode[] = "\x00\x00\x01";

    if (size < 3) {
        return -EAGAIN;
    }

    if (memcmp(kStartCode, data, 3)) {
        TRESPASS();
    }

    size_t offset = 3;
    while (offset + 2 < size) {
        if (!memcmp(&data[offset], kStartCode, 3)) {
            return offset;
        }

        ++offset;
    }

    return -EAGAIN;
}

sp<ABuffer> ElementaryStreamQueue::dequeueAccessUnitMPEG4Video() {
    uint8_t *data = mBuffer->data();
    size_t size = mBuffer->size();

    enum {
        SKIP_TO_VISUAL_OBJECT_SEQ_START,
        EXPECT_VISUAL_OBJECT_START,
        EXPECT_VO_START,
        EXPECT_VOL_START,
        WAIT_FOR_VOP_START,
        SKIP_TO_VOP_START,

    } state;

    if (mFormat == NULL) {
        state = SKIP_TO_VISUAL_OBJECT_SEQ_START;
    } else {
        state = SKIP_TO_VOP_START;
    }

    int32_t width = -1, height = -1;

    size_t offset = 0;
    ssize_t chunkSize;
    while ((chunkSize = getNextChunkSize(
                    &data[offset], size - offset)) > 0) {
        bool discard = false;

        unsigned chunkType = data[offset + 3];

        switch (state) {
            case SKIP_TO_VISUAL_OBJECT_SEQ_START:
            {
                if (chunkType == 0xb0) {
                    // Discard anything before this marker.

                    state = EXPECT_VISUAL_OBJECT_START;
                } else {
                    discard = true;
                }
                break;
            }

            case EXPECT_VISUAL_OBJECT_START:
            {
                CHECK_EQ(chunkType, 0xb5);
                state = EXPECT_VO_START;
                break;
            }

            case EXPECT_VO_START:
            {
                CHECK_LE(chunkType, 0x1f);
                state = EXPECT_VOL_START;
                break;
            }

            case EXPECT_VOL_START:
            {
                CHECK((chunkType & 0xf0) == 0x20);

                CHECK(ExtractDimensionsFromVOLHeader(
                            &data[offset], chunkSize,
                            &width, &height));

                state = WAIT_FOR_VOP_START;
                break;
            }

            case WAIT_FOR_VOP_START:
            {
                if (chunkType == 0xb3 || chunkType == 0xb6) {
                    // group of VOP or VOP start.

                    mFormat = new MetaData;
                    mFormat->setCString(
                            kKeyMIMEType, MEDIA_MIMETYPE_VIDEO_MPEG4);

                    mFormat->setInt32(kKeyWidth, width);
                    mFormat->setInt32(kKeyHeight, height);

                    ALOGI("found MPEG4 video codec config (%d x %d)",
                         width, height);

                    sp<ABuffer> csd = new ABuffer(offset);
                    memcpy(csd->data(), data, offset);

                    // hexdump(csd->data(), csd->size());

                    sp<ABuffer> esds = MakeMPEGVideoESDS(csd);
                    mFormat->setData(
                            kKeyESDS, kTypeESDS,
                            esds->data(), esds->size());

                    discard = true;
                    state = SKIP_TO_VOP_START;
                }

                break;
            }

            case SKIP_TO_VOP_START:
            {
                if (chunkType == 0xb6) {
                    offset += chunkSize;

                    sp<ABuffer> accessUnit = new ABuffer(offset);
                    memcpy(accessUnit->data(), data, offset);

                    memmove(data, &data[offset], size - offset);
                    size -= offset;
                    mBuffer->setRange(0, size);

                    int64_t timeUs = fetchTimestamp(offset);
                    CHECK_GE(timeUs, 0ll);

                    offset = 0;

                    accessUnit->meta()->setInt64("timeUs", timeUs);

                    ALOGV("returning MPEG4 video access unit at time %lld us",
                         timeUs);

                    // hexdump(accessUnit->data(), accessUnit->size());

                    return accessUnit;
                } else if (chunkType != 0xb3) {
                    offset += chunkSize;
                    discard = true;
                }

                break;
            }

            default:
                TRESPASS();
        }

        if (discard) {
            (void)fetchTimestamp(offset);
            memmove(data, &data[offset], size - offset);
            size -= offset;
            offset = 0;
            mBuffer->setRange(0, size);
        } else {
            offset += chunkSize;
        }
    }

    return NULL;
}

}  // namespace android
