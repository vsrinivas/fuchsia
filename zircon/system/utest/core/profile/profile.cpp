// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <thread>

#include <lib/zx/job.h>
#include <lib/zx/profile.h>
#include <lib/zx/thread.h>
#include <zircon/syscalls/profile.h>
#include <zircon/syscalls/types.h>
#include <zxtest/zxtest.h>

namespace profile {
namespace {

zx_profile_info_t MakeSchedulerProfileInfo(int32_t priority) {
    zx_profile_info_t info = {};
    info.type = ZX_PROFILE_INFO_SCHEDULER;
    info.scheduler.priority = priority;
    return info;
}

// Tests in this file rely that the default job is the root job.
TEST(SchedulerProfileTest, CreateProfileWithDefaultPriorityIsOk) {
    zx::unowned_job root_job(zx::job::default_job());
    ASSERT_TRUE(root_job->is_valid());
    zx_profile_info_t profile_info = MakeSchedulerProfileInfo(ZX_PRIORITY_DEFAULT);
    zx::profile profile;

    ASSERT_OK(zx::profile::create(*root_job, 0u, &profile_info, &profile));
}

TEST(SchedulerProfileTest, CreateProfileWithLowestPriorityIsOk) {
    zx::unowned_job root_job(zx::job::default_job());
    ASSERT_TRUE(root_job->is_valid());
    zx_profile_info_t profile_info = MakeSchedulerProfileInfo(ZX_PRIORITY_LOWEST);
    zx::profile profile;

    ASSERT_OK(zx::profile::create(*root_job, 0u, &profile_info, &profile));
}

TEST(SchedulerProfileTest, CreateProfileWithLowPriorityIsOk) {
    zx::unowned_job root_job(zx::job::default_job());
    ASSERT_TRUE(root_job->is_valid());
    zx_profile_info_t profile_info = MakeSchedulerProfileInfo(ZX_PRIORITY_LOW);
    zx::profile profile;

    ASSERT_OK(zx::profile::create(*root_job, 0u, &profile_info, &profile));
}

TEST(SchedulerProfileTest, CreateProfileWithHihgPriorityIsOk) {
    zx::unowned_job root_job(zx::job::default_job());
    ASSERT_TRUE(root_job->is_valid());
    zx_profile_info_t profile_info = MakeSchedulerProfileInfo(ZX_PRIORITY_HIGH);
    zx::profile profile;

    ASSERT_OK(zx::profile::create(*root_job, 0u, &profile_info, &profile));
}

TEST(SchedulerProfileTest, CreateProfileWithHighestPriorityIsOk) {
    zx::unowned_job root_job(zx::job::default_job());
    ASSERT_TRUE(root_job->is_valid());
    zx_profile_info_t profile_info = MakeSchedulerProfileInfo(ZX_PRIORITY_HIGHEST);
    zx::profile profile;

    ASSERT_OK(zx::profile::create(*root_job, 0u, &profile_info, &profile));
}

TEST(SchedulerProfileTest, CreateProfileWithPriorityExceedingHighestIsInvalidArgs) {
    zx::unowned_job root_job(zx::job::default_job());
    ASSERT_TRUE(root_job->is_valid());
    zx_profile_info_t profile_info = MakeSchedulerProfileInfo(ZX_PRIORITY_HIGHEST + 1);
    zx::profile profile;

    ASSERT_EQ(ZX_ERR_INVALID_ARGS, zx::profile::create(*root_job, 0u, &profile_info, &profile));
}

TEST(SchedulerProfileTest, CreateProfileWithPriorityBelowLowestIsInvalidArgs) {
    zx::unowned_job root_job(zx::job::default_job());
    ASSERT_TRUE(root_job->is_valid());
    zx_profile_info_t profile_info = MakeSchedulerProfileInfo(ZX_PRIORITY_LOWEST - 1);
    zx::profile profile;

    ASSERT_EQ(ZX_ERR_INVALID_ARGS, zx::profile::create(*root_job, 0u, &profile_info, &profile));
}

TEST(SchedulerProfileTest, CreateProfileOnNonRootJobIsAccessDenied) {
    zx::unowned_job root_job(zx::job::default_job());
    ASSERT_TRUE(root_job->is_valid());
    zx::job child_job;
    ASSERT_OK(zx::job::create(*root_job, 0u, &child_job));
    zx_profile_info_t profile_info = MakeSchedulerProfileInfo(ZX_PRIORITY_DEFAULT);
    zx::profile profile;

    ASSERT_EQ(ZX_ERR_ACCESS_DENIED, zx::profile::create(child_job, 0u, &profile_info, &profile));
}

TEST(SchedulerProfileTest, CreateProfileWithNonZeroOptionsIsInvalidArgs) {
    zx::unowned_job root_job(zx::job::default_job());
    ASSERT_TRUE(root_job->is_valid());
    zx::job child_job;
    ASSERT_OK(zx::job::create(*root_job, 0u, &child_job));
    zx_profile_info_t profile_info = MakeSchedulerProfileInfo(ZX_PRIORITY_DEFAULT);
    zx::profile profile;

    ASSERT_EQ(ZX_ERR_INVALID_ARGS, zx::profile::create(*root_job, 1u, &profile_info, &profile));
}

TEST(SchedulerProfileTest, SetThreadPriorityIsOk) {
    zx::unowned_job root_job(zx::job::default_job());
    ASSERT_TRUE(root_job->is_valid());

    std::atomic<const char*> error = nullptr;
    std::atomic<zx_status_t> result = ZX_OK;

    zx::profile profile_1;
    zx_profile_info_t info_1 = MakeSchedulerProfileInfo(ZX_PRIORITY_LOWEST);
    ASSERT_OK(zx::profile::create(*root_job, 0u, &info_1, &profile_1));

    zx::profile profile_2;
    zx_profile_info_t info_2 = MakeSchedulerProfileInfo(ZX_PRIORITY_HIGH);
    ASSERT_OK(zx::profile::create(*root_job, 0u, &info_2, &profile_2));

    // Operate on a background thread, just in case a failure changes the priority of the main
    // thread.
    std::thread worker(
        [](zx::profile first, zx::profile second, std::atomic<const char*>* error,
           std::atomic<zx_status_t>* result) {
            *result = zx::thread::self()->set_profile(first, 0);
            if (result != ZX_OK) {
                *error = "Failed to set first profile on thread";
                return;
            }
            std::this_thread::yield();

            *result = zx::thread::self()->set_profile(second, 0);
            if (result != ZX_OK) {
                *error = "Failed to set second profile on thread";
                return;
            }
        },
        std::move(profile_1), std::move(profile_2), &error, &result);

    // Wait until is completed.
    worker.join();

    ASSERT_OK(result.load(), "%s", error.load());
}

TEST(ProfileTest, CreateProfileWithDefaultInitializedProfileInfoIsNotSupported) {
    zx::unowned_job root_job(zx::job::default_job());
    ASSERT_TRUE(root_job->is_valid());
    zx_profile_info_t profile_info = {};
    zx::profile profile;

    ASSERT_EQ(ZX_ERR_NOT_SUPPORTED, zx::profile::create(*root_job, 0u, &profile_info, &profile));
}

TEST(ProfileTest, CreateProfileWithNoProfileInfoIsInvalidArgs) {
    zx::unowned_job root_job(zx::job::default_job());
    ASSERT_TRUE(root_job->is_valid());
    zx::profile profile;

    ASSERT_EQ(ZX_ERR_INVALID_ARGS, zx::profile::create(*root_job, 0u, nullptr, &profile));
}

TEST(ProfileTest, CreateProfileWithInvalidHandleIsBadHandle) {
    zx::profile profile;

    ASSERT_EQ(ZX_ERR_BAD_HANDLE, zx::profile::create(zx::job(), 0u, nullptr, &profile));
}

TEST(ProfileTest, CreateProfileWithNullProfileIsInvalidArgs) {
    zx::unowned_job root_job(zx::job::default_job());
    ASSERT_TRUE(root_job->is_valid());
    zx_profile_info_t profile_info = MakeSchedulerProfileInfo(ZX_PRIORITY_DEFAULT);

// Needed to test API coverage of null params in GCC.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnonnull"
    ASSERT_EQ(ZX_ERR_INVALID_ARGS, zx_profile_create(root_job->get(), 0u, &profile_info, nullptr));
#pragma GCC diagnostic pop
}

} // namespace
} // namespace profile
