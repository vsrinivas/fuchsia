# Copyright 2016 The Fuchsia Authors
#
# Use of this source code is governed by a MIT-style
# license that can be found in the LICENSE file or at
# https://opensource.org/licenses/MIT

# check for disallowed options
ifneq ($(MODULE_DEPS),)
$(error $(MODULE) $(MODULE_TYPE) modules must use MODULE_{LIBS,STATIC_LIBS}, not MODULE_DEPS)
endif

# default install location
ifeq ($(MODULE_INSTALL_PATH),)
MODULE_INSTALL_PATH := bin
endif

# ensure that library deps are short-name style
$(foreach d,$(MODULE_LIBS),$(call modname-require-short,$(d)))
$(foreach d,$(MODULE_STATIC_LIBS),$(call modname-require-short,$(d)))

# track the requested install name of the module
ifeq ($(MODULE_NAME),)
MODULE_NAME := $(basename $(notdir $(MODULE)))
endif

MODULE_USERAPP_OBJECT := $(patsubst %.mod.o,%.elf,$(MODULE_OBJECT))
ALLUSER_APPS += $(MODULE_USERAPP_OBJECT)
ALLUSER_MODULES += $(MODULE)

USER_MANIFEST_LINES += $(MODULE_INSTALL_PATH)/$(MODULE_NAME)=$(addsuffix .strip,$(MODULE_USERAPP_OBJECT))
ifeq ($(and $(filter $(subst $(COMMA),$(SPACE),$(USER_DEBUG_MODULES)),$(MODULE_SHORTNAME)),yes),yes)
USER_MANIFEST_DEBUG_INPUTS += $(addsuffix .debug,$(MODULE_USERAPP_OBJECT))
endif

MODULE_ALIBS := $(foreach lib,$(MODULE_STATIC_LIBS),$(call TOBUILDDIR,$(lib))/lib$(notdir $(lib)).a)
MODULE_SOLIBS := $(foreach lib,$(MODULE_LIBS),$(call TOBUILDDIR,$(lib))/lib$(notdir $(lib)).so.abi)

$(MODULE_USERAPP_OBJECT): _OBJS := $(USER_CRT1_OBJ) $(MODULE_OBJS) $(MODULE_EXTRA_OBJS)
$(MODULE_USERAPP_OBJECT): _LIBS := $(MODULE_ALIBS) $(MODULE_SOLIBS)
$(MODULE_USERAPP_OBJECT): _LDFLAGS := $(MODULE_LDFLAGS) $(USERAPP_LDFLAGS)
$(MODULE_USERAPP_OBJECT): $(USER_CRT1_OBJ) $(MODULE_OBJS) $(MODULE_EXTRA_OBJS) $(MODULE_ALIBS) $(MODULE_SOLIBS)
	@$(MKDIR)
	@echo linking userapp $@
	$(NOECHO)$(USER_LD) $(GLOBAL_LDFLAGS) $(ARCH_LDFLAGS) $(_LDFLAGS) \
		$(_OBJS) $(_LIBS) $(LIBGCC) -o $@

EXTRA_IDFILES += $(MODULE_USERAPP_OBJECT).id

# build list and debugging files if asked to
ifeq ($(call TOBOOL,$(ENABLE_BUILD_LISTFILES)),true)
EXTRA_BUILDDEPS += $(MODULE_USERAPP_OBJECT).lst
EXTRA_BUILDDEPS += $(MODULE_USERAPP_OBJECT).sym
endif
