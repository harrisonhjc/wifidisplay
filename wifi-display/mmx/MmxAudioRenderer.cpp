#include <assert.h>
#include <pthread.h>
#include <utils/Log.h>
#include <sys/types.h>
#include <math.h>
#include <media/stagefright/foundation/hexdump.h>
#include <media/stagefright/foundation/ADebug.h>
#include "MmxAudioRenderer.h"
#include "MmxUtil.h"
#include <jni.h>

namespace android {

extern int getStopIssued(void);
extern JavaVM  *mJvm;
////////////////////////////////////////////////////////////
// engine interfaces
static SLObjectItf engineObject = NULL;
static SLEngineItf engineEngine;
// output mix interfaces
static SLObjectItf outputMixObject = NULL;
static SLEnvironmentalReverbItf outputMixEnvironmentalReverb = NULL;
// aux effect on the output mix, used by the buffer queue player
static const SLEnvironmentalReverbSettings reverbSettings = SL_I3DL2_ENVIRONMENT_PRESET_STONECORRIDOR;
// buffer queue player interfaces
static SLObjectItf bqPlayerObject = NULL;
static SLPlayItf bqPlayerPlay;
static SLAndroidSimpleBufferQueueItf bqPlayerBufferQueue;
//static SLEffectSendItf bqPlayerEffectSend;
//static SLMuteSoloItf bqPlayerMuteSolo;
static SLVolumeItf bqPlayerVolume;
// pointer and size of the next player buffer to enqueue, and number of remaining buffers
static short *nextBuffer;
static unsigned nextSize;
static int nextCount;
////////////////////////////////////////////////////////////
#define AUDIO_BUFFER_LENGTH     2048
#define AUDIO_BLOCK_NUMER       512

sDECODER_AUDIO_HW_BUFFER* pAudio_slice[AUDIO_BLOCK_NUMER];
int index_audio_slice = 0;
int index_current_audio_slice = 0;

MmxAudioRenderer::~MmxAudioRenderer()
{
    //ALOGI("~MmxAudioScheduler:");
    
}


status_t MmxAudioRenderer::startService()
{
    audio_sink_init();
    startThread();
    return OK;
    
}
status_t MmxAudioRenderer::stopService()
{
    //ALOGI("stop:");
    
    //shutdownAudioPlayer();
    //audio_sink_release();
    return OK;
    
}

void  MmxAudioRenderer::startThread(void)
{
    MmxAudioRendererPtr t = &MmxAudioRenderer::threadLoop;
    PthreadPtr p = *(PthreadPtr*)&t;
    pthread_t    tid;
    if(pthread_create(&tid, 0, p, this) == 0){
        pthread_detach(tid);
    }
}


void* MmxAudioRenderer::threadLoop(void)
{
    //ALOGI("MmxAudioRenderer::threadLoop++");

    JNIEnv *env = NULL;
    int isAttached = 0;
    if(mJvm){
        if(mJvm->AttachCurrentThread(&env, NULL) < 0){
            ALOGE("MmxAudioRenderer::threadLoop:thread can not attach current thread." );
            return NULL;
        }
        isAttached = 1;
    }

    createEngine();
    createBufferQueueAudioPlayer();
    while(getStopIssued() == 0){
        sleep(5);
    }
    (*bqPlayerPlay)->SetPlayState(bqPlayerPlay, SL_PLAYSTATE_STOPPED);
    shutdownAudioPlayer();
    audio_sink_release();
    
    if(isAttached){
        mJvm->DetachCurrentThread();
    }
    pthread_exit((void *)0);
    return 0;
}

void MmxAudioRenderer::audio_sink_init(void)
{

    for(int i=0;i<AUDIO_BLOCK_NUMER;i++){
        pAudio_slice[i] = (sDECODER_AUDIO_HW_BUFFER*)malloc(sizeof(sDECODER_AUDIO_HW_BUFFER));
        if(NULL != pAudio_slice[i])
            pAudio_slice[i]->buffer = (unsigned char*)malloc(AUDIO_BUFFER_LENGTH);
    }
    
}

void MmxAudioRenderer::audio_sink_release(void)
{

    for(int i=0;i<AUDIO_BLOCK_NUMER;i++){
        if(NULL != pAudio_slice[i]->buffer)
            free(pAudio_slice[i]->buffer);

        if(NULL != pAudio_slice[i])
            free(pAudio_slice[i]);
    }
    
}

sDECODER_AUDIO_HW_BUFFER* MmxAudioRenderer::audio_sink_buffer_get()
{
    if(index_audio_slice >= AUDIO_BLOCK_NUMER)
        index_audio_slice = 0;

    return pAudio_slice[index_audio_slice++];
}

void MmxAudioRenderer::setMute(bool status)
{
    if(bqPlayerVolume == NULL)
        return;

    (*bqPlayerVolume)->SetMute(bqPlayerVolume, status);
    
}
//////////////////////////////////////////////////////////////////////////////
// this callback handler is called every time a buffer finishes playing
void MmxAudioRenderer::bqPlayerCallback(SLAndroidSimpleBufferQueueItf bq, void *context)
{
    
    sSX_DESC *desc;
    sSX_DESC *curr;
    sSX_DESC *next;
    sDECODER_AUDIO_HW_BUFFER *hw_buf;

    //assert(bq == bqPlayerBufferQueue);
    //assert(NULL == context);
    
    SLresult result;

    if(index_current_audio_slice >= AUDIO_BLOCK_NUMER)
        index_current_audio_slice = 0;

    
    do {
    
        desc = (sSX_DESC*)MmxUtil::sx_pipe_get(SX_VRDMA_AUDIO_SLICE_READY);
        if(desc == NULL){
          usleep(500);
          continue;
        }
        
        curr = desc->next;
        hw_buf = (sDECODER_AUDIO_HW_BUFFER*)curr->data;
        free(curr);
        free(desc);
        break;
    } while(getStopIssued() == 0);

    if(getStopIssued() == 0){
        result = (*bqPlayerBufferQueue)->Enqueue(bqPlayerBufferQueue,
                                                hw_buf->buffer+4,
                                                hw_buf->buffer_len-4);
    }
    // the most likely other result is SL_RESULT_BUFFER_INSUFFICIENT,
    // which for this code example would indicate a programming error
    //assert(SL_RESULT_SUCCESS == result);
    
}
// create the engine and output mix objects
void MmxAudioRenderer::createEngine()
{
    SLresult result;

    //ALOGI("createEngine ++");

    // create engine
    result = slCreateEngine(&engineObject, 0, NULL, 0, NULL, NULL);
    assert(SL_RESULT_SUCCESS == result);

    // realize the engine
    result = (*engineObject)->Realize(engineObject, SL_BOOLEAN_FALSE);
    assert(SL_RESULT_SUCCESS == result);

    // get the engine interface, which is needed in order to create other objects
    result = (*engineObject)->GetInterface(engineObject, SL_IID_ENGINE, &engineEngine);
    assert(SL_RESULT_SUCCESS == result);

    // create output mix, with environmental reverb specified as a non-required interface
    const SLInterfaceID ids[1] = {SL_IID_ENVIRONMENTALREVERB};
    const SLboolean req[1] = {SL_BOOLEAN_FALSE};
    result = (*engineEngine)->CreateOutputMix(engineEngine, &outputMixObject, 1, ids, req);
    assert(SL_RESULT_SUCCESS == result);

    // realize the output mix
    result = (*outputMixObject)->Realize(outputMixObject, SL_BOOLEAN_FALSE);
    assert(SL_RESULT_SUCCESS == result);

    // get the environmental reverb interface
    // this could fail if the environmental reverb effect is not available,
    // either because the feature is not present, excessive CPU load, or
    // the required MODIFY_AUDIO_SETTINGS permission was not requested and granted
    result = (*outputMixObject)->GetInterface(outputMixObject, SL_IID_ENVIRONMENTALREVERB,
            &outputMixEnvironmentalReverb);
    if (SL_RESULT_SUCCESS == result) {
        result = (*outputMixEnvironmentalReverb)->SetEnvironmentalReverbProperties(
                outputMixEnvironmentalReverb, &reverbSettings);
    }
    // ignore unsuccessful result codes for environmental reverb, as it is optional for this example

}



// create buffer queue audio player
void MmxAudioRenderer::createBufferQueueAudioPlayer()
{
    SLresult result;

    //ALOGI("createBufferQueueAudioPlayer ++");

    // configure audio source
    SLDataLocator_AndroidSimpleBufferQueue loc_bufq = {SL_DATALOCATOR_ANDROIDSIMPLEBUFFERQUEUE, 4};
    
    
    //SLDataFormat_PCM format_pcm = {SL_DATAFORMAT_PCM, 2, SL_SAMPLINGRATE_48,
    //    SL_PCMSAMPLEFORMAT_FIXED_16, SL_PCMSAMPLEFORMAT_FIXED_16,
    //    SL_SPEAKER_FRONT_CENTER, SL_BYTEORDER_LITTLEENDIAN};
    
    SLDataFormat_PCM format_pcm;
    format_pcm.formatType = SL_DATAFORMAT_PCM;
    format_pcm.numChannels = 2;
    format_pcm.samplesPerSec = SL_SAMPLINGRATE_48;//SL_SAMPLINGRATE_44_1
    format_pcm.containerSize = 16;
    format_pcm.bitsPerSample = SL_PCMSAMPLEFORMAT_FIXED_16;
    format_pcm.endianness = SL_BYTEORDER_LITTLEENDIAN;

    //SL_SPEAKER_BACK_CENTER --> crash.
    //SL_SPEAKER_FRONT_CENTER --> crash
    //SL_SPEAKER_FRONT_LEFT | SL_SPEAKER_FRONT_RIGHT --> OK
    format_pcm.channelMask = SL_SPEAKER_FRONT_LEFT | SL_SPEAKER_FRONT_RIGHT; 

    
    
    SLDataSource audioSrc = {&loc_bufq, &format_pcm};

    // configure audio sink
    SLDataLocator_OutputMix loc_outmix = {SL_DATALOCATOR_OUTPUTMIX, outputMixObject};
    SLDataSink audioSnk = {&loc_outmix, NULL};

    // create audio player
    //const SLInterfaceID ids[3] = {SL_IID_BUFFERQUEUE, SL_IID_EFFECTSEND,SL_IID_VOLUME};
    const SLInterfaceID ids[2] = {SL_IID_BUFFERQUEUE, SL_IID_VOLUME};
    
    const SLboolean req[3] = {SL_BOOLEAN_TRUE, SL_BOOLEAN_TRUE,
            /*SL_BOOLEAN_TRUE,*/ SL_BOOLEAN_TRUE};

    result = (*engineEngine)->CreateAudioPlayer(engineEngine, &bqPlayerObject,
                                                &audioSrc, &audioSnk,
                                                2, ids, req); //2: ids array size
    assert(SL_RESULT_SUCCESS == result);

    // realize the player
    result = (*bqPlayerObject)->Realize(bqPlayerObject, SL_BOOLEAN_FALSE);
    assert(SL_RESULT_SUCCESS == result);

    // get the play interface
    result = (*bqPlayerObject)->GetInterface(bqPlayerObject, SL_IID_PLAY, &bqPlayerPlay);
    assert(SL_RESULT_SUCCESS == result);

    // get the buffer queue interface
    result = (*bqPlayerObject)->GetInterface(bqPlayerObject, SL_IID_BUFFERQUEUE,
            &bqPlayerBufferQueue);
    assert(SL_RESULT_SUCCESS == result);

    // register callback on the buffer queue
    result = (*bqPlayerBufferQueue)->RegisterCallback(bqPlayerBufferQueue, bqPlayerCallback, NULL);
    assert(SL_RESULT_SUCCESS == result);

    // get the effect send interface
    //result = (*bqPlayerObject)->GetInterface(bqPlayerObject, SL_IID_EFFECTSEND,
    //        &bqPlayerEffectSend);
    //assert(SL_RESULT_SUCCESS == result);


    // get the volume interface
    result = (*bqPlayerObject)->GetInterface(bqPlayerObject, SL_IID_VOLUME, &bqPlayerVolume);
    assert(SL_RESULT_SUCCESS == result);

    
    (*bqPlayerVolume)->SetVolumeLevel(bqPlayerVolume, -1000);


    sSX_DESC *desc;
    sSX_DESC *curr;
    sDECODER_AUDIO_HW_BUFFER *hw_buf;
    

    //enqueue 
    int count = 0;
    do{
        desc = (sSX_DESC*)MmxUtil::sx_pipe_get(SX_VRDMA_AUDIO_SLICE_READY);
        if(desc == NULL){
          usleep(5000);
          continue;
        }
        curr = desc->next;
        hw_buf = (sDECODER_AUDIO_HW_BUFFER*)curr->data;
        result = (*bqPlayerBufferQueue)->Enqueue(bqPlayerBufferQueue,
                                            hw_buf->buffer+4,
                                            hw_buf->buffer_len-4);
        free(curr);
        free(desc);
        count++;
    } while((count < 3) && getStopIssued() == 0);
        
    
    // set the player's state to playing
    result = (*bqPlayerPlay)->SetPlayState(bqPlayerPlay, SL_PLAYSTATE_PLAYING);
    //assert(SL_RESULT_SUCCESS == result);
}

// shut down the native audio system
void MmxAudioRenderer::shutdownAudioPlayer()
{
     (*bqPlayerPlay)->SetPlayState(bqPlayerPlay, SL_PLAYSTATE_STOPPED);
    // destroy buffer queue audio player object, and invalidate all associated interfaces
    if (bqPlayerObject != NULL) {
        (*bqPlayerObject)->Destroy(bqPlayerObject);
        bqPlayerObject = NULL;
        bqPlayerPlay = NULL;
        bqPlayerBufferQueue = NULL;
        bqPlayerVolume = NULL;
    }

    

    // destroy output mix object, and invalidate all associated interfaces
    if (outputMixObject != NULL) {
        (*outputMixObject)->Destroy(outputMixObject);
        outputMixObject = NULL;
        outputMixEnvironmentalReverb = NULL;
    }

    // destroy engine object, and invalidate all associated interfaces
    if (engineObject != NULL) {
        (*engineObject)->Destroy(engineObject);
        engineObject = NULL;
        engineEngine = NULL;
    }
}

} // namespace android