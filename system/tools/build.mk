# Copyright 2016 The Fuchsia Authors
#
# Use of this source code is governed by a MIT-style
# license that can be found in the LICENSE file or at
# https://opensource.org/licenses/MIT

MKBOOTFS := $(BUILDDIR)/tools/mkbootfs
BOOTSERVER := $(BUILDDIR)/tools/bootserver
LOGLISTENER := $(BUILDDIR)/tools/loglistener
NETRUNCMD := $(BUILDDIR)/tools/netruncmd
NETCP := $(BUILDDIR)/tools/netcp
SYSGEN := $(BUILDDIR)/tools/sysgen

TOOLS_CFLAGS := -std=c11 -Wall -Isystem/public -Isystem/private
TOOLS_CXXFLAGS := -std=c++11 -Wall

ALL_TOOLS := $(MKBOOTFS) $(BOOTSERVER) $(LOGLISTENER) $(NETRUNCMD) $(NETCP) $(SYSGEN)

# phony rule to build just the tools
.PHONY: tools
tools: $(ALL_TOOLS)

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
