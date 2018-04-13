# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Variables shared between the userlib and hostlib
LOCAL_DIR := $(GET_LOCAL_DIR)

SHARED_COMPILEFLAGS := \
    -fvisibility=hidden -Wno-unused-function -include $(LOCAL_DIR)/stack-note.S

CRYPTO_DIR=$(LOCAL_DIR)/crypto
SHARED_SRCS := \
    $(CRYPTO_DIR)/chacha/chacha.c \
    $(CRYPTO_DIR)/cipher_extra/e_aesgcmsiv.c \
    $(CRYPTO_DIR)/cpu-aarch64-fuchsia.c \
    $(CRYPTO_DIR)/cpu-arm.c \
    $(CRYPTO_DIR)/cpu-intel.c \
    $(CRYPTO_DIR)/crypto.c \
    $(CRYPTO_DIR)/err/err.c \
    $(CRYPTO_DIR)/fipsmodule/aes/aes.c \
    $(CRYPTO_DIR)/fipsmodule/aes/mode_wrappers.c \
    $(CRYPTO_DIR)/fipsmodule/cipher/aead.c \
    $(CRYPTO_DIR)/fipsmodule/cipher/cipher.c \
    $(CRYPTO_DIR)/fipsmodule/cipher/e_aes.c \
    $(CRYPTO_DIR)/fipsmodule/digest/digest.c \
    $(CRYPTO_DIR)/fipsmodule/digest/digests.c \
    $(CRYPTO_DIR)/fipsmodule/hmac/hmac.c \
    $(CRYPTO_DIR)/fipsmodule/md4/md4.c \
    $(CRYPTO_DIR)/fipsmodule/md5/md5.c \
    $(CRYPTO_DIR)/fipsmodule/modes/cbc.c \
    $(CRYPTO_DIR)/fipsmodule/modes/cfb.c \
    $(CRYPTO_DIR)/fipsmodule/modes/ctr.c \
    $(CRYPTO_DIR)/fipsmodule/modes/gcm.c \
    $(CRYPTO_DIR)/fipsmodule/modes/ofb.c \
    $(CRYPTO_DIR)/fipsmodule/modes/polyval.c \
    $(CRYPTO_DIR)/fipsmodule/rand/ctrdrbg.c \
    $(CRYPTO_DIR)/fipsmodule/rand/rand.c \
    $(CRYPTO_DIR)/fipsmodule/sha/sha1.c \
    $(CRYPTO_DIR)/fipsmodule/sha/sha256.c \
    $(CRYPTO_DIR)/fipsmodule/sha/sha512.c \
    $(CRYPTO_DIR)/hkdf/hkdf.c \
    $(CRYPTO_DIR)/mem.c \
    $(CRYPTO_DIR)/rand_extra/forkunsafe.c \
    $(CRYPTO_DIR)/rand_extra/fuchsia.c \
    $(CRYPTO_DIR)/thread_none.c \

# TODO(aarongreen): Replace or get upstream to support more fully.
DECREPIT_DIR=$(LOCAL_DIR)/decrepit
SHARED_SRCS += \
    $(DECREPIT_DIR)/xts/xts.c \

# Auto-generated sources
SHARED_SRCS += \
    $(LOCAL_DIR)/err_data.c \

ifeq ($(ARCH),arm64)
# TODO(aarongreen): Workaround for the non-hidden OPENSSL_armcap_P symbol, which causes arm/clang to
# fail to link.  Remove when resolved upstream.
SHARED_COMPILEFLAGS += -DOPENSSL_NO_ASM

ASM_DIR = $(LOCAL_DIR)/linux-aarch64/crypto
SHARED_SRCS += \
    $(ASM_DIR)/chacha/chacha-armv8.S \
    $(ASM_DIR)/fipsmodule/sha512-armv8.S \
    $(ASM_DIR)/fipsmodule/ghashv8-armx64.S \
    $(ASM_DIR)/fipsmodule/sha1-armv8.S \
    $(ASM_DIR)/fipsmodule/sha256-armv8.S \
    $(ASM_DIR)/fipsmodule/aesv8-armx64.S \

else ifeq ($(ARCH),x86)

ASM_DIR = $(LOCAL_DIR)/linux-x86_64/crypto
SHARED_SRCS += \
    $(ASM_DIR)/cipher_extra/aes128gcmsiv-x86_64.S \
    $(ASM_DIR)/chacha/chacha-x86_64.S \
    $(ASM_DIR)/fipsmodule/sha256-x86_64.S \
    $(ASM_DIR)/fipsmodule/ghash-x86_64.S \
    $(ASM_DIR)/fipsmodule/bsaes-x86_64.S \
    $(ASM_DIR)/fipsmodule/aesni-x86_64.S \
    $(ASM_DIR)/fipsmodule/sha1-x86_64.S \
    $(ASM_DIR)/fipsmodule/vpaes-x86_64.S \
    $(ASM_DIR)/fipsmodule/md5-x86_64.S \
    $(ASM_DIR)/fipsmodule/aes-x86_64.S \
    $(ASM_DIR)/fipsmodule/sha512-x86_64.S \
    $(ASM_DIR)/fipsmodule/aesni-gcm-x86_64.S \
    $(ASM_DIR)/fipsmodule/rdrand-x86_64.S \

else
$(error Unsupported architecture)

endif

# userlib
MODULE := $(LOCAL_DIR)
MODULE_TYPE := userlib
MODULE_SRCS := $(SHARED_SRCS)
MODULE_COMPILEFLAGS += $(SHARED_COMPILEFLAGS)
MODULE_LIBS := \
    system/ulib/fdio \
    system/ulib/c \

include make/module.mk


# hostlib
MODULE := $(LOCAL_DIR).hostlib
MODULE_TYPE := hostlib
MODULE_SRCS := $(SHARED_SRCS)
MODULE_COMPILEFLAGS += $(SHARED_COMPILEFLAGS) -DOPENSSL_NO_THREADS -DOPENSSL_NO_ASM

include make/module.mk
