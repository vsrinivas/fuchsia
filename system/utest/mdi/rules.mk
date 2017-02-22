# Copyright 2016 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := usertest

MODULE_SRCS += \
    $(LOCAL_DIR)/mditest.c

MODULE_NAME := mdi-test

MODULE_LIBS := ulib/unittest ulib/mdi ulib/mxio ulib/musl

# for including MDI_HEADER
MODULE_COMPILEFLAGS := -I$(BUILDDIR)/utest/mdi

MDIGEN := $(BUILDDIR)/tools/mdigen
MDI_TEST_SRCS := $(LOCAL_DIR)/test.mdi
MDI_TEST_DEPS := $(LOCAL_DIR)/test-defs.mdi
MDI_TEST_BIN := $(BUILDDIR)/utest/mdi/mdi-test.bin
MDI_TEST_HEADER := $(BUILDDIR)/utest/mdi/gen-mdi-test.h

# to force running mdigen before compiling test program
MODULE_SRCDEPS := $(MDI_TEST_BIN) $(MDI_TEST_HEADER)

include make/module.mk

# add "MDI_" prefix and make header file symbols uppercase
MDI_TEST_OPTS := -p "MDI_" -u

$(MDI_TEST_HEADER): $(MDI_TEST_BIN)

$(MDI_TEST_BIN): $(MDIGEN) $(MDI_TEST_SRCS) $(MDI_TEST_DEPS)
	@echo generating $@
	@$(MKDIR)
	$(NOECHO)$(MDIGEN) -o $@ $(MDI_TEST_SRCS) -h $(MDI_TEST_HEADER) $(MDI_TEST_OPTS)

GENERATED += $(MDI_TEST_BIN) $(MDI_TEST_HEADER)
EXTRA_BUILDDEPS += $(MDI_TEST_BIN) $(MDI_TEST_HEADER)

# add our MDI file to the boot image
USER_MANIFEST_LINES += data/mditest.mdi=$(MDI_TEST_BIN)
