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
#define LOG_TAG "hdcptest"

#undef NDEBUG 
#define LOG_NDEBUG   0  
#define LOG_NIDEBUG  0  
#define LOG_NDDEBUG 0  
#include <utils/Log.h>
#include <media/stagefright/foundation/hexdump.h>
#include <binder/ProcessState.h>
#include <binder/IServiceManager.h>
#include <gui/ISurfaceComposer.h>
#include <gui/SurfaceComposerClient.h>
#include <media/AudioSystem.h>
#include <media/IMediaPlayerService.h>
#include <media/IRemoteDisplay.h>
#include <media/IRemoteDisplayClient.h>
#include <media/stagefright/DataSource.h>
#include <media/stagefright/foundation/ADebug.h>
#include <media/stagefright/foundation/AMessage.h>
#include <ui/DisplayInfo.h>
#include <utils/RefBase.h>
#include <utils/Thread.h>
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

#include "openssl/ssl.h"
#include "openssl/rsa.h"
#include "openssl/evp.h"
#include "openssl/bio.h"
#include "openssl/err.h"
#include "openssl/pem.h"

 #include <media/stagefright/foundation/hexdump.h>
//#include <stdio.h>
//#include <string.h>

void genkey1(){
    using namespace android;

    RSA           *rsa;
    int            n;
    unsigned char text[128] = "Hello world!"; 
    unsigned char cipher[128];
    unsigned char decipher[128];
    unsigned char  rsa_n[128], rsa_d[128];
    BIGNUM          *bne = NULL;
    int             ret = 0;
    unsigned long   e = RSA_F4;

    ERR_load_crypto_strings();  
    
    
    // 生成密钥
    bne = BN_new();
    ret = BN_set_word(bne,e);
    if(ret != 1){
        return ;
    }
 
    rsa = RSA_new();
    ret = RSA_generate_key_ex(rsa, 1024, bne, NULL);
    if(ret != 1 ){
        ALOGE("RSA_generate_key_ex:failed.");
        return ;
    }

    
    n = RSA_size( rsa );
    BN_bn2bin( rsa->n, rsa_n ); // 保存公钥
    BN_bn2bin( rsa->d, rsa_d ); // 保存私钥
    RSA_free( rsa );
    ALOGI("RSA_size=%d.", n);
    ALOGI("rsa_n:top=%d,dmax=%d,neg=%d,flags=%d.",
          rsa->n->top,
          rsa->n->dmax,
          rsa->n->neg,
          rsa->n->flags);
    
    ALOGI("rsa_d:top=%d,dmax=%d,neg=%d,flags=%d.",
          rsa->d->top,
          rsa->d->dmax,
          rsa->d->neg,
          rsa->d->flags);
    hexdump(rsa_n, sizeof(rsa_n));
    ALOGI("private exponent:rsa_d:");
    hexdump(rsa_d, sizeof(rsa_d));
    // 加密的过程
    rsa = RSA_new();
    if( NULL == rsa ){
        ALOGE("RSA_new failed.");
        return ;
    }
    
    // 设置公钥
    rsa->n = BN_bin2bn( rsa_n, sizeof(rsa_n), rsa->n );
    rsa->e = BN_bin2bn( (unsigned char*)"\x01\x00\x01", 3, rsa->e );
    // 设置明文并加密
    n = RSA_public_encrypt(strlen((char*)text), text, cipher, rsa, RSA_PKCS1_OAEP_PADDING);
    if(n == -1){
        ALOGE("RSA_public_encrypt failed.err=%s.",ERR_error_string(ERR_get_error(),NULL));
    }else{
        ALOGI("RSA_public_encrypt:len=%d.", n);
        hexdump(cipher, sizeof(cipher));
    }
    RSA_free( rsa );
    
    // 解密的过程
    rsa = RSA_new();
    if( NULL == rsa )
    {
        ALOGE("RSA_new failed.");
        return ;
    }
    rsa->n = BN_bin2bn(rsa_n, sizeof(rsa_n), rsa->n );
    rsa->e = BN_bin2bn( (unsigned char*)"\x01\x00\x01", 3, rsa->e );
    rsa->d = BN_bin2bn( rsa_d, sizeof(rsa_d), rsa->d );
    
    // 解密数据
    n = RSA_private_decrypt(sizeof(cipher), cipher, decipher, rsa, RSA_PKCS1_OAEP_PADDING );
    if(n == -1){
        ALOGE("RSA_private_decrypt failed.err=%s.",ERR_error_string(ERR_get_error(),NULL));
    }else{
        ALOGI("RSA_private_decrypt:len=%d.", n);
        hexdump(decipher, n);
    }
    ALOGI("RSA decrypt:end.");
    RSA_free(rsa);
    return ;
    
}

int dumpPrivateKey(RSA  *rsa)
{
    using namespace android;

   unsigned char *buffera[8];
   buffera[0] = (unsigned char *)malloc(BN_num_bytes(rsa->n));
   buffera[1] = (unsigned char *)malloc(BN_num_bytes(rsa->e));
   buffera[2] = (unsigned char *)malloc(BN_num_bytes(rsa->d));
   buffera[3] = (unsigned char *)malloc(BN_num_bytes(rsa->p));
   buffera[4] = (unsigned char *)malloc(BN_num_bytes(rsa->q));
   buffera[5] = (unsigned char *)malloc(BN_num_bytes(rsa->iqmp));
   buffera[6] = (unsigned char *)malloc(BN_num_bytes(rsa->dmp1));
   buffera[7] = (unsigned char *)malloc(BN_num_bytes(rsa->dmq1));

   BN_bn2bin(rsa->n, buffera[0]);
   BN_bn2bin(rsa->e, buffera[1]);
   BN_bn2bin(rsa->d, buffera[2]);
   BN_bn2bin(rsa->p, buffera[3]);
   BN_bn2bin(rsa->q, buffera[4]);
   BN_bn2bin(rsa->iqmp, buffera[5]);
   BN_bn2bin(rsa->dmp1, buffera[6]);
   BN_bn2bin(rsa->dmq1, buffera[7]);
   ALOGI("RSA key:n:len=%d.", BN_num_bytes(rsa->n));
   hexdump(buffera[0], BN_num_bytes(rsa->n));
   ALOGI("RSA key:e:len=%d.", BN_num_bytes(rsa->e));
   hexdump(buffera[1], BN_num_bytes(rsa->e));
   ALOGI("RSA key:d:len=%d.", BN_num_bytes(rsa->d));
   hexdump(buffera[2], BN_num_bytes(rsa->d));
   ALOGI("RSA key:p:len=%d.", BN_num_bytes(rsa->p));
   hexdump(buffera[3], BN_num_bytes(rsa->p));
   ALOGI("RSA key:q:len=%d.", BN_num_bytes(rsa->q));
   hexdump(buffera[4], BN_num_bytes(rsa->q));
   ALOGI("RSA key:iqmp:len=%d.", BN_num_bytes(rsa->iqmp));
   hexdump(buffera[5], BN_num_bytes(rsa->iqmp));
   ALOGI("RSA key:dmp1:len=%d.", BN_num_bytes(rsa->dmp1));
   hexdump(buffera[6], BN_num_bytes(rsa->dmp1));
   ALOGI("RSA key:dmq1:len=%d.", BN_num_bytes(rsa->dmq1));
   hexdump(buffera[7], BN_num_bytes(rsa->dmq1));
   

   for (int i = 0; i < 8; i++) free(buffera[i]);
   return 0;
}
void genkey12(){
    using namespace android;

    RSA           *rsa;
    int            n;
    unsigned char text[16] = "123456789012345"; 
    unsigned char cipher[128];
    unsigned char decipher[128];
    unsigned char rsa_n[128];
    unsigned char rsa_d[128];
    unsigned char rsa_p[64];
    unsigned char rsa_q[64];
    BIGNUM          *bne = NULL;
    int             ret = 0;
    unsigned long   e = RSA_F4;

    ERR_load_crypto_strings();  
    
    
    // 生成密钥
    bne = BN_new();
    ret = BN_set_word(bne,e);
    if(ret != 1){
        return ;
    }
 
    rsa = RSA_new();
    ret = RSA_generate_key_ex(rsa, 1024, bne, NULL);
    if(ret != 1 ){
        ALOGE("RSA_generate_key_ex:failed.");
        return ;
    }

    dumpPrivateKey(rsa);

    int len,len1;
    unsigned char *p, *bufkey;
    bufkey = (unsigned char *) malloc (2048);
    memset(bufkey,0xff,2048);
    p = bufkey;
    len = i2d_RSAPublicKey (rsa, (unsigned char**)&p);
    ALOGI("public key:bufkey=%x,p=%x,len=%d.", bufkey, p, len);
    hexdump(bufkey, len);
    len1 = i2d_RSAPrivateKey (rsa, (unsigned char**)&p);
    ALOGI("private key:bufkey=%x,p=%x,len=%d.", bufkey, p, len1);
    hexdump(bufkey+len, len1);
    ALOGI("bufkey=%x.",bufkey);
    hexdump(bufkey, len+len1);
    
    BN_bn2bin( rsa->n, rsa_n);
    BN_bn2bin( rsa->d, rsa_d);
    BN_bn2bin( rsa->p, rsa_p);
    BN_bn2bin( rsa->q, rsa_q);
    n = RSA_size( rsa );
    ALOGI("RSA_size=%d.", n);
    ALOGI("public modulus:rsa_n:");
    hexdump(rsa_n, sizeof(rsa_n));
    ALOGI("private exponent:rsa_d:");
    hexdump(rsa_d, sizeof(rsa_d));
    ALOGI("rsa->n:top=%d,dmax=%d,neg=%d,flags=%d.",
          rsa->n->top,
          rsa->n->dmax,
          rsa->n->neg,
          rsa->n->flags);
    hexdump(rsa->n->d, rsa->n->top*4);
    ALOGI("rsa->d:top=%d,dmax=%d,neg=%d,flags=%d.",
          rsa->d->top,
          rsa->d->dmax,
          rsa->d->neg,
          rsa->d->flags);
    hexdump(rsa->d->d, rsa->d->top*4);
    RSA_free( rsa );


    rsa = RSA_new();
    if( NULL == rsa ){
        ALOGE("RSA_new failed.");
        return ;
    }
    

    rsa->n = BN_bin2bn( rsa_n, sizeof(rsa_n), rsa->n );
    rsa->e = BN_bin2bn( (unsigned char*)"\x01\x00\x01", 3, rsa->e );
    // 设置明文并加密
    n = RSA_public_encrypt(strlen((char*)text), text, cipher, rsa, RSA_PKCS1_OAEP_PADDING);
    if(n == -1){
        ALOGE("RSA_public_encrypt failed.err=%s.",ERR_error_string(ERR_get_error(),NULL));
    }else{
        ALOGI("RSA_public_encrypt:len=%d.", n);
        hexdump(cipher, sizeof(cipher));
    }
    RSA_free( rsa );
    
    //decrypt

    // 解密的过程
    rsa = RSA_new();
    if( NULL == rsa )
    {
        ALOGE("RSA_new failed.");
        return ;
    }
    rsa->n = BN_bin2bn(rsa_n, sizeof(rsa_n), rsa->n );
    rsa->e = BN_bin2bn( (unsigned char*)"\x01\x00\x01", 3, rsa->e );
    //rsa->d = BN_bin2bn( rsa_d, sizeof(rsa_d), rsa->d );
    /*
    for(int i=0;i<(len+len1-128);i++){
        rsa->d = BN_bin2bn( bufkey+i, 128, rsa->d );
        // 解密数据
        n = RSA_private_decrypt(sizeof(cipher), cipher, decipher, rsa, RSA_PKCS1_OAEP_PADDING );
        if(n == -1){
            ALOGE("RSA_private_decrypt failed.err=%s.",ERR_error_string(ERR_get_error(),NULL));
        }else{
            ALOGI("RSA_private_decrypt:len=%d.", n);
            hexdump(decipher, n);
            break;
        }
    }
    */
    rsa->d = BN_bin2bn(rsa_d,128,rsa->d);
    rsa->p = BN_bin2bn(rsa_p,64,rsa->p);
    rsa->q = BN_bin2bn(rsa_q,64,rsa->q);
    n = RSA_private_decrypt(sizeof(cipher), cipher, decipher, rsa, RSA_PKCS1_OAEP_PADDING );
    if(n == -1){
        ALOGE("RSA_private_decrypt failed.err=%s.",ERR_error_string(ERR_get_error(),NULL));
    }else{
        ALOGI("RSA_private_decrypt:len=%d.", n);
        hexdump(decipher, n);
        
    }
   
    ALOGI("RSA decrypt:end.");
    RSA_free(rsa);
    return ;
    
}

void genkey2(){
    using namespace android;

    RSA           *rsa;
    int            n;
    unsigned char text[16] = {0x31,0x32,0x33,0x34,0x35,0x36,0x37,0x38,0x39,0x31,0x32,0x33,0x34,0x35,0x36,0x37}; 
    
    unsigned char decipher[128];
    unsigned char  rsa_n[128], rsa_d[128];
    BIGNUM          *bne = NULL;
    int             ret = 0;
    unsigned long   e = RSA_F4;

    ERR_load_crypto_strings();  
    
    int fd;
    ssize_t nRead;
    unsigned char buf[1024];
    unsigned char Certrx[522] ;
    unsigned char Kprivrx[128];

    unsigned char N[128];
    unsigned char P[64];
    unsigned char Q[64];
    unsigned char dmp1[64];
    unsigned char dmq1[64];
    unsigned char iqmp[64];
    
    fd = open("/system/etc/confidential", O_RDONLY);
    if(fd == -1){
        ALOGE("Devicekeyset:open confidential failed.");
        return ;
    }
    nRead = read(fd, &buf, sizeof(buf));
    if(nRead == 902){
      ALOGI("read confidential.");
      memcpy(N, buf+45, sizeof(N));
      memcpy(P, buf+562, sizeof(P));
      memcpy(Q, buf+626, sizeof(Q));
      memcpy(dmp1, buf+690, sizeof(dmp1));
      memcpy(dmq1, buf+754, sizeof(dmq1));
      memcpy(iqmp, buf+818, sizeof(iqmp));
    }else{
        ALOGE("Devicekeyset:wrong file length .");
        close(fd);
        return ;
    }
    close(fd);
    ALOGI("got confidential.");
    
   
    rsa = RSA_new();
    if( NULL == rsa ){
        ALOGE("RSA_new failed.");
        return ;
    }
    int lenCiphered;
    rsa->n = BN_bin2bn( N, sizeof(N), rsa->n );
    rsa->e = BN_bin2bn( (unsigned char*)"\x01\x00\x01", 3, rsa->e );
    rsa->p = BN_bin2bn( P, sizeof(P), rsa->p );
    rsa->q = BN_bin2bn( Q, sizeof(Q), rsa->q );
    rsa->dmp1 = BN_bin2bn( dmp1, sizeof(dmp1), rsa->dmp1 );
    rsa->dmq1 = BN_bin2bn( dmq1, sizeof(dmq1), rsa->dmq1 );
    rsa->iqmp = BN_bin2bn( iqmp, sizeof(iqmp), rsa->iqmp );
    
    /*
    // encrypt
    unsigned char cipher[128];
    lenCiphered = RSA_public_encrypt(16, text, cipher, rsa,  RSA_PKCS1_OAEP_PADDING);
    if(lenCiphered == -1){
        ALOGE("RSA_public_encrypt failed.err=%s.",ERR_error_string(ERR_get_error(),NULL));
    }else{
        ALOGI("RSA_public_encrypt:len=%d.", lenCiphered);
        hexdump(cipher, sizeof(cipher));
    }*/
    /*
    unsigned char cipher[128] = {
    0x46,0x36,0x42,0x3c,0xe0,0xac,0xc7,0xe0,0xf0,0xa6,0x8d,0x30,0xd6,0x6b,0x8e,0x1b,\
    0x0c,0xc9,0xee,0xe9,0xf1,0xc0,0x9b,0x7d,0xed,0x1f,0x97,0x24,0x93,0xf8,0x07,0x04,\
    0xca,0x0e,0x27,0xf8,0x67,0x37,0xe0,0x76,0x56,0x1a,0x68,0x9e,0x9b,0x0b,0x27,0x17,\
    0x86,0x63,0xbd,0xfc,0x09,0x44,0x1a,0xc8,0x4c,0x03,0xe0,0xa5,0x7b,0x02,0x6f,0x20,\
    0x29,0xde,0x18,0xae,0xc3,0x35,0xe3,0x46,0xcb,0xc0,0x42,0x01,0x15,0x4a,0xc9,0x41,\
    0x01,0xbf,0xa1,0x08,0x60,0x82,0xfb,0x16,0xac,0x2f,0xf2,0x6e,0xcf,0x02,0x22,0x5b,\
    0xa0,0xf2,0x88,0x5d,0x18,0x9c,0xc1,0x4a,0xde,0x83,0x17,0x5d,0xd7,0xe8,0xe8,0x69,\
    0xa7,0x2a,0x82,0x37,0xa6,0x2d,0x37,0x6c,0xd6,0x71,0x84,0x77,0x5c,0x75,0x80,0x81
    };
    lenCiphered = 128;
    */
    
    unsigned char cipher[128] = {
    0x6e,0x50,0xdb,0xfe,0xf7,0xa3,0x33,0xef,0x02,0x7f,0x0b,0xca,0x07,0xe6,0x01,0xac, \
    0x1c,0x57,0x8a,0x80,0xe6,0x9c,0xd2,0x19,0x46,0xdf,0x3b,0xc9,0x12,0x86,0xb0,0x16,\
    0x32,0x0b,0x54,0x37,0x0c,0x37,0xf2,0xdf,0x65,0x31,0x1c,0xbe,0x7e,0xb9,0x06,0x8b,\
    0x95,0x01,0x79,0x61,0x58,0x9d,0x9f,0x84,0x37,0xc1,0x3b,0x35,0x4c,0x9a,0x98,0x67,\
    0xea,0x4e,0x11,0xc8,0x5b,0x10,0x10,0x99,0xdc,0xdd,0x85,0xad,0xf0,0xc1,0xa5,0xa8,\
    0xda,0xcc,0x0f,0x61,0x5f,0xb5,0xdd,0x1b,0xc5,0x53,0x7b,0x0f,0x75,0x38,0x7e,0x4e,\
    0x2b,0x72,0xfe,0xb8,0x0e,0x6f,0x7e,0x77,0x55,0x1e,0x98,0x76,0x10,0x3a,0xc8,0x13,\
    0xa1,0x1b,0xc0,0xac,0x54,0xee,0xd2,0x89,0x2c,0xdd,0x54,0xbe,0xca,0xb3,0xc5,0x13
    };
    lenCiphered = 128;

    n = RSA_private_decrypt(lenCiphered, cipher, decipher, rsa,  RSA_PKCS1_OAEP_PADDING );
    if(n == -1){
        ALOGE("RSA_private_decrypt failed.err=%s.",ERR_error_string(ERR_get_error(),NULL));
    }else{
        ALOGI("RSA_private_decrypt succeeded:len=%d.", n);
        hexdump(decipher, n);
    }
    n = RSA_private_decrypt(lenCiphered, cipher, decipher, rsa,  RSA_PKCS1_PADDING );
    if(n == -1){
        ALOGE("RSA_private_decrypt failed.err=%s.",ERR_error_string(ERR_get_error(),NULL));
    }else{
        ALOGI("RSA_private_decrypt succeeded:len=%d.", n);
        hexdump(decipher, n);
    }
    n = RSA_private_decrypt(lenCiphered, cipher, decipher, rsa,  RSA_SSLV23_PADDING );
    if(n == -1){
        ALOGE("RSA_private_decrypt failed.err=%s.",ERR_error_string(ERR_get_error(),NULL));
    }else{
        ALOGI("RSA_private_decrypt succeeded:len=%d.", n);
        hexdump(decipher, n);
    }
    n = RSA_private_decrypt(lenCiphered, cipher, decipher, rsa,  RSA_NO_PADDING );
    if(n == -1){
        ALOGE("RSA_private_decrypt failed.err=%s.",ERR_error_string(ERR_get_error(),NULL));
    }else{
        ALOGI("RSA_private_decrypt succeeded:len=%d.", n);
        hexdump(decipher, n);
    }
   
    RSA_free(rsa);
    ALOGI("RSA decrypt:end.");
    
    return ;
    
}

void genkey3(){
    using namespace android;
    
    
    int             ret = 0;
    RSA             *r = NULL;
    BIGNUM          *bne = NULL;
    BIO             *bp_public = NULL, *bp_private = NULL;
 
    int             bits = 1024;
    unsigned long   e = RSA_F4;
    
    char msg[2048/8];
    char   *err;  
    char *encrypt;
    char   *decrypt = NULL;
    int encrypt_len = -1;
    int decrypt_len = -1;
    
    
    // 1. generate rsa key
    bne = BN_new();
    ret = BN_set_word(bne,e);
    if(ret != 1){
        return;
    }
 
    r = RSA_new();
    ret = RSA_generate_key_ex(r, bits, bne, NULL);
    if(ret != 1){
        return;
    }
    ALOGI("RSA r:len=%d.",RSA_size(r));
    hexdump(r, RSA_size(r));

    int len,len1;
    unsigned char *p, *bufkey;
    bufkey = (unsigned char *) malloc (2048);
    memset(bufkey,0xff,2048);
    p = bufkey;
    len = i2d_RSAPublicKey (r, (unsigned char**)&p);
    ALOGI("public key:bufkey=%x,p=%x,len=%d.", bufkey, p, len);
    hexdump(bufkey, len);
    len1 = i2d_RSAPrivateKey (r, (unsigned char**)&p);
    ALOGI("private key:bufkey=%x,p=%x,len=%d.", bufkey, p, len1);
    hexdump(bufkey+len, len1);
    
    ALOGI("bufkey=%x.",bufkey);
    hexdump(bufkey, len+len1);

    RSA *rsa, *pub_rsa, *priv_rsa;
    p = bufkey;
    len += len1;
    pub_rsa = d2i_RSAPublicKey (NULL, (const unsigned char**)&p, (long) len);
    ALOGI("d2i_RSAPublicKey:bufkey=%x,p=%x,len=%d.", bufkey, p, len);
    ALOGI("pub_rsa:len=%d.",RSA_size(pub_rsa));
    hexdump(pub_rsa, RSA_size(pub_rsa));

    len -= (p - bufkey);
    priv_rsa = d2i_RSAPrivateKey (NULL, (const unsigned char**)&p, (long) len);
    ALOGI("d2i_RSAPrivateKey:bufkey=%x,p=%x,len=%d.", bufkey, p, len);
    ALOGI("priv_rsa:len=%d.",RSA_size(priv_rsa));
    hexdump(priv_rsa, RSA_size(priv_rsa));

    if ((pub_rsa == NULL) || (priv_rsa == NULL))
        ALOGE("d2i_RSAPublicKey failed.");
    else
        ALOGI("d2i_RSAPublicKey succeeded.");

    strcpy(msg, "Hello world!");
    
    // Encrypt the message
    encrypt = (char*)malloc(RSA_size(r));
    
    err = (char*)malloc(130);
    if((encrypt_len = RSA_public_encrypt(strlen(msg)+1, (unsigned char*)msg,
       (unsigned char*)encrypt, r, RSA_PKCS1_OAEP_PADDING)) == -1) {
        ERR_load_crypto_strings();
        ERR_error_string(ERR_get_error(), err);
        ALOGE("RSA_public_encrypt failed.err=%s.", err);
        
    }
    ALOGI("encrypt data:");
    hexdump(encrypt, encrypt_len);
    
    //decrypt
    decrypt = (char*)malloc(encrypt_len);
    if((decrypt_len = RSA_private_decrypt(encrypt_len, (unsigned char*)encrypt, (unsigned char*)decrypt,
                           r, RSA_PKCS1_OAEP_PADDING)) == -1) {
        ERR_load_crypto_strings();
        ERR_error_string(ERR_get_error(), err);
        ALOGE("RSA_private_decrypt failed.err=%s.", err);
        
    }
    ALOGI("decrypt data:%s,decrypt_len=%d.", decrypt, decrypt_len);
    hexdump(decrypt, decrypt_len);

    free(bufkey);
    RSA_free(r);
    BN_free(bne);

    /*
    // 2. save public key
    bp_public = BIO_new_file("public.pem", "w+");
    ret = PEM_write_bio_RSAPublicKey(bp_public, r);
    if(ret != 1){
        goto free_all;
    }
 
    // 3. save private key
    bp_private = BIO_new_file("private.pem", "w+");
    ret = PEM_write_bio_RSAPrivateKey(bp_private, r, NULL, NULL, 0, NULL, NULL);
 
    BIO_free_all(bp_public);
    BIO_free_all(bp_private);
    */

}
int main( int argc, char *argv[] )
{
    using namespace android;
    //genkey1();
    unsigned long long l=0;
    char ctr[8];
    char *endptr;

    ctr[0] = 0;
    ctr[1] = 0;
    ctr[2] = 0;
    ctr[3] = 0;
    ctr[4] = 0;
    ctr[5] = 0;
    ctr[6] = 0x01;
    ctr[7] = 0x2d;

    l = (ctr[0] << 56) |
        (ctr[1] << 48) |
        (ctr[2] << 40) |
        (ctr[3] <<  32) |
        (ctr[4] <<  24) |
        (ctr[5] <<  16) |
        (ctr[6] <<  8) |
        (ctr[7]);

    for(int i=0;i<16;i++){
        ALOGI("ctr:c=%02x%02x%02x%02x%02x%02x%02x%02x,l=%llx",
               ctr[0],ctr[1],ctr[2],ctr[3],
               ctr[4],ctr[5],ctr[6],ctr[7],l);
        l++;
        ctr[0] = l >> 56;
        ctr[1] = l >> 48;
        ctr[2] = l >> 40;
        ctr[3] = l >> 32;
        ctr[4] = l >> 24;
        ctr[5] = l >> 16;
        ctr[6] = l >> 8;
        ctr[7] = l ;

        
        
    }

    return 0;
    
}

//bool generate_key()
//{
    

    
/*
    // 2. save public key
    bp_public = BIO_new_file("public.pem", "w+");
    ret = PEM_write_bio_RSAPublicKey(bp_public, r);
    if(ret != 1){
        goto free_all;
    }
 
    // 3. save private key
    bp_private = BIO_new_file("private.pem", "w+");
    ret = PEM_write_bio_RSAPrivateKey(bp_private, r, NULL, NULL, 0, NULL, NULL);
 */
    // 4. free
//free_all:
 
    //BIO_free_all(bp_public);
    //BIO_free_all(bp_private);
    
 
//    return (ret == 1);
//}
/* 
int main(int argc, char* argv[]) 
{
    using namespace android;
    generate_key();

    return 0;
}
*/

/*
int main(int argc, char **argv) {
    using namespace android;
    
    ALOGI("hcptest:+");

    ProcessState::self()->startThreadPool();

   
    int fd;
  	ssize_t n;
  	unsigned char buf[1024];
  	unsigned char Certrx[522] ;
    unsigned char Kprivrx[340];
    unsigned char Kpubrx[131];
    char data[2048/8] = "Hello this is Ravi"; 
    unsigned char outData[4098] = {};
    unsigned char outData2[4098] = {};
    unsigned long outLen, outLen2;

    fd = open("/system/etc/confidential", O_RDONLY);
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
    

    n = read(fd, &buf, sizeof(buf));
    if(n == 902){
    	//copy cert
    	memcpy(&Certrx, buf+0x28, sizeof(Certrx));
    	//copy kpubrx
    	memcpy(&Kpubrx, buf+0x2d, sizeof(Kpubrx));
    	//copy kprivrx
    	memcpy(&Kprivrx, buf+0x28+522, keysetSize-522-0x28);
    	
    	
    }
    close(fd);

    Crypto::importRSAPublicKey(Kpubrx,131);
    Crypto::importRSAPrivateKey(Kprivrx,340);
    memset(outData,0,sizeof(outData));
    ALOGI("hdcptest:orignal:");  
    hexdump(data, sizeof(data));
    Crypto::public_encrypt((unsigned char*)data, strlen(data),
                           (unsigned char*)outData, &outLen);
    ALOGI("hdcptest:encrypted:");
    hexdump(outData, outLen);
    memset(outData2,0,sizeof(outData2));
    //Crypto::private_decrypt((unsigned char*)outData, outLen, (unsigned char*)outData2, &outLen2);
    ALOGI("hdcptest:decrypted:");
    hexdump(outData2, outLen2);
    ALOGI("hcptest:-");    

    return 0;
}
*/