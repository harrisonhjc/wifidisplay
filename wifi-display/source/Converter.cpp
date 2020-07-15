/*
 * Copyright 2012, The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

//#define LOG_NDEBUG 0
#define LOG_TAG "Converter"
#include <utils/Log.h>

#include "Converter.h"

#include "MediaPuller.h"

#include <cutils/properties.h>
#include <gui/SurfaceTextureClient.h>
#include <media/ICrypto.h>
#include <media/stagefright/foundation/ABuffer.h>
#include <media/stagefright/foundation/ADebug.h>
#include <media/stagefright/foundation/AMessage.h>
#include <media/stagefright/MediaBuffer.h>
#include <media/stagefright/MediaCodec.h>
#include <media/stagefright/MediaDefs.h>
#include <media/stagefright/MediaErrors.h>

#include <OMX_Video.h>

namespace android {

Converter::Converter(
        const sp<AMessage> &notify,
        const sp<ALooper> &codecLooper,
        const sp<AMessage> &format,
        bool usePCMAudio)
    : mInitCheck(NO_INIT),
      mNotify(notify),
      mCodecLooper(codecLooper),
      mInputFormat(format),
      mIsVideo(false),
      mIsPCMAudio(usePCMAudio),
      mNeedToManuallyPrependSPSPPS(false),
      mDoMoreWorkPending(false)
#if ENABLE_SILENCE_DETECTION
      ,mFirstSilentFrameUs(-1ll)
      ,mInSilentMode(false)
#endif
    {
    AString mime;
    CHECK(mInputFormat->findString("mime", &mime));

    if (!strncasecmp("video/", mime.c_str(), 6)) {
        mIsVideo = true;
    }

    CHECK(!usePCMAudio || !mIsVideo);

    mInitCheck = initEncoder();

    if (mInitCheck != OK) {
        if (mEncoder != NULL) {
            mEncoder->release();
            mEncoder.clear();
        }
    }
}

Converter::~Converter() {
    CHECK(mEncoder == NULL);
}

void Converter::shutdownAsync() {
    ALOGV("shutdown");
    (new AMessage(kWhatShutdown, id()))->post();
}

status_t Converter::initCheck() const {
    return mInitCheck;
}

size_t Converter::getInputBufferCount() const {
    return mEncoderInputBuffers.size();
}

sp<AMessage> Converter::getOutputFormat() const {
    return mOutputFormat;
}

bool Converter::needToManuallyPrependSPSPPS() const {
    return mNeedToManuallyPrependSPSPPS;
}

static int32_t getBitrate(const char *propName, int32_t defaultValue) {
    char val[PROPERTY_VALUE_MAX];
    if (property_get(propName, val, NULL)) {
        char *end;
        unsigned long x = strtoul(val, &end, 10);

        if (*end == '\0' && end > val && x > 0) {
            return x;
        }
    }

    return defaultValue;
}

status_t Converter::initEncoder() {
    AString inputMIME;
    CHECK(mInputFormat->findString("mime", &inputMIME));

    AString outputMIME;
    bool isAudio = false;
    if (!strcasecmp(inputMIME.c_str(), MEDIA_MIMETYPE_AUDIO_RAW)) {
        if (mIsPCMAudio) {
            outputMIME = MEDIA_MIMETYPE_AUDIO_RAW;
        } else {
            outputMIME = MEDIA_MIMETYPE_AUDIO_AAC;
        }
        isAudio = true;
    } else if (!strcasecmp(inputMIME.c_str(), MEDIA_MIMETYPE_VIDEO_RAW)) {
        outputMIME = MEDIA_MIMETYPE_VIDEO_AVC;
    } else {
        TRESPASS();
    }

    if (!mIsPCMAudio) {
        mEncoder = MediaCodec::CreateByType(
                mCodecLooper, outputMIME.c_str(), true /* encoder */);

        if (mEncoder == NULL) {
            return ERROR_UNSUPPORTED;
        }
    }

    mOutputFormat = mInputFormat->dup();

    if (mIsPCMAudio) {
        return OK;
    }

    mOutputFormat->setString("mime", outputMIME.c_str());

    int32_t audioBitrate = getBitrate("media.wfd.audio-bitrate", 128000);
    int32_t videoBitrate = getBitrate("media.wfd.video-bitrate", 5000000);

    ALOGI("using audio bitrate of %d bps, video bitrate of %d bps",
          audioBitrate, videoBitrate);

    if (isAudio) {
        mOutputFormat->setInt32("bitrate", audioBitrate);
    } else {
        mOutputFormat->setInt32("bitrate", videoBitrate);
        mOutputFormat->setInt32("bitrate-mode", OMX_Video_ControlRateConstant);
        mOutputFormat->setInt32("frame-rate", 30);
        mOutputFormat->setInt32("i-frame-interval", 15);  // Iframes every 15 secs

        // Configure encoder to use intra macroblock refresh mode
        mOutputFormat->setInt32("intra-refresh-mode", OMX_VIDEO_IntraRefreshCyclic);

        int width, height, mbs;
        if (!mOutputFormat->findInt32("width", &width)
                || !mOutputFormat->findInt32("height", &height)) {
            return ERROR_UNSUPPORTED;
        }

        // Update macroblocks in a cyclic fashion with 10% of all MBs within
        // frame gets updated at one time. It takes about 10 frames to
        // completely update a whole video frame. If the frame rate is 30,
        // it takes about 333 ms in the best case (if next frame is not an IDR)
        // to recover from a lost/corrupted packet.
        mbs = (((width + 15) / 16) * ((height + 15) / 16) * 10) / 100;
        mOutputFormat->setInt32("intra-refresh-CIR-mbs", mbs);
    }

    ALOGV("output format is '%s'", mOutputFormat->debugString(0).c_str());

    mNeedToManuallyPrependSPSPPS = false;

    status_t err = NO_INIT;

    if (!isAudio) {
        sp<AMessage> tmp = mOutputFormat->dup();
        tmp->setInt32("prepend-sps-pps-to-idr-frames", 1);

        err = mEncoder->configure(
                tmp,
                NULL /* nativeWindow */,
                NULL /* crypto */,
                MediaCodec::CONFIGURE_FLAG_ENCODE);

        if (err == OK) {
            // Encoder supported prepending SPS/PPS, we don't need to emulate
            // it.
            mOutputFormat = tmp;
        } else {
            mNeedToManuallyPrependSPSPPS = true;

            ALOGI("We going to manually prepend SPS and PPS to IDR frames.");
        }
    }

    if (err != OK) {
        // We'll get here for audio or if we failed to configure the encoder
        // to automatically prepend SPS/PPS in the case of video.

        err = mEncoder->configure(
                    mOutputFormat,
                    NULL /* nativeWindow */,
                    NULL /* crypto */,
                    MediaCodec::CONFIGURE_FLAG_ENCODE);
    }

    if (err != OK) {
        return err;
    }

    err = mEncoder->start();

    if (err != OK) {
        return err;
    }

    err = mEncoder->getInputBuffers(&mEncoderInputBuffers);

    if (err != OK) {
        return err;
    }

    return mEncoder->getOutputBuffers(&mEncoderOutputBuffers);
}

void Converter::notifyError(status_t err) {
    sp<AMessage> notify = mNotify->dup();
    notify->setInt32("what", kWhatError);
    notify->setInt32("err", err);
    notify->post();
}

// static
bool Converter::IsSilence(const sp<ABuffer> &accessUnit) {
    const uint8_t *ptr = accessUnit->data();
    const uint8_t *end = ptr + accessUnit->size();
    while (ptr < end) {
        if (*ptr != 0) {
            return false;
        }
        ++ptr;
    }

    return true;
}

void Converter::onMessageReceived(const sp<AMessage> &msg) {
    switch (msg->what()) {
        case kWhatMediaPullerNotify:
        {
            int32_t what;
            CHECK(msg->findInt32("what", &what));

            if (!mIsPCMAudio && mEncoder == NULL) {
                ALOGV("got msg '%s' after encoder shutdown.",
                      msg->debugString().c_str());

                if (what == MediaPuller::kWhatAccessUnit) {
                    sp<ABuffer> accessUnit;
                    CHECK(msg->findBuffer("accessUnit", &accessUnit));

                    void *mbuf;
                    if (accessUnit->meta()->findPointer("mediaBuffer", &mbuf)
                            && mbuf != NULL) {
                        ALOGV("releasing mbuf %p", mbuf);

                        accessUnit->meta()->setPointer("mediaBuffer", NULL);

                        static_cast<MediaBuffer *>(mbuf)->release();
                        mbuf = NULL;
                    }
                }
                break;
            }

            if (what == MediaPuller::kWhatEOS) {
                mInputBufferQueue.push_back(NULL);

                feedEncoderInputBuffers();

                scheduleDoMoreWork();
            } else {
                CHECK_EQ(what, MediaPuller::kWhatAccessUnit);

                sp<ABuffer> accessUnit;
                CHECK(msg->findBuffer("accessUnit", &accessUnit));

#if 0
                void *mbuf;
                if (accessUnit->meta()->findPointer("mediaBuffer", &mbuf)
                        && mbuf != NULL) {
                    ALOGI("queueing mbuf %p", mbuf);
                }
#endif

#if ENABLE_SILENCE_DETECTION
                if (!mIsVideo) {
                    if (IsSilence(accessUnit)) {
                        if (mInSilentMode) {
                            break;
                        }

                        int64_t nowUs = ALooper::GetNowUs();

                        if (mFirstSilentFrameUs < 0ll) {
                            mFirstSilentFrameUs = nowUs;
                        } else if (nowUs >= mFirstSilentFrameUs + 10000000ll) {
                            mInSilentMode = true;
                            ALOGI("audio in silent mode now.");
                            break;
                        }
                    } else {
                        if (mInSilentMode) {
                            ALOGI("audio no longer in silent mode.");
                        }
                        mInSilentMode = false;
                        mFirstSilentFrameUs = -1ll;
                    }
                }
#endif

                mInputBufferQueue.push_back(accessUnit);

                feedEncoderInputBuffers();

                scheduleDoMoreWork();
            }
            break;
        }

        case kWhatEncoderActivity:
        {
#if 0
            int64_t whenUs;
            if (msg->findInt64("whenUs", &whenUs)) {
                int64_t nowUs = ALooper::GetNowUs();
                ALOGI("[%s] kWhatEncoderActivity after %lld us",
                      mIsVideo ? "video" : "audio", nowUs - whenUs);
            }
#endif

            mDoMoreWorkPending = false;

            if (mEncoder == NULL) {
                break;
            }

            status_t err = doMoreWork();

            if (err != OK) {
                notifyError(err);
            } else {
                scheduleDoMoreWork();
            }
            break;
        }

        case kWhatRequestIDRFrame:
        {
            if (mEncoder == NULL) {
                break;
            }

            if (mIsVideo) {
                ALOGI("requesting IDR frame");
                mEncoder->requestIDRFrame();
            }
            break;
        }

        case kWhatShutdown:
        {
            ALOGI("shutting down encoder");

            if (mEncoder != NULL) {
                mEncoder->release();
                mEncoder.clear();
            }

            AString mime;
            CHECK(mInputFormat->findString("mime", &mime));
            ALOGI("encoder (%s) shut down.", mime.c_str());
            break;
        }

        default:
            TRESPASS();
    }
}

void Converter::scheduleDoMoreWork() {
    if (mIsPCMAudio) {
        // There's no encoder involved in this case.
        return;
    }

    if (mDoMoreWorkPending) {
        return;
    }

    mDoMoreWorkPending = true;

#if 1
    if (mEncoderActivityNotify == NULL) {
        mEncoderActivityNotify = new AMessage(kWhatEncoderActivity, id());
    }
    mEncoder->requestActivityNotification(mEncoderActivityNotify->dup());
#else
    sp<AMessage> notify = new AMessage(kWhatEncoderActivity, id());
    notify->setInt64("whenUs", ALooper::GetNowUs());
    mEncoder->requestActivityNotification(notify);
#endif
}

status_t Converter::feedRawAudioInputBuffers() {
    // Split incoming PCM audio into buffers of 6 AUs of 80 audio frames each
    // and add a 4 byte header according to the wifi display specs.

    while (!mInputBufferQueue.empty()) {
        sp<ABuffer> buffer = *mInputBufferQueue.begin();
        mInputBufferQueue.erase(mInputBufferQueue.begin());

        int16_t *ptr = (int16_t *)buffer->data();
        int16_t *stop = (int16_t *)(buffer->data() + buffer->size());
        while (ptr < stop) {
            *ptr = htons(*ptr);
            ++ptr;
        }

        static const size_t kFrameSize = 2 * sizeof(int16_t);  // stereo
        static const size_t kFramesPerAU = 80;
        static const size_t kNumAUsPerPESPacket = 6;

        if (mPartialAudioAU != NULL) {
            size_t bytesMissingForFullAU =
                kNumAUsPerPESPacket * kFramesPerAU * kFrameSize
                - mPartialAudioAU->size() + 4;

            size_t copy = buffer->size();
            if(copy > bytesMissingForFullAU) {
                copy = bytesMissingForFullAU;
            }

            memcpy(mPartialAudioAU->data() + mPartialAudioAU->size(),
                   buffer->data(),
                   copy);

            mPartialAudioAU->setRange(0, mPartialAudioAU->size() + copy);

            buffer->setRange(buffer->offset() + copy, buffer->size() - copy);

            int64_t timeUs;
            CHECK(buffer->meta()->findInt64("timeUs", &timeUs));

            int64_t copyUs = (int64_t)((copy / kFrameSize) * 1E6 / 48000.0);
            timeUs += copyUs;
            buffer->meta()->setInt64("timeUs", timeUs);

            if (bytesMissingForFullAU == copy) {
                sp<AMessage> notify = mNotify->dup();
                notify->setInt32("what", kWhatAccessUnit);
                notify->setBuffer("accessUnit", mPartialAudioAU);
                notify->post();

                mPartialAudioAU.clear();
            }
        }

        while (buffer->size() > 0) {
            sp<ABuffer> partialAudioAU =
                new ABuffer(
                        4
                        + kNumAUsPerPESPacket * kFrameSize * kFramesPerAU);

            uint8_t *ptr = partialAudioAU->data();
            ptr[0] = 0xa0;  // 10100000b
            ptr[1] = kNumAUsPerPESPacket;
            ptr[2] = 0;  // reserved, audio _emphasis_flag = 0

            static const unsigned kQuantizationWordLength = 0;  // 16-bit
            static const unsigned kAudioSamplingFrequency = 2;  // 48Khz
            static const unsigned kNumberOfAudioChannels = 1;  // stereo

            ptr[3] = (kQuantizationWordLength << 6)
                    | (kAudioSamplingFrequency << 3)
                    | kNumberOfAudioChannels;

            size_t copy = buffer->size();
            if (copy > partialAudioAU->size() - 4) {
                copy = partialAudioAU->size() - 4;
            }

            memcpy(&ptr[4], buffer->data(), copy);

            partialAudioAU->setRange(0, 4 + copy);
            buffer->setRange(buffer->offset() + copy, buffer->size() - copy);

            int64_t timeUs;
            CHECK(buffer->meta()->findInt64("timeUs", &timeUs));

            partialAudioAU->meta()->setInt64("timeUs", timeUs);

            int64_t copyUs = (int64_t)((copy / kFrameSize) * 1E6 / 48000.0);
            timeUs += copyUs;
            buffer->meta()->setInt64("timeUs", timeUs);

            if (copy == partialAudioAU->capacity() - 4) {
                sp<AMessage> notify = mNotify->dup();
                notify->setInt32("what", kWhatAccessUnit);
                notify->setBuffer("accessUnit", partialAudioAU);
                notify->post();

                partialAudioAU.clear();
                continue;
            }

            mPartialAudioAU = partialAudioAU;
        }
    }

    return OK;
}

status_t Converter::feedEncoderInputBuffers() {
    if (mIsPCMAudio) {
        return feedRawAudioInputBuffers();
    }

    while (!mInputBufferQueue.empty()
            && !mAvailEncoderInputIndices.empty()) {
        sp<ABuffer> buffer = *mInputBufferQueue.begin();
        mInputBufferQueue.erase(mInputBufferQueue.begin());

        size_t bufferIndex = *mAvailEncoderInputIndices.begin();
        mAvailEncoderInputIndices.erase(mAvailEncoderInputIndices.begin());

        int64_t timeUs = 0ll;
        uint32_t flags = 0;

        if (buffer != NULL) {
            CHECK(buffer->meta()->findInt64("timeUs", &timeUs));

            memcpy(mEncoderInputBuffers.itemAt(bufferIndex)->data(),
                   buffer->data(),
                   buffer->size());

            void *mediaBuffer;
            if (buffer->meta()->findPointer("mediaBuffer", &mediaBuffer)
                    && mediaBuffer != NULL) {
                mEncoderInputBuffers.itemAt(bufferIndex)->meta()
                    ->setPointer("mediaBuffer", mediaBuffer);

                buffer->meta()->setPointer("mediaBuffer", NULL);
            }
        } else {
            flags = MediaCodec::BUFFER_FLAG_EOS;
        }

        status_t err = mEncoder->queueInputBuffer(
                bufferIndex, 0, (buffer == NULL) ? 0 : buffer->size(),
                timeUs, flags);

        if (err != OK) {
            return err;
        }
    }

    return OK;
}

status_t Converter::doMoreWork() {
    status_t err;

    for (;;) {
        size_t bufferIndex;
        err = mEncoder->dequeueInputBuffer(&bufferIndex);

        if (err != OK) {
            break;
        }

        mAvailEncoderInputIndices.push_back(bufferIndex);
    }

    feedEncoderInputBuffers();

    for (;;) {
        size_t bufferIndex;
        size_t offset;
        size_t size;
        int64_t timeUs;
        uint32_t flags;
        err = mEncoder->dequeueOutputBuffer(
                &bufferIndex, &offset, &size, &timeUs, &flags);

        if (err != OK) {
            if (err == -EAGAIN) {
                err = OK;
            }
            break;
        }

        if (flags & MediaCodec::BUFFER_FLAG_EOS) {
            sp<AMessage> notify = mNotify->dup();
            notify->setInt32("what", kWhatEOS);
            notify->post();
        } else {
            sp<ABuffer> buffer = new ABuffer(size);
            buffer->meta()->setInt64("timeUs", timeUs);

            ALOGV("[%s] time %lld us (%.2f secs)",
                  mIsVideo ? "video" : "audio", timeUs, timeUs / 1E6);

            memcpy(buffer->data(),
                   mEncoderOutputBuffers.itemAt(bufferIndex)->base() + offset,
                   size);

            if (flags & MediaCodec::BUFFER_FLAG_CODECCONFIG) {
                mOutputFormat->setBuffer("csd-0", buffer);
            } else {
                sp<AMessage> notify = mNotify->dup();
                notify->setInt32("what", kWhatAccessUnit);
                notify->setBuffer("accessUnit", buffer);
                notify->post();
            }
        }

        mEncoder->releaseOutputBuffer(bufferIndex);

        if (flags & MediaCodec::BUFFER_FLAG_EOS) {
            break;
        }
    }

    return err;
}

void Converter::requestIDRFrame() {
    (new AMessage(kWhatRequestIDRFrame, id()))->post();
}

}  // namespace android
