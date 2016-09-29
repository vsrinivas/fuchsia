# Copyright 2016 The Fuchsia Authors
# Copyright (c) 2008-2015 Travis Geiselbrecht
#
# Use of this source code is governed by a MIT-style
# license that can be found in the LICENSE file or at
# https://opensource.org/licenses/MIT

# modules needed to implement user space

KERNEL_DEFINES += WITH_DEBUG_LINEBUFFER=1

MODULES += \
    lib/syscalls \
    lib/userboot \
    lib/debuglog \
    lib/ktrace \

# include all core, uapp, udev, ulib and utest from system/...
MODULES += $(patsubst %/rules.mk,%,$(wildcard system/core/*/rules.mk))
MODULES += $(patsubst %/rules.mk,%,$(wildcard system/uapp/*/rules.mk))
MODULES += $(patsubst %/rules.mk,%,$(wildcard system/udev/*/rules.mk))
MODULES += $(patsubst %/rules.mk,%,$(wildcard system/ulib/*/rules.mk))
MODULES += $(patsubst %/rules.mk,%,$(wildcard system/utest/*/rules.mk))
MODULES := $(patsubst system/%,%,$(MODULES))

# include all uapp, udev, ulib and utest from third_party/...
MODULES += $(patsubst %/rules.mk,%,$(wildcard third_party/uapp/*/rules.mk))
MODULES += $(patsubst %/rules.mk,%,$(wildcard third_party/udev/*/rules.mk))
MODULES += $(patsubst %/rules.mk,%,$(wildcard third_party/ulib/*/rules.mk))
MODULES += $(patsubst %/rules.mk,%,$(wildcard third_party/utest/*/rules.mk))
MODULES := $(patsubst third_party/%,%,$(MODULES))

# if we're not embedding the bootfs, build a standalone image
ifneq ($(EMBED_USER_BOOTFS),true)
EXTRA_BUILDDEPS += $(USER_FS)
endif

EXTRA_BUILDDEPS += $(USER_BOOTFS)

