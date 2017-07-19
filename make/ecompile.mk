# Copyright 2017 The Fuchsia Authors
#
# Use of this source code is governed by a MIT-style
# license that can be found in the LICENSE file or at
# https://opensource.org/licenses/MIT

# create a separate list of objects per source type
MODULE_CSRCS := $(filter %.c,$(MODULE_SRCS))
MODULE_CPPSRCS := $(filter %.cpp,$(MODULE_SRCS))

MODULE_COBJS := $(call TOMODULEDIR,$(patsubst %.c,%.c.o,$(MODULE_CSRCS)))
MODULE_CPPOBJS := $(call TOMODULEDIR,$(patsubst %.cpp,%.cpp.o,$(MODULE_CPPSRCS)))

MODULE_OBJS := $(MODULE_COBJS) $(MODULE_CPPOBJS)

$(MODULE_OBJS): MODULE_OPTFLAGS:=$(MODULE_OPTFLAGS)
$(MODULE_OBJS): MODULE_COMPILEFLAGS:=$(MODULE_COMPILEFLAGS)
$(MODULE_OBJS): MODULE_CFLAGS:=$(MODULE_CFLAGS)
$(MODULE_OBJS): MODULE_CPPFLAGS:=$(MODULE_CPPFLAGS)
$(MODULE_OBJS): MODULE_SRCDEPS:=$(MODULE_SRCDEPS)

$(MODULE_COBJS): $(MODULE_BUILDDIR)/%.c.o: %.c $(MODULE_SRCDEPS)
	@$(MKDIR)
	$(call BUILDECHO, compiling $<)
	$(NOECHO)$(EFI_CC) $(EFI_OPTFLAGS) $(MODULE_OPTFLAGS) $(EFI_COMPILEFLAGS) $(MODULE_COMPILEFLAGS) $(EFI_CFLAGS) $(MODULE_CFLAGS) $(EFI_INCLUDES) -c $< -MMD -MP -MT $@ -MF $(@:%o=%d) -o $@

$(MODULE_CPPOBJS): $(MODULE_BUILDDIR)/%.cpp.o: %.cpp $(MODULE_SRCDEPS)
	@$(MKDIR)
	$(call BUILDECHO, compiling $<)
	$(NOECHO)$(EFI_CXX) $(EFI_OPTFLAGS) $(MODULE_OPTFLAGS) $(EFI_COMPILEFLAGS) $(MODULE_COMPILEFLAGS) $(EFI_CPPFLAGS) $(MODULE_CPPFLAGS) $(EFI_INCLUDES) -c $< -MMD -MP -MT $@ -MF $(@:%o=%d) -o $@


# clear some variables we set here
MODULE_CSRCS :=
MODULE_CPPSRCS :=
MODULE_COBJS :=
MODULE_CPPOBJS :=

# MODULE_OBJS is passed back
#MODULE_OBJS :=

