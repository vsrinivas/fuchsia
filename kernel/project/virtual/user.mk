# Copyright 2016 The Fuchsia Authors
# Copyright (c) 2008-2015 Travis Geiselbrecht
#
# Use of this source code is governed by a MIT-style
# license that can be found in the LICENSE file or at
# https://opensource.org/licenses/MIT

# modules needed to implement user space

KERNEL_DEFINES += WITH_DEBUG_LINEBUFFER=1

MODULES += \
    kernel/lib/syscalls \
    kernel/lib/userboot \
    kernel/lib/debuglog \
    kernel/lib/ktrace \
    kernel/lib/mtrace \
    kernel/object

# include all core, dev, uapp, ulib and utest from system/...
MODULES += $(patsubst %/rules.mk,%,$(wildcard system/core/*/rules.mk))
MODULES += $(patsubst %/rules.mk,%,$(wildcard system/dev/*/*/rules.mk))
MODULES += $(patsubst %/rules.mk,%,$(wildcard system/uapp/*/rules.mk))
MODULES += $(patsubst %/rules.mk,%,$(wildcard system/ulib/*/rules.mk))
ifeq ($(call TOBOOL,$(DISABLE_UTEST)),false)
MODULES += $(patsubst %/rules.mk,%,$(wildcard system/utest/*/rules.mk))
endif

# include all uapp, udev, ulib and utest from third_party/...
MODULES += $(patsubst %/rules.mk,%,$(wildcard third_party/uapp/*/rules.mk))
MODULES += $(patsubst %/rules.mk,%,$(wildcard third_party/udev/*/rules.mk))
MODULES += $(patsubst %/rules.mk,%,$(wildcard third_party/ulib/*/rules.mk))
ifeq ($(call TOBOOL,$(DISABLE_UTEST)),false)
MODULES += $(patsubst %/rules.mk,%,$(wildcard third_party/utest/*/rules.mk))
endif

EXTRA_BUILDDEPS += $(USER_BOOTDATA)
