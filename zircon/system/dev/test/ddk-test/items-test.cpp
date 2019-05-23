// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/boot/c/fidl.h>
#include <lib/fdio/directory.h>
#include <lib/zx/channel.h>
#include <unittest/unittest.h>

constexpr char kItemsPath[] = "/svc/" fuchsia_boot_Items_Name;

static bool test_open_items(void) {
    BEGIN_TEST;

    zx::channel client, server;
    zx_status_t status = zx::channel::create(0, &client, &server);
    ASSERT_EQ(ZX_OK, status, "zx::channel::create failed");

    status = fdio_service_connect(kItemsPath, server.release());
    ASSERT_EQ(ZX_OK, status, "fdio_service_connect failed");

    END_TEST;
}

BEGIN_TEST_CASE(items_tests)
RUN_TEST(test_open_items)
END_TEST_CASE(items_tests)

extern "C" {
    struct test_case_element* test_case_ddk_items = TEST_CASE_ELEMENT(items_tests);
}
