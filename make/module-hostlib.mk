# Copyright 2017 The Fuchsia Authors
#
# Use of this source code is governed by a MIT-style
# license that can be found in the LICENSE file or at
# https://opensource.org/licenses/MIT

# check for disallowed options
ifneq ($(MODULE_DEPS)$(MODULE_LIBS)$(MODULE_STATIC_LIBS),)
$(error $(MODULE) $(MODULE_TYPE) modules must not use MODULE_{DEPS,LIBS,STATIC_LIBS})
endif

MODULE_HOSTLIB := $(call TOBUILDDIR,tools/lib/lib$(MODULE_NAME).a)

$(MODULE_HOSTLIB): $(MODULE_OBJS)
	@$(MKDIR)
	$(call BUILDECHO,linking hostlib $@)
	$(NOECHO)rm -f -- "$@"
	$(NOECHO)$(HOST_AR) cr $@ $^

ALLHOST_LIBS += $(MODULE_HOSTLIB)

GENERATED += $(MODULE_HOSTLIB)

