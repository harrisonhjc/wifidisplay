#ifndef DEVICE_KEYSET_H_
#define DEVICE_KEYSET_H_

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
#include <openssl/ssl.h>
#include <openssl/rsa.h>
#include <openssl/evp.h>
#include <openssl/bio.h>
#include <openssl/err.h>
#include <openssl/aes.h>
#include <openssl/rand.h> 
#include <openssl/hmac.h>
#include <openssl/buffer.h>
#include <openssl/engine.h>
#include <openssl/bn.h>

namespace android {

struct ctr_state {
    unsigned char ivec[16];  /* ivec[0..7] is the IV, ivec[8..15] is the big-endian counter */
    unsigned int num;
    unsigned char ecount[16];
};

struct AKE_Transmitter_Info{
    unsigned char msg_id;
    unsigned short LENGTH;
    unsigned char VERSION;
    unsigned short TRANSMITTER_CAPABILITY_MASK;
};

const int keysetSize = 902;

struct DeviceKeyset {

    DeviceKeyset(){};
    ~DeviceKeyset();
    status_t init();
    unsigned char* getCertrx();
    unsigned char* getRrx();
    unsigned char* getHprime();
    unsigned char* getLprime();
    int getCertrxLen();
    int getRrxLen();
    int getHprimeLen();
    int getLprimeLen();
    status_t setRtx(const unsigned char*, int len);
    status_t setRn(const unsigned char*, int len);
    status_t decryptKm(const unsigned char*, int len);
    status_t decryptKmStoreKm(const unsigned char* buf, int len);
    status_t computeKh();
    status_t computeKd();
    status_t computeKd2();
    
    status_t computeKs(const unsigned char*);
    status_t computeHprime();
    status_t computeLprime();
    status_t encryptKm(unsigned char EKm[16]);
    void setAkeTransmitterInfo(AKE_Transmitter_Info*);
    status_t getKpriv(unsigned char Kpriv[128]);
    unsigned char* getKs();
    unsigned char* getRiv();
    unsigned char* getLc();
private:
    RSA *rsa;
    AKE_Transmitter_Info* pTransInfo;
    unsigned long long ctr;
    unsigned char lc[16];
    unsigned char rtx[8];
    unsigned char rrx[8];
    unsigned char Km[16];
    unsigned char Kh[16];
    unsigned char Kd[32];
    unsigned char Kd2[16];
    unsigned char Ks[16];
    unsigned char Hprime[32];
    unsigned char Lprime[32];
    unsigned char Certrx[522] ;
    unsigned char GC128[16];
    unsigned char rn[8];
    unsigned char riv[8];
    unsigned char N[128];
    unsigned char P[64];
    unsigned char Q[64];
    unsigned char dmp1[64];
    unsigned char dmq1[64];
    unsigned char iqmp[64];
    
    
};

}  // namespace android

#endif  // DEVICE_KEYSET_H_
