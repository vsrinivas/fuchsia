// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/syslog/cpp/macros.h>
#include <lib/zx/job.h>
#include <lib/zx/process.h>

#include <gtest/gtest.h>

#include "src/performance/trace/tests/run_test.h"

namespace tracing {
namespace test {

namespace {

const char kChildPath[] = "/pkg/bin/run_awhile";

// Only run tracing for this long, not the default 10 seconds.
const char kTraceDurationArg[] = "--duration=1";

// 60 seconds is typically the test timeout.
const char kChildDurationArg[] = "60";

const int kMaxRetryCount = 5;

TEST(DetachTest, SpawnedAppNotDetached) {
  zx::job job{};
  ASSERT_EQ(zx::job::create(*zx::job::default_job(), 0, &job), ZX_OK);

  std::vector<std::string> args{"record", "--spawn", kTraceDurationArg, kChildPath,
                                kChildDurationArg};
  ASSERT_TRUE(RunTraceAndWait(job, args));

  FX_LOGS(INFO) << "Trace exited, checking for helper presence";

  // The test helper should have been killed.
  zx_koid_t test_helper_pid;
  size_t actual_count, avail_count;
  ASSERT_EQ(job.get_info(ZX_INFO_JOB_PROCESSES, &test_helper_pid, sizeof(test_helper_pid),
                         &actual_count, &avail_count),
            ZX_OK);
  // Allow a little bit of slack for the process counts to update.
  for (int retry_count = 0; retry_count < kMaxRetryCount; retry_count++) {
    if (actual_count == 0 && avail_count == 0) {
      break;
    }
    zx::nanosleep(zx::deadline_after(zx::msec(50)));
    ASSERT_EQ(job.get_info(ZX_INFO_JOB_PROCESSES, &test_helper_pid, sizeof(test_helper_pid),
                           &actual_count, &avail_count),
              ZX_OK);
  }
  ASSERT_EQ(actual_count, 0u);
  ASSERT_EQ(avail_count, 0u);
}

TEST(DetachTest, SpawnedAppDetached) {
  zx::job job{};
  ASSERT_EQ(zx::job::create(*zx::job::default_job(), 0, &job), ZX_OK);

  std::vector<std::string> args{"record",          "--detach", "--spawn",
                                kTraceDurationArg, kChildPath, kChildDurationArg};
  ASSERT_TRUE(RunTraceAndWait(job, args));

  FX_LOGS(INFO) << "Trace exited, checking for helper presence";

  // The test helper should still be running.
  zx_koid_t test_helper_pid;
  size_t actual_count, avail_count;
  ASSERT_EQ(job.get_info(ZX_INFO_JOB_PROCESSES, &test_helper_pid, sizeof(test_helper_pid),
                         &actual_count, &avail_count),
            ZX_OK);
  // Allow a little bit of slack for the process counts to update.
  for (int retry_count = 0; retry_count < kMaxRetryCount; retry_count++) {
    if (actual_count == 1 && avail_count == 1) {
      break;
    }
    zx::nanosleep(zx::deadline_after(zx::msec(50)));
    ASSERT_EQ(job.get_info(ZX_INFO_JOB_PROCESSES, &test_helper_pid, sizeof(test_helper_pid),
                           &actual_count, &avail_count),
              ZX_OK);
  }
  ASSERT_EQ(actual_count, 1u);
  ASSERT_EQ(avail_count, 1u);

  FX_LOGS(INFO) << "Process " << test_helper_pid << " present";

  // Don't need the test helper anymore.
  zx::process test_helper;
  ASSERT_EQ(job.get_child(test_helper_pid, ZX_RIGHT_SAME_RIGHTS, &test_helper), ZX_OK);
  ASSERT_TRUE(test_helper);
  EXPECT_EQ(test_helper.kill(), ZX_OK);
}

}  // namespace

}  // namespace test
}  // namespace tracing
