// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <unittest/unittest.h>
#include <lib/zx/profile.h>
#include <lib/zx/thread.h>
#include <lib/zx/job.h>

// Tests in this file rely that the default job is the root job.

static bool profile_failures_test() {
    BEGIN_TEST;

    zx::unowned_job root_job(zx_job_default());
    if (!root_job->is_valid()) {
        unittest_printf("no root job. skipping test\n");
    } else {
        zx::profile profile;

        ASSERT_EQ(zx::profile::create(*root_job, nullptr, &profile), ZX_ERR_INVALID_ARGS, "");
        ASSERT_EQ(zx::profile::create(zx::job(), nullptr, &profile), ZX_ERR_BAD_HANDLE, "");

        zx_profile_info_t profile_info = {};
        ASSERT_EQ(zx::profile::create(
            *root_job, &profile_info, &profile), ZX_ERR_NOT_SUPPORTED, "");

        profile_info.type = ZX_PROFILE_INFO_SCHEDULER;
        profile_info.scheduler.priority = ZX_PRIORITY_HIGHEST + 1;
        ASSERT_EQ(zx::profile::create(
            *root_job, &profile_info, &profile), ZX_ERR_INVALID_ARGS, "");

        zx::job child_job;
        ASSERT_EQ(zx::job::create(*root_job, 0u, &child_job), ZX_OK, "");
        profile_info.scheduler.priority = ZX_PRIORITY_HIGH;
        ASSERT_EQ(zx::profile::create(
            child_job, &profile_info, &profile), ZX_ERR_ACCESS_DENIED, "");
    }

    END_TEST;
}

static bool profile_priority_test(void) {
    BEGIN_TEST;

    zx::unowned_job root_job(zx_job_default());
    if (!root_job->is_valid()) {
        unittest_printf("no root job. skipping test\n");
    } else {
        zx_profile_info_t profile_info = {};
        profile_info.type = ZX_PROFILE_INFO_SCHEDULER;

        zx::profile profile1;
        profile_info.scheduler.priority = ZX_PRIORITY_HIGH;
        ASSERT_EQ(zx::profile::create(*root_job, &profile_info, &profile1), ZX_OK, "");

        zx::profile profile2;
        profile_info.scheduler.priority = ZX_PRIORITY_DEFAULT;
        ASSERT_EQ(zx::profile::create(*root_job, &profile_info, &profile2), ZX_OK, "");

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
