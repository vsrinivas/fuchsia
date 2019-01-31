// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <unittest/unittest.h>
#include <zircon/process.h>
#include <zircon/syscalls.h>

// Tests in this file rely that the default job is the root job.

static bool make_profile_fails(void) {
    BEGIN_TEST;

    zx_handle_t root_job = zx_job_default();
    if (root_job == ZX_HANDLE_INVALID) {
        unittest_printf("no root job. skipping test\n");
    } else {
        zx_handle_t profile;

        ASSERT_EQ(zx_profile_create(root_job, NULL, &profile), ZX_ERR_INVALID_ARGS, "");
        ASSERT_EQ(zx_profile_create(ZX_HANDLE_INVALID, NULL, &profile), ZX_ERR_BAD_HANDLE, "");

        zx_profile_info_t profile_info = { 0 };
        ASSERT_EQ(zx_profile_create(root_job, &profile_info, &profile), ZX_ERR_NOT_SUPPORTED, "");

        profile_info.type = ZX_PROFILE_INFO_SCHEDULER;
        profile_info.scheduler.priority = ZX_PRIORITY_HIGHEST + 1;
        ASSERT_EQ(zx_profile_create(root_job, &profile_info, &profile), ZX_ERR_INVALID_ARGS, "");

        zx_handle_t child_job;
        ASSERT_EQ(zx_job_create(root_job, 0u, &child_job), ZX_OK, "");
        profile_info.scheduler.priority = ZX_PRIORITY_HIGH;
        ASSERT_EQ(zx_profile_create(child_job, &profile_info, &profile), ZX_ERR_ACCESS_DENIED, "");
        zx_handle_close(child_job);
    }

    END_TEST;
}

static bool change_priority_via_profile(void) {
    BEGIN_TEST;

    zx_handle_t root_job = zx_job_default();
    if (root_job == ZX_HANDLE_INVALID) {
        unittest_printf("no root job. skipping test\n");
    } else {
        zx_profile_info_t profile_info = { 0 };
        profile_info.type = ZX_PROFILE_INFO_SCHEDULER;

        zx_handle_t profile1;
        profile_info.scheduler.priority = ZX_PRIORITY_HIGH;
        ASSERT_EQ(zx_profile_create(root_job, &profile_info, &profile1), ZX_OK, "");

        zx_handle_t profile2;
        profile_info.scheduler.priority = ZX_PRIORITY_DEFAULT;
        ASSERT_EQ(zx_profile_create(root_job, &profile_info, &profile2), ZX_OK, "");

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
