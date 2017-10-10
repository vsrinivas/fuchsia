// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/usb-request.h>
#include <unittest/unittest.h>

static bool test_alloc_simple(void) {
    BEGIN_TEST;
    usb_request_t* req;
    ASSERT_EQ(usb_request_alloc(&req, PAGE_SIZE * 3, 1), ZX_OK, "");
    ASSERT_NONNULL(req, "");
    ASSERT_TRUE(io_buffer_is_valid(&req->buffer), "");

    ASSERT_EQ(usb_request_physmap(req), ZX_OK, "");
    ASSERT_NONNULL(req->buffer.phys_list, "expected phys list to be set");
    ASSERT_EQ(req->buffer.phys_count, 3u, "unexpected phys count");

    usb_request_release(req);
    END_TEST;
}

BEGIN_TEST_CASE(usb_request_tests)
RUN_TEST(test_alloc_simple)
END_TEST_CASE(usb_request_tests)

struct test_case_element* test_case_ddk_usb_request = TEST_CASE_ELEMENT(usb_request_tests);
