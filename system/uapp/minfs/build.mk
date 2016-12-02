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

LFLAGS := -Wl,-wrap,open -Wl,-wrap,unlink -Wl,-wrap,stat -Wl,-wrap,mkdir
LFLAGS += -Wl,-wrap,close -Wl,-wrap,read -Wl,-wrap,write -Wl,-wrap,fstat
LFLAGS += -Wl,-wrap,lseek -Wl,-wrap,rename

SRCS += main.c wrap.c test.c
SRCS += bitmap.c bcache.c
SRCS += minfs.c minfs-ops.c minfs-check.c
LIBFS_SRCS += vfs.c

OBJS := $(patsubst %.c,$(BUILDDIR)/host/system/uapp/minfs/%.o,$(SRCS))
DEPS := $(patsubst %.c,$(BUILDDIR)/host/system/uapp/minfs/%.d,$(SRCS))
LIBFS_OBJS := $(patsubst %.c,$(BUILDDIR)/host/system/ulib/fs/%.o,$(LIBFS_SRCS))
LIBFS_DEPS := $(patsubst %.c,$(BUILDDIR)/host/system/ulib/fs/%.d,$(LIBFS_SRCS))

.PHONY: minfs
minfs:: $(BUILDDIR)/tools/minfs

-include $(DEPS)
-include $(LIBFS_DEPS)

$(BUILDDIR)/host/system/uapp/minfs/%.o: system/uapp/minfs/%.c
	@echo compiling $@
	@mkdir -p $(dir $@)
	@$(HOST_CC) -MMD -MP -c -o $@ $(CFLAGS) $<

$(BUILDDIR)/host/system/ulib/fs/%.o: system/ulib/fs/%.c
	@echo compilin $@
	@mkdir -p $(dir $@)
	@$(HOST_CC) -MMD -MP -c -o $@ $(CFLAGS) $<

$(BUILDDIR)/tools/minfs: $(OBJS) $(LIBFS_OBJS)
	@echo linking $@
	@$(HOST_CC) -o $@ $(LFLAGS) $(CFLAGS) $(OBJS) $(LIBFS_OBJS)

GENERATED += $(BUILDDIR)/tools/minfs
EXTRA_BUILDDEPS += $(BUILDDIR)/tools/minfs
endif

