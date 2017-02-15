# Copyright 2016 The Fuchsia Authors
#
# Use of this source code is governed by a MIT-style
# license that can be found in the LICENSE file or at
# https://opensource.org/licenses/MIT

LOCAL_DIR := $(GET_LOCAL_DIR)

SYSGEN := $(BUILDDIR)/tools/sysgen

TOOLS := $(SYSGEN)

$(BUILDDIR)/tools/%:  $(LOCAL_DIR)/%.cpp
	@echo compiling $@
	@$(MKDIR)
	$(NOECHO)$(HOST_CXX) $(HOST_COMPILEFLAGS) $(HOST_CPPFLAGS) -o $@ $<

GENERATED += $(TOOLS)
EXTRA_BUILDDEPS += $(TOOLS)

# phony rule to build just the tools
.PHONY: tools
tools: $(TOOLS)
