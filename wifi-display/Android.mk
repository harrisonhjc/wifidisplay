LOCAL_PATH:= $(call my-dir)

include $(CLEAR_VARS)

LOCAL_SRC_FILES:= \
        ANetworkSession.cpp             \
        Parameters.cpp                  \
        ParsedMessage.cpp               \
        sink/WifiDisplaySink.cpp        \
        sink/ContentRecv.cpp            \
        TimeSeries.cpp                  \
        mmx/MmxRecv.cpp                \
        mmx/MmxUtil.cpp                \
        mmx/MmxM2ts.cpp                \
        mmx/MmxVideoDecoder.cpp        \
        mmx/MmxVideoScheduler.cpp      \
        mmx/MmxVideoRenderer.cpp       \
        mmx/MmxPcr.cpp                 \
        mmx/MmxSlicePack.cpp           \
        mmx/MmxAudioDecoder.cpp        \
        mmx/MmxAudioScheduler.cpp      \
        mmx/MmxAudioRenderer.cpp       \
        mmx/AVFormatSource.cpp         \
        
        
        



LOCAL_C_INCLUDES:= \
        $(TOP)/frameworks/av/media/libstagefright \
        $(TOP)/frameworks/native/include/media/openmax \
        $(TOP)/frameworks/av/media/libstagefright/mpeg2ts \
        $(TOP)/frameworks/wilhelm/include         \
        bionic \
        bionic/libstdc++/include \
        external/gtest/include \
        external/stlport/stlport \
        external/openssl/include  \

LOCAL_SHARED_LIBRARIES:= \
        libbinder                       \
        libcutils                       \
        libgui                          \
        libmedia                        \
        libstagefright                  \
        libstagefright_foundation       \
        libui                           \
        libutils                        \
        libOpenSLES                     \
        libOpenMAXAL                    \
        libnetutils                     \
        libAmmboxCrypto                 \
        libAmmboxHdcp                   \
        libandroid                      \
        libAmmboxUibc                   \
        

LOCAL_MODULE:= libAmmboxCore

LOCAL_MODULE_TAGS:= optional

include $(BUILD_SHARED_LIBRARY)
################################################################################
include $(CLEAR_VARS)

LOCAL_SRC_FILES:= \
        hdcpReceiver/HdcpReceiver.cpp           \
        hdcpReceiver/DeviceKeyset.cpp            \
        


LOCAL_C_INCLUDES:= \
        $(TOP)/frameworks/av/media/libstagefright \
        $(TOP)/external/openssl/include         \
        

LOCAL_SHARED_LIBRARIES:= \
        libbinder                       \
        libcutils                       \
        libgui                          \
        libmedia                        \
        libstagefright_foundation       \
        libui                           \
        libutils                        \
        libAmmboxCrypto                 \



LOCAL_MODULE:= libAmmboxHdcp

LOCAL_MODULE_TAGS:= optional

include $(BUILD_SHARED_LIBRARY)
################################################################################
include $(CLEAR_VARS)

LOCAL_SRC_FILES:= \
        uibcSink/uibcSink.cpp           \
        

LOCAL_C_INCLUDES:= \
        $(TOP)/frameworks/av/media/libstagefright \
        $(TOP)/external/openssl/include         \
        

LOCAL_SHARED_LIBRARIES:= \
        libbinder                       \
        libcutils                       \
        libgui                          \
        libmedia                        \
        libstagefright_foundation       \
        libui                           \
        libutils                        \
        libandroid                      \
        
LOCAL_MODULE:= libAmmboxUibc

LOCAL_MODULE_TAGS:= optional

include $(BUILD_SHARED_LIBRARY)
################################################################################
include $(CLEAR_VARS)

LOCAL_SRC_FILES:= \
        hdcptest.cpp                 \

LOCAL_C_INCLUDES:= \
        external/openssl/include

LOCAL_SHARED_LIBRARIES:= \
        libbinder                       \
        libgui                          \
        libmedia                        \
        libstagefright                  \
        libstagefright_foundation       \
        libutils                        \
        liblog                          \
        libAmmboxCrypto                 \

        
LOCAL_MODULE:= hdcptest

LOCAL_MODULE_TAGS := debug

include $(BUILD_EXECUTABLE)

################################################################################
include $(CLEAR_VARS)

LOCAL_SRC_FILES:= \
        mpegtsParserTest/bitreader.cpp      \
        mpegtsParserTest/tsparser.cpp      \
        mpegtsParserTest/tsunpacker.cpp      \

LOCAL_C_INCLUDES:= \
        external/openssl/include

LOCAL_SHARED_LIBRARIES:= \
        libbinder                       \
        libgui                          \
        libmedia                        \
        libstagefright                  \
        libstagefright_foundation       \
        libutils                        \
        liblog                          \
        libAmmboxCrypto                 \

        
LOCAL_MODULE:= tsparser

LOCAL_MODULE_TAGS := debug

include $(BUILD_EXECUTABLE)

################################################################################
