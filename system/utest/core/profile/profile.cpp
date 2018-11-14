// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <unittest/unittest.h>
#include <lib/zx/profile.h>
#include <lib/zx/thread.h>

extern "C" zx_handle_t get_root_resource();

static bool profile_failures_test() {
    BEGIN_TEST;

    zx::unowned_resource rrh(get_root_resource());
    if (!rrh->is_valid()) {
        unittest_printf("no root resource. skipping test\n");
    } else {
        zx::profile profile;

        ASSERT_EQ(zx::profile::create(*rrh, nullptr, &profile), ZX_ERR_INVALID_ARGS, "");
        ASSERT_EQ(zx::profile::create(zx::resource(), nullptr, &profile), ZX_ERR_BAD_HANDLE, "");

        zx_profile_info_t profile_info = {};
        ASSERT_EQ(zx::profile::create(*rrh, &profile_info, &profile), ZX_ERR_NOT_SUPPORTED, "");

        profile_info.type = ZX_PROFILE_INFO_SCHEDULER;
        profile_info.scheduler.priority = ZX_PRIORITY_HIGHEST + 1;
        ASSERT_EQ(zx::profile::create(*rrh, &profile_info, &profile), ZX_ERR_INVALID_ARGS, "");
    }

    END_TEST;
}

static bool profile_priority_test(void) {
    BEGIN_TEST;

    zx::unowned_resource rrh(get_root_resource());
    if (!rrh->is_valid()) {
        unittest_printf("no root resource. skipping test\n");
    } else {
        zx_profile_info_t profile_info = {};
        profile_info.type = ZX_PROFILE_INFO_SCHEDULER;

        zx::profile profile1;
        profile_info.scheduler.priority = ZX_PRIORITY_HIGH;
        ASSERT_EQ(zx::profile::create(*rrh, &profile_info, &profile1), ZX_OK, "");

        zx::profile profile2;
        profile_info.scheduler.priority = ZX_PRIORITY_DEFAULT;
        ASSERT_EQ(zx::profile::create(*rrh, &profile_info, &profile2), ZX_OK, "");

        ASSERT_EQ(zx::thread::self()->set_profile(profile1, 0), ZX_OK, "");
        zx_nanosleep(ZX_USEC(100));
        ASSERT_EQ(zx::thread::self()->set_profile(profile2, 0), ZX_OK, "");
    }

    END_TEST;
}

BEGIN_TEST_CASE(profile_cpp_tests)
RUN_TEST(profile_failures_test)
RUN_TEST(profile_priority_test)
END_TEST_CASE(profile_cpp_tests)
