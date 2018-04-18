# Copyright 2016 The Fuchsia Authors
# Copyright (c) 2008-2015 Travis Geiselbrecht
#
# Use of this source code is governed by a MIT-style
# license that can be found in the LICENSE file or at
# https://opensource.org/licenses/MIT

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

BOOT_HEADER_SIZE ?= 0x70
KERNEL_LOAD_OFFSET ?= 0x00100000 # 1MB
KERNEL_BASE ?= 0xffffffff80100000 # has KERNEL_LOAD_OFFSET baked into it
KERNEL_SIZE ?= 0x40000000 # 1GB
KERNEL_ASPACE_BASE ?= 0xffffff8000000000UL # -512GB
KERNEL_ASPACE_SIZE ?= 0x0000008000000000UL
USER_ASPACE_BASE   ?= 0x0000000001000000UL # 16MB
# We set the top of user address space to be (1 << 47) - 4k.  See
# docs/sysret_problem.md for why we subtract 4k here.  Subtracting
# USER_ASPACE_BASE from that value gives the value for USER_ASPACE_SIZE
# below.
USER_ASPACE_SIZE   ?= 0x00007ffffefff000UL

LOCAL_BUILDDIR := $(call TOBUILDDIR,$(LOCAL_DIR))

KERNEL_DEFINES += \
	ARCH_$(ARCH)=1 \
	KERNEL_BASE=$(KERNEL_BASE) \
	KERNEL_SIZE=$(KERNEL_SIZE) \
	KERNEL_LOAD_OFFSET=$(KERNEL_LOAD_OFFSET)

GLOBAL_DEFINES += \
	KERNEL_ASPACE_BASE=$(KERNEL_ASPACE_BASE) \
	KERNEL_ASPACE_SIZE=$(KERNEL_ASPACE_SIZE) \
	USER_ASPACE_BASE=$(USER_ASPACE_BASE) \
	USER_ASPACE_SIZE=$(USER_ASPACE_SIZE)

MODULE_SRCS += \
	$(LOCAL_DIR)/acpi.S \
	$(LOCAL_DIR)/arch.cpp \
	$(LOCAL_DIR)/asm.S \
	$(LOCAL_DIR)/bp_percpu.c \
	$(LOCAL_DIR)/cache.cpp \
	$(LOCAL_DIR)/cpu_topology.cpp \
	$(LOCAL_DIR)/debugger.cpp \
	$(LOCAL_DIR)/descriptor.cpp \
	$(LOCAL_DIR)/exceptions.S \
	$(LOCAL_DIR)/faults.cpp \
	$(LOCAL_DIR)/feature.cpp \
	$(LOCAL_DIR)/gdt.S \
	$(LOCAL_DIR)/hwp.cpp \
	$(LOCAL_DIR)/idt.cpp \
	$(LOCAL_DIR)/ioapic.cpp \
	$(LOCAL_DIR)/ioport.cpp \
	$(LOCAL_DIR)/lapic.cpp \
	$(LOCAL_DIR)/mexec.S \
	$(LOCAL_DIR)/mmu.cpp \
	$(LOCAL_DIR)/mmu_mem_types.cpp \
	$(LOCAL_DIR)/mmu_tests.cpp \
	$(LOCAL_DIR)/mp.cpp \
	$(LOCAL_DIR)/ops.S \
	$(LOCAL_DIR)/perf_mon.cpp \
	$(LOCAL_DIR)/proc_trace.cpp \
	$(LOCAL_DIR)/pvclock.cpp \
	$(LOCAL_DIR)/registers.cpp \
	$(LOCAL_DIR)/start.S \
	$(LOCAL_DIR)/syscall.S \
	$(LOCAL_DIR)/thread.cpp \
	$(LOCAL_DIR)/timer_freq.cpp \
	$(LOCAL_DIR)/tsc.cpp \
	$(LOCAL_DIR)/user_copy.S \
	$(LOCAL_DIR)/user_copy.cpp \
	$(LOCAL_DIR)/uspace_entry.S \

MODULE_DEPS += \
	kernel/arch/x86/page_tables \
	kernel/dev/iommu/dummy \
	kernel/lib/bitmap \
	kernel/lib/code_patching \
	kernel/lib/fbl \
	kernel/object

include $(LOCAL_DIR)/toolchain.mk

MODULE_SRCS += \
	$(LOCAL_DIR)/bootstrap16.cpp \
	$(LOCAL_DIR)/smp.cpp \
	$(LOCAL_DIR)/start16.S

# default to 16 cpu max support
SMP_MAX_CPUS ?= 16
KERNEL_DEFINES += \
	SMP_MAX_CPUS=$(SMP_MAX_CPUS)

# set the default toolchain to x86 elf and set a #define
ifndef TOOLCHAIN_PREFIX
TOOLCHAIN_PREFIX := $(ARCH_x86_64_TOOLCHAIN_PREFIX)
endif

#$(warning ARCH_x86_TOOLCHAIN_PREFIX = $(ARCH_x86_TOOLCHAIN_PREFIX))
#$(warning ARCH_x86_64_TOOLCHAIN_PREFIX = $(ARCH_x86_64_TOOLCHAIN_PREFIX))
#$(warning TOOLCHAIN_PREFIX = $(TOOLCHAIN_PREFIX))

cc-option = $(shell if test -z "`$(1) $(2) -S -o /dev/null -xc /dev/null 2>&1`"; \
	then echo "$(2)"; else echo "$(3)"; fi ;)

# disable SSP if the compiler supports it; it will break stuff
GLOBAL_CFLAGS += $(call cc-option,$(CC),-fno-stack-protector,)

# set the default architecture
GLOBAL_COMPILEFLAGS += -march=x86-64 -mcx16

CLANG_ARCH := x86_64
ifeq ($(call TOBOOL,$(USE_CLANG)),true)
GLOBAL_LDFLAGS += -m elf_x86_64
GLOBAL_MODULE_LDFLAGS += -m elf_x86_64
endif
GLOBAL_LDFLAGS += -z max-page-size=4096
ifeq ($(call TOBOOL,$(USE_CLANG)),false)
KERNEL_COMPILEFLAGS += -falign-jumps=1 -falign-loops=1 -falign-functions=4
endif

# hard disable floating point in the kernel
KERNEL_COMPILEFLAGS += -msoft-float -mno-mmx -mno-sse -mno-sse2 -mno-3dnow -mno-avx -mno-avx2
ifeq ($(call TOBOOL,$(USE_CLANG)),false)
KERNEL_COMPILEFLAGS += -mno-80387 -mno-fp-ret-in-387
endif

KERNEL_COMPILEFLAGS += -fPIE -include kernel/include/hidden.h
KERNEL_COMPILEFLAGS += -mno-red-zone

# Clang needs -mcmodel=kernel to tell it to use the right safe-stack ABI for
# the kernel.
ifeq ($(call TOBOOL,$(USE_CLANG)),true)
KERNEL_COMPILEFLAGS += -mcmodel=kernel
endif

# optimization: since fpu is disabled, do not pass flag in rax to varargs routines
# that floating point args are in use.
ifeq ($(call TOBOOL,$(USE_CLANG)),false)
KERNEL_COMPILEFLAGS += -mskip-rax-setup
endif

ifeq ($(call TOBOOL,$(ENABLE_NEW_BOOTDATA)),true)
MODULE_DEFINES += ENABLE_NEW_BOOTDATA=1
endif

include make/module.mk
