# Copyright 2017 The Fuchsia Authors
#
# Use of this source code is governed by a MIT-style
# license that can be found in the LICENSE file or at
# https://opensource.org/licenses/MIT

# check for disallowed options
ifneq ($(MODULE_DEPS)$(MODULE_LIBS)$(MODULE_STATIC_LIBS),)
$(error $(MODULE) $(MODULE_TYPE) modules must not use MODULE_{DEPS,LIBS,STATIC_LIBS})
endif

MODULE_HOSTAPP_BIN := $(BUILDDIR)/tools/$(MODULE_NAME)

MODULE_ALIBS := $(foreach lib,$(MODULE_HOST_LIBS), \
                          $(call TOBUILDDIR,tools/lib/lib$(notdir $(lib)).a))

$(MODULE_HOSTAPP_BIN): _OBJS := $(MODULE_OBJS)
$(MODULE_HOSTAPP_BIN): _LIBS := $(MODULE_ALIBS) $(MODULE_HOST_SYSLIBS)
$(MODULE_HOSTAPP_BIN): $(MODULE_OBJS) $(MODULE_ALIBS)
	@$(MKDIR)
	$(call BUILDECHO,linking hostapp $@)
	$(NOECHO)$(HOST_CXX) -o $@ $(HOST_COMPILEFLAGS) $(HOST_LDFLAGS) \
		$(_OBJS) $(_LIBS)

ALLHOST_APPS += $(MODULE_HOSTAPP_BIN)

GENERATED += $(MODULE_HOSTAPP_BIN)
