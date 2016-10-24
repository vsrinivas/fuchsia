# Copyright 2016 The Fuchsia Authors
#
# Use of this source code is governed by a MIT-style
# license that can be found in the LICENSE file or at
# https://opensource.org/licenses/MIT

BOOTSERVER := $(BUILDDIR)/tools/bootserver
LOGLISTENER := $(BUILDDIR)/tools/loglistener
NETRUNCMD := $(BUILDDIR)/tools/netruncmd
NETCP := $(BUILDDIR)/tools/netcp
SYSGEN := $(BUILDDIR)/tools/sysgen

TOOLS_CFLAGS := -g -std=c11 -Wall -Isystem/public -Isystem/private
TOOLS_CXXFLAGS := -std=c++11 -Wall

ALL_TOOLS := $(BOOTSERVER) $(LOGLISTENER) $(NETRUNCMD) $(NETCP) $(SYSGEN)

# LZ4 host lib
# TODO: set up third_party build rules for system/tools
LZ4_ROOT := third_party/ulib/lz4
LZ4_CFLAGS := -O3 -I$(LZ4_ROOT)/include/lz4
LZ4_SRCS := $(patsubst %,$(LZ4_ROOT)/%, \
    lz4.c lz4frame.c lz4hc.c xxhash.c)
LZ4_OBJS := $(patsubst $(LZ4_ROOT)/%.c,$(BUILDDIR)/tools/lz4/%.o,$(LZ4_SRCS))
LZ4_LIB := $(BUILDDIR)/tools/lz4/liblz4.a

$(BUILDDIR)/tools/lz4/%.o: $(LZ4_ROOT)/%.c
	@echo compiling $@
	@$(MKDIR)
	$(NOECHO)cc $(TOOLS_CFLAGS) $(LZ4_CFLAGS) -o $@ -c $<

$(LZ4_LIB): $(LZ4_OBJS)
	@echo archiving $@
	@$(MKDIR)
	$(NOECHO)ar rcs $@ $^

# mkbootfs
MKBOOTFS := $(BUILDDIR)/tools/mkbootfs
MKBOOTFS_CFLAGS := $(TOOLS_CFLAGS) -I$(LZ4_ROOT)/include/lz4
MKBOOTFS_LDFLAGS := -L$(BUILDDIR)/tools/lz4 -Bstatic -llz4 -Bdynamic

$(BUILDDIR)/tools/mkbootfs: system/tools/mkbootfs.c $(LZ4_LIB)
	@echo compiling $@
	@$(MKDIR)
	$(NOECHO)cc $(MKBOOTFS_CFLAGS) -o $@ $< $(MKBOOTFS_LDFLAGS)

ALL_TOOLS += $(MKBOOTFS)

# remaining host tools
$(BUILDDIR)/tools/%: system/tools/%.c
	@echo compiling $@
	@$(MKDIR)
	$(NOECHO)cc $(TOOLS_CFLAGS) -o $@ $<

$(BUILDDIR)/tools/%: system/tools/%.cpp
	@echo compiling $@
	@$(MKDIR)
	$(NOECHO)c++ $(TOOLS_CXXFLAGS) -o $@ $<

$(BUILDDIR)/tools/netruncmd: system/tools/netruncmd.c system/tools/netprotocol.c
	@echo compiling $@
	@$(MKDIR)
	$(NOECHO)cc $(TOOLS_CFLAGS) -o $@ $^

$(BUILDDIR)/tools/netcp: system/tools/netcp.c system/tools/netprotocol.c
	@echo compiling $@
	@$(MKDIR)
	$(NOECHO)cc $(TOOLS_CFLAGS) -o $@ $^

GENERATED += $(ALL_TOOLS)
EXTRA_BUILDDEPS += $(ALL_TOOLS)

# phony rule to build just the tools
.PHONY: tools
tools: $(ALL_TOOLS)
