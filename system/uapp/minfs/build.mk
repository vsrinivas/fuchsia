# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

ifneq ($(HOST_PLATFORM),darwin)
CFLAGS := -Wall -std=c11 -g -O0
CFLAGS += -Werror-implicit-function-declaration
CFLAGS += -Wstrict-prototypes -Wwrite-strings
CFLAGS += -Isystem/ulib/system/include
CFLAGS += -Isystem/ulib/magenta/include
CFLAGS += -Isystem/ulib/mxio/include
CFLAGS += -Isystem/ulib/fs/include
CFLAGS += -Isystem/public

FUSE_CFLAGS := $(CFLAGS) -D_FILE_OFFSET_BITS=64
ifeq ($(call TOBOOL,$(ENABLE_MINFS_FUSE_DEBUG)),true)
FUSE_CFLAGS += -DDEBUG
endif

LFLAGS := -Wl,-wrap,open -Wl,-wrap,unlink -Wl,-wrap,stat -Wl,-wrap,mkdir
LFLAGS += -Wl,-wrap,close -Wl,-wrap,read -Wl,-wrap,write -Wl,-wrap,fstat
LFLAGS += -Wl,-wrap,lseek -Wl,-wrap,rename

FUSE_LFLAGS := $(LFLAGS) -lfuse

SRCS += main.c test.c
LIBMINFS_SRCS += wrap.c bitmap.c bcache.c
LIBMINFS_SRCS += minfs.c minfs-ops.c minfs-check.c
LIBFS_SRCS += vfs.c

OBJS := $(patsubst %.c,$(BUILDDIR)/host/system/uapp/minfs/%.o,$(SRCS))
DEPS := $(patsubst %.c,$(BUILDDIR)/host/system/uapp/minfs/%.d,$(SRCS))
LIBMINFS_OBJS := $(patsubst %.c,$(BUILDDIR)/host/system/uapp/minfs/%.o,$(LIBMINFS_SRCS))
LIBMINFS_DEPS := $(patsubst %.c,$(BUILDDIR)/host/system/uapp/minfs/%.d,$(LIBMINFS_SRCS))
LIBFS_OBJS := $(patsubst %.c,$(BUILDDIR)/host/system/ulib/fs/%.o,$(LIBFS_SRCS))
LIBFS_DEPS := $(patsubst %.c,$(BUILDDIR)/host/system/ulib/fs/%.d,$(LIBFS_SRCS))
MINFS_TOOLS := $(BUILDDIR)/tools/minfs

ifeq ($(call TOBOOL,$(ENABLE_BUILD_MINFS_FUSE)),true)
MINFS_TOOLS += $(BUILDDIR)/tools/fuse-minfs
endif

.PHONY: minfs
minfs:: $(MINFS_TOOLS)

-include $(DEPS)
-include $(LIBMINFS_DEPS)
-include $(LIBFS_DEPS)

$(BUILDDIR)/host/system/uapp/minfs/%.o: system/uapp/minfs/%.c
	@echo compiling $@
	@mkdir -p $(dir $@)
	@$(HOST_CC) -MMD -MP -c -o $@ $(CFLAGS) $<

$(BUILDDIR)/host/system/ulib/fs/%.o: system/ulib/fs/%.c
	@echo compiling $@
	@mkdir -p $(dir $@)
	@$(HOST_CC) -MMD -MP -c -o $@ $(CFLAGS) $<

$(BUILDDIR)/tools/minfs: $(OBJS) $(LIBMINFS_OBJS) $(LIBFS_OBJS)
	@echo linking $@
	@$(HOST_CC) -o $@ $(LFLAGS) $(CFLAGS) $(OBJS) $(LIBMINFS_OBJS) $(LIBFS_OBJS)

$(BUILDDIR)/tools/fuse-minfs: system/uapp/minfs/fuse.c $(LIBMINFS_OBJS) $(LIBFS_OBJS)
	@echo linking $@
	@$(HOST_CC) -o $@ system/uapp/minfs/fuse.c $(FUSE_LFLAGS) $(FUSE_CFLAGS) $(LIBMINFS_OBJS) $(LIBFS_OBJS)

GENERATED += $(MINFS_TOOLS)
EXTRA_BUILDDEPS += $(MINFS_TOOLS)
endif

