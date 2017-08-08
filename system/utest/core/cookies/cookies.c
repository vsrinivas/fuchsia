// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <magenta/process.h>
#include <magenta/types.h>
#include <magenta/syscalls.h>
#include <unittest/unittest.h>
#include <stdio.h>


static bool test_cookie_actions(void) {
    BEGIN_TEST;

    static const uint64_t magic1 = 0x1020304050607080;
    static const uint64_t magic2 = 0x1122334455667788;

    // create some objects
    mx_handle_t scope1, scope2, token;
    ASSERT_EQ(mx_event_create(0, &scope1), MX_OK, "");
    ASSERT_EQ(mx_event_create(0, &scope2), MX_OK, "");
    ASSERT_EQ(mx_event_create(0, &token), MX_OK, "");

    // cookies are not readable before being set
    uint64_t cookie;
    ASSERT_EQ(mx_object_get_cookie(token, scope1, &cookie), MX_ERR_ACCESS_DENIED, "");

    // cookies may be read back using the scope they were set with
    ASSERT_EQ(mx_object_set_cookie(token, scope1, magic1), MX_OK, "");
    ASSERT_EQ(mx_object_get_cookie(token, scope1, &cookie), MX_OK, "");
    ASSERT_EQ(cookie, magic1, "");

    // cookies are only settable on objects that support them
    ASSERT_EQ(mx_object_set_cookie(mx_process_self(), scope1, magic1), MX_ERR_NOT_SUPPORTED, "");

    // cookies are only gettable on objects that support them
    ASSERT_EQ(mx_object_get_cookie(mx_process_self(), scope1, &cookie), MX_ERR_NOT_SUPPORTED, "");

    // cookies are not readable with a different scope
    ASSERT_EQ(mx_object_get_cookie(token, scope2, &cookie), MX_ERR_ACCESS_DENIED, "");

    // cookies are not writeable with a different scope
    ASSERT_EQ(mx_object_set_cookie(token, scope2, magic1), MX_ERR_ACCESS_DENIED, "");

    // cookies are modifyable with the original scope
    ASSERT_EQ(mx_object_set_cookie(token, scope1, magic2), MX_OK, "");
    ASSERT_EQ(mx_object_get_cookie(token, scope1, &cookie), MX_OK, "");
    ASSERT_EQ(cookie, magic2, "");

    // bogus handles
    ASSERT_EQ(mx_object_get_cookie(token, MX_HANDLE_INVALID, &cookie), MX_ERR_BAD_HANDLE, "");
    ASSERT_EQ(mx_object_get_cookie(MX_HANDLE_INVALID, scope1, &cookie), MX_ERR_BAD_HANDLE, "");
    ASSERT_EQ(mx_object_set_cookie(token, MX_HANDLE_INVALID, magic1), MX_ERR_BAD_HANDLE, "");
    ASSERT_EQ(mx_object_set_cookie(MX_HANDLE_INVALID, scope1, magic1), MX_ERR_BAD_HANDLE, "");

    ASSERT_EQ(mx_handle_close(token), MX_OK, "");
    ASSERT_EQ(mx_handle_close(scope1), MX_OK, "");
    ASSERT_EQ(mx_handle_close(scope2), MX_OK, "");

    END_TEST;
}

// Eventpairs have special cookie semantics in that when one side closes, the other side's
// cookie gets invalidated.
static bool test_cookie_eventpair(void) {
    BEGIN_TEST;
    static const uint64_t magic1 = 0x1020304050607080;
    static const uint64_t magic2 = 0x1122334455667788;

    // create some objects
    mx_handle_t scope1, side1, side2;

    ASSERT_EQ(mx_event_create(0, &scope1), MX_OK, "");
    ASSERT_EQ(mx_eventpair_create(0, &side1, &side2), MX_OK, "");

    uint64_t cookie;
    ASSERT_EQ(mx_object_set_cookie(side1, scope1, magic1), MX_OK, "");
    ASSERT_EQ(mx_object_get_cookie(side1, scope1, &cookie), MX_OK, "");
    ASSERT_EQ(cookie, magic1, "");

    mx_handle_close(side2);
    ASSERT_EQ(mx_object_get_cookie(side1, scope1, &cookie), MX_ERR_ACCESS_DENIED, "");
    mx_handle_close(side1);

    // Make sure it works from both sides.
    ASSERT_EQ(mx_eventpair_create(0, &side1, &side2), MX_OK, "");
    ASSERT_EQ(mx_object_set_cookie(side2, scope1, magic2), MX_OK, "");
    ASSERT_EQ(mx_object_get_cookie(side2, scope1, &cookie), MX_OK, "");
    ASSERT_EQ(cookie, magic2, "");

    mx_handle_close(side1);
    ASSERT_EQ(mx_object_get_cookie(side2, scope1, &cookie), MX_ERR_ACCESS_DENIED, "");
    mx_handle_close(side2);

    END_TEST;
}

BEGIN_TEST_CASE(cookie_tests)
RUN_TEST(test_cookie_actions);
RUN_TEST(test_cookie_eventpair);
END_TEST_CASE(cookie_tests)

#ifndef BUILD_COMBINED_TESTS
int main(int argc, char** argv) {
    return unittest_run_all_tests(argc, argv) ? 0 : -1;
}
#endif
