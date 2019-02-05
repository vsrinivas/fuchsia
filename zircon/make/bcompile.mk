# Copyright 2018 The Fuchsia Authors
# Copyright (c) 2008-2015 Travis Geiselbrecht
#
# Use of this source code is governed by a MIT-style
# license that can be found in the LICENSE file or at
# https://opensource.org/licenses/MIT

MODULE_BANJOSRCS := $(filter %.banjo,$(MODULE_SRCS))

MODULE_BANJO_FILE_LIST := $(MODULE_GENDIR)/banjo-files
MODULE_BANJO_C_RSP := $(MODULE_GENDIR)/banjo_c.rsp
MODULE_BANJO_CPP_RSP := $(MODULE_GENDIR)/banjo_cpp.rsp
MODULE_BANJO_CPP_INTERNAL_RSP := $(MODULE_GENDIR)/banjo_cpp_i.rsp
MODULE_BANJO_INCLUDE := $(MODULE_GENDIR)/include
MODULE_BANJO_C_HEADER := $(MODULE_BANJO_INCLUDE)/ddk/protocol/$(MODULE_BANJO_NAME).h
MODULE_BANJO_CPP_HEADER := $(MODULE_BANJO_INCLUDE)/ddktl/protocol/$(MODULE_BANJO_NAME).h
MODULE_BANJO_CPP_INTERNAL_HEADER := $(MODULE_BANJO_INCLUDE)/ddktl/protocol/$(MODULE_BANJO_NAME)-internal.h

MODULE_SRCDEPS += $(MODULE_BANJO_C_HEADER) $(MODULE_BANJO_CPP_HEADER) $(MODULE_BANJO_CPP_INTERNAL_HEADER)
MODULE_GEN_HDR += $(MODULE_BANJO_C_HEADER) $(MODULE_BANJO_CPP_HEADER) $(MODULE_BANJO_CPP_INTERNAL_HEADER)

# There is probably a more correct way to express this dependency, but having
# this dependency here makes the build non-flakey.
MODULE_SRCDEPS += $(BUILDDIR)/gen/abigen-stamp

#$(info MODULE_BANJOSRCS = $(MODULE_BANJOSRCS))

$(MODULE_BANJO_FILE_LIST): BANJO_SRCS:=$(MODULE_BANJOSRCS)
$(MODULE_BANJO_FILE_LIST):
	@$(MKDIR)
	$(NOECHO)echo $(BANJO_SRCS) > $@

$(MODULE_BANJO_C_RSP): BANJO_DEPS:=$(MODULE_BANJO_DEPS)
$(MODULE_BANJO_C_RSP): BANJO_NAME:=$(MODULE_BANJO_LIBRARY)
$(MODULE_BANJO_C_RSP): BANJO_HEADER:=$(MODULE_BANJO_C_HEADER)
$(MODULE_BANJO_C_RSP): BANJO_SRCS:=$(MODULE_BANJOSRCS)
$(MODULE_BANJO_C_RSP): $(foreach dep,$(MODULE_BANJO_DEPS),$(call TOBUILDDIR,$(dep))/gen/banjo-files) $(MODULE_BANJOSRCS) make/bcompile.mk
	@$(MKDIR)
	$(NOECHO)echo --name $(BANJO_NAME) --backend c --output $(BANJO_HEADER) $(foreach dep,$(BANJO_DEPS),--files $(shell cat $(call TOBUILDDIR,$(dep))/gen/banjo-files)) --files $(BANJO_SRCS) > $@

$(MODULE_BANJO_CPP_RSP): BANJO_DEPS:=$(MODULE_BANJO_DEPS)
$(MODULE_BANJO_CPP_RSP): BANJO_NAME:=$(MODULE_BANJO_LIBRARY)
$(MODULE_BANJO_CPP_RSP): BANJO_HEADER:=$(MODULE_BANJO_CPP_HEADER)
$(MODULE_BANJO_CPP_RSP): BANJO_SRCS:=$(MODULE_BANJOSRCS)
$(MODULE_BANJO_CPP_RSP): $(foreach dep,$(MODULE_BANJO_DEPS),$(call TOBUILDDIR,$(dep))/gen/banjo-files) $(MODULE_BANJOSRCS) make/bcompile.mk
	@$(MKDIR)
	$(NOECHO)echo --name $(BANJO_NAME) --backend cpp --output $(BANJO_HEADER) $(foreach dep,$(BANJO_DEPS),--files $(shell cat $(call TOBUILDDIR,$(dep))/gen/banjo-files)) --files $(BANJO_SRCS) > $@

$(MODULE_BANJO_CPP_INTERNAL_RSP): BANJO_DEPS:=$(MODULE_BANJO_DEPS)
$(MODULE_BANJO_CPP_INTERNAL_RSP): BANJO_NAME:=$(MODULE_BANJO_LIBRARY)
$(MODULE_BANJO_CPP_INTERNAL_RSP): BANJO_HEADER:=$(MODULE_BANJO_CPP_INTERNAL_HEADER)
$(MODULE_BANJO_CPP_INTERNAL_RSP): BANJO_SRCS:=$(MODULE_BANJOSRCS)
$(MODULE_BANJO_CPP_INTERNAL_RSP): $(foreach dep,$(MODULE_BANJO_DEPS),$(call TOBUILDDIR,$(dep))/gen/banjo-files) $(MODULE_BANJOSRCS) make/bcompile.mk
	@$(MKDIR)
	$(NOECHO)echo --name $(BANJO_NAME) --backend cpp_i --output $(BANJO_HEADER) $(foreach dep,$(BANJO_DEPS),--files $(shell cat $(call TOBUILDDIR,$(dep))/gen/banjo-files)) --files $(BANJO_SRCS) > $@

# $@ only lists one of the multiple targets, so we use $< (first dep) to
# compute the (related) destination directories to create
$(MODULE_BANJO_C_HEADER): $(MODULE_BANJO_C_RSP) $(BANJO)
	$(call BUILDECHO, generating banjo from $<)
	@mkdir -p $(<D)/include/ddk/protocol
	$(NOECHO)$(BANJO) @$<

$(MODULE_BANJO_CPP_HEADER): $(MODULE_BANJO_CPP_RSP) $(BANJO)
	$(call BUILDECHO, generating banjo from $<)
	@mkdir -p $(<D)/include/ddktl/protocol
	$(NOECHO)$(BANJO) @$<

$(MODULE_BANJO_CPP_INTERNAL_HEADER): $(MODULE_BANJO_CPP_INTERNAL_RSP) $(BANJO)
	$(call BUILDECHO, generating banjo from $<)
	@mkdir -p $(<D)/include/ddktl/protocol
	$(NOECHO)$(BANJO) @$<

EXTRA_BUILDDEPS += make/bcompile.mk
GENERATED += $(MODULE_BANJO_C_HEADER) $(MODULE_BANJO_CPP_HEADER) $(MODULE_BANJO_CPP_INTERNAL_HEADER)

# clear some variables we set here
MODULE_BANJOSRCS :=
MODULE_BANJO_INCLUDE :=
MODULE_BANJO_C_HEADER :=
MODULE_BANJO_CPP_HEADER :=
MODULE_BANJO_CPP_INTERNAL_HEADER :=
MODULE_BANJO_RSP :=
