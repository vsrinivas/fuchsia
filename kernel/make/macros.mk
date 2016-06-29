# Copyright 2016 The Fuchsia Authors
# Copyright (c) 2008-2015 Travis Geiselbrecht
#
# Use of this source code is governed by a MIT-style
# license that can be found in the LICENSE file or at
# https://opensource.org/licenses/MIT

# Find the local dir of the make file
GET_LOCAL_DIR    = $(patsubst %/,%,$(dir $(word $(words $(MAKEFILE_LIST)),$(MAKEFILE_LIST))))

# makes sure the target dir exists
MKDIR = if [ ! -d $(dir $@) ]; then mkdir -p $(dir $@); fi

# prepends the BUILD_DIR var to each item in the list
TOBUILDDIR = $(addprefix $(BUILDDIR)/,$(1))
TOMODULEDIR = $(addprefix $(MODULE_BUILDDIR)/,$(1))

# converts specified variable to boolean value
TOBOOL = $(if $(filter-out 0 false,$1),true,false)

COMMA := ,
SPACE :=
SPACE +=

# test if two files are different, replacing the first
# with the second if so
# args: $1 - temporary file to test
#       $2 - file to replace
define TESTANDREPLACEFILE
	if [ -f "$2" ]; then \
		if cmp "$1" "$2"; then \
			rm -f $1; \
		else \
			mv $1 $2; \
		fi \
	else \
		mv $1 $2; \
	fi
endef

# generate a header file at $1 with an expanded variable in $2
define MAKECONFIGHEADER
	$(MKDIR); \
	rm -f $1.tmp; \
	echo \#pragma once > $1.tmp; \
	for d in `echo $($2) | tr '[:lower:]' '[:upper:]'`; do \
		echo "#define $$d" | sed "s/=/\ /g;s/-/_/g;s/\//_/g;s/\./_/g;s/\//_/g;s/C++/CPP/g" >> $1.tmp; \
	done; \
	$(call TESTANDREPLACEFILE,$1.tmp,$1)
endef

# Tools to make module names canonical (expand to a full path relative
# to LKROOT) and to ensure that canonical names exist and are non-ambigous.

# Check that the expansion $(2) of a module name $(1) resolves to exactly
# one canonical name, error out (differently) on 0 or >1 matches
modname-check = $(if $(word 2,$(2)),$(error MODULE $(1): resolves to: $(2)),$(if $(2),$(2),$(error MODULE $(1): unresolvable)))

# First, check if the name resolves directly, in which case, take that.
# Second, wildcard against each LKINC path as a .../ prefix and make
# sure that there is only one match (via modname-check)
modname-make-canonical = $(strip $(if $(wildcard $(1)),$(1),$(call modname-check,$(1),$(foreach pfx,$(LKINC),$(wildcard $(pfx)/$(1))))))


define generate-copy-dst-src
$1: $2
	@$$(MKDIR)
	@echo installing $$@
	$$(NOECHO) cp -f $$< $$@
endef

copy-dst-src = $(eval $(call generate-copy-dst-src,$(strip $1),$(strip $2)))
