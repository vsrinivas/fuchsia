# Copyright 2017 The Fuchsia Authors
#
# Use of this source code is governed by a MIT-style
# license that can be found in the LICENSE file or at
# https://opensource.org/licenses/MIT

LOCAL_DIR := $(GET_LOCAL_DIR)

MDIGEN := $(BUILDDIR)/tools/mdigen
MDIDUMP := $(BUILDDIR)/tools/mdidump
MDI_CFLAGS := -Isystem/public

TOOLS := $(MDIGEN) $(MDIDUMP)

SRC := \
	$(LOCAL_DIR)/mdigen.cpp \
	$(LOCAL_DIR)/node.cpp \
	$(LOCAL_DIR)/parser.cpp \
	$(LOCAL_DIR)/tokens.cpp \

HEADERS := \
	$(LOCAL_DIR)/node.h \
	$(LOCAL_DIR)/parser.h \
	$(LOCAL_DIR)/tokens.h \
	system/public/magenta/mdi.h \

$(MDIGEN): $(SRC) $(HEADERS)
	@echo compiling $@
	@$(MKDIR)
	$(NOECHO)$(HOST_CXX) $(HOST_COMPILEFLAGS) $(HOST_CPPFLAGS) $(MDI_CFLAGS) -o $@ $(SRC)

$(MDIDUMP): $(LOCAL_DIR)/mdidump.cpp $(HEADERS)
	@echo compiling $@
	@$(MKDIR)
	$(NOECHO)$(HOST_CXX) $(HOST_COMPILEFLAGS) $(HOST_CPPFLAGS) $(MDI_CFLAGS) -o $@ $<

GENERATED += $(TOOLS)
EXTRA_BUILDDEPS += $(TOOLS)

# phony rule to build just the tools
.PHONY: tools
tools: $(TOOLS)
