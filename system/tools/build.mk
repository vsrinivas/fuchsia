# Copyright 2016 The Fuchsia Authors
#
# Use of this source code is governed by a MIT-style
# license that can be found in the LICENSE file or at
# https://opensource.org/licenses/MIT

MKBOOTFS := $(BUILDDIR)/tools/mkbootfs
BOOTSERVER := $(BUILDDIR)/tools/bootserver
LOGLISTENER := $(BUILDDIR)/tools/loglistener
NETRUNCMD := $(BUILDDIR)/tools/netruncmd
NETCP:= $(BUILDDIR)/tools/netcp

TOOLS_CFLAGS := -std=c11 -Wall -Isystem/public -Isystem/private

ALL_TOOLS := $(MKBOOTFS) $(BOOTSERVER) $(LOGLISTENER) $(NETRUNCMD) $(NETCP)

$(BUILDDIR)/tools/%: system/tools/%.c
	@echo compiling $@
	@$(MKDIR)
	$(NOECHO)cc $(TOOLS_CFLAGS) -o $@ $<

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
