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
$(MODULE_HOSTAPP_BIN): _LDFLAGS := $(foreach opt,$(MODULE_LDFLAGS),-Wl,$(opt))
$(MODULE_HOSTAPP_BIN): $(MODULE_OBJS) $(MODULE_ALIBS)
	@$(MKDIR)
	$(call BUILDECHO,linking hostapp $@)
	$(NOECHO)$(HOST_CXX) -o $@ $(HOST_COMPILEFLAGS) $(_LDFLAGS) $(HOST_LDFLAGS) \
		$(_OBJS) $(_LIBS)

ifeq ($(filter bin,$(MODULE_PACKAGE)),bin)
MODULE_PKG_FILE := $(MODULE_BUILDDIR)/$(MODULE_NAME).pkg
MODULE_EXP_FILE := $(BUILDDIR)/export/$(MODULE_NAME).pkg

MODULE_PKG_BIN := $(MODULE_NAME)=$(patsubst $(BUILDDIR)/%,BUILD/%,$(MODULE_HOSTAPP_BIN))

$(MODULE_PKG_FILE): _NAME := $(MODULE_NAME)
$(MODULE_PKG_FILE): _BIN := $(MODULE_PKG_BIN)
$(MODULE_PKG_FILE): $(MODULE_RULESMK) make/module-hostapp.mk
	@$(call BUILDECHO,creating package $@ ;)\
	$(MKDIR) ;\
	echo "[package]" > $@ ;\
	echo "name=$(_NAME)" >> $@ ;\
	echo "type=tool" >> $@ ;\
	echo "arch=host" >> $@ ;\
	echo "[bin]" >> $@ ;\
	echo "$(_BIN)" >> $@

$(MODULE_EXP_FILE): $(MODULE_PKG_FILE)
	@$(MKDIR) ;\
	if [ -f "$@" ]; then \
		if ! cmp "$<" "$@" >/dev/null 2>&1; then \
			$(if $(BUILDECHO),echo installing $@ ;)\
			cp -f $< $@; \
		fi \
	else \
		$(if $(BUILDECHO),echo installing $@ ;)\
		cp -f $< $@; \
	fi

GENERATED += $(MODULE_EXP_FILE) $(MODULE_PKG_FILE)
ALLPKGS += $(MODULE_EXP_FILE)

endif


ALLHOST_APPS += $(MODULE_HOSTAPP_BIN)

GENERATED += $(MODULE_HOSTAPP_BIN)
