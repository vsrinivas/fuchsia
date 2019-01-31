LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := userlib

MODULE_EXPORT := so

MODULE_PACKAGE := shared

MODULE_COMPILEFLAGS := \
    -DDEBUG \
    -I$(LOCAL_DIR)/include/ngunwind \
    -I$(LOCAL_DIR)/include/ngunwind/private \

MODULE_SRCS := \
    $(LOCAL_DIR)/src/dwarf/Gexpr.c \
    $(LOCAL_DIR)/src/dwarf/Gfde.c \
    $(LOCAL_DIR)/src/dwarf/Gfind_proc_info-lsb.c \
    $(LOCAL_DIR)/src/dwarf/Gfind_unwind_table.c \
    $(LOCAL_DIR)/src/dwarf/Gparser.c \
    $(LOCAL_DIR)/src/dwarf/Gpe.c \
    $(LOCAL_DIR)/src/dwarf/Gstep.c \
    $(LOCAL_DIR)/src/dwarf/global.c \
    $(LOCAL_DIR)/src/fuchsia.c \
    $(LOCAL_DIR)/src/mi/Gdestroy_addr_space.c \
    $(LOCAL_DIR)/src/mi/Gdyn-extract.c \
    $(LOCAL_DIR)/src/mi/Gdyn-remote.c \
    $(LOCAL_DIR)/src/mi/Gfind_dynamic_proc_info.c \
    $(LOCAL_DIR)/src/mi/Gget_accessors.c \
    $(LOCAL_DIR)/src/mi/Gget_fpreg.c \
    $(LOCAL_DIR)/src/mi/Gget_proc_info_by_ip.c \
    $(LOCAL_DIR)/src/mi/Gget_proc_name.c \
    $(LOCAL_DIR)/src/mi/Gget_reg.c \
    $(LOCAL_DIR)/src/mi/Gput_dynamic_unwind_info.c \
    $(LOCAL_DIR)/src/mi/Gset_caching_policy.c \
    $(LOCAL_DIR)/src/mi/Gset_fpreg.c \
    $(LOCAL_DIR)/src/mi/Gset_reg.c \
    $(LOCAL_DIR)/src/mi/common.c \
    $(LOCAL_DIR)/src/mi/flush_cache.c \
    $(LOCAL_DIR)/src/mi/init.c \
    $(LOCAL_DIR)/src/mi/mempool.c \
    $(LOCAL_DIR)/src/mi/strerror.c \

ifeq ($(ARCH),arm64)
MODULE_SRCS += \
    $(LOCAL_DIR)/src/elf64.c \
    $(LOCAL_DIR)/src/aarch64/Gcreate_addr_space.c \
    $(LOCAL_DIR)/src/aarch64/Gget_proc_info.c \
    $(LOCAL_DIR)/src/aarch64/Gget_save_loc.c \
    $(LOCAL_DIR)/src/aarch64/Gglobal.c \
    $(LOCAL_DIR)/src/aarch64/Ginit.c \
    $(LOCAL_DIR)/src/aarch64/Ginit_local.c \
    $(LOCAL_DIR)/src/aarch64/Ginit_remote.c \
    $(LOCAL_DIR)/src/aarch64/Gsignal_frame.c \
    $(LOCAL_DIR)/src/aarch64/Gregs.c \
    $(LOCAL_DIR)/src/aarch64/Gstash_frame.c \
    $(LOCAL_DIR)/src/aarch64/Gstep.c \
    $(LOCAL_DIR)/src/aarch64/fuchsia.c \
    $(LOCAL_DIR)/src/aarch64/regname.c \

else ifeq ($(ARCH),x86)
MODULE_SRCS += \
    $(LOCAL_DIR)/src/elf64.c \
    $(LOCAL_DIR)/src/x86_64/Gcreate_addr_space.c \
    $(LOCAL_DIR)/src/x86_64/Gget_proc_info.c \
    $(LOCAL_DIR)/src/x86_64/Gget_save_loc.c \
    $(LOCAL_DIR)/src/x86_64/Gglobal.c \
    $(LOCAL_DIR)/src/x86_64/Ginit.c \
    $(LOCAL_DIR)/src/x86_64/Ginit_local.c \
    $(LOCAL_DIR)/src/x86_64/Ginit_remote.c \
    $(LOCAL_DIR)/src/x86_64/Gregs.c \
    $(LOCAL_DIR)/src/x86_64/Gsignal_frame.c \
    $(LOCAL_DIR)/src/x86_64/Gstash_frame.c \
    $(LOCAL_DIR)/src/x86_64/Gstep.c \
    $(LOCAL_DIR)/src/x86_64/fuchsia.c \
    $(LOCAL_DIR)/src/x86_64/regname.c \

else
error Unsupported architecture for ngunwind build!
endif

MODULE_SO_NAME := ngunwind

MODULE_LIBS := \
    system/ulib/zircon system/ulib/c

# Force -O2 since the library doesn't build with -Og
MODULE_OPTFLAGS += -O2

# Compile this with frame pointers so that if we crash the crashlogger
# the simplistic unwinder will work.
MODULE_COMPILEFLAGS += $(KEEP_FRAME_POINTER_COMPILEFLAGS)

ifeq ($(call TOBOOL,$(USE_CLANG)),true)
MODULE_COMPILEFLAGS += -Wno-absolute-value
endif

include make/module.mk
