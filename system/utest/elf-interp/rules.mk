# Copyright 2016 The Fuchsia Authors
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := usertest

MODULE_SRCS += \
    $(LOCAL_DIR)/elf-interp-main.c

MODULE_NAME := elf-interp-test

MODULE_DEPS := \
    ulib/musl ulib/magenta ulib/mxio ulib/unittest

include make/module.mk

$(BUILDDIR)/$(LOCAL_DIR)/$(ARCH).S.o: $(BUILDDIR)/$(LOCAL_DIR)/main-addrs.h
$(BUILDDIR)/$(LOCAL_DIR)/main-addrs.h: \
    $(BUILDDIR)/utest/elf-interp/elf-interp.elf \
    $(LOCAL_DIR)/generate-main-addrs.sh
	@$(MKDIR)
	$(word 2,$^) "$(NM)" '$<' '$@'

$(BUILDDIR)/$(LOCAL_DIR)/$(ARCH).S.o: MODULE_COMPILEFLAGS := \
    -I$(BUILDDIR)/$(LOCAL_DIR)
$(BUILDDIR)/$(LOCAL_DIR)/$(ARCH).S.o: $(LOCAL_DIR)/$(ARCH).S
	@$(MKDIR)
	@echo compiling $<
	$(NOECHO)$(CC) $(GLOBAL_OPTFLAGS) $(MODULE_OPTFLAGS) $(GLOBAL_COMPILEFLAGS) $(USER_COMPILEFLAGS) $(ARCH_COMPILEFLAGS) $(MODULE_COMPILEFLAGS) $(GLOBAL_ASMFLAGS) $(USER_ASMFLAGS) $(ARCH_ASMFLAGS) $(MODULE_ASMFLAGS) $(THUMBCFLAGS) -c $< -MD -MP -MT $@ -MF $(@:%o=%d) -o $@

$(BUILDDIR)/$(LOCAL_DIR)/elf-interp-helper.so: \
    $(BUILDDIR)/$(LOCAL_DIR)/$(ARCH).S.o
	$(NOECHO)$(LD) $(GLOBAL_LDFLAGS) -shared $^ -o $@

USER_MANIFEST_LINES += \
    bin/elf-interp-helper.so=$(BUILDDIR)/$(LOCAL_DIR)/elf-interp-helper.so
