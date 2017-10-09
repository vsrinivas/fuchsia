// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/types.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/object.h>
#include <zircon/syscalls/port.h>
#include <zircon/syscalls/resource.h>
#include <unittest/unittest.h>
#include <stdio.h>

extern zx_handle_t get_root_resource(void);

static bool test_resource_actions(void) {
    BEGIN_TEST;

    zx_handle_t rrh = get_root_resource();
    ASSERT_NE(rrh, ZX_HANDLE_INVALID, "no root resource handle");

    zx_handle_t h;

    // root resources can be used to create any resources
    ASSERT_EQ(zx_resource_create(rrh, ZX_RSRC_KIND_ROOT, 1, 2, &h), ZX_OK, "");

    zx_info_resource_t info;

    ASSERT_EQ(zx_object_get_info(h, ZX_INFO_RESOURCE, &info, sizeof(info), NULL, NULL), ZX_OK, "");
    ASSERT_EQ(info.kind, ZX_RSRC_KIND_ROOT, "");
    ASSERT_EQ(info.low, 1u, "");
    ASSERT_EQ(info.high, 2u, "");

    // but ranges may not be backwards for any resource
    ASSERT_EQ(zx_resource_create(rrh, ZX_RSRC_KIND_ROOT, 2, 1, &h), ZX_ERR_INVALID_ARGS, "");

    // create a non-root resource
    ASSERT_EQ(zx_resource_create(rrh, 0x12345678, 0x1000, 0x2000, &h), ZX_OK, "");

    ASSERT_EQ(zx_object_get_info(h, ZX_INFO_RESOURCE, &info, sizeof(info), NULL, NULL), ZX_OK, "");
    ASSERT_EQ(info.kind, 0x12345678u, "");
    ASSERT_EQ(info.low, 0x1000u, "");
    ASSERT_EQ(info.high, 0x2000u, "");

    zx_handle_t h1;

    // verify range checks
    ASSERT_EQ(zx_resource_create(h, 0x12345678, 0, 1, &h1), ZX_ERR_OUT_OF_RANGE, "");
    ASSERT_EQ(zx_resource_create(h, 0x12345678, 0x1F00, 0x2010, &h1), ZX_ERR_OUT_OF_RANGE, "");

    // verify permission checks
    ASSERT_EQ(zx_resource_create(h, ZX_RSRC_KIND_ROOT, 0x1000, 0x1001, &h), ZX_ERR_ACCESS_DENIED, "");
    ASSERT_EQ(zx_resource_create(h, 0x11111111, 0x1000, 0x1001, &h), ZX_ERR_ACCESS_DENIED, "");

    END_TEST;
}

BEGIN_TEST_CASE(resource_tests)
RUN_TEST(test_resource_actions);
END_TEST_CASE(resource_tests)
