# Copyright 2018 The Fuchsia Authors
# Copyright (c) 2008-2015 Travis Geiselbrecht
#
# Use of this source code is governed by a MIT-style
# license that can be found in the LICENSE file or at
# https://opensource.org/licenses/MIT

MODULE_FIDLSRCS := $(filter %.fidl,$(MODULE_SRCS))

MODULE_FIDL_RSP := $(MODULE_GENDIR)/fidl.rsp
MODULE_FIDL_LIB_PATH := $(subst .,/,$(MODULE_FIDL_LIBRARY))
MODULE_FIDL_INCLUDE := $(MODULE_GENDIR)/include
MODULE_FIDL_H := $(MODULE_FIDL_INCLUDE)/$(MODULE_FIDL_LIB_PATH)/c/fidl.h
MODULE_FIDL_CPP := $(MODULE_GENDIR)/src/tables.cpp
MODULE_FIDL_CLIENT_CPP := $(MODULE_GENDIR)/src/client.cpp
MODULE_FIDL_OBJ := $(MODULE_GENDIR)/obj/tables.cpp.o $(MODULE_GENDIR)/obj/client.cpp.o

MODULE_SRCDEPS += $(MODULE_FIDL_H) $(MODULE_FIDL_CPP) $(MODULE_FIDL_CLIENT_C)
MODULE_GEN_HDR += $(MODULE_FIDL_H)

#$(info MODULE_FIDLSRCS = $(MODULE_FIDLSRCS))

$(MODULE_FIDL_OBJ): MODULE_OPTFLAGS:=$(MODULE_OPTFLAGS)
$(MODULE_FIDL_OBJ): MODULE_COMPILEFLAGS:=$(MODULE_COMPILEFLAGS) -Isystem/ulib/fidl/include $(foreach dep,$(MODULE_FIDL_DEPS),-I$(MODULE_FIDL_INCLUDE_$(dep))) -I$(MODULE_FIDL_INCLUDE)
$(MODULE_FIDL_OBJ): MODULE_CPPFLAGS:=$(MODULE_CPPFLAGS)
$(MODULE_FIDL_OBJ): MODULE_SRCDEPS:=$(MODULE_SRCDEPS)

$(MODULE_FIDL_OBJ): $(MODULE_GENDIR)/obj/%.cpp.o: $(MODULE_GENDIR)/src/%.cpp $(MODULE_SRCDEPS)
	@$(MKDIR)
	$(call BUILDECHO, compiling $<)
	$(NOECHO)$(CC) $(GLOBAL_OPTFLAGS) $(MODULE_OPTFLAGS) $(GLOBAL_COMPILEFLAGS) $(USER_COMPILEFLAGS) $(ARCH_COMPILEFLAGS) $(MODULE_COMPILEFLAGS) $(GLOBAL_ASMFLAGS) $(USER_ASMFLAGS) $(ARCH_ASMFLAGS) $(MODULE_OPTFLAGS) $(MODULE_ASMFLAGS) $(GLOBAL_INCLUDES) -c $< -MMD -MP -MT $@ -MF $(@:%o=%d) -o $@

$(MODULE_FIDL_RSP): FIDL_NAME:=$(MODULE_FIDL_LIBRARY)
$(MODULE_FIDL_RSP): FIDL_H:=$(MODULE_FIDL_H)
$(MODULE_FIDL_RSP): FIDL_CPP:=$(MODULE_FIDL_CPP)
$(MODULE_FIDL_RSP): FIDL_CLIENT_CPP:=$(MODULE_FIDL_CLIENT_CPP)
$(MODULE_FIDL_RSP): FIDL_SRCS:=$(MODULE_FIDLSRCS)
$(MODULE_FIDL_RSP): FIDL_DEPS:=$(foreach dep,$(MODULE_FIDL_DEPS),--files $(MODULE_FIDL_SRCS_$(dep)))
$(MODULE_FIDL_RSP): $(MODULE_FIDLSRCS)
	@$(MKDIR)
	$(NOECHO)echo --name $(FIDL_NAME) --c-header $(FIDL_H) --c-client $(FIDL_CLIENT_CPP) --tables $(FIDL_CPP) $(FIDL_DEPS) --files $(FIDL_SRCS) > $@

# $@ only lists one of the multiple targets, so we use $< (first dep) to
# compute the (related) destination directories to create
%/gen/include/$(MODULE_FIDL_LIB_PATH)/c/fidl.h %/gen/src/tables.cpp %/gen/src/client.cpp: %/gen/fidl.rsp $(FIDL)
	$(call BUILDECHO, generating fidl from $<)
	@mkdir -p $(<D)/include $(<D)/src
	$(NOECHO)$(FIDL) @$<

EXTRA_BUILDDEPS += make/fcompile.mk
GENERATED += $(MODULE_FIDL_H) $(MODULE_FIDL_CPP) $(MODULE_FIDL_CLIENT_CPP)

# clear some variables we set here
MODULE_FIDLSRCS :=
MODULE_FIDL_LIB_PATH :=
MODULE_FIDL_INCLUDE :=
MODULE_FIDL_H :=
MODULE_FIDL_CPP :=
MODULE_FIDL_CLIENT_CPP :=
MODULE_FIDL_RSP :=

# MODULE_FIDL_OBJ is passed back
#MODULE_FIDL_OBJ :=
