# Copyright 2016 The Fuchsia Authors
# Copyright (c) 2008-2015 Travis Geiselbrecht
#
# Use of this source code is governed by a MIT-style
# license that can be found in the LICENSE file or at
# https://opensource.org/licenses/MIT

# x86-32 toolchain
ifeq ($(SUBARCH),x86-32)
ifndef ARCH_x86_TOOLCHAIN_INCLUDED
ARCH_x86_TOOLCHAIN_INCLUDED := 1

# if the x86-64 toolchain is set, prefer that
ifdef ARCH_x86_64_TOOLCHAIN_PREFIX
ARCH_x86_TOOLCHAIN_PREFIX := $(ARCH_x86_64_TOOLCHAIN_PREFIX)
GLOBAL_COMPILEFLAGS += -m32
GLOBAL_LDFLAGS += -melf_i386
GLOBAL_MODULE_LDFLAGS += -melf_i386
endif

ifndef ARCH_x86_TOOLCHAIN_PREFIX
ARCH_x86_TOOLCHAIN_PREFIX := i386-elf-
endif
FOUNDTOOL=$(shell which $(ARCH_x86_TOOLCHAIN_PREFIX)gcc)

ifeq ($(FOUNDTOOL),)
$(error cannot find toolchain, please set ARCH_x86_TOOLCHAIN_PREFIX or add it to your path)
endif

endif
endif

# x86-64 toolchain
ifeq ($(SUBARCH),x86-64)
ifndef ARCH_x86_64_TOOLCHAIN_INCLUDED
ARCH_x86_64_TOOLCHAIN_INCLUDED := 1

ifndef ARCH_x86_64_TOOLCHAIN_PREFIX
ARCH_x86_64_TOOLCHAIN_PREFIX := x86_64-elf-
endif
ifeq ($(call TOBOOL,$(USE_CLANG)),true)
FOUNDTOOL=$(shell which $(ARCH_x86_64_TOOLCHAIN_PREFIX)clang)
else
FOUNDTOOL=$(shell which $(ARCH_x86_64_TOOLCHAIN_PREFIX)gcc)
endif

ifeq ($(FOUNDTOOL),)
$(error cannot find toolchain, please set ARCH_x86_64_TOOLCHAIN_PREFIX or add it to your path)
endif

endif
endif

