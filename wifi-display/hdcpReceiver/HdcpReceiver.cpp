#define LOG_TAG "HdcpReceiver"
//#define HDCPLOG   ALOGI
#define HDCPLOG

#undef NDEBUG 
#define LOG_NDEBUG   0   //打开LOGV
#define LOG_NIDEBUG  0   //打开LOGI
#define LOG_NDDEBUG 0    //打开LOGD
#define LOG_NEDEBUG 0    //打开LOGD
#include <utils/Log.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <dirent.h>
#include <netinet/ether.h>
#include <netdb.h>
#include <linux/if_packet.h>
#include <netinet/if_ether.h>
#include <linux/if_arp.h>
#include <net/if.h>
#include <netutils/ifc.h>
#include "HdcpReceiver.h"
#include <media/stagefright/foundation/hexdump.h>
#include <media/stagefright/foundation/AMessage.h>

namespace android {

int stopIssued = 0;
pthread_mutex_t stopMutex;

int getStopIssued(void) {
  int ret = 0;
  pthread_mutex_lock(&stopMutex);
  ret = stopIssued;
  pthread_mutex_unlock(&stopMutex);
  return ret;
}

void setStopIssued(int val) {
  pthread_mutex_lock(&stopMutex);
  stopIssued = val;
  pthread_mutex_unlock(&stopMutex);
}


void  HdcpReceiver::startThread(void)
{
    HdcpReceiverPtr t = &HdcpReceiver::threadLoop;
    PthreadPtr p = *(PthreadPtr*)&t;
    pthread_t    tid;
    if(pthread_create(&tid, 0, p, this) == 0){
        pthread_detach(tid);
    }
}


HdcpReceiver::~HdcpReceiver()
{

}

status_t HdcpReceiver::start(const char *host, unsigned port)
{
	
    setStopIssued(0);
    
    hdcpVersion = 1;
    memset(localHost,0,sizeof(localHost));
    memcpy(localHost, host, strlen(host));
    this->port = port;
    if(-1 == mKeyset.init()){
        ALOGE("Keyset error.");
        return -1;
    }
    
	//run("HdcpReceiver");
    startThread();
    
	return OK;
	
}

void HdcpReceiver::stop()
{
    
    setStopIssued(1);
    
}

void HdcpReceiver::setHdcpVersion(int ver)
{
    hdcpVersion = ver;
}

void* HdcpReceiver::threadLoop(void)
{
	//HDCPLOG("HdcpReceiver::threadLoop:+");
	
    JNIEnv *env = NULL;
    int isAttached = 0;
    
    if(mJvm){
        if(mJvm->AttachCurrentThread(&env, NULL) < 0){
            ALOGE("HdcpReceiver::threadLoop:thread can not attach current thread." );
            return 0;
        }
        isAttached = 1;
    }

	
    status_t err = OK;
    int serverSocket, res;

    
    serverSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (serverSocket < 0) {
        err = -errno;
        ALOGE("HdcpReceiver::threadLoop:socket failed.");
        return 0;
    }
    
    const int yes = 1;
    res = setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    if (res < 0) {
        close(serverSocket);
        err = -errno;
        ALOGE("HdcpReceiver::threadLoop:setsockopt failed.");
        return 0;
    }
    
    /*
    int flags = fcntl(serverSocket, F_GETFL, 0);
    if (flags < 0) {
        flags = 0;
    }
    res = fcntl(serverSocket, F_SETFL, flags | O_NONBLOCK);
    if (res < 0) {
        close(serverSocket);
        err = -errno;
        ALOGE("HdcpReceiver::threadLoop:fcntl:O_NONBLOCK failed.");
        return false;
    }
    */

    struct sockaddr_in addr;
    memset(addr.sin_zero, 0, sizeof(addr.sin_zero));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);//inet_addr(localHost);
    res = bind(serverSocket, (const struct sockaddr *)&addr, sizeof(addr));
    
    if (res == 0){
        //HDCPLOG("HdcpReceiver::threadLoop:bind succeeded.");
        res = listen(serverSocket, 5);
    }else{
        close(serverSocket);
        err = -errno;
        ALOGE("HdcpReceiver::threadLoop:bind failed.");
        return 0;
    }
    
    while(getStopIssued() == 0){
        int clientSocket;
        struct sockaddr_in clientAddr;
        socklen_t clientlen = sizeof(clientAddr);
        
        clientSocket =accept(serverSocket, (struct sockaddr *) &clientAddr, &clientlen);
        if(clientSocket < 0){
            ALOGE("HdcpReceiver::threadLoop:accept return error.");
            
        }else{
            ALOGI("HdcpReceiver:HDCP Transmitter connected.");
            MakeSocketNonBlocking(clientSocket);
            int flag = 1; 
            setsockopt(clientSocket, IPPROTO_TCP, TCP_NODELAY, (char *) &flag, sizeof(int));
            flag = IPTOS_LOWDELAY;
            setsockopt(clientSocket, IPPROTO_IP, IP_TOS, (char *) &flag, sizeof(int));
            flag = IPTOS_THROUGHPUT;
            setsockopt(clientSocket, IPPROTO_IP, IP_TOS, (char *) &flag, sizeof(int));
            flag = IPTOS_RELIABILITY;
            setsockopt(clientSocket, IPPROTO_IP, IP_TOS, (char *) &flag, sizeof(int));
              
            
            HandleHdcp(clientSocket);
        }
    }
    close(serverSocket);
    serverSocket = -1;
    
    if(isAttached){
        mJvm->DetachCurrentThread();
    }
    //HDCPLOG("HdcpReceiver::threadLoop:-");
    pthread_exit((void *)0);
    return 0;
}


status_t HdcpReceiver::HandleHdcp(int clientSocket)
{
    char buffer[1024];
    int n;
    char* pData;
    struct timeval start, end;
    long mtime, seconds, useconds;
    
    gettimeofday(&start, NULL);

    do{
        
        n = recv(clientSocket, buffer, sizeof(buffer), 0);
        
        if(n > 0){
            
            //HDCPLOG("HandleHdcp in:%d bytes.",n);
            //hexdump(buffer, n);

            pData = buffer;
            switch(*pData){
                case 2: //AKE_Init
                //gettimeofday(&start, NULL);
                on_AKE_Init(clientSocket, pData);
                n -= 9; // AKE_Init size
                if(n >= 6){  //AKE_Transmitter_Info size
                    pData += 9;
                    AKE_Send_Cert(clientSocket);
                    AKE_Receiver_Info(clientSocket);
                   
                    on_AKE_Transmitter_Info(clientSocket, pData);
                    
                    /*
                    gettimeofday(&end, NULL);
                    seconds  = end.tv_sec  - start.tv_sec;
                    useconds = end.tv_usec - start.tv_usec;
                    mtime = ((seconds) * 1000 + useconds/1000.0) + 0.5;
                    HDCPLOG("HandleHdcp:AKE_Receiver_Info:elapsed:%ld ms.",mtime);
                    */
                }
                break;
                
                case 4: //AKE_No_Stored_Km
                //gettimeofday(&start, NULL);
                
                on_AKE_No_Stored_Km(clientSocket, pData);
                /*
                gettimeofday(&end, NULL);
                seconds  = end.tv_sec  - start.tv_sec;
                useconds = end.tv_usec - start.tv_usec;
                mtime = ((seconds) * 1000 + useconds/1000.0) + 0.5;
                HDCPLOG("on_AKE_No_Stored_Km:elapsed:%ld ms.",mtime);
                */
                break;

                case 5: //AKE_Stored_Km
                on_AKE_Stored_Km(clientSocket, pData);
                break;

                case 9: // LC_Init
                //gettimeofday(&start, NULL);
                //int flag2;
                //flag2 = 1; 
                //setsockopt(clientSocket, IPPROTO_TCP, TCP_NODELAY, (char *) &flag2, sizeof(int));
                //setsockopt(clientSocket, IPPROTO_IP, IP_TOS, (char *) &flag2, sizeof(int));

                on_LC_Init(clientSocket, pData);

                //gettimeofday(&end, NULL);
                //seconds  = end.tv_sec  - start.tv_sec;
                //useconds = end.tv_usec - start.tv_usec;
                //mtime = ((seconds) * 1000 + useconds/1000.0) + 0.5;
                //HDCPLOG("on_LC_Init:elapsed:%ld ms.",mtime);
                break;

                case 11: //SKE_Send_Eks
                onSKE_Send_Eks(clientSocket, pData);
                gettimeofday(&end, NULL);
                seconds  = end.tv_sec  - start.tv_sec;
                useconds = end.tv_usec - start.tv_usec;
                mtime = ((seconds) * 1000 + useconds/1000.0) + 0.5;
                ALOGI("HDCP takes %ld ms.",mtime);
                break;

                case 19: //AKE_Transmitter_Info
                AKE_Send_Cert(clientSocket);
                AKE_Receiver_Info(clientSocket);
                
                on_AKE_Transmitter_Info(clientSocket, pData);
                
                
                

                break;

                default:
                break;   
            }
        }
    } while(getStopIssued() == 0);

    close(clientSocket);
    

    return OK;
}


status_t HdcpReceiver::on_AKE_Init(int fd,const char* buf)
{
    HDCPLOG("on_AKE_Init.");
    mKeyset.setRtx((const unsigned char*)(buf+1), 8);  //rrx len=8
    
    return OK;
}

status_t HdcpReceiver::on_AKE_Transmitter_Info(int fd,const char* buf)
{
    HDCPLOG("on_AKE_Transmitter_Info.");

    ake_transmitter_info.msg_id = *buf;
    ake_transmitter_info.LENGTH = *(buf+2);
    ake_transmitter_info.VERSION = *(buf+3);
    ake_transmitter_info.TRANSMITTER_CAPABILITY_MASK = *(buf+5);
    HDCPLOG("AKE_Transmitter_Info.VERSION=%x.",ake_transmitter_info.VERSION);
    HDCPLOG("AKE_Transmitter_Info.TRANSMITTER_CAPABILITY_MASK=0x%02x.",ake_transmitter_info.TRANSMITTER_CAPABILITY_MASK);
    mKeyset.setAkeTransmitterInfo(&ake_transmitter_info);
    
    return OK;
}

status_t HdcpReceiver::on_LC_Init(int fd,const char* buf)
{
    HDCPLOG("on_LC_Init.");
    mKeyset.setRn((const unsigned char*)(buf+1), 8);  //rn len=8
    LC_Send_L_prime(fd);
    
    return OK;
}


status_t HdcpReceiver::onSKE_Send_Eks(int fd,const char* buf)
{
    HDCPLOG("onSKE_Send_Eks.");
    mKeyset.computeKs((const unsigned char*)(buf+1));
    //signal that hdcp auth is complete.
    //sp<AMessage> msg = new AMessage(kWhatHdcpAuthOK, wfdHandlerId);
    //msg->post();
    //HDCPLOG("onSKE_Send_Eks:send kWhatHdcpAuthOK");
    notifyAuthComplete();
    
    return OK;
}

status_t HdcpReceiver::LC_Send_L_prime(int fd)
{
    char buf[33];
    ssize_t n;
    buf[0] = 10;
    mKeyset.computeLprime();
    memcpy(buf+1,mKeyset.getLprime(),mKeyset.getLprimeLen());
    n = send(fd, buf, sizeof(buf), 0);
    HDCPLOG("LC_Send_L_prime.send %ld bytes.",n);
    return OK;
}



status_t HdcpReceiver::on_AKE_No_Stored_Km(int fd,const char* buf)
{
    HDCPLOG("on_AKE_No_Stored_Km.");
    //AKE_Send_Rrx(fd);
    if(OK != mKeyset.decryptKm((unsigned char*)(buf+1), 128)){
        ALOGE("Km error.");
        return -1;
    }
    AKE_Send_Rrx(fd);
    AKE_Send_H_prime(fd);
    AKE_Send_Pairing_Info(fd);
    return OK;
}

status_t HdcpReceiver::on_AKE_Stored_Km(int fd, const  char* buf)
{
    HDCPLOG("on_AKE_Stored_Km.");
    
    AKE_Send_Rrx(fd);
    
    mKeyset.computeKh();
    //Kh --> decrypt E_Kh(Km)
    mKeyset.decryptKmStoreKm((unsigned char*)(buf+1),32);
    //Km --> compute Kd
    mKeyset.computeKd();
        
    AKE_Send_H_prime(fd);
    return OK;
}


status_t HdcpReceiver::AKE_Send_H_prime(int fd)
{
    char buf[33];
    ssize_t n;
    mKeyset.computeHprime();
    buf[0] = 7;
    memcpy(buf+1,mKeyset.getHprime(),mKeyset.getHprimeLen());
    n = send(fd, buf, 33, 0);
    HDCPLOG("AKE_Send_H_prime.send %ld bytes.",n);
    
    return OK;
}
    
status_t HdcpReceiver::AKE_Send_Pairing_Info(int fd)
{
    char buf[17];
    ssize_t n;
    unsigned char EKm[16];

    mKeyset.computeKh();
    mKeyset.encryptKm(EKm);
    buf[0] = 8;
    memcpy(buf+1, EKm, sizeof(EKm));
    n = send(fd, buf , sizeof(buf), 0);
    HDCPLOG("AKE_Send_Pairing_Info.send %ld bytes.",n);
    return OK;
}





status_t HdcpReceiver::AKE_Send_Cert(int fd)
{
    ssize_t n;
    unsigned char buf[524];

    buf[0] = 3;
    buf[1] = 0;
    memcpy(buf+2, mKeyset.getCertrx(), mKeyset.getCertrxLen());
    n = send(fd, buf, 524, 0);
    
    HDCPLOG("AKE_Send_Cert:send %ld bytes.", n);
    return OK;
}

status_t HdcpReceiver::AKE_Receiver_Info(int fd)
{
    char buf[6];
    ssize_t n;
    /*
    tReceiverInfo recvInfo;
    recvInfo.id = 20;
    recvInfo.len = htonl(0x0006);
    recvInfo.version = 0x02;
    recvInfo.cap = htonl(0x0002);
    send(fd, &recvInfo, sizeof(struct sReceiverInfo), 0);
    */
    buf[0] = 20;
    buf[1] = 0x00;
    buf[2] = 0x06;
    
    if(hdcpVersion == 1)
        buf[3] = 0x01; //v2.1
    else
    if(hdcpVersion == 2)
        buf[3] = 0x02; //v2.2

    
    buf[4] = 0x00;
    buf[5] = 0x00;
    n = send(fd, buf, 6, 0);

    HDCPLOG("AKE_Receiver_Info:send %ld bytes.", n);
    return OK;
}

    
status_t HdcpReceiver::AKE_Send_Rrx(int fd)
{
    
    char buf[9];
    ssize_t n;

    buf[0] = 6;
    memcpy(buf+1,mKeyset.getRrx(),mKeyset.getRrxLen());
    n = send(fd, buf, 9, 0);
    HDCPLOG("AKE_Send_rrx.send %ld bytes.",n);
    return OK;
}
    
unsigned char* HdcpReceiver::getKs()
{
    return mKeyset.getKs();
}

unsigned char* HdcpReceiver::getRiv()
{
    return mKeyset.getRiv();
}

unsigned char* HdcpReceiver::getLc()
{
    return mKeyset.getLc();
}
    

status_t HdcpReceiver::MakeSocketNonBlocking(int s) {
    int flags = fcntl(s, F_GETFL, 0);
    if (flags < 0) {
        flags = 0;
    }

    int res = fcntl(s, F_SETFL, flags | O_NONBLOCK);
    if (res < 0) {
        return -errno;
    }

    return OK;
}


} // namespace android