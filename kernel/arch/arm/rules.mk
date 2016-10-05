# Copyright 2016 The Fuchsia Authors
# Copyright (c) 2008-2015 Travis Geiselbrecht
#
# Use of this source code is governed by a MIT-style
# license that can be found in the LICENSE file or at
# https://opensource.org/licenses/MIT

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

# can override this in local.mk
ENABLE_THUMB?=true

# default to the regular arm subarch
SUBARCH := arm

KERNEL_DEFINES += \
	ARM_CPU_$(ARM_CPU)=1

# do set some options based on the cpu core
HANDLED_CORE := false
ifeq ($(ARM_CPU),cortex-m0)
KERNEL_DEFINES += \
	ARM_CPU_CORTEX_M0=1 \
	ARM_ISA_ARMV6M=1 \
	ARM_WITH_THUMB=1
HANDLED_CORE := true
ENABLE_THUMB := true
SUBARCH := arm-m
endif
ifeq ($(ARM_CPU),cortex-m0plus)
KERNEL_DEFINES += \
	ARM_CPU_CORTEX_M0_PLUS=1 \
	ARM_ISA_ARMV6M=1 \
	ARM_WITH_THUMB=1
HANDLED_CORE := true
ENABLE_THUMB := true
SUBARCH := arm-m
endif
ifeq ($(ARM_CPU),cortex-m3)
KERNEL_DEFINES += \
	ARM_CPU_CORTEX_M3=1 \
	ARM_ISA_ARMv7=1 \
	ARM_ISA_ARMv7M=1 \
	ARM_WITH_THUMB=1 \
	ARM_WITH_THUMB2=1
HANDLED_CORE := true
ENABLE_THUMB := true
SUBARCH := arm-m
endif
ifeq ($(ARM_CPU),cortex-m4)
KERNEL_DEFINES += \
	ARM_CPU_CORTEX_M4=1 \
	ARM_ISA_ARMv7=1 \
	ARM_ISA_ARMv7M=1 \
	ARM_WITH_THUMB=1 \
	ARM_WITH_THUMB2=1
HANDLED_CORE := true
ENABLE_THUMB := true
SUBARCH := arm-m
endif
ifeq ($(ARM_CPU),cortex-m4f)
KERNEL_DEFINES += \
	ARM_CPU_CORTEX_M4=1 \
	ARM_CPU_CORTEX_M4F=1 \
	ARM_ISA_ARMv7=1 \
	ARM_ISA_ARMv7M=1 \
	ARM_WITH_THUMB=1 \
	ARM_WITH_THUMB2=1 \
	ARM_WITH_VFP=1 \
	__FPU_PRESENT=1
HANDLED_CORE := true
ENABLE_THUMB := true
SUBARCH := arm-m
endif
ifeq ($(ARM_CPU),cortex-m7)
KERNEL_DEFINES += \
	ARM_CPU_CORTEX_M7=1 \
	ARM_ISA_ARMv7=1 \
	ARM_ISA_ARMv7M=1 \
	ARM_WITH_THUMB=1 \
	ARM_WITH_THUMB2=1 \
	ARM_WITH_CACHE=1
HANDLED_CORE := true
ENABLE_THUMB := true
SUBARCH := arm-m
endif
ifeq ($(ARM_CPU),cortex-a7)
KERNEL_DEFINES += \
	ARM_WITH_CP15=1 \
	ARM_WITH_MMU=1 \
	ARM_ISA_ARMv7=1 \
	ARM_ISA_ARMv7A=1 \
	ARM_WITH_VFP=1 \
	ARM_WITH_NEON=1 \
	ARM_WITH_THUMB=1 \
	ARM_WITH_THUMB2=1 \
	ARM_WITH_CACHE=1
HANDLED_CORE := true
endif
ifeq ($(ARM_CPU),cortex-a15)
KERNEL_DEFINES += \
	ARM_WITH_CP15=1 \
	ARM_WITH_MMU=1 \
	ARM_ISA_ARMv7=1 \
	ARM_ISA_ARMv7A=1 \
	ARM_WITH_THUMB=1 \
	ARM_WITH_THUMB2=1 \
	ARM_WITH_CACHE=1 \
	ARM_WITH_L2=1
ifneq ($(ARM_WITHOUT_VFP_NEON),true)
KERNEL_DEFINES += \
	ARM_WITH_VFP=1 \
	ARM_WITH_NEON=1
endif
HANDLED_CORE := true
endif
ifeq ($(ARM_CPU),cortex-a8)
KERNEL_DEFINES += \
	ARM_WITH_CP15=1 \
	ARM_WITH_MMU=1 \
	ARM_ISA_ARMv7=1 \
	ARM_ISA_ARMv7A=1 \
	ARM_WITH_VFP=1 \
	ARM_WITH_NEON=1 \
	ARM_WITH_THUMB=1 \
	ARM_WITH_THUMB2=1 \
	ARM_WITH_CACHE=1 \
	ARM_WITH_L2=1
HANDLED_CORE := true
endif
ifeq ($(ARM_CPU),cortex-a9)
KERNEL_DEFINES += \
	ARM_WITH_CP15=1 \
	ARM_WITH_MMU=1 \
	ARM_ISA_ARMv7=1 \
	ARM_ISA_ARMv7A=1 \
	ARM_WITH_THUMB=1 \
	ARM_WITH_THUMB2=1 \
	ARM_WITH_CACHE=1
HANDLED_CORE := true
endif
ifeq ($(ARM_CPU),cortex-a9-neon)
KERNEL_DEFINES += \
	ARM_CPU_CORTEX_A9=1 \
	ARM_WITH_CP15=1 \
	ARM_WITH_MMU=1 \
	ARM_ISA_ARMv7=1 \
	ARM_ISA_ARMv7A=1 \
	ARM_WITH_VFP=1 \
	ARM_WITH_NEON=1 \
	ARM_WITH_THUMB=1 \
	ARM_WITH_THUMB2=1 \
	ARM_WITH_CACHE=1
HANDLED_CORE := true
endif
ifeq ($(ARM_CPU),arm1136j-s)
KERNEL_DEFINES += \
	ARM_WITH_CP15=1 \
	ARM_WITH_MMU=1 \
	ARM_ISA_ARMv6=1 \
	ARM_WITH_THUMB=1 \
	ARM_WITH_CACHE=1 \
	ARM_CPU_ARM1136=1
HANDLED_CORE := true
endif
ifeq ($(ARM_CPU),arm1176jzf-s)
KERNEL_DEFINES += \
	ARM_WITH_CP15=1 \
	ARM_WITH_MMU=1 \
	ARM_ISA_ARMv6=1 \
	ARM_WITH_VFP=1 \
	ARM_WITH_THUMB=1 \
	ARM_WITH_CACHE=1 \
	ARM_CPU_ARM1136=1
HANDLED_CORE := true
endif
ifeq ($(ARM_CPU),armemu)
# flavor of emulated cpu by the armemu project
KERNEL_DEFINES += \
	ARM_WITH_CP15=1 \
	ARM_ISA_ARMv7=1 \
	ARM_ISA_ARMv7A=1 \
	ARM_WITH_CACHE=1
HANDLED_CORE := true
ENABLE_THUMB := false # armemu doesn't currently support thumb properly
endif

ifneq ($(HANDLED_CORE),true)
$(error $(LOCAL_DIR)/rules.mk doesnt have logic for arm core $(ARM_CPU))
endif

ifeq ($(ENABLE_THUMB),true)
ARCH_COMPILEFLAGS += -mthumb -D__thumb__ -mthumb-interwork
endif

KERNEL_INCLUDES += \
	$(LOCAL_DIR)/$(SUBARCH)/include

ifeq ($(SUBARCH),arm)
MODULE_SRCS += \
	$(LOCAL_DIR)/arm/start.S \
	$(LOCAL_DIR)/arm/arch.c \
	$(LOCAL_DIR)/arm/asm.S \
	$(LOCAL_DIR)/arm/cache-ops.S \
	$(LOCAL_DIR)/arm/cache.c \
	$(LOCAL_DIR)/arm/debug.c \
	$(LOCAL_DIR)/arm/ops.S \
	$(LOCAL_DIR)/arm/exceptions.S \
	$(LOCAL_DIR)/arm/faults.c \
	$(LOCAL_DIR)/arm/fpu.c \
	$(LOCAL_DIR)/arm/mmu.c \
	$(LOCAL_DIR)/arm/thread.c \
	$(LOCAL_DIR)/arm/user_copy.c \
	$(LOCAL_DIR)/arm/user_copy.S \
	$(LOCAL_DIR)/arm/uspace_entry.S

KERNEL_DEFINES += \
	ARCH_DEFAULT_STACK_SIZE=4096

ARCH_OPTFLAGS := -O2

# we have a mmu and want the vmm/pmm
WITH_KERNEL_VM ?= 1

# for arm, have the kernel occupy the entire top 3GB of virtual space,
# but put the kernel itself at 0x80000000.
# this leaves 0x40000000 - 0x80000000 open for kernel space to use.
GLOBAL_DEFINES += \
    KERNEL_ASPACE_BASE=0x40000000 \
    KERNEL_ASPACE_SIZE=0xc0000000

KERNEL_BASE ?= 0x80000000
KERNEL_LOAD_OFFSET ?= 0

KERNEL_DEFINES += \
    KERNEL_BASE=$(KERNEL_BASE) \
    KERNEL_LOAD_OFFSET=$(KERNEL_LOAD_OFFSET)

# if its requested we build with SMP, arm generically supports 4 cpus
ifeq ($(WITH_SMP),1)
SMP_MAX_CPUS ?= 4
SMP_CPU_CLUSTER_SHIFT ?= 8
SMP_CPU_ID_BITS ?= 24

KERNEL_DEFINES += \
    WITH_SMP=1 \
    SMP_MAX_CPUS=$(SMP_MAX_CPUS) \
    SMP_CPU_CLUSTER_SHIFT=$(SMP_CPU_CLUSTER_SHIFT) \
    SMP_CPU_ID_BITS=$(SMP_CPU_ID_BITS)

MODULE_SRCS += \
	$(LOCAL_DIR)/arm/mp.c
else
KERNEL_DEFINES += \
    SMP_MAX_CPUS=1
endif

ifeq (true,$(call TOBOOL,$(WITH_NS_MAPPING)))
KERNEL_DEFINES += \
    WITH_ARCH_MMU_PICK_SPOT=1
endif

endif
ifeq ($(SUBARCH),arm-m)
MODULE_SRCS += \
	$(LOCAL_DIR)/arm-m/arch.c \
	$(LOCAL_DIR)/arm-m/cache.c \
	$(LOCAL_DIR)/arm-m/exceptions.c \
	$(LOCAL_DIR)/arm-m/start.c \
	$(LOCAL_DIR)/arm-m/spin_cycles.c \
	$(LOCAL_DIR)/arm-m/thread.c \
	$(LOCAL_DIR)/arm-m/vectab.c

# we're building for small binaries
KERNEL_DEFINES += \
	ARM_ONLY_THUMB=1 \
	ARCH_DEFAULT_STACK_SIZE=1024 \
	SMP_MAX_CPUS=1

MODULE_DEPS += \
	arch/arm/arm-m/CMSIS

ARCH_OPTFLAGS := -Os
endif

MODULE_SRCS += \
	$(LOCAL_DIR)/arm/debugger.c

# try to find toolchain
include $(LOCAL_DIR)/toolchain.mk
TOOLCHAIN_PREFIX := $(ARCH_$(ARCH)_TOOLCHAIN_PREFIX)
#$(info TOOLCHAIN_PREFIX = $(TOOLCHAIN_PREFIX))

ARCH_COMPILEFLAGS += $(ARCH_$(ARCH)_COMPILEFLAGS)

OBJDUMP_LIST_FLAGS := -Mreg-names-raw

# hard disable the fpu in kernel code
KERNEL_COMPILEFLAGS += -msoft-float -mfloat-abi=soft -DWITH_NO_FP=1

# set the max page size to something more reasonables (defaults to 64K or above)
GLOBAL_LDFLAGS += -z max-page-size=4096

# find the direct path to libgcc.a for our particular multilib variant
LIBGCC := $(shell $(TOOLCHAIN_PREFIX)gcc $(GLOBAL_COMPILEFLAGS) $(ARCH_COMPILEFLAGS) -print-libgcc-file-name)
#$(info LIBGCC = $(LIBGCC))
$(info GLOBAL_COMPILEFLAGS = $(GLOBAL_COMPILEFLAGS) $(ARCH_COMPILEFLAGS))

# make sure some bits were set up
MEMVARS_SET := 0
ifneq ($(MEMBASE),)
MEMVARS_SET := 1
endif
ifneq ($(MEMSIZE),)
MEMVARS_SET := 1
endif
ifeq ($(MEMVARS_SET),0)
$(error missing MEMBASE or MEMSIZE variable, please set in target rules.mk)
endif

KERNEL_DEFINES += \
	MEMBASE=$(MEMBASE) \
	MEMSIZE=$(MEMSIZE)

# potentially generated files that should be cleaned out with clean make rule
GENERATED += \
	$(BUILDDIR)/system-onesegment.ld \
	$(BUILDDIR)/system-twosegment.ld

# rules for generating the linker scripts
$(BUILDDIR)/system-onesegment.ld: $(LOCAL_DIR)/system-onesegment.ld $(wildcard arch/*.ld) linkerscript.phony
	@echo generating $@
	@$(MKDIR)
	$(NOECHO)sed "s/%MEMBASE%/$(MEMBASE)/;s/%MEMSIZE%/$(MEMSIZE)/;s/%KERNEL_BASE%/$(KERNEL_BASE)/;s/%KERNEL_LOAD_OFFSET%/$(KERNEL_LOAD_OFFSET)/" < $< > $@.tmp
	@$(call TESTANDREPLACEFILE,$@.tmp,$@)

$(BUILDDIR)/system-twosegment.ld: $(LOCAL_DIR)/system-twosegment.ld $(wildcard arch/*.ld) linkerscript.phony
	@echo generating $@
	@$(MKDIR)
	$(NOECHO)sed "s/%ROMBASE%/$(ROMBASE)/;s/%MEMBASE%/$(MEMBASE)/;s/%MEMSIZE%/$(MEMSIZE)/" < $< > $@.tmp
	@$(call TESTANDREPLACEFILE,$@.tmp,$@)

linkerscript.phony:
.PHONY: linkerscript.phony

# arm specific script to try to guess stack usage
$(OUTLKELF).stack: LOCAL_DIR:=$(LOCAL_DIR)
$(OUTLKELF).stack: $(OUTLKELF)
	$(NOECHO)echo generating stack usage $@
	$(NOECHO)$(OBJDUMP) -Mreg-names-raw -d $< | $(LOCAL_DIR)/stackusage | $(CPPFILT) | sort -n -k 1 -r > $@

EXTRA_BUILDDEPS += $(OUTLKELF).stack
GENERATED += $(OUTLKELF).stack

include make/module.mk
