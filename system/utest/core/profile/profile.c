// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <unittest/unittest.h>
#include <zircon/process.h>
#include <zircon/syscalls.h>

extern zx_handle_t get_root_resource(void);


static bool make_profile_fails(void) {
    BEGIN_TEST;

    zx_handle_t rrh = get_root_resource();
    if (rrh == ZX_HANDLE_INVALID) {
        unittest_printf("no root resource. skipping test\n");
    } else {
        zx_handle_t profile;

        ASSERT_EQ(zx_profile_create(rrh, NULL, &profile), ZX_ERR_INVALID_ARGS, "");
        ASSERT_EQ(zx_profile_create(ZX_HANDLE_INVALID, NULL, &profile), ZX_ERR_BAD_HANDLE, "");

        zx_profile_info_t profile_info = { 0 };
        ASSERT_EQ(zx_profile_create(rrh, &profile_info, &profile), ZX_ERR_NOT_SUPPORTED, "");

        profile_info.type = ZX_PROFILE_INFO_SCHEDULER;
        profile_info.scheduler.priority = ZX_PRIORITY_HIGHEST + 1;
        ASSERT_EQ(zx_profile_create(rrh, &profile_info, &profile), ZX_ERR_INVALID_ARGS, "");
    }

    END_TEST;
}

static bool change_priority_via_profile(void) {
    BEGIN_TEST;

    zx_handle_t rrh = get_root_resource();
    if (rrh == ZX_HANDLE_INVALID) {
        unittest_printf("no root resource. skipping test\n");
    } else {
        zx_profile_info_t profile_info = { 0 };
        profile_info.type = ZX_PROFILE_INFO_SCHEDULER;

        zx_handle_t profile1;
        profile_info.scheduler.priority = ZX_PRIORITY_HIGH;
        ASSERT_EQ(zx_profile_create(rrh, &profile_info, &profile1), ZX_OK, "");

        zx_handle_t profile2;
        profile_info.scheduler.priority = ZX_PRIORITY_DEFAULT;
        ASSERT_EQ(zx_profile_create(rrh, &profile_info, &profile2), ZX_OK, "");

        ASSERT_EQ(zx_object_set_profile(zx_thread_self(), profile1, 0), ZX_OK, "");
        zx_nanosleep(ZX_USEC(100));
        ASSERT_EQ(zx_object_set_profile(zx_thread_self(), profile2, 0), ZX_OK, "");

        ASSERT_EQ(zx_handle_close(profile1), ZX_OK, "");
        ASSERT_EQ(zx_handle_close(profile2), ZX_OK, "");
    }

    END_TEST;
}

BEGIN_TEST_CASE(profile_tests)
RUN_TEST(make_profile_fails)
RUN_TEST(change_priority_via_profile)
END_TEST_CASE(profile_tests)
