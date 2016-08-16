// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include <stdlib.h>

#include <magenta/syscalls.h>
#include <unittest/unittest.h>

#define CHECK(f, expected, message)                                 \
    do {                                                            \
        mx_status_t ret = (f);                                                  \
        char msg[32];                                               \
        snprintf(msg, sizeof(msg), "Test failed (%s): " #f " returned %d vs. %d\n",     \
                   message, (int)ret, (int)expected);               \
        EXPECT_EQ(ret, (int)(expected), msg);                            \
        if ((ret = (f)) != (expected)) {                            \
            return __LINE__;                                        \
        }                                                           \
    } while (0)

bool handle_info_test(void) {
    BEGIN_TEST;

    mx_handle_t event = mx_event_create(0u);
    mx_handle_t duped = mx_handle_duplicate(event, MX_RIGHT_SAME_RIGHTS);

    CHECK(mx_handle_get_info(
              event, MX_INFO_HANDLE_VALID, NULL, 0u),
          NO_ERROR, "handle should be valid");
    CHECK(mx_handle_close(event), NO_ERROR, "failed to close the handle");
    CHECK(mx_handle_get_info(
              event, MX_INFO_HANDLE_VALID, NULL, 0u),
          ERR_BAD_HANDLE, "handle should be valid");

    mx_handle_basic_info_t info = {0};
    CHECK(mx_handle_get_info(
              duped, MX_INFO_HANDLE_BASIC, &info, 4u),
          ERR_NOT_ENOUGH_BUFFER, "bad struct size validation");

    CHECK(mx_handle_get_info(
              duped, MX_INFO_HANDLE_BASIC, &info, sizeof(info)),
          sizeof(info), "handle should be valid");

    const mx_rights_t evr = MX_RIGHT_DUPLICATE | MX_RIGHT_TRANSFER |
                            MX_RIGHT_READ | MX_RIGHT_WRITE;

    EXPECT_GT(info.koid, 0ULL, "object id should be positive");
    EXPECT_EQ(info.type, (uint32_t)MX_OBJ_TYPE_EVENT, "handle should be an event");
    EXPECT_EQ(info.rights, evr, "wrong set of rights");
    EXPECT_EQ(info.props, (uint32_t)MX_OBJ_PROP_WAITABLE, "");

    mx_handle_close(event);
    mx_handle_close(duped);

    END_TEST;
}

bool handle_rights_test(void) {
    BEGIN_TEST;

    mx_handle_t event = mx_event_create(0u);
    mx_handle_t duped_ro = mx_handle_duplicate(event, MX_RIGHT_READ);

    mx_handle_basic_info_t info = {0};
    CHECK(mx_handle_get_info(
              duped_ro, MX_INFO_HANDLE_BASIC, &info, sizeof(info)),
          sizeof(info), "handle should be valid");

    if (info.rights != MX_RIGHT_READ)
        CHECK(0, 1, "wrong set of rights");

    mx_handle_t h;

    h = mx_handle_duplicate(duped_ro, MX_RIGHT_SAME_RIGHTS);
    CHECK(h, ERR_ACCESS_DENIED, "should fail rights check");

    h = mx_handle_duplicate(event, MX_RIGHT_EXECUTE | MX_RIGHT_READ);
    CHECK(h, ERR_INVALID_ARGS, "cannot upgrade rights");

    mx_handle_close(event);
    mx_handle_close(duped_ro);

    END_TEST;
}

BEGIN_TEST_CASE(handle_info_tests)
RUN_TEST(handle_info_test)
END_TEST_CASE(handle_info_tests)

#ifndef BUILD_COMBINED_TESTS
int main(int argc, char** argv) {
    return unittest_run_all_tests(argc, argv) ? 0 : -1;
}
#endif
