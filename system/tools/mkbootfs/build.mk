# Copyright 2016 The Fuchsia Authors
#
# Use of this source code is governed by a MIT-style
# license that can be found in the LICENSE file or at
# https://opensource.org/licenses/MIT

LOCAL_DIR := $(GET_LOCAL_DIR)

MKBOOTFS := $(BUILDDIR)/tools/mkbootfs

TOOLS := $(MKBOOTFS)

# LZ4 host lib
# TODO: set up third_party build rules for system/tools
LZ4_ROOT := third_party/ulib/lz4
LZ4_CFLAGS := -O3 -I$(LZ4_ROOT)/include/lz4
LZ4_SRCS := $(patsubst %,$(LZ4_ROOT)/%, \
    lz4.c lz4frame.c lz4hc.c xxhash.c)
LZ4_OBJS := $(patsubst $(LZ4_ROOT)/%.c,$(BUILDDIR)/tools/lz4/%.o,$(LZ4_SRCS))
LZ4_LIB := $(BUILDDIR)/tools/lz4/liblz4.a

MKBOOTFS_CFLAGS := -I$(LZ4_ROOT)/include/lz4
MKBOOTFS_LDFLAGS := -L$(BUILDDIR)/tools/lz4 -Bstatic -llz4 -Bdynamic

$(BUILDDIR)/tools/lz4/%.o: $(LZ4_ROOT)/%.c
	@echo compiling $@
	@$(MKDIR)
	$(NOECHO)$(HOST_CC) $(HOST_COMPILEFLAGS) $(HOST_CFLAGS) $(LZ4_CFLAGS) -o $@ -c $<

$(LZ4_LIB): $(LZ4_OBJS)
	@echo archiving $@
	@$(MKDIR)
	$(NOECHO)$(HOST_AR) rcs $@ $^

$(BUILDDIR)/tools/mkbootfs: $(LOCAL_DIR)/mkbootfs.c $(LZ4_LIB)
	@echo compiling $@
	@$(MKDIR)
	$(NOECHO)$(HOST_CC) $(HOST_COMPILEFLAGS) $(HOST_CFLAGS) $(MKBOOTFS_CFLAGS) -o $@ $< $(MKBOOTFS_LDFLAGS)

GENERATED += $(TOOLS)
EXTRA_BUILDDEPS += $(TOOLS)

# phony rule to build just the tools
.PHONY: tools
tools: $(TOOLS)
