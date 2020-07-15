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
#define LOG_TAG "AmmboxSink"

#define WFDLOG
#define WFDLOG   ALOGI

/*
#undef  NDEBUG 
#define LOG_NDEBUG   0   
#define LOG_NIDEBUG  0   
#define LOG_NDDEBUG 0    
#define LOG_NEDEBUG 0  
*/
#include <utils/Log.h>

#include "WifiDisplaySink.h"
#include "ParsedMessage.h"

#include <media/stagefright/foundation/ABuffer.h>
#include <media/stagefright/foundation/ADebug.h>
#include <media/stagefright/foundation/AMessage.h>
#include <media/stagefright/MediaErrors.h>
#include "ContentRecv.h"
#include "hdcpReceiver/HdcpReceiver.h"
#include "uibcSink/uibcSink.h"

namespace android {

sp<uibcSink> mUibcSink;
sp<ContentRecv> mContentRecv;
sp<HdcpReceiver> mHdcp;
int hdcpVersion; //1 for 2.1 , 2 for 2.2
bool hdcpEnabled = false;
bool hdcpAuthOK = false;
int videoWidth = 1280;
int videoHeight = 720;
int watchDogCount = 0;
bool uibcEnabled = false;
extern int getWfdVideoWidth();
extern int getWfdVideoHeight();

WifiDisplaySink::WifiDisplaySink(
        const sp<ANetworkSession> &netSession,
        const sp<ISurfaceTexture> &surfaceTex)
    : mState(UNDEFINED),
      mNetSession(netSession),
      mSurfaceTex(surfaceTex),
      mSessionID(0),
      mNextCSeq(1) {
    
    
    //mHdcp.start("192.168.49.1",35000);
}
//WifiDisplaySink::~WifiDisplaySink() {
    //ALOGI("~WifiDisplaySink:");
    
//}

void WifiDisplaySink::cb_hdcpAuthComplete(void* vself, int data)
{
    WifiDisplaySink* self = static_cast<WifiDisplaySink*>(vself);
    self->hdcpAuthComplete(data);
}
void WifiDisplaySink::hdcpAuthComplete(int data)
{
    //sp<AMessage> msg = new AMessage(kWhatHdcpAuthOK, id());
    //msg->post();
    ALOGI("HDCP auth complete.");
    mContentRecv->setkeyPES(mHdcp->getKs(), mHdcp->getRiv(), mHdcp->getLc());
    hdcpAuthOK = true;
    /*
    if(mState == ONSETUP_RESPONSE){
        sendPlay(
            mSessionPlayId,
            !mSetupURI.empty()
                ? mSetupURI.c_str() : "rtsp://x.x.x.x:x/wfd1.0/streamid=0");
        mState = PLAYING;
        mContentRecv->sendStatusReport("PLAYING\n");
    }*/

}

void WifiDisplaySink::start(const char *sourceHost, int32_t sourcePort,
                            ANativeWindow* window, const char *deviceName,
                            JavaVM *jvm)
{
    
    
    ALOGI("WFDSink:devName=%s.", deviceName);

    
    //if(strstr(deviceName,"Android") || strstr(deviceName,"Xperia") ){
    if(strstr(deviceName,"Xperia") ){
        hdcpVersion = 1; //1:2.1 , 2:2.2
        hdcpAuthOK = false;
        mHdcp = new HdcpReceiver;
        if(mHdcp != NULL){
            mHdcp->setJvm(jvm);
            if(mHdcp->start("192.168.49.1",35000) != OK){
                ALOGE("HDCP key error.");
                return ;
            }
            mHdcp->setHdcpVersion(hdcpVersion);
            mHdcp->set_callback(&WifiDisplaySink::cb_hdcpAuthComplete, this);
        }
        hdcpEnabled = true;
        mContentRecv = new ContentRecv(id());
        mContentRecv->enableHdcp(hdcpEnabled);    
        videoWidth = 1920;
        videoHeight = 1080;   
        
    }
    else{
        hdcpEnabled = false;
        mContentRecv = new ContentRecv(id());
        videoWidth = 1280;
        videoHeight = 720;
        //if(strstr(deviceName,"S6")){
        //    videoWidth = 640;
        //}
    }
    
    mWindow = window;
    sp<AMessage> msg = new AMessage(kWhatStart, id());
    msg->setString("sourceHost", sourceHost);
    msg->setInt32("sourcePort", sourcePort);
    msg->post();
    
    mContentRecv->setJvm(jvm);
    mContentRecv->initStatusReport();
    firstShowup = true;
    
    //uibc
    mUibcSink = new uibcSink;
    if(mUibcSink != NULL){
        mUibcSink->setJvm(jvm);
    }
    //ALOGI("Video w:h=%d:%d",videoWidth, videoHeight);
    
}

void WifiDisplaySink::start(const char *uri) {
    sp<AMessage> msg = new AMessage(kWhatStart, id());
    msg->setString("setupURI", uri);
    msg->post();
}

void WifiDisplaySink::stop() {
    //sp<AMessage> msg = new AMessage(kWhatStop, id());
    //msg->post();
    ALOGI("WifiDisplaySink:stop++");

    mState = DISCONNECTED;   
    mContentRecv->sendStatusReport("DISCONNECTED\n");

    if(mHdcp != NULL)
        mHdcp->stop();
    
    if(mUibcSink != NULL)
        mUibcSink->stop();

    mContentRecv->stopService();
    mNetSession->destroySession(mSessionID);
    mSessionID = 0;
    //looper()->stop();
    ALOGI("WifiDisplaySink:stop--");
    
    
}
// static
bool WifiDisplaySink::ParseURL(
        const char *url, AString *host, int32_t *port, AString *path,
        AString *user, AString *pass) {
    host->clear();
    *port = 0;
    path->clear();
    user->clear();
    pass->clear();

    if (strncasecmp("rtsp://", url, 7)) {
        return false;
    }

    const char *slashPos = strchr(&url[7], '/');

    if (slashPos == NULL) {
        host->setTo(&url[7]);
        path->setTo("/");
    } else {
        host->setTo(&url[7], slashPos - &url[7]);
        path->setTo(slashPos);
    }

    ssize_t atPos = host->find("@");

    if (atPos >= 0) {
        // Split of user:pass@ from hostname.

        AString userPass(*host, 0, atPos);
        host->erase(0, atPos + 1);

        ssize_t colonPos = userPass.find(":");

        if (colonPos < 0) {
            *user = userPass;
        } else {
            user->setTo(userPass, 0, colonPos);
            pass->setTo(userPass, colonPos + 1, userPass.size() - colonPos - 1);
        }
    }

    const char *colonPos = strchr(host->c_str(), ':');

    if (colonPos != NULL) {
        char *end;
        unsigned long x = strtoul(colonPos + 1, &end, 10);

        if (end == colonPos + 1 || *end != '\0' || x >= 65536) {
            return false;
        }

        *port = x;

        size_t colonOffset = colonPos - host->c_str();
        size_t trailing = host->size() - colonOffset;
        host->erase(colonOffset, trailing);
    } else {
        *port = 554;
    }

    return true;
}

void WifiDisplaySink::onMessageReceived(const sp<AMessage> &msg) {
    switch (msg->what()) {

        case kWhatWatchDog:
        {
            //ALOGI("kWhatWatchDog:watchDogCount=%d.", watchDogCount);
            if(watchDogCount > 5){
                looper()->stop();
                mState = DISCONNECTED;   
                mContentRecv->sendStatusReport("DISCONNECTED\n");
                break;
            }
            sp<AMessage> msg = new AMessage(kWhatWatchDog, id());
            msg->post(20000000);
            watchDogCount++;
            break;
        }

        case kWhatVideoResolution:
        {
            ALOGI("onMessageReceived:kWhatVideoResolution.");
            int w,h;
            msg->findInt32("w", &w);
            msg->findInt32("h", &h);
            if(w > 0)
                videoWidth = w;

            if(h > 0)
                videoHeight = h;

            ALOGI("Video:w:h=%d,%d.",videoWidth,videoHeight);
            break;
        }
        case kWhatHdcpAuthOK:
        { 
            
             /*
            hdcpAuthOK = true;
            mContentRecv.setkeyPES(mHdcp->getKs(), mHdcp->getRiv(), mHdcp->getLc());
            ALOGI("kWhatHdcpAuthOK:mState=%d.",mState);
            */
            
            /*
            if(mState == CONNECTED){
                ALOGI("WifiDisplaySink::onMessageReceived:sendsetup.");

                sendSetup(msessionSetupId,
                                "rtsp://x.x.x.x:x/wfd1.0/streamid=0");
                
            }else*/
            /*
            if(mState == ONSETUP_RESPONSE){
                sendPlay(
                    mSessionPlayId,
                    !mSetupURI.empty()
                        ? mSetupURI.c_str() : "rtsp://x.x.x.x:x/wfd1.0/streamid=0");
                mState = PLAYING;
                mContentRecv.sendStatusReport("PLAYING\n");
            }*/
            break;
        }
        case kWhatWfdTouchEvent:
        {
            int type,x,y,w,h;

            msg->findInt32("type", &type);
            msg->findInt32("x", &x);
            msg->findInt32("y", &y);
            msg->findInt32("w", &w);
            msg->findInt32("h", &h);
            
            if(w == 0 || h == 0)
                break;
                 
            
            if(mUibcSink != NULL){
                ALOGI("kWhatWfdTouchEvent:%d:%d:%d:%d",w,h,x,y);
                float x1 = float(x);
                float y1 = float(y);
                x1 *= float(float(videoWidth)/float(w));
                y1 *= float(float(videoHeight)/float(h));
                x = x1;
                y = y1;
                mUibcSink->sendTouchEvent(type,x,y);
                //ALOGI("kWhatWfdTouchEvent:%d:%d:%d:%d:%d:%d",w,h,videoWidth,videoHeight,x,y);
            }
            
            break;
        }

        case kWhatWfdKeyEvent:
        {
            int type,keyCode1,keyCode2;

            msg->findInt32("type", &type);
            msg->findInt32("keycode1", &keyCode1);
            msg->findInt32("keycode2", &keyCode2);
            
            if(mUibcSink != NULL){
                ALOGI("kWhatWfdKeyEvent:type:key:%d,%d,%d",type,keyCode1,keyCode2);
                mUibcSink->sendKeyEvent(type, keyCode1, keyCode2);
                //ALOGI("kWhatWfdTouchEvent:%d:%d:%d:%d:%d:%d",w,h,videoWidth,videoHeight,x,y);
            }
            
            break;
        }

        
        case kWhatWfdShow:
        {
            ALOGI("AmmboxSink:show.");
            void* win;
            msg->findPointer("window", &win);
            mWindow = (ANativeWindow*)win;
            if(firstShowup){
                mContentRecv->startService(mWindow);
            }else{
                mContentRecv->resumeService(mWindow);
            }
            mState = PLAYING;
            mContentRecv->sendStatusReport("PLAYING\n");
            firstShowup = false;
            //ADD:20161013
            if(uibcEnabled)
                mContentRecv->sendStatusReport("UIBC_ENABLED\n");
            
        }
        break;

        case kWhatWfdSuspend:
            ALOGI("AmmboxSink:kWhatWfdSuspend");
            mContentRecv->suspendService();
            mState = SUSPEND;
            mContentRecv->sendStatusReport("SUSPEND\n");
        break;

        case kWhatWfdSetAudioPath:
        {
            ALOGI("AmmboxSink:kWhatWfdSetAudioPath");
            int type;
            msg->findInt32("audiopath", &type);
            mContentRecv->setAudioPath(type);
        }
        break;

        case kWhatWfdSetVolume:
        {
            ALOGI("AmmboxSink:kWhatWfdSetVolume");
            int vol;
            msg->findInt32("volume", &vol);
            mContentRecv->setVolume(vol);
        }
        break;

        case kWhatWfdSetScale:
            ALOGI("AmmboxSink:kWhatWfdSetScale");
            int type;
            msg->findInt32("scale", &type);
            mContentRecv->setScale(type);
            
        break;

        case kWhatWfdCmd:
        {
            ALOGI("AmmboxSink:CMD.");
            int cmd;
            msg->findInt32("cmd", &cmd);
            //ALOGI("WifiDisplaySink:onMessageReceived:kWhatWfdCmd:%d.",cmd);
            if(cmd == 0){ //cmd_pause
                if(mState != PAUSED){
                    sendPause(mSessionID,
                            !mSetupURI.empty() ? mSetupURI.c_str() : "rtsp://x.x.x.x:x/wfd1.0/streamid=0");
                    mState = PAUSED;
                    //mContentRecv.sendStatusReport("PAUSED\n");
                }
            }else
            if(cmd == 1){ //cmd_play
                if(mState != PLAYING){
                    sendPlay(mSessionID,
                            !mSetupURI.empty() ? mSetupURI.c_str() : "rtsp://x.x.x.x:x/wfd1.0/streamid=0");
                    mState = PLAYING;
                    mContentRecv->sendStatusReport("PLAYING\n");
                    //mContentRecv.sendStatusReport("PLAYING\n");
                }
            }else
            if(cmd == 2){ //cmd_mute
                mContentRecv->setMute(true);
                mState = PLAYING_MUTE;
                mContentRecv->sendStatusReport("PLAYING_MUTE\n");
            }
            else
            if(cmd == 3){ //cmd_unmute
                mContentRecv->setMute(false);   
                mState = PLAYING_UNMUTE;
                mContentRecv->sendStatusReport("PLAYING_UNMUTE\n");
            }
            break;
        }
        case kWhatStart:
        {
            ALOGI("AmmboxSink:start.");

            int32_t sourcePort;
            if (msg->findString("setupURI", &mSetupURI)) {
                AString path, user, pass;
                CHECK(ParseURL(
                            mSetupURI.c_str(),
                            &mRTSPHost, &sourcePort, &path, &user, &pass)
                        && user.empty() && pass.empty());
            } else {
                CHECK(msg->findString("sourceHost", &mRTSPHost));
                CHECK(msg->findInt32("sourcePort", &sourcePort));
            }

            sp<AMessage> notify = new AMessage(kWhatRTSPNotify, id());
            //ALOGI("WifiDisplaySink::onMessageReceived:kWhatStart:mRTSPHost=%s,port=%d,",
            //       mRTSPHost.c_str(),
            //       sourcePort);

            status_t err = mNetSession->createRTSPClient(
                    mRTSPHost.c_str(), sourcePort, notify, &mSessionID);
            CHECK_EQ(err, (status_t)OK);

            mState = CONNECTING;
            mContentRecv->sendStatusReport("CONNECTING\n");
            
            break;
        }

        case kWhatRTSPNotify:
        {
            int32_t reason;
            CHECK(msg->findInt32("reason", &reason));

            switch (reason) {
                case ANetworkSession::kWhatError:
                {
                    int32_t sessionID;
                    CHECK(msg->findInt32("sessionID", &sessionID));

                    int32_t err;
                    CHECK(msg->findInt32("err", &err));

                    AString detail;
                    CHECK(msg->findString("detail", &detail));

                    ALOGE("An error occurred in session %d (%d, '%s/%s').",
                          sessionID,
                          err,
                          detail.c_str(),
                          strerror(-err));

                    ALOGE("An error occurred in session %d-%d (%d, '%s/%s').",
                          sessionID,
                          mSessionID,
                          err,
                          detail.c_str(),
                          strerror(-err));

                    if (sessionID == mSessionID) {
                        ALOGI("Lost control connection.");
                        looper()->stop();
                        mState = DISCONNECTED;   
                        mContentRecv->sendStatusReport("DISCONNECTED\n");
                        
                    }
                    break;
                }

                case ANetworkSession::kWhatConnected:
                {
                    //ALOGI("We're now connected.");
                    mState = CONNECTED;
                    mContentRecv->sendStatusReport("CONNECTED\n");
                    if (!mSetupURI.empty()) {
                        status_t err =
                            sendDescribe(mSessionID, mSetupURI.c_str());

                        CHECK_EQ(err, (status_t)OK);
                    }
                    //fire watchdog
                    sp<AMessage> msg = new AMessage(kWhatWatchDog, id());
                    msg->post();
                    break;
                }

                case ANetworkSession::kWhatData:
                {
                    onReceiveClientData(msg);
                    break;
                }

                case ANetworkSession::kWhatBinaryData:
                {
                    CHECK(sUseTCPInterleaving);

                    int32_t channel;
                    CHECK(msg->findInt32("channel", &channel));

                    sp<ABuffer> data;
                    CHECK(msg->findBuffer("data", &data));

                    //mRTPSink->injectPacket(channel == 0 /* isRTP */, data);
                    //ALOGI("mRTPSink->injectPacket");
                    break;
                }

                default:
                    TRESPASS();
            }
            break;
        }

        case kWhatStop:
        {
            
            
            ALOGI("AmmboxSink:kWhatStop.");
            looper()->stop();
            mState = DISCONNECTED;   
            mContentRecv->sendStatusReport("DISCONNECTED\n");
            /*
            mContentRecv.stopService();
            mNetSession->destroySession(mSessionID);
            mSessionID = 0;
            looper()->stop();
            ALOGI("AmmboxSink:stop--.");
            */
            break;
        }

        default:
            TRESPASS();
    }
}

void WifiDisplaySink::registerResponseHandler(
        int32_t sessionID, int32_t cseq, HandleRTSPResponseFunc func) {
    ResponseID id;
    id.mSessionID = sessionID;
    id.mCSeq = cseq;
    mResponseHandlers.add(id, func);
}

status_t WifiDisplaySink::sendM2(int32_t sessionID) {
    AString request = "OPTIONS * RTSP/1.0\r\n";
    AppendCommonResponse(&request, mNextCSeq);

    request.append(
            "Require: org.wfa.wfd1.0\r\n"
            "\r\n");

    WFDLOG("send:'%s'", request.c_str());
    status_t err =
        mNetSession->sendRequest(sessionID, request.c_str(), request.size());

    if (err != OK) {
        return err;
    }

    registerResponseHandler(
            sessionID, mNextCSeq, &WifiDisplaySink::onReceiveM2Response);

    ++mNextCSeq;

    return OK;
}

status_t WifiDisplaySink::onReceiveM2Response(
        int32_t sessionID, const sp<ParsedMessage> &msg) {
    int32_t statusCode;
    if (!msg->getStatusCode(&statusCode)) {
        return ERROR_MALFORMED;
    }

    if (statusCode != 200) {
        return ERROR_UNSUPPORTED;
    }

    return OK;
}

status_t WifiDisplaySink::onReceiveDescribeResponse(
        int32_t sessionID, const sp<ParsedMessage> &msg) {
    int32_t statusCode;
    if (!msg->getStatusCode(&statusCode)) {
        return ERROR_MALFORMED;
    }

    if (statusCode != 200) {
        return ERROR_UNSUPPORTED;
    }

    return sendSetup(sessionID, mSetupURI.c_str());
}

status_t WifiDisplaySink::onReceiveSetupResponse(
        int32_t sessionID, const sp<ParsedMessage> &msg) 
{
    //ALOGI("onReceiveSetupResponse ++");
    int32_t statusCode;

    if (!msg->getStatusCode(&statusCode)) {
        return ERROR_MALFORMED;
    }

    if (statusCode != 200) {
        return ERROR_UNSUPPORTED;
    }

    if (!msg->findString("session", &mPlaybackSessionID)) {
        return ERROR_MALFORMED;
    }

    if (!ParsedMessage::GetInt32Attribute(
                mPlaybackSessionID.c_str(),
                "timeout",
                &mPlaybackSessionTimeoutSecs)) {
        mPlaybackSessionTimeoutSecs = -1;
    }

    ssize_t colonPos = mPlaybackSessionID.find(";");
    if (colonPos >= 0) {
        // Strip any options from the returned session id.
        mPlaybackSessionID.erase(
                colonPos, mPlaybackSessionID.size() - colonPos);
    }

    status_t err = configureTransport(msg);

    if (err != OK) {
        return err;
    }
    mState = PAUSED;
    mContentRecv->sendStatusReport("PAUSED\n");


    mState = ONSETUP_RESPONSE;
    mSessionPlayId = sessionID;
    //ALOGI("onReceiveSetupResponse:mState=%d.",mState);
    if(hdcpEnabled){
        //ALOGI("onReceiveSetupResponse:hdcpEnabled.");
        mState = ONSETUP_RESPONSE;
        mSessionPlayId = sessionID;
        if(hdcpAuthOK){
            ALOGI("onReceiveSetupResponse:sendplay.");
            err = sendPlay(
                sessionID,
                !mSetupURI.empty()
                    ? mSetupURI.c_str() : "rtsp://x.x.x.x:x/wfd1.0/streamid=0");
            mState = PLAYING;
            mContentRecv->sendStatusReport("PLAYING\n");
        }else{
            ALOGI("onReceiveSetupResponse:not authed.sendSetup.");
            err = sendSetup(sessionID, "rtsp://x.x.x.x:x/wfd1.0/streamid=0");
        }
        
        return OK;

    }else{
        ALOGI("onReceiveSetupResponse:sendpause.");
        
        err = sendPlay(
                sessionID,
                !mSetupURI.empty()
                    ? mSetupURI.c_str() : "rtsp://x.x.x.x:x/wfd1.0/streamid=0");
        mState = PLAYING;
        mContentRecv->sendStatusReport("PLAYING\n");
        return err;
    }
}

status_t WifiDisplaySink::configureTransport(const sp<ParsedMessage> &msg)
{
    if (sUseTCPInterleaving) {
        return OK;
    }
    int rtpPort, rtcpPort;
    AString transport;
    if (!msg->findString("transport", &transport)) {
        ALOGE("Missing 'transport' field in SETUP response.");
        return ERROR_MALFORMED;
    }

    AString sourceHost;
    if (!ParsedMessage::GetAttribute(
                transport.c_str(), "source", &sourceHost)) {
        sourceHost = mRTSPHost;
    }

    AString serverPortStr;
    if (!ParsedMessage::GetAttribute(
                transport.c_str(), "server_port", &serverPortStr)) {
        rtpPort = 19022;
        rtcpPort = rtpPort + 1;
        //ALOGI("Missing 'server_port' in Transport field:set rtpPort=%d.",rtpPort);
        
        //harrison
        /*
        AString clientPortStr;
        if (!ParsedMessage::GetAttribute(
                    transport.c_str(), "client_port", &clientPortStr)) {
            ALOGE("Missing server_port:client_port in Transport field.");
            return ERROR_MALFORMED;
        }else{
            char buf[32],buf2[32];
            ALOGI("Missing server_port:client_port=%s.",clientPortStr.c_str());
            sscanf(clientPortStr.c_str(), "%s=%s", buf, buf2);
            rtpPort = atoi(buf);
            rtcpPort = rtpPort +1;
            ALOGI("Missing 'server_port':buf=%s,buf2=%s,port=%d.",buf,buf2,rtpPort);
        }
        */
    }else{
        if(sscanf(serverPortStr.c_str(), "%d-%d", &rtpPort, &rtcpPort) == 1){
            rtcpPort = rtpPort + 1;

        }
        if( rtpPort <= 0 || rtpPort > 65535
            || rtcpPort <=0 || rtcpPort > 65535
            || rtcpPort != rtpPort + 1) {

            ALOGE("Invalid server_port description '%s'.",
                    serverPortStr.c_str());

            return ERROR_MALFORMED;
        }

    }

    if (rtpPort & 1) {
        ALOGW("Server picked an odd numbered RTP port.");
    }
    //ALOGI("rtp=%d,rtcp=%d.",rtpPort, rtcpPort);
    mContentRecv->connectRemote(sourceHost.c_str(), rtpPort, rtcpPort);
    
    //mRTPSink->connect(sourceHost.c_str(), rtpPort, rtcpPort);
    return OK;
}

status_t WifiDisplaySink::onReceivePlayResponse(
        int32_t sessionID, const sp<ParsedMessage> &msg) 
{
    int32_t statusCode;
    if (!msg->getStatusCode(&statusCode)) {
        return ERROR_MALFORMED;
    }

    if (statusCode != 200) {
        return ERROR_UNSUPPORTED;
    }
    /*
    mState = PLAYING;
    mContentRecv->startService();
    mContentRecv->sendStatusReport("PLAYING\n");
    */
    return OK;
}

status_t WifiDisplaySink::onReceivePauseResponse(
        int32_t sessionID, const sp<ParsedMessage> &msg)
{
    int32_t statusCode;
    if (!msg->getStatusCode(&statusCode)) {
        return ERROR_MALFORMED;
    }

    if (statusCode != 200) {
        return ERROR_UNSUPPORTED;
    }

    mState = PAUSED;
    //mContentRecv->startService();
    mContentRecv->sendStatusReport("PAUSED\n");
    return OK;
}

void WifiDisplaySink::onReceiveClientData(const sp<AMessage> &msg) {
    int32_t sessionID;
    CHECK(msg->findInt32("sessionID", &sessionID));

    sp<RefBase> obj;
    CHECK(msg->findObject("data", &obj));

    sp<ParsedMessage> data =
        static_cast<ParsedMessage *>(obj.get());

    WFDLOG("received:'%s'", data->debugString().c_str());

    AString method;
    AString uri;
    data->getRequestField(0, &method);

    int32_t cseq;
    if (!data->findInt32("cseq", &cseq)) {
        sendErrorResponse(sessionID, "400 Bad Request", -1 /* cseq */);
        return;
    }

    if (method.startsWith("RTSP/")) {
        // This is a response.

        ResponseID id;
        id.mSessionID = sessionID;
        id.mCSeq = cseq;

        //ALOGI("SessionID=%d,CSeq=%d.", sessionID,cseq);
        ssize_t index = mResponseHandlers.indexOfKey(id);

        if (index < 0) {
            ALOGW("Received unsolicited server response, cseq %d", cseq);
            return;
        }

        HandleRTSPResponseFunc func = mResponseHandlers.valueAt(index);
        mResponseHandlers.removeItemsAt(index);

        status_t err = (this->*func)(sessionID, data);
        CHECK_EQ(err, (status_t)OK);

    } else {
        AString version;
        data->getRequestField(2, &version);
        if (!(version == AString("RTSP/1.0"))) {
            sendErrorResponse(sessionID, "505 RTSP Version not supported", cseq);
            return;
        }

        if (method == "OPTIONS") {
            onOptionsRequest(sessionID, cseq, data);
        } else if (method == "GET_PARAMETER") {
            onGetParameterRequest(sessionID, cseq, data);
        } else if (method == "SET_PARAMETER") {
            onSetParameterRequest(sessionID, cseq, data);
        } else {
            sendErrorResponse(sessionID, "405 Method Not Allowed", cseq);
        }
    }
}

void WifiDisplaySink::onOptionsRequest(
        int32_t sessionID,
        int32_t cseq,
        const sp<ParsedMessage> &data) {
    AString response = "RTSP/1.0 200 OK\r\n";
    AppendCommonResponse(&response, cseq);
    response.append("Public: org.wfa.wfd1.0, GET_PARAMETER, SET_PARAMETER\r\n");
    response.append("\r\n");

    WFDLOG("send:'%s'", response.c_str());

    status_t err = mNetSession->sendRequest(sessionID, response.c_str());
    CHECK_EQ(err, (status_t)OK);

    err = sendM2(sessionID);
    CHECK_EQ(err, (status_t)OK);
}

void WifiDisplaySink::onGetParameterRequest(
        int32_t sessionID,
        int32_t cseq,
        const sp<ParsedMessage> &data) {

    AString body;
    
    if (mState == CONNECTED) {
        /*
        body =
            //"wfd_video_formats: 40 00 02 02 0001DEFF 157C7FFF 00000FFF 00 0000 0000 11 none none, 01 02 0001DEFF 157C7FFF 00000FFF 00 0000 0000 11 none none\r\n" // TODO FIXMI ^^;;
            //"wfd_video_formats: 00 00 00 00 0000000F 00000000 00000000 00 0000 0000 00 none none\r\n" // TODO FIXMI ^^;;
            //"wfd_audio_codecs: LPCM 00000003 00, AAC 0000000F 00\r\n"
            "wfd_audio_codecs: LPCM 00000003 00\r\n"
            //"wfd_content_protection: HDCP2.1 port=35000\r\n"
            "wfd_client_rtp_ports: RTP/AVP/UDP;unicast 15550 0 mode=play\r\n";
            // "wfd_video_formats: 38 01 01 08 0001deff 07ffffff 00000fff 02 0000 0000 11 0780 0438" // Q-WH-D1
            // "wfd_video_formats: 40 00 02 02 0001DEFF 157C7FFF 00000FFF 00 0000 0000 11 none none, 01 02 0001DEFF 157C7FFF 00000FFF 00 0000 0000 11 none none" // PTV3000
            // "wfd_video_formats: 79 00 02 02 0001DEFF 157C7FFF 00000FFF 00 0000 0000 11 none none, 01 02 0001DEFF 157C7FFF 00000FFF 00 0000 0000 11 none none" // NEC wsbox
        */
        if(hdcpEnabled){
            //all size
            //body.append("wfd_video_formats: 40 00 02 02 0001DEFF 157C7FFF 00000FFF 00 0000 0000 11 none none, 01 02 0001DEFF 157C7FFF 00000FFF 00 0000 0000 11 none none\r\n");
            //1024x768
            //body.append("wfd_video_formats: 01 00 00 00 00000000 00000004 00000000 00 0000 0000 00 none none\r\n");
            //800x600
            //body.append("wfd_video_formats: 01 00 00 00 00000000 00000001 00000000 00 0000 0000 00 none none\r\n");
            //800x480
            //body.append("wfd_video_formats: 02 00 00 00 00000000 00000000 00000003 00 0000 0000 00 none none\r\n");
            //640x480
            //body.append("wfd_video_formats: 00 00 00 00 00000001 00000000 00000000 00 0000 0000 00 none none\r\n");
            //test
            body.append("wfd_video_formats: 00 00 00 00 0000007F 00000000 00000000 00 0000 0000 00 none none\r\n");
            body.append("wfd_audio_codecs: LPCM 00000003 00, AAC 0000000F 00\r\n");
            //body.append("wfd_audio_codecs: LPCM 00000003 00\r\n");

            if(hdcpVersion == 1)
                body.append("wfd_content_protection: HDCP2.1 port=35000\r\n");
            else
            if(hdcpVersion == 2)
                body.append("wfd_content_protection: HDCP2.2 port=35000\r\n");
        }else{
            //800x600
            //body.append("wfd_video_formats: 01 00 00 00 00000000 00000001 00000000 00 0000 0000 00 none none\r\n");
            //800x480
            //body.append("wfd_video_formats: 02 00 00 00 00000000 00000000 00000003 00 0000 0000 00 none none\r\n");
            //640x480
            //body.append("wfd_video_formats: 00 00 00 00 00000001 00000000 00000000 00 0000 0000 00 none none\r\n");
            //all size
            body.append("wfd_video_formats: 00 00 00 00 00000060 00000000 000000000 00 0000 0000 11 none none, 01 02 0001DEFF 157C7FFF 00000FFF 00 0000 0000 11 none none\r\n");
            body.append("wfd_audio_codecs: LPCM 00000003 00, AAC 0000000F 00\r\n");
        }
        //uibc
        const char *content = data->getContent();
        ALOGI("M3:%s",content);
        if (strstr(content, "wfd_uibc_capability") != NULL) {
            body.append("wfd_uibc_capability: input_category_list=GENERIC;generic_cap_list=Keyboard,Mouse,SingleTouch;hidc_cap_list=none;port=none\r\n");

        }
        body.append("wfd_client_rtp_ports: RTP/AVP/UDP;unicast 15550 0 mode=play\r\n");
        if(hdcpEnabled){
            mContentRecv->initHdcp(mWindow);
        }else{
            mContentRecv->init(mWindow);
        }
        
        AString response = "RTSP/1.0 200 OK\r\n";
        AppendCommonResponse(&response, cseq);
        response.append("Content-Type: text/parameters\r\n");
        response.append(StringPrintf("Content-Length: %d\r\n", body.size()));
        response.append("\r\n");
        response.append(body);

        WFDLOG("send:'%s'", response.c_str());
        status_t err = mNetSession->sendRequest(sessionID, response.c_str());
        CHECK_EQ(err, (status_t)OK);
        gettimeofday(&liveTimeStart, NULL);
        
    }else
    if(mState == PLAYING ||
       mState == SUSPEND ){

        AString response = "RTSP/1.0 200 OK\r\n";
        AppendCommonResponse(&response, cseq);
        response.append("Content-Type: text/parameters\r\n");
        response.append(StringPrintf("Content-Length: %d\r\n", body.size()));
        response.append("\r\n");
        response.append(body);
        WFDLOG("send:'%s'", response.c_str());
        status_t err = mNetSession->sendRequest(sessionID, response.c_str());
        CHECK_EQ(err, (status_t)OK);

        //ALOGI("WFDSink:reset watchDogCount.");
        watchDogCount = 0;
        /*
        long mtime, seconds, useconds;
        gettimeofday(&liveTimeEnd, NULL);
        seconds  = liveTimeEnd.tv_sec  - liveTimeStart.tv_sec;
        useconds = liveTimeEnd.tv_usec - liveTimeStart.tv_usec;
        mtime = ((seconds) * 1000 + useconds/1000.0) + 0.5;
        WFDLOG("M16:elapsed:%ld ms.",mtime);
        if(mtime > 60000){ //M16 default timeout value
            //abort RTSP connection
            mState = ABORT;   
            mContentRecv->sendStatusReport("ABORT\n"); 
        }
        gettimeofday(&liveTimeStart, NULL);
        */
    }
    
    
    
}

status_t WifiDisplaySink::sendDescribe(int32_t sessionID, const char *uri) {
    uri = "rtsp://xwgntvx.is.livestream-api.com/livestreamiphone/wgntv";
    uri = "rtsp://v2.cache6.c.youtube.com/video.3gp?cid=e101d4bf280055f9&fmt=18";

    AString request = StringPrintf("DESCRIBE %s RTSP/1.0\r\n", uri);
    AppendCommonResponse(&request, mNextCSeq);

    request.append("Accept: application/sdp\r\n");
    request.append("\r\n");

    WFDLOG("send:'%s'", request.c_str());
    status_t err = mNetSession->sendRequest(
            sessionID, request.c_str(), request.size());

    if (err != OK) {
        return err;
    }

    registerResponseHandler(
            sessionID, mNextCSeq, &WifiDisplaySink::onReceiveDescribeResponse);

    ++mNextCSeq;

    return OK;
}

status_t WifiDisplaySink::sendSetup(int32_t sessionID, const char *uri)
{
    ALOGI("send M6:sessionID:%d,cseq=%d.",sessionID,mNextCSeq);

    status_t err;
        
    AString request = StringPrintf("SETUP %s RTSP/1.0\r\n", uri);
    AppendCommonResponse(&request, mNextCSeq);
    if (sUseTCPInterleaving) {
        request.append("Transport: RTP/AVP/TCP;interleaved=0-1\r\n");
    } else {
        int32_t rtpPort = 15550;
        //int32_t rtpPort = mRTPSink->getRTPPort();

        request.append(
                StringPrintf(
                    "Transport: RTP/AVP/UDP;unicast;client_port=%d-%d\r\n",
                    rtpPort, rtpPort + 1));
    }

    request.append("\r\n");

    WFDLOG("send:'%s'", request.c_str());
    err = mNetSession->sendRequest(sessionID, request.c_str(), request.size());

    if (err != OK) {
        return err;
    }
    
    registerResponseHandler(
            sessionID, mNextCSeq, &WifiDisplaySink::onReceiveSetupResponse);

    ++mNextCSeq;

    return OK;
}

status_t WifiDisplaySink::sendPlay(int32_t sessionID, const char *uri)
{
    ALOGI("send M7:sessionID:%d,cseq=%d.",sessionID,mNextCSeq);

    AString request = StringPrintf("PLAY %s RTSP/1.0\r\n", uri);
    AppendCommonResponse(&request, mNextCSeq);
    request.append(StringPrintf("Session: %s\r\n", mPlaybackSessionID.c_str()));
    request.append("\r\n");

    WFDLOG("send:'%s'", request.c_str());
    status_t err =
        mNetSession->sendRequest(sessionID, request.c_str(), request.size());

    if (err != OK) {
        return err;
    }

    registerResponseHandler(
            sessionID, mNextCSeq, &WifiDisplaySink::onReceivePlayResponse);

    ++mNextCSeq;

    
    return OK;
}

status_t WifiDisplaySink::sendPause(int32_t sessionID, const char *uri)
{
    AString request = StringPrintf("PAUSE %s RTSP/1.0\r\n", uri);
    AppendCommonResponse(&request, mNextCSeq);
    request.append(StringPrintf("Session: %s\r\n", mPlaybackSessionID.c_str()));
    request.append("\r\n");

    WFDLOG("send:'%s'", request.c_str());
    status_t err =
        mNetSession->sendRequest(sessionID, request.c_str(), request.size());

    if (err != OK) {
        return err;
    }

    registerResponseHandler(
            sessionID, mNextCSeq, &WifiDisplaySink::onReceivePauseResponse);

    ++mNextCSeq;
    
    return OK;
}

void WifiDisplaySink::onSetParameterRequest(
        int32_t sessionID,
        int32_t cseq,
        const sp<ParsedMessage> &data)
{
    status_t err;

    const char *content = data->getContent();

    if (strstr(content, "wfd_trigger_method: enable\r\n") != NULL) {
        if(mUibcSink != NULL){
            mUibcSink->start(mRTSPHost.c_str(),mPortUIBC);
        }
    }

    if (strstr(content, "wfd_trigger_method: SETUP\r\n") != NULL) {

        msessionSetupId = sessionID;        
        /*
        if(hdcpEnabled){
            if(hdcpAuthOK){
                ALOGI("wfd_trigger_method: SETUP:hdcpAuthOK");
                msessionSetupId = sessionID;
                err = sendSetup(sessionID,
                                "rtsp://x.x.x.x:x/wfd1.0/streamid=0");
                CHECK_EQ(err, (status_t)OK);
            }else{
                ALOGI("wfd_trigger_method: SETUP: NOT hdcpAuthOK:do nothing.");
            }
        }else{*/
            
       //}
 
        mCseq = cseq;
        
        AString response = "RTSP/1.0 200 OK\r\n";
        AppendCommonResponse(&response, cseq);
        response.append("\r\n");

        WFDLOG("send:'%s'", response.c_str());
        err = mNetSession->sendRequest(sessionID, response.c_str());
        CHECK_EQ(err, (status_t)OK);

        err = sendSetup(sessionID, "rtsp://x.x.x.x:x/wfd1.0/streamid=0");
        CHECK_EQ(err, (status_t)OK);
    }else
    if (strstr(content, "wfd_trigger_method: PAUSE\r\n") != NULL) {
        //ALOGI("WifiDisplaySink::onSetParameterRequest:wfd_trigger_method: PAUSE.");
        

        AString request = StringPrintf("SETUP %s RTSP/1.0\r\n", "rtsp://x.x.x.x:x/wfd1.0/streamid=0");
        AppendCommonResponse(&request, mNextCSeq);
        if (sUseTCPInterleaving) {
            request.append("Transport: RTP/AVP/TCP;interleaved=0-1\r\n");
        } else {
            int32_t rtpPort = 15550;
            //int32_t rtpPort = mRTPSink->getRTPPort();
            request.append(
                    StringPrintf(
                        "Transport: RTP/AVP/UDP;unicast;client_port=%d-%d\r\n",
                        rtpPort, rtpPort + 1));
        }
        request.append("\r\n");
        WFDLOG("send:'%s'", request.c_str());
        err = mNetSession->sendRequest(sessionID, request.c_str(), request.size());
        if (err != OK) {
            return ;
        }
        //registerResponseHandler(sessionID, mNextCSeq, &WifiDisplaySink::onReceiveSetupResponse);
        ++mNextCSeq;

        //
        AString response = "RTSP/1.0 200 OK\r\n";
        AppendCommonResponse(&response, cseq);
        response.append("\r\n");

        WFDLOG("send:'%s'", response.c_str());
        err = mNetSession->sendRequest(sessionID, response.c_str());
        CHECK_EQ(err, (status_t)OK);
    }else
    if(strstr(content, "wfd_trigger_method: TEARDOWN\r\n") != NULL) {
        ALOGI("wfd_trigger_method: TEARDOWN.");
             
        AString response = "RTSP/1.0 200 OK\r\n";
        AppendCommonResponse(&response, cseq);
        response.append("\r\n");
        WFDLOG("send:'%s'", response.c_str());
        err = mNetSession->sendRequest(sessionID, response.c_str());
        CHECK_EQ(err, (status_t)OK);

        mState = DISCONNECTED;   
        mContentRecv->sendStatusReport("DISCONNECTED\n");
        looper()->stop();
        
    }else{
        AString response = "RTSP/1.0 200 OK\r\n";
        AppendCommonResponse(&response, cseq);
        response.append("\r\n");

        WFDLOG("send:'%s'", response.c_str());
        err = mNetSession->sendRequest(sessionID, response.c_str());
        CHECK_EQ(err, (status_t)OK);
    }

    //uibc 
    if ( strstr(content, "wfd_uibc_capability: ") != NULL) {
        WFDLOG("UIBC enabled.");
        //parse uibc port
        char* str = strstr(content, "wfd_uibc_capability");
        char* portptr = strstr(str,"port");
        
        if(portptr != NULL){
            portptr += strlen("port=");
            mPortUIBC = atoi(portptr);
        } 
    }
    if (strstr(content, "wfd_uibc_setting: enable\r\n") != NULL) {
        if(mUibcSink != NULL){
            mUibcSink->start(mRTSPHost.c_str(),mPortUIBC);
            uibcEnabled = true;
        }
    }

    
}

void WifiDisplaySink::sendErrorResponse(
        int32_t sessionID,
        const char *errorDetail,
        int32_t cseq) {
    AString response;
    response.append("RTSP/1.0 ");
    response.append(errorDetail);
    response.append("\r\n");

    AppendCommonResponse(&response, cseq);
    response.append("\r\n");

    WFDLOG("send:'%s'", response.c_str());
    status_t err = mNetSession->sendRequest(sessionID, response.c_str());
    CHECK_EQ(err, (status_t)OK);
}

// static
void WifiDisplaySink::AppendCommonResponse(AString *response, int32_t cseq) {
    time_t now = time(NULL);
    struct tm *now2 = gmtime(&now);
    char buf[128];
    strftime(buf, sizeof(buf), "%a, %d %b %Y %H:%M:%S %z", now2);

    response->append("Date: ");
    response->append(buf);
    response->append("\r\n");

    response->append("User-Agent: stagefright/1.1 (Linux;Android 4.1)\r\n");

    if (cseq >= 0) {
        response->append(StringPrintf("CSeq: %d\r\n", cseq));
    }
}

}  // namespace android
