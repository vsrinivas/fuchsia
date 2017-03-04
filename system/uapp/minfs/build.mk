# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

MINFS_CFLAGS += -Werror-implicit-function-declaration
MINFS_CFLAGS += -Wstrict-prototypes -Wwrite-strings
MINFS_CFLAGS += -Isystem/ulib/bitmap/include
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

SRCS += main.cpp test.cpp
LIBMINFS_SRCS += host.cpp bcache.cpp
LIBMINFS_SRCS += minfs.cpp minfs-ops.cpp minfs-check.cpp
LIBFS_SRCS += vfs.c
LIBMXCPP_SRCS := new.cpp pure_virtual.cpp
LIBBITMAP_SRCS := raw-bitmap.cpp

OBJS := $(patsubst %.cpp,$(BUILDDIR)/host/system/uapp/minfs/%.cpp.o,$(SRCS))
DEPS := $(patsubst %.cpp,$(BUILDDIR)/host/system/uapp/minfs/%.cpp.d,$(SRCS))
LIBMINFS_OBJS := $(patsubst %.cpp,$(BUILDDIR)/host/system/uapp/minfs/%.cpp.o,$(LIBMINFS_SRCS))
LIBMINFS_DEPS := $(patsubst %.cpp,$(BUILDDIR)/host/system/uapp/minfs/%.cpp.d,$(LIBMINFS_SRCS))
LIBFS_OBJS := $(patsubst %.c,$(BUILDDIR)/host/system/ulib/fs/%.c.o,$(LIBFS_SRCS))
LIBFS_DEPS := $(patsubst %.c,$(BUILDDIR)/host/system/ulib/fs/%.c.d,$(LIBFS_SRCS))
LIBMXCPP_OBJS := $(patsubst %.cpp,$(BUILDDIR)/host/system/ulib/mxcpp/%.cpp.o,$(LIBMXCPP_SRCS))
LIBMXCPP_DEPS := $(patsubst %.cpp,$(BUILDDIR)/host/system/ulib/mxcpp/%.cpp.d,$(LIBMXCPP_SRCS))
LIBBITMAP_OBJS := $(patsubst %.cpp,$(BUILDDIR)/host/system/ulib/bitmap/%.cpp.o,$(LIBBITMAP_SRCS))
LIBBITMAP_DEPS := $(patsubst %.cpp,$(BUILDDIR)/host/system/ulib/bitmap/%.cpp.d,$(LIBBITMAP_SRCS))
MINFS_TOOLS := $(BUILDDIR)/tools/minfs

.PHONY: minfs
minfs: $(MINFS_TOOLS)

-include $(DEPS)
-include $(LIBMINFS_DEPS)
-include $(LIBFS_DEPS)
-include $(LIBMXCPP_DEPS)
-include $(LIBBITMAPS_DEPS)

$(OBJS) $(LIBMINFS_OBJS) $(LIBMXCPP_OBJS): $(BUILDDIR)/host/%.cpp.o: %.cpp
	@echo compiling $@
	@$(MKDIR)
	$(NOECHO)$(HOST_CC) -MMD -MP $(HOST_COMPILEFLAGS) $(HOST_CPPFLAGS) $(MINFS_CFLAGS) -c -o $@ $<

$(LIBBITMAP_OBJS): $(BUILDDIR)/host/%.cpp.o: %.cpp $(LIBMXCPP_OBJS)
	@echo compiling $@
	@$(MKDIR)
	$(NOECHO)$(HOST_CC) -MMD -MP $(HOST_COMPILEFLAGS) $(HOST_CPPFLAGS) $(MINFS_CFLAGS) -c -o $@ $<

$(LIBFS_OBJS): $(BUILDDIR)/host/%.c.o: %.c
	@echo compiling $@
	@$(MKDIR)
	$(NOECHO)$(HOST_CC) -MMD -MP $(HOST_COMPILEFLAGS) $(HOST_CFLAGS) $(MINFS_CFLAGS) -c -o $@ $<

$(BUILDDIR)/tools/minfs: $(OBJS) $(LIBMINFS_OBJS) $(LIBBITMAP_OBJS) $(LIBFS_OBJS) $(LIBMXCPP_OBJS)
	@echo linking $@
	@$(MKDIR)
	$(NOECHO)$(HOST_CC) $(MINFS_LDFLAGS) -o $@ $^

GENERATED += $(OBJS) $(LIBMINFS_OBJS) $(LIBBITMAP_OBJS) $(LIBMXCPP_OBJS) $(LIBFS_OBJS)
GENERATED += $(MINFS_TOOLS)
EXTRA_BUILDDEPS += $(MINFS_TOOLS)
