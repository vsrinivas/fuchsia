// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zx/job.h>
#include <lib/zx/profile.h>
#include <lib/zx/thread.h>
#include <zircon/errors.h>
#include <zircon/syscalls/profile.h>
#include <zircon/syscalls/types.h>

#include <thread>

#include <zxtest/zxtest.h>

extern "C" zx_handle_t get_root_resource(void);

namespace profile {
namespace {

zx::unowned_job GetRootJob() {
  zx::unowned_job root_job(zx::job::default_job());
  EXPECT_TRUE(root_job->is_valid());
  return root_job;
}

zx_profile_info_t MakeSchedulerProfileInfo(int32_t priority) {
  zx_profile_info_t info = {};
  info.flags = ZX_PROFILE_INFO_FLAG_PRIORITY;
  info.priority = priority;
  return info;
}

zx_profile_info_t MakeCpuMaskProfile(uint64_t mask) {
  zx_profile_info_t info = {};
  info.flags = ZX_PROFILE_INFO_FLAG_CPU_MASK;
  info.cpu_affinity_mask.mask[0] = mask;
  return info;
}

size_t GetCpuCount() {
  size_t actual, available;
  zx::unowned_handle root_resource(get_root_resource());
  zx_status_t status = root_resource->get_info(ZX_INFO_CPU_STATS, nullptr, 0, &actual, &available);
  ZX_ASSERT(status == ZX_OK);
  return available;
}

uint64_t GetAffinityMask(const zx::thread& thread) {
  zx_info_thread_t info;
  zx_status_t status = thread.get_info(ZX_INFO_THREAD, &info, sizeof(info), nullptr, nullptr);
  ZX_ASSERT(status == ZX_OK);
  return info.cpu_affinity_mask.mask[0];
}

uint32_t GetLastScheduledCpu(const zx::thread& thread) {
  zx_info_thread_stats_t info;
  zx_status_t status = thread.get_info(ZX_INFO_THREAD_STATS, &info, sizeof(info), nullptr, nullptr);
  ZX_ASSERT(status == ZX_OK);
  return info.last_scheduled_cpu;
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

TEST(ProfileTest, CreateProfileWithDefaultInitializedProfileInfoIsError) {
  zx::unowned_job root_job(zx::job::default_job());
  ASSERT_TRUE(root_job->is_valid());
  zx_profile_info_t profile_info = {};
  zx::profile profile;

  ASSERT_EQ(ZX_ERR_INVALID_ARGS, zx::profile::create(*root_job, 0u, &profile_info, &profile));
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

zx_status_t RunThreadWithProfile(const zx::profile& profile,
                                 const std::function<zx_status_t()>& body) {
  zx_status_t result;
  std::thread worker([&body, &result, &profile]() {
    result = zx::thread::self()->set_profile(profile, 0);
    if (result != ZX_OK) {
      return;
    }
    result = body();
  });
  worker.join();
  return result;
}

TEST(CpuMaskProfile, EmptyMaskIsValid) {
  zx::profile profile;
  zx_profile_info_t profile_info = MakeCpuMaskProfile(0);
  ASSERT_OK(zx::profile::create(*GetRootJob(), 0u, &profile_info, &profile));

  // Ensure that the thread can still run, despite the affinity mask
  // having no valid CPUs in it. (The kernel will just fall back to
  // its own choice of CPUs if this mask can't be respected.)
  ASSERT_OK(RunThreadWithProfile(profile, []() {
    EXPECT_EQ(GetAffinityMask(*zx::thread::self()), 0);
    EXPECT_NE(GetLastScheduledCpu(*zx::thread::self()), ZX_INFO_INVALID_CPU);
    return ZX_OK;
  }));
}

TEST(CpuMaskProfile, ApplyProfile) {
  const size_t num_cpus = GetCpuCount();
  ASSERT_LT(num_cpus, ZX_CPU_SET_BITS_PER_WORD,
            "Test assumes system running with less than %d cores.", ZX_CPU_SET_BITS_PER_WORD);
  for (size_t i = 0; i < num_cpus; i++) {
    zx_profile_info_t profile_info = MakeCpuMaskProfile(1 << i);
    zx::profile profile;
    ASSERT_OK(zx::profile::create(*GetRootJob(), 0u, &profile_info, &profile));

    // Ensure that the correct mask was applied.
    ASSERT_OK(RunThreadWithProfile(profile, [i]() {
      EXPECT_EQ(GetAffinityMask(*zx::thread::self()), (1 << i));
      EXPECT_EQ(GetLastScheduledCpu(*zx::thread::self()), i);
      return ZX_OK;
    }));
  }
}

}  // namespace
}  // namespace profile
