# Copyright 2016 The Fuchsia Authors
#
# Use of this source code is governed by a MIT-style
# license that can be found in the LICENSE file or at
# https://opensource.org/licenses/MIT

ifneq ($(MODULE_DEPS)$(MODULE_HOST_LIBS)$(MODULE_HOST_SYSLIBS),)
$(error $(MODULE) $(MODULE_TYPE) fidl modules must use MODULE_FIDL_DEPS)
endif

# build static library
$(MODULE_LIBNAME).a: $(MODULE_FIDL_OBJ)
	@$(MKDIR)
	$(call BUILDECHO,linking $@)
	@rm -f -- "$@"
	$(call BUILDCMD,$(AR),cr $@ $^)

# always build all libraries
EXTRA_BUILDDEPS += $(MODULE_LIBNAME).a make/module-fidl.mk
GENERATED += $(MODULE_LIBNAME).a

MODULE_RULESMK := $(MODULE_SRCDIR)/rules.mk

ifeq ($(filter fidl,$(MODULE_EXPORT)),fidl)
MODULE_PACKAGE += $(sort $(MODULE_PACKAGE) fidl)
endif

# Export a list of files for dependent libraries to consume.
MODULE_FIDL_SRCS_$(MODULE) := $(MODULE_SRCS)
MODULE_FIDL_INCLUDE_$(MODULE) := $(MODULE_GENDIR)/include

ifneq ($(strip $(MODULE_PACKAGE)),)

MODULE_PKG_FILE := $(MODULE_BUILDDIR)/$(MODULE_NAME).pkg
MODULE_EXP_FILE := $(BUILDDIR)/export/$(MODULE_NAME).pkg

MODULE_PKG_INCS += $(foreach inc,$(sort $(ABIGEN_PUBLIC_HEADERS)),$(patsubst $(ABIGEN_BUILDDIR)/%,%,$(inc))=$(patsubst $(BUILDDIR)/%,BUILD/%,$(inc)))

MODULE_PKG_SRCS := $(MODULE_SRCS)
MODULE_PKG_DEPS := $(MODULE_FIDL_DEPS)

ifneq ($(strip $(MODULE_PKG_DEPS)),)
MODULE_PKG_DEPS := $(foreach dep,$(MODULE_FIDL_DEPS),$(patsubst system/fidl/%,%,$(dep))=SOURCE/$(dep))
endif

ifneq ($(strip $(MODULE_PKG_SRCS)),)
MODULE_PKG_SRCS := $(foreach src,$(MODULE_PKG_SRCS),$(patsubst $(MODULE_SRCDIR)/%,%,$(src))=SOURCE/$(src))
MODULE_PKG_TAG := "[fidl]"
endif

$(MODULE_PKG_FILE): _NAME := $(MODULE_NAME)
$(MODULE_PKG_FILE): _LIBRARY := $(MODULE_FIDL_LIBRARY)
$(MODULE_PKG_FILE): _SRCS := $(if $(MODULE_PKG_SRCS),$(MODULE_PKG_TAG) $(sort $(MODULE_PKG_SRCS)))
$(MODULE_PKG_FILE): _DEPS := $(if $(MODULE_PKG_DEPS),"[fidl-deps]" $(sort $(MODULE_PKG_DEPS)))
$(MODULE_PKG_FILE): $(MODULE_RULESMK) make/module-fidl.mk
	@$(call BUILDECHO,creating fidl library package $@ ;)\
	$(MKDIR) ;\
	echo "[package]" > $@ ;\
	echo "name=$(_NAME)" >> $@ ;\
	echo "library=$(_LIBRARY)" >> $@ ;\
	echo "arch=fidl" >> $@ ;\
	echo "type=fidl" >> $@ ;\
	for i in $(_SRCS) ; do echo $$i >> $@ ; done ;\
	for i in $(_DEPS) ; do echo $$i >> $@ ; done


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

endif # // ifneq ($(strip $(MODULE_PACKAGE)),)
