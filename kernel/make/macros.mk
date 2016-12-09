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
# $3 provides an (optional) raw footer to append to the end
define MAKECONFIGHEADER
	$(MKDIR); \
	rm -f $1.tmp; \
	echo \#pragma once > $1.tmp; \
	for d in `echo $($2) | tr '[:lower:]' '[:upper:]'`; do \
		echo "#define $$d" | sed "s/=/\ /g;s/-/_/g;s/\//_/g;s/\./_/g;s/\//_/g;s/C++/CPP/g" >> $1.tmp; \
	done; \
	echo "$3" >> $1.tmp; \
	$(call TESTANDREPLACEFILE,$1.tmp,$1)
endef

# Tools to make module names canonical (expand to a full path relative
# to LKROOT) and to ensure that canonical names exist and are non-ambigous.

# Check that the expansion $(2) of a module name $(1) resolves to exactly
# one canonical name, error out (differently) on 0 or >1 matches
modname-check = $(if $(word 2,$(2)),$(error MODULE $(1): resolves to: $(2)),$(if $(strip $(2)),$(2),$(error MODULE $(1): unresolvable)))

# First, check if the name resolves directly, in which case, take that.
# Second, wildcard against each LKINC path as a .../ prefix and make
# sure that there is only one match (via modname-check).  If there is
# no match and the name ends in -static, then try canonicalizing the
# name sans -static and then append -static to the canonicalized name.
modname-make-canonical = \
    $(call modname-check,$(1),$(call modname-find-canonical,$(1)))
modname-find-canonical = \
    $(or $(call modname-expand,$(1)),\
         $(if $(filter %-static,$(1)),\
	      $(addsuffix -static,$(call modname-expand,$(1:%-static=%)))))
modname-expand = \
    $(if $(wildcard $(1)),$(1),$(wildcard $(addsuffix /$(1),$(LKINC))))


# Convert a canonical (full-path) module name to a short (as used
# in the builddir) module name
#
# Recursively strips all the top level directory prefixes
modname-make-short- = \
    $(if $(strip $(2)),\
        $(call modname-make-short-,\
            $(patsubst $(firstword $(2))%,%,$(1)),\
            $(wordlist 2,100,$(2))),\
        $(1))

modname-make-short = $(strip $(call modname-make-short-,$(1),$(LKPREFIXES)))

# Verify that the module name is a short name by checking
# for the presence of any of the top level directory prefixes
modname-require-short = \
    $(if $(filter $(LKPATTERNS),$(1)),\
        $(error $(MODULE): full path module name $(1) is invalid here),)

define generate-copy-dst-src
$1: $2
	@$$(MKDIR)
	@echo installing $$@
	$$(NOECHO) cp -f $$< $$@
endef

copy-dst-src = $(eval $(call generate-copy-dst-src,$(strip $1),$(strip $2)))

