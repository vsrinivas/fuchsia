// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zxtest/zxtest.h>
#include "smbios.h"

// This is needed because smbios.cpp uses get_root_resource() for some of the
// driver functionality (in that case, it comes from the devhost).  Since this
// test binary is a standalone executable, we need something to define this
// symbol.  On a build that GCs symbols, this will likely be omitted.
extern "C" zx_handle_t get_root_resource() { return ZX_HANDLE_INVALID; }

TEST(SmbiosTestCase, ProductNameAllSpaces) {
    char buf[32] = {};
    memset(buf, ' ', sizeof(buf) - 1);
    while (strlen(buf) > 0) {
        ASSERT_FALSE(smbios_product_name_is_valid(buf));
        buf[strlen(buf) - 1] = 0;
    }
}

TEST(SmbiosTestCase, ProductNameEmpty) {
    ASSERT_FALSE(smbios_product_name_is_valid(""));
}

TEST(SmbiosTestCase, ProductNameNull) {
    ASSERT_FALSE(smbios_product_name_is_valid(nullptr));
    ASSERT_FALSE(smbios_product_name_is_valid("<null>"));
}

TEST(SmbiosTestCase, ProductNameValid) {
    ASSERT_TRUE(smbios_product_name_is_valid("NUC6i3SYB"));
    ASSERT_TRUE(smbios_product_name_is_valid("Test Name"));
}
