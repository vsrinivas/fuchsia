# Copyright 2016 The Fuchsia Authors
#
# Use of this source code is governed by a MIT-style
# license that can be found in the LICENSE file or at
# https://opensource.org/licenses/MIT

# check for disallowed options
ifneq ($(MODULE_DEPS),)
$(error $(MODULE) $(MODULE_TYPE) modules must use MODULE_{LIBS,STATIC_LIBS}, not MODULE_DEPS)
endif
ifneq ($(MODULE_HOST_LIBS)$(MODULE_HOST_SYSLIBS),)
$(error $(MODULE) $(MODULE_TYPE) modules must not use MODULE_{LIBS,STATIC_LIBS}, not MODULE_HOST_{LIBS,SYSLIBS})
endif

# default install location
ifeq ($(MODULE_INSTALL_PATH),)
MODULE_INSTALL_PATH := bin
endif

MODULE_USERAPP_OBJECT := $(patsubst %.mod.o,%.elf,$(MODULE_OBJECT))
ALLUSER_APPS += $(MODULE_USERAPP_OBJECT)
ALLUSER_MODULES += $(MODULE)

USER_MANIFEST_LINES += $(MODULE_GROUP)$(MODULE_INSTALL_PATH)/$(MODULE_NAME)=$(addsuffix .strip,$(MODULE_USERAPP_OBJECT))

# These debug info files go in the bootfs image.
ifeq ($(and $(filter $(subst $(COMMA),$(SPACE),$(BOOTFS_DEBUG_MODULES)),$(MODULE)),yes),yes)
USER_MANIFEST_DEBUG_INPUTS += $(MODULE_USERAPP_OBJECT)
endif

MODULE_ALIBS := $(foreach lib,$(MODULE_STATIC_LIBS),$(call TOBUILDDIR,$(lib))/lib$(notdir $(lib)).a)
MODULE_SOLIBS := $(foreach lib,$(MODULE_LIBS),$(call TOBUILDDIR,$(lib))/lib$(notdir $(lib)).so.abi)

# Include this in every link.
MODULE_EXTRA_OBJS += scripts/dso_handle.ld

# Link the ASan runtime into everything compiled with ASan.
ifeq (,$(filter -fno-sanitize=all,$(MODULE_COMPILEFLAGS)))
MODULE_EXTRA_OBJS += $(ASAN_SOLIB)
endif

$(MODULE_USERAPP_OBJECT): _OBJS := $(USER_CRT1_OBJ) $(MODULE_OBJS) $(MODULE_EXTRA_OBJS)
$(MODULE_USERAPP_OBJECT): _LIBS := $(MODULE_ALIBS) $(MODULE_SOLIBS)
$(MODULE_USERAPP_OBJECT): _LDFLAGS := $(MODULE_LDFLAGS) $(USERAPP_LDFLAGS)
$(MODULE_USERAPP_OBJECT): $(USER_CRT1_OBJ) $(MODULE_OBJS) $(MODULE_EXTRA_OBJS) $(MODULE_ALIBS) $(MODULE_SOLIBS)
	@$(MKDIR)
	$(call BUILDECHO,linking userapp $@)
	$(NOECHO)$(USER_LD) $(GLOBAL_LDFLAGS) $(ARCH_LDFLAGS) $(_LDFLAGS) \
		$(_OBJS) $(_LIBS) $(LIBGCC) -o $@

EXTRA_IDFILES += $(MODULE_USERAPP_OBJECT).id

# build list and debugging files if asked to
ifeq ($(ENABLE_BUILD_LISTFILES),true)
EXTRA_BUILDDEPS += $(MODULE_USERAPP_OBJECT).lst
EXTRA_BUILDDEPS += $(MODULE_USERAPP_OBJECT).sym
endif
