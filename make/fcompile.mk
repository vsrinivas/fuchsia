# Copyright 2018 The Fuchsia Authors
# Copyright (c) 2008-2015 Travis Geiselbrecht
#
# Use of this source code is governed by a MIT-style
# license that can be found in the LICENSE file or at
# https://opensource.org/licenses/MIT

MODULE_FIDLSRCS := $(filter %.fidl,$(MODULE_SRCS))

# TODO: should be more like include/foo/fidl/library.h
MODULE_FIDL_RSP := $(MODULE_GENDIR)/fidl.rsp
MODULE_FIDL_H := $(MODULE_GENDIR)/include/library.h
MODULE_FIDL_CPP := $(MODULE_GENDIR)/src/library.cpp
MODULE_FIDL_OBJ := $(MODULE_GENDIR)/obj/library.cpp.o

MODULE_SRCDEPS += $(MODULE_FIDL_H) $(MODULE_FIDL_CPP)
MODULE_GEN_HDR += $(MODULE_FIDL_H)

MODULE_OBJS := $(MODULE_FIDL_OBJ)

#$(info MODULE_FIDLSRCS = $(MODULE_FIDLSRCS))

$(MODULE_OBJS): MODULE_OPTFLAGS:=$(MODULE_OPTFLAGS)
$(MODULE_OBJS): MODULE_COMPILEFLAGS:=$(MODULE_COMPILEFLAGS)
$(MODULE_OBJS): MODULE_CFLAGS:=$(MODULE_CFLAGS)
$(MODULE_OBJS): MODULE_CPPFLAGS:=$(MODULE_CPPFLAGS)
$(MODULE_OBJS): MODULE_ASMFLAGS:=$(MODULE_ASMFLAGS)
$(MODULE_OBJS): MODULE_SRCDEPS:=$(MODULE_SRCDEPS)

$(MODULE_FIDL_OBJ): MODULE_OPTFLAGS:=$(MODULE_OPTFLAGS)
$(MODULE_FIDL_OBJ): MODULE_COMPILEFLAGS:=$(MODULE_COMPILEFLAGS)
$(MODULE_FIDL_OBJ): MODULE_CPPFLAGS:=$(MODULE_CPPFLAGS)
$(MODULE_FIDL_OBJ): MODULE_SRCDEPS:=$(MODULE_SRCDEPS)

$(MODULE_FIDL_OBJ): $(MODULE_GENDIR)/obj/%.cpp.o: $(MODULE_GENDIR)/src/%.cpp $(MODULE_SRCDEPS)
	@$(MKDIR)
	$(call BUILDECHO, compiling $<)
	$(NOECHO)$(CC) $(GLOBAL_OPTFLAGS) $(MODULE_OPTFLAGS) $(GLOBAL_COMPILEFLAGS) $(USER_COMPILEFLAGS) $(ARCH_COMPILEFLAGS) $(MODULE_COMPILEFLAGS) $(GLOBAL_ASMFLAGS) $(USER_ASMFLAGS) $(ARCH_ASMFLAGS) $(MODULE_OPTFLAGS) $(MODULE_ASMFLAGS) $(GLOBAL_INCLUDES) -c $< -MMD -MP -MT $@ -MF $(@:%o=%d) -o $@

$(MODULE_FIDL_RSP): FIDL_H:=$(MODULE_FIDL_H)
$(MODULE_FIDL_RSP): FIDL_CPP:=$(MODULE_FIDL_CPP)
$(MODULE_FIDL_RSP): FIDL_SRCS:=$(MODULE_FIDLSRCS)
$(MODULE_FIDL_RSP): $(MODULE_FIDLSRCS)
	@$(MKDIR)
	$(NOECHO)echo --c-header $(FIDL_H) --tables $(FIDL_CPP) --files $(FIDL_SRCS) > $@

# $@ only lists one of the multiple targets, so we use $< (first dep) to
# compute the (related) destination directories to create
%/gen/include/library.h %/gen/src/library.cpp: %/gen/fidl.rsp $(FIDL)
	$(call BUILDECHO, generating fidl from $<)
	@mkdir -p $(<D)/include $(<D)/src
	$(NOECHO)$(FIDL) @$<

GENERATED += $(MODULE_FIDL_H) $(MODULE_FIDL_CPP)

# clear some variables we set here
MODULE_FIDLSRCS :=
MODULE_FIDL_H :=
MODULE_FIDL_CPP :=
MODULE_FIDL_RSP :=
MODULE_FIDL_OBJ :=

# MODULE_OBJS is passed back
#MODULE_OBJS :=

