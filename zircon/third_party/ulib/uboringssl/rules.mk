# Copyright 2017 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Modules which depend on uboringssl should have a "#define BORINGSSL_NO_CXX"
# statement before any "#include <openssl/...>" statements to avoid pulling in
# C++ headers.

# Variables shared between the userlib and hostlib
LOCAL_DIR := $(GET_LOCAL_DIR)

SHARED_COMPILEFLAGS := \
    -fvisibility=hidden -Wno-unused-function -include $(LOCAL_DIR)/stack-note.S

CRYPTO_DIR=$(LOCAL_DIR)/crypto
SHARED_SRCS := \
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
    $(CRYPTO_DIR)/fipsmodule/modes/gcm.c \
    $(CRYPTO_DIR)/fipsmodule/modes/polyval.c \
    $(CRYPTO_DIR)/fipsmodule/sha/sha256.c \
    $(CRYPTO_DIR)/hkdf/hkdf.c \
    $(CRYPTO_DIR)/mem.c \
    $(CRYPTO_DIR)/thread_pthread.c \
    $(LOCAL_DIR)/zircon-unused.c

# TODO(aarongreen): Replace or get upstream to support more fully.
DECREPIT_DIR=$(LOCAL_DIR)/decrepit
SHARED_SRCS += \
    $(DECREPIT_DIR)/xts/xts.c \

# Auto-generated sources
SHARED_SRCS += \
    $(LOCAL_DIR)/err_data.c \

ARM_64_ASMSRCS := \
    fipsmodule/sha256-armv8.S \
    fipsmodule/ghashv8-armx64.S \
    fipsmodule/aesv8-armx64.S
LINUX_ARM64_ASMSRCS := $(patsubst %,$(LOCAL_DIR)/linux-aarch64/crypto/%,$(ARM_64_ASMSRCS))

X86_64_ASMSRCS := \
    cipher_extra/aes128gcmsiv-x86_64.S \
    fipsmodule/aesni-gcm-x86_64.S \
    fipsmodule/aesni-x86_64.S \
    fipsmodule/aes-x86_64.S \
    fipsmodule/bsaes-x86_64.S \
    fipsmodule/ghash-x86_64.S \
    fipsmodule/sha256-x86_64.S \
    fipsmodule/vpaes-x86_64.S
DARWIN_X86_64_ASMSRCS := $(patsubst %,$(LOCAL_DIR)/mac-x86_64/crypto/%,$(X86_64_ASMSRCS))
LINUX_X86_64_ASMSRCS := $(patsubst %,$(LOCAL_DIR)/linux-x86_64/crypto/%,$(X86_64_ASMSRCS))

# userlib
ifeq ($(ARCH),arm64)
TARGET_ASMSRCS=$(LINUX_ARM64_ASMSRCS)
else ifeq ($(ARCH),x86)
TARGET_ASMSRCS=$(LINUX_X86_64_ASMSRCS)
else
$(error Unsupported target architecture)
endif

MODULE := $(LOCAL_DIR)
MODULE_TYPE := userlib
MODULE_SRCS := $(SHARED_SRCS) $(TARGET_ASMSRCS)
MODULE_COMPILEFLAGS := $(SHARED_COMPILEFLAGS)
MODULE_LIBS := \
    system/ulib/fdio \
    system/ulib/c \

include make/module.mk

# hostlib
ifeq ($(HOST_PLATFORM),linux)
ifeq ($(HOST_ARCH),aarch64)
HOST_ASMSRCS := $(LINUX_ARM64_ASMSRCS)
else ifeq ($(HOST_ARCH),x86_64)
HOST_ASMSRCS := $(LINUX_X86_64_ASMSRCS)
else
MODULE_DEFINES := OPENSSL_NO_ASM
endif
else ifeq ($(HOST_PLATFORM),darwin)
ifeq ($(HOST_ARCH),x86_64)
HOST_ASMSRCS := $(DARWIN_X86_64_ASMSRCS)
else
MODULE_DEFINES := OPENSSL_NO_ASM
endif
else
MODULE_DEFINES := OPENSSL_NO_ASM
endif

MODULE := $(LOCAL_DIR).hostlib
MODULE_TYPE := hostlib
MODULE_SRCS := $(SHARED_SRCS) $(HOST_ASMSRCS)
MODULE_DEFINES += _XOPEN_SOURCE=700 # Required for pthread_rwlock_t
MODULE_COMPILEFLAGS := $(SHARED_COMPILEFLAGS)

include make/module.mk
