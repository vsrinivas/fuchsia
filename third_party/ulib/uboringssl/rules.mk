# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := userlib

MODULE_COMPILEFLAGS += -fvisibility=hidden -Wa,--noexecstack

MODULE_SRCS := \
    $(LOCAL_DIR)/crypto/cpu-aarch64-zircon.cpp \

MODULE_SRCS += \
   $(LOCAL_DIR)/crypto/mem.c \
   $(LOCAL_DIR)/crypto/err/err.c \
   $(LOCAL_DIR)/crypto/err/err_data.c \
   $(LOCAL_DIR)/crypto/cpu-intel.c \
   $(LOCAL_DIR)/crypto/chacha/chacha.c \
   $(LOCAL_DIR)/crypto/cipher_extra/e_aesgcmsiv.c \
   $(LOCAL_DIR)/crypto/crypto.c \
   $(LOCAL_DIR)/crypto/fipsmodule/aes/aes.c \
   $(LOCAL_DIR)/crypto/fipsmodule/aes/mode_wrappers.c \
   $(LOCAL_DIR)/crypto/fipsmodule/cipher/aead.c \
   $(LOCAL_DIR)/crypto/fipsmodule/cipher/cipher.c \
   $(LOCAL_DIR)/crypto/fipsmodule/cipher/e_aes.c \
   $(LOCAL_DIR)/crypto/fipsmodule/digest/digests.c \
   $(LOCAL_DIR)/crypto/fipsmodule/digest/digest.c \
   $(LOCAL_DIR)/crypto/fipsmodule/hmac/hmac.c \
   $(LOCAL_DIR)/crypto/fipsmodule/md4/md4.c \
   $(LOCAL_DIR)/crypto/fipsmodule/md5/md5.c \
   $(LOCAL_DIR)/crypto/fipsmodule/modes/cbc.c \
   $(LOCAL_DIR)/crypto/fipsmodule/modes/cfb.c \
   $(LOCAL_DIR)/crypto/fipsmodule/modes/ctr.c \
   $(LOCAL_DIR)/crypto/fipsmodule/modes/gcm.c \
   $(LOCAL_DIR)/crypto/fipsmodule/modes/ofb.c \
   $(LOCAL_DIR)/crypto/fipsmodule/modes/polyval.c \
   $(LOCAL_DIR)/crypto/fipsmodule/rand/ctrdrbg.c \
   $(LOCAL_DIR)/crypto/fipsmodule/rand/rand.c \
   $(LOCAL_DIR)/crypto/fipsmodule/sha/sha512.c \
   $(LOCAL_DIR)/crypto/fipsmodule/sha/sha1.c \
   $(LOCAL_DIR)/crypto/fipsmodule/sha/sha256.c \
   $(LOCAL_DIR)/crypto/hkdf/hkdf.c \
   $(LOCAL_DIR)/crypto/rand_extra/forkunsafe.c \
   $(LOCAL_DIR)/crypto/rand_extra/fuchsia.c \
   $(LOCAL_DIR)/crypto/thread_none.c \
   $(LOCAL_DIR)/decrepit/xts/xts.c \

ifeq ($(ARCH),arm64)
# TODO(aarongreen): Workaround for the non-hidden OPENSSL_armcap_P symbol, which causes arm/clang to
# fail to link.  Remove when resolved upstream.
MODULE_COMPILEFLAGS += -DOPENSSL_NO_ASM

MODULE_SRCS += \
    $(LOCAL_DIR)/asm/aes-arm64.S \
    $(LOCAL_DIR)/asm/chacha-arm64.S \
    ${LOCAL_DIR}/asm/ghash-arm64.S \
    $(LOCAL_DIR)/asm/sha1-arm64.S \
    $(LOCAL_DIR)/asm/sha256-arm64.S \
    $(LOCAL_DIR)/asm/sha512-arm64.S \

else ifeq ($(ARCH),x86)
MODULE_SRCS += \
    $(LOCAL_DIR)/asm/aes-x86-64.S \
    $(LOCAL_DIR)/asm/aes128gcmsiv-x86-64.S \
    ${LOCAL_DIR}/asm/aesni-x86-64.S \
    ${LOCAL_DIR}/asm/aesnigcm-x86-64.S \
    ${LOCAL_DIR}/asm/bsaes-x86-64.S \
    $(LOCAL_DIR)/asm/chacha-x86-64.S \
    ${LOCAL_DIR}/asm/ghash-x86-64.S \
    $(LOCAL_DIR)/asm/md5-x86-64.S \
    ${LOCAL_DIR}/asm/rdrand-x86-64.S \
    $(LOCAL_DIR)/asm/sha1-x86-64.S \
    $(LOCAL_DIR)/asm/sha256-x86-64.S \
    $(LOCAL_DIR)/asm/sha512-x86-64.S\
    ${LOCAL_DIR}/asm/vpaes-x86-64.S \

endif

MODULE_NAME := uboringssl

MODULE_LIBS := \
    system/ulib/fdio \
    system/ulib/c \

include make/module.mk
