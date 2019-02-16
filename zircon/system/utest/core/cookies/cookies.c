// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/process.h>
#include <zircon/types.h>
#include <zircon/syscalls.h>
#include <unittest/unittest.h>
#include <stdio.h>


static bool test_cookie_actions(void) {
    BEGIN_TEST;

    static const uint64_t magic1 = 0x1020304050607080;
    static const uint64_t magic2 = 0x1122334455667788;

    // create some objects
    zx_handle_t scope1, scope2, token;
    ASSERT_EQ(zx_event_create(0, &scope1), ZX_OK, "");
    ASSERT_EQ(zx_event_create(0, &scope2), ZX_OK, "");
    ASSERT_EQ(zx_event_create(0, &token), ZX_OK, "");

    // cookies are not readable before being set
    uint64_t cookie;
    ASSERT_EQ(zx_object_get_cookie(token, scope1, &cookie), ZX_ERR_ACCESS_DENIED, "");

    // cookies may be read back using the scope they were set with
    ASSERT_EQ(zx_object_set_cookie(token, scope1, magic1), ZX_OK, "");
    ASSERT_EQ(zx_object_get_cookie(token, scope1, &cookie), ZX_OK, "");
    ASSERT_EQ(cookie, magic1, "");

    // cookies are only settable on objects that support them
    ASSERT_EQ(zx_object_set_cookie(zx_process_self(), scope1, magic1), ZX_ERR_NOT_SUPPORTED, "");

    // cookies are only gettable on objects that support them
    ASSERT_EQ(zx_object_get_cookie(zx_process_self(), scope1, &cookie), ZX_ERR_NOT_SUPPORTED, "");

    // cookies are not readable with a different scope
    ASSERT_EQ(zx_object_get_cookie(token, scope2, &cookie), ZX_ERR_ACCESS_DENIED, "");

    // cookies are not writeable with a different scope
    ASSERT_EQ(zx_object_set_cookie(token, scope2, magic1), ZX_ERR_ACCESS_DENIED, "");

    // cookies are modifyable with the original scope
    ASSERT_EQ(zx_object_set_cookie(token, scope1, magic2), ZX_OK, "");
    ASSERT_EQ(zx_object_get_cookie(token, scope1, &cookie), ZX_OK, "");
    ASSERT_EQ(cookie, magic2, "");

    // bogus handles
    ASSERT_EQ(zx_object_get_cookie(token, ZX_HANDLE_INVALID, &cookie), ZX_ERR_BAD_HANDLE, "");
    ASSERT_EQ(zx_object_get_cookie(ZX_HANDLE_INVALID, scope1, &cookie), ZX_ERR_BAD_HANDLE, "");
    ASSERT_EQ(zx_object_set_cookie(token, ZX_HANDLE_INVALID, magic1), ZX_ERR_BAD_HANDLE, "");
    ASSERT_EQ(zx_object_set_cookie(ZX_HANDLE_INVALID, scope1, magic1), ZX_ERR_BAD_HANDLE, "");

    ASSERT_EQ(zx_handle_close(token), ZX_OK, "");
    ASSERT_EQ(zx_handle_close(scope1), ZX_OK, "");
    ASSERT_EQ(zx_handle_close(scope2), ZX_OK, "");

    END_TEST;
}

// Eventpairs have special cookie semantics in that when one side closes, the other side's
// cookie gets invalidated.
static bool test_cookie_eventpair(void) {
    BEGIN_TEST;
    static const uint64_t magic1 = 0x1020304050607080;
    static const uint64_t magic2 = 0x1122334455667788;

    // create some objects
    zx_handle_t scope1, side1, side2;

    ASSERT_EQ(zx_event_create(0, &scope1), ZX_OK, "");
    ASSERT_EQ(zx_eventpair_create(0, &side1, &side2), ZX_OK, "");

    uint64_t cookie;
    ASSERT_EQ(zx_object_set_cookie(side1, scope1, magic1), ZX_OK, "");
    ASSERT_EQ(zx_object_get_cookie(side1, scope1, &cookie), ZX_OK, "");
    ASSERT_EQ(cookie, magic1, "");

    zx_handle_close(side2);
    ASSERT_EQ(zx_object_get_cookie(side1, scope1, &cookie), ZX_ERR_ACCESS_DENIED, "");
    zx_handle_close(side1);

    // Make sure it works from both sides.
    ASSERT_EQ(zx_eventpair_create(0, &side1, &side2), ZX_OK, "");
    ASSERT_EQ(zx_object_set_cookie(side2, scope1, magic2), ZX_OK, "");
    ASSERT_EQ(zx_object_get_cookie(side2, scope1, &cookie), ZX_OK, "");
    ASSERT_EQ(cookie, magic2, "");

    zx_handle_close(side1);
    ASSERT_EQ(zx_object_get_cookie(side2, scope1, &cookie), ZX_ERR_ACCESS_DENIED, "");
    zx_handle_close(side2);

    END_TEST;
}

BEGIN_TEST_CASE(cookie_tests)
RUN_TEST(test_cookie_actions);
RUN_TEST(test_cookie_eventpair);
END_TEST_CASE(cookie_tests)
