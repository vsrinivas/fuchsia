# Copyright 2016 The Fuchsia Authors
# Copyright (c) 2008-2015 Travis Geiselbrecht
#
# Use of this source code is governed by a MIT-style
# license that can be found in the LICENSE file or at
# https://opensource.org/licenses/MIT

LKMAKEROOT := .
LKROOT := kernel
LKINC := system third_party
BUILDROOT ?= .
DEFAULT_PROJECT ?= magenta-pc-x86-64
TOOLCHAIN_PREFIX ?=

ENABLE_BUILD_SYSROOT ?= true
# if true, $BUILDDIR/sysroot/{lib,include,...} will be populated with
# public libraries, headers, and other "build artifacts" necessary
# for a toolchain to compile binaries for Magenta.

ENABLE_BUILD_LISTFILES ?= false
# If true, various verbose listings (*.lst, *.sym, *,dump, etc) will
# be generated for the kernel and userspace binaries.  These can be
# useful for debugging, but are large and can slow the build some.

# check if LKROOT is already a part of LKINC list and add it only if it is not
ifneq ($(findstring $(LKROOT),$(LKINC)), $(LKROOT))
LKINC := $(LKROOT) $(LKINC)
endif

export LKMAKEROOT
export LKROOT
export LKINC
export BUILDROOT
export DEFAULT_PROJECT
export TOOLCHAIN_PREFIX
export ENABLE_BUILD_SYSROOT
export ENABLE_BUILD_LISTFILES

# vaneer makefile that calls into the engine with lk as the build root
# if we're the top level invocation, call ourselves with additional args
$(MAKECMDGOALS) _top:
	@$(MAKE) -C $(LKMAKEROOT) --no-print-directory -rR -f $(LKROOT)/engine.mk $(addprefix -I,$(LKINC)) $(MAKECMDGOALS)

.PHONY: _top
