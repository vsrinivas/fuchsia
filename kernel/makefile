#
# Copyright 2016 The Fuchsia Authors
# Copyright (c) 2008-2015 Travis Geiselbrecht
#
# Use of this source code is governed by a MIT-style
# license that can be found in the LICENSE file or at
# https://opensource.org/licenses/MIT
#

# the above include may override LKROOT and LKINC to allow external
# directories to be included in the build
-include lk_inc.mk

LKMAKEROOT ?= .
LKROOT ?= .
LKINC ?=
BUILDROOT ?= .
DEFAULT_PROJECT ?=
TOOLCHAIN_PREFIX ?=

# check if LKROOT is already a part of LKINC list and add it only if it is not
ifneq ($(findstring $(LKROOT),$(LKINC)), $(LKROOT))
LKINC := $(LKROOT) $(LKINC)
endif

# add the external path to LKINC
ifneq ($(LKROOT),.)
LKINC += $(LKROOT)/external
else
LKINC += external
endif

export LKMAKEROOT
export LKROOT
export LKINC
export BUILDROOT
export DEFAULT_PROJECT
export TOOLCHAIN_PREFIX

# vaneer makefile that calls into the engine with lk as the build root
# if we're the top level invocation, call ourselves with additional args
$(MAKECMDGOALS) _top:
	@$(MAKE) -C $(LKMAKEROOT) -rR -f $(LKROOT)/engine.mk $(addprefix -I,$(LKINC)) $(MAKECMDGOALS)

.PHONY: _top
