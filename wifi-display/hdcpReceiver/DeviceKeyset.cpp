#define LOG_TAG "Devicekeyset"

#undef NDEBUG 
#define LOG_NDEBUG  0
#define LOG_NIDEBUG 0
#define LOG_NDDEBUG 0
#define LOG_NEDEBUG 0
#define LOG_NVDEBUG 0
#include <utils/Log.h>

#include <time.h>
#include <stdlib.h>

#include "DeviceKeyset.h"
#include <media/stagefright/foundation/hexdump.h>
#include <media/stagefright/foundation/ADebug.h>

namespace android {


DeviceKeyset::~DeviceKeyset()
{
	if(rsa != NULL){
		RSA_free(rsa);
	}
	
}

status_t DeviceKeyset::init()
{
	int fd;
	ssize_t nRead;
	char buf[1024];
	
    ctr = 0;

    fd = open("/etc/miracast.dat", O_RDONLY);
    if(fd == -1){
            ALOGE("Devicekeyset:open confidential failed.");
            return -1;
    }
    //ketset file format (902 bytes)
    //Global Constant :128-bit = 16 bytes
	//Private key : 364 bytes
	//Certificate :522 bytes
		//ID:5 offset=0x17c
		//Public key: 131 offset=0x181
		//Protocol Desp.: 4 bits
		//Reserved : 12 bits
		//DCP LLC Sign:384 offset=0x206

    nRead = read(fd, &buf, sizeof(buf));
    if(nRead == 902){
      memcpy(lc, buf+4, sizeof(lc));  
      memcpy(Certrx, buf+40, sizeof(Certrx));
      memcpy(N, buf+45, sizeof(N));
      memcpy(P, buf+562, sizeof(P));
      memcpy(Q, buf+626, sizeof(Q));
      memcpy(dmp1, buf+690, sizeof(dmp1));
      memcpy(dmq1, buf+754, sizeof(dmq1));
      memcpy(iqmp, buf+818, sizeof(iqmp));
    }else{
        ALOGE("Devicekeyset:wrong confidential file.");
        close(fd);
        return -1;
    }
    close(fd);
    //ALOGI("got confidential.");

    //new rsa 
    rsa = RSA_new();
    if( NULL == rsa ){
        ALOGE("RSA_new failed.");
        return -1;
    }
    int lenCiphered;
    rsa->n = BN_bin2bn( N, sizeof(N), rsa->n );
    rsa->e = BN_bin2bn( (unsigned char*)"\x01\x00\x01", 3, rsa->e );
    rsa->p = BN_bin2bn( P, sizeof(P), rsa->p );
    rsa->q = BN_bin2bn( Q, sizeof(Q), rsa->q );
    rsa->dmp1 = BN_bin2bn( dmp1, sizeof(dmp1), rsa->dmp1 );
    rsa->dmq1 = BN_bin2bn( dmq1, sizeof(dmq1), rsa->dmq1 );
    rsa->iqmp = BN_bin2bn( iqmp, sizeof(iqmp), rsa->iqmp );

    //gen rrx
    srand(time(NULL));
	int r = rand();
	rrx[0] = (char)(r);
	rrx[1] = (char)(r >> 8);
	rrx[2] = (char)(r >> 16);
	rrx[3] = (char)(r >> 24);
	r = rand();
	rrx[4] = (char)(r);
	rrx[5] = (char)(r >> 8);
	rrx[6] = (char)(r >> 16);
	rrx[7] = (char)(r >> 24);

	return OK;
}

unsigned char* DeviceKeyset::getRrx()
{
	return rrx;
}
int DeviceKeyset::getRrxLen()
{
	return sizeof(rrx);
}

unsigned char* DeviceKeyset::getHprime()
{
    return Hprime;
}
int DeviceKeyset::getHprimeLen()
{
    return sizeof(Hprime);
}

unsigned char* DeviceKeyset::getLprime()
{
    return Lprime;
}
int DeviceKeyset::getLprimeLen()
{
    return sizeof(Lprime);
}

unsigned char* DeviceKeyset::getCertrx()
{
	return Certrx;
}
int DeviceKeyset::getCertrxLen()
{
	return sizeof(Certrx);
}

unsigned char* DeviceKeyset::getKs()
{
    
    return Ks;
}

unsigned char* DeviceKeyset::getRiv()
{
    return riv;
}

unsigned char* DeviceKeyset::getLc()
{
    return lc;
}

status_t DeviceKeyset::setRtx(const unsigned char* buf, int len)
{
	if(len == sizeof(rtx)){
		memcpy(rtx,buf,len);
		
	}else return -1;

	return OK;
}

status_t DeviceKeyset::setRn(const unsigned char* buf, int len)
{
    if(len == sizeof(rn)){
        memcpy(rn,buf,len);
        
    }else return -1;

    return OK;
}

status_t DeviceKeyset::decryptKm(const unsigned char* buf, int len)
{
   
	unsigned char* ciphered;
	unsigned char deciphered[128];
	int outlen;
    status_t ret;

	if(len == 128){
		ciphered = (unsigned char*)buf;
		//outlen = RSA_private_decrypt(len, ciphered, deciphered, rsa, RSA_NO_PADDING);
        outlen = RSA_private_decrypt(len, ciphered, deciphered, rsa, RSA_PKCS1_OAEP_PADDING);
  		if(outlen > 0){
			//ALOGI("Km decrypted.len=%d.", outlen);
			memcpy(Km, deciphered, outlen);

		}else{
            ALOGI("Km decrypt failed.");
            return -1;
        }
        
	}
    
    ret = computeKd();
    CHECK(ret == OK);
    
    
	return OK;

}

status_t DeviceKeyset::decryptKmStoreKm(const unsigned char* buf, int len)
{
    
    unsigned char m[16];
    unsigned char EKm[16];
    unsigned char out[16];
    AES_KEY dec_key;
    
    memcpy(EKm, buf, 16);
    memcpy(m, buf+16, 16);
    
    memset(Km,0,sizeof(Km));
    memset(out,0,sizeof(out));
    if(AES_set_decrypt_key(Kh, 128, &dec_key) < 0){
        ALOGE("decryptKmStoreKm:AES_set_decrypt_key failed.");
        return -1;
    }

    AES_cbc_encrypt(EKm, out, sizeof(EKm), &dec_key, m, AES_DECRYPT);
    memcpy(Km,out,sizeof(out));
    
    return OK;

}

#define NULL_BYTES \
   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00

#define KD_SIZE 32

status_t DeviceKeyset::computeKd()
{
    
    EVP_CIPHER_CTX ctx;
    unsigned char *buf = (unsigned char*)malloc(KD_SIZE);
    unsigned char iv[16];
    unsigned char input[] = { NULL_BYTES, NULL_BYTES };
    int ret;
    unsigned char tmp[100];
    int outl;
    unsigned char buf2[16];

    memcpy(iv,rtx,8);
    memset(iv+8,0,8);
    memset(buf, 0x0, KD_SIZE);
    memset(buf2, 0x0, sizeof(buf2));

    EVP_CIPHER_CTX_init(&ctx);

    ret = EVP_EncryptInit_ex(&ctx, EVP_aes_128_ctr(), NULL, Km, iv);
    CHECK(ret == 1);

    ret = EVP_EncryptUpdate(&ctx, buf, &outl, input, sizeof(input));
    CHECK(ret == 1);
    CHECK(outl == sizeof(input));

    ret = EVP_EncryptFinal_ex(&ctx, tmp, &outl);
    CHECK(ret == 1);
    CHECK(outl == 0);

    iv[sizeof(iv)-1] = 0x01; /* iv is now r_tx|0...01 */

    ret = EVP_EncryptInit_ex(&ctx, EVP_aes_128_ctr(), NULL, Km, iv);
    CHECK(ret == 1);

    ret = EVP_EncryptUpdate(&ctx, &buf[16], &outl, input, sizeof(input));
    CHECK(ret == 1);
    CHECK(outl == sizeof(input));

    ret = EVP_EncryptFinal_ex(&ctx, tmp, &outl);
    CHECK(ret == 1);
    CHECK(outl == 0);
    memcpy(Kd,buf,KD_SIZE);
    //ALOGI("Kd:");
    //hexdump(Kd,sizeof(Kd));

    iv[sizeof(iv)-1] = 0x02; //kd2
    //rn XORed with LSB 64-bits of Km.
    unsigned char key[16];
    memcpy(key,Km,sizeof(Km));
    for(int i=0;i<8;i++){
        key[i+8] ^= rn[i];
    }
    ret = EVP_EncryptInit_ex(&ctx, EVP_aes_128_ctr(), NULL, key, iv);
    CHECK(ret == 1);

    ret = EVP_EncryptUpdate(&ctx, Kd2, &outl, input, sizeof(input));
    CHECK(ret == 1);
    CHECK(outl == sizeof(input));

    ret = EVP_EncryptFinal_ex(&ctx, tmp, &outl);
    CHECK(ret == 1);
    CHECK(outl == 0);
    
    //ALOGI("Kd2:");
    //hexdump(Kd2,sizeof(Kd2));

    ret = EVP_CIPHER_CTX_cleanup(&ctx);
    CHECK(ret == 1);
    
    return OK;
    
}

status_t DeviceKeyset::computeKd2()
{
    EVP_CIPHER_CTX ctx;
    unsigned char *buf = (unsigned char*)malloc(KD_SIZE);
    unsigned char iv[16];
    unsigned char input[] = { NULL_BYTES, NULL_BYTES };
    int ret;
    unsigned char tmp[100];
    int outl;
    unsigned char buf2[16];

    memcpy(iv,rtx,8);
    memset(iv+8,0,8);
    memset(buf, 0x0, KD_SIZE);
    

    EVP_CIPHER_CTX_init(&ctx);

    
    iv[sizeof(iv)-1] = 0x02; //kd2
    //rn XORed with LSB 64-bits of Km.
    unsigned char key[16];
    memcpy(key,Km,sizeof(Km));
    for(int i=0;i<8;i++){
        key[i+8] ^= rn[i];
    }
    ret = EVP_EncryptInit_ex(&ctx, EVP_aes_128_ctr(), NULL, key, iv);
    CHECK(ret == 1);

    ret = EVP_EncryptUpdate(&ctx, Kd2, &outl, input, sizeof(input));
    CHECK(ret == 1);
    CHECK(outl == sizeof(input));

    ret = EVP_EncryptFinal_ex(&ctx, tmp, &outl);
    CHECK(ret == 1);
    CHECK(outl == 0);
    /*
    ALOGI("rn:");
    hexdump(rn,sizeof(rn));
    ALOGI("Kd2:");
    hexdump(Kd2,sizeof(Kd2));
*/
    ret = EVP_CIPHER_CTX_cleanup(&ctx);
    CHECK(ret == 1);
    
    return OK;
    
}

status_t DeviceKeyset::computeHprime()
{
    
    HMAC_CTX ctx;
    unsigned char res[200];
    unsigned int resLen;

    /* input == r_tx XOR REPEATER, but REPEATER==0 */
    //unsigned char input[] = { R_TX };

    HMAC_CTX_init(&ctx);

    HMAC_Init_ex(&ctx, Kd, KD_SIZE, EVP_sha256(), NULL);
    HMAC_Update(&ctx, rtx, sizeof(rtx));
    HMAC_Final(&ctx, res, &resLen);
    HMAC_CTX_cleanup(&ctx);
    memcpy(Hprime,res,resLen);
    //ALOGI("Hprime:resLen=%d.",resLen);
    //hexdump(Hprime,sizeof(Hprime));
    return OK;
}

status_t DeviceKeyset::computeKh()
{
    //ALOGI("DeviceKeyset::computeKh++");

    unsigned char hash[SHA256_DIGEST_LENGTH];
    unsigned char Kpriv[128];
    SHA256_CTX sha256;

    memset(Kpriv,0,sizeof(Kpriv));
    getKpriv(Kpriv);
    //ALOGI("Kpriv:");
    //hexdump(Kpriv,sizeof(Kpriv));

    SHA256_Init(&sha256);
    SHA256_Update(&sha256, Kpriv, sizeof(Kpriv));
    SHA256_Final(hash, &sha256);
    memcpy(Kh,hash,16);
    memset(Kpriv,0,sizeof(Kpriv));
    //ALOGI("Kh:");
    //hexdump(Kh, sizeof(Kh));
    
    return OK;
}

status_t DeviceKeyset::computeKs(const unsigned char* pbuf)
{
    unsigned char EKs[16];
    unsigned char tmpKd2[16];

    computeKd2();
    
    memcpy(EKs,pbuf,16);
    memcpy(riv,pbuf+16,8);

    //rrx XORed with LSB 64bits Kd2
    memcpy(tmpKd2,Kd2,16);
    for(int i=0;i<8;i++){
        tmpKd2[i+8] ^= rrx[i];    
    }
    for(int i=0;i<16;i++){
        Ks[i] = EKs[i] ^ tmpKd2[i];    
    }

   
    return OK;
}
status_t DeviceKeyset::encryptKm(unsigned char EKm[16])
{
    unsigned char m[16];
    AES_KEY enc_key;
    unsigned char out[128];
    
    memset(m,0,sizeof(m));
    memcpy(m,rtx,8);
    
    memset(out,0,sizeof(out));
    AES_set_encrypt_key(Kh, 128, &enc_key);
    AES_cbc_encrypt(Km, out, sizeof(Km), &enc_key, m, AES_ENCRYPT);
    memcpy(EKm,out,sizeof(EKm));
    //ALOGI("EKm:");
    //hexdump(out,16);
    
    
    return OK;
}

status_t DeviceKeyset::computeLprime()
{
    
    HMAC_CTX ctx;
    unsigned char res[200];
    unsigned int resLen;
    unsigned char key[32];
   
    //rrx XORed with LSB 64-bits of Kd.
    memcpy(key,Kd,sizeof(Kd));
    for(int i=0;i<8;i++){
        key[i+24] ^= rrx[i];
    }
    HMAC_CTX_init(&ctx);
    HMAC_Init_ex(&ctx, key, KD_SIZE, EVP_sha256(), NULL);
    HMAC_Update(&ctx, rn, sizeof(rn));
    HMAC_Final(&ctx, res, &resLen);
    HMAC_CTX_cleanup(&ctx);
    memcpy(Lprime,res,resLen);
    //ALOGI("Lprime:resLen=%d.",resLen);
    //hexdump(Hprime,sizeof(Hprime));
    return OK;
}

status_t DeviceKeyset::getKpriv(unsigned char Kpriv[128])
{
    
    if(rsa == NULL)
        return -1;
    
    BIGNUM *d = BN_new();
    BIGNUM *e = BN_new();
    BIGNUM *p1 = BN_new();
    BIGNUM *q1 = BN_new();
    BIGNUM *phi = BN_new();
    BN_CTX *ctx = BN_CTX_new();

    BN_dec2bn (&e, "65537");
    // p1 = p-1 
    BN_sub(p1, rsa->p, BN_value_one());
    // q1 = q-1 
    BN_sub(q1, rsa->q, BN_value_one());
    // phi(pq) = (p-1)*(q-1) 
    BN_mul(phi, p1, q1, ctx);
    // d = e^-1 mod phi 
    //ALOGI("BN_mod_inverse:");
    //ALOGI("e:");
    //hexdump(rsa->e,rsa->e->dmax);
    //ALOGI("phi:");
    //hexdump(phi,phi->dmax);
    BN_mod_inverse(d, rsa->e, phi, ctx);
    BN_bn2bin(d, Kpriv);

    BN_CTX_free (ctx);
    BN_clear_free (phi);
    BN_clear_free (p1);
    BN_clear_free (q1);
    BN_clear_free (e);

    return OK;

}





void  DeviceKeyset::setAkeTransmitterInfo(AKE_Transmitter_Info* pInfo)
{
    pTransInfo = pInfo;
}

}// namespace android