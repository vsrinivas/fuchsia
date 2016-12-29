# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

MINFS_CFLAGS += -Werror-implicit-function-declaration
MINFS_CFLAGS += -Wstrict-prototypes -Wwrite-strings
MINFS_CFLAGS += -Isystem/ulib/system/include
MINFS_CFLAGS += -Isystem/ulib/magenta/include
MINFS_CFLAGS += -Isystem/ulib/mxcpp/include
MINFS_CFLAGS += -Isystem/ulib/mxio/include
MINFS_CFLAGS += -Isystem/ulib/mxtl/include
MINFS_CFLAGS += -Isystem/ulib/fs/include
MINFS_CFLAGS += -Isystem/public
MINFS_CFLAGS += -Isystem/private

ifeq ($(HOST_PLATFORM),darwin)
MINFS_CFLAGS += -DO_DIRECTORY=0200000
else
MINFS_CFLAGS += -D_POSIX_C_SOURCE=200809L
endif

MINFS_LDFLAGS :=

FUSE_CFLAGS := -D_FILE_OFFSET_BITS=64
ifeq ($(call TOBOOL,$(ENABLE_MINFS_FUSE_DEBUG)),true)
FUSE_CFLAGS += -DDEBUG
endif

FUSE_LDFLAGS :=

ifeq ($(HOST_PLATFORM),darwin)
FUSE_CFLAGS += -D_DARWIN_USE_64_BIT_INODE -I/usr/local/include/osxfuse
FUSE_LDFLAGS += -L/usr/local/lib -losxfuse
else
FUSE_LDFLAGS += -lfuse
endif

SRCS += main.cpp test.cpp
LIBMINFS_SRCS += host.cpp bitmap.cpp bcache.cpp
LIBMINFS_SRCS += minfs.cpp minfs-ops.cpp minfs-check.cpp
LIBFS_SRCS += vfs.c
LIBMXCPP_SRCS := new.cpp pure_virtual.cpp

OBJS := $(patsubst %.cpp,$(BUILDDIR)/host/system/uapp/minfs/%.o,$(SRCS))
DEPS := $(patsubst %.cpp,$(BUILDDIR)/host/system/uapp/minfs/%.d,$(SRCS))
LIBMINFS_OBJS := $(patsubst %.cpp,$(BUILDDIR)/host/system/uapp/minfs/%.o,$(LIBMINFS_SRCS))
LIBMINFS_DEPS := $(patsubst %.cpp,$(BUILDDIR)/host/system/uapp/minfs/%.d,$(LIBMINFS_SRCS))
LIBFS_OBJS := $(patsubst %.c,$(BUILDDIR)/host/system/ulib/fs/%.o,$(LIBFS_SRCS))
LIBFS_DEPS := $(patsubst %.c,$(BUILDDIR)/host/system/ulib/fs/%.d,$(LIBFS_SRCS))
LIBMXCPP_OBJS := $(patsubst %.cpp,$(BUILDDIR)/host/system/ulib/mxcpp/%.o,$(LIBMXCPP_SRCS))
LIBMXCPP_DEPS := $(patsubst %.cpp,$(BUILDDIR)/host/system/ulib/mxcpp/%.d,$(LIBMXCPP_SRCS))
MINFS_TOOLS := $(BUILDDIR)/tools/minfs

ifeq ($(call TOBOOL,$(ENABLE_BUILD_MINFS_FUSE)),true)
MINFS_TOOLS += $(BUILDDIR)/tools/fuse-minfs
endif

.PHONY: minfs
minfs:: $(MINFS_TOOLS)

-include $(DEPS)
-include $(LIBMINFS_DEPS)
-include $(LIBFS_DEPS)
-include $(LIBMXCPP_DEPS)

$(BUILDDIR)/host/system/uapp/minfs/%.o: system/uapp/minfs/%.cpp
	@echo compiling $@
	@mkdir -p $(dir $@)
	@$(HOST_CC) -MMD -MP $(HOST_COMPILEFLAGS) $(HOST_CPPFLAGS) $(MINFS_CFLAGS) -c -o $@ $<

$(BUILDDIR)/host/system/ulib/fs/%.o: system/ulib/fs/%.c
	@echo compiling $@
	@mkdir -p $(dir $@)
	@$(HOST_CC) -MMD -MP $(HOST_COMPILEFLAGS) $(HOST_CFLAGS) $(MINFS_CFLAGS) -c -o $@ $<

$(BUILDDIR)/host/system/ulib/mxcpp/%.o: system/ulib/mxcpp/%.cpp
	@echo compiling $@
	@mkdir -p $(dir $@)
	@$(HOST_CC) -MMD -MP $(HOST_COMPILEFLAGS) $(HOST_CPPFLAGS) $(MINFS_CFLAGS) -c -o $@ $<

$(BUILDDIR)/tools/minfs: $(OBJS) $(LIBMINFS_OBJS) $(LIBFS_OBJS) $(LIBMXCPP_OBJS)
	@echo linking $@
	@$(HOST_CC) $(MINFS_LDFLAGS) -o $@ $^

$(BUILDDIR)/host/system/uapp/minfs/fuse.o: system/uapp/minfs/fuse.cpp
	@echo compiling $@
	@mkdir -p $(dir $@)
	@$(HOST_CC) -MMD -MP $(HOST_COMPILEFLAGS) $(HOST_CPPFLAGS) $(MINFS_CFLAGS) $(FUSE_CFLAGS) -c -o $@ $<

$(BUILDDIR)/tools/fuse-minfs: $(LIBMINFS_OBJS) $(LIBFS_OBJS) $(BUILDDIR)/host/system/uapp/minfs/fuse.o
	@echo linking $@
	@$(HOST_CC) $(MINFS_LDFLAGS) $(FUSE_LDFLAGS) -o $@ $^

GENERATED += $(MINFS_TOOLS)
EXTRA_BUILDDEPS += $(MINFS_TOOLS)
