// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zx/job.h>
#include <lib/zx/process.h>
#include <src/lib/fxl/command_line.h>
#include <src/lib/fxl/log_settings_command_line.h>
#include <src/lib/fxl/logging.h>

#include "garnet/bin/trace/tests/run_test.h"
#include "gtest/gtest.h"

const char kTracePath[] = "/bin/trace";
const char kChildPath[] = "/pkg/bin/run_awhile";

// Only run tracing for this long, not the default 10 seconds.
const char kTraceDurationArg[] = "--duration=1";

// 60 seconds is typically the test timeout.
const char kChildDurationArg[] = "60";

// TODO(FLK-193): Disabled until fixed.
TEST(DetachTest, DISABLED_SpawnedAppNotDetached) {
  zx::job job;
  ASSERT_EQ(zx::job::create(*zx::job::default_job(), 0, &job), ZX_OK);

  zx::process child;
  std::vector<std::string> argv{
      kTracePath, "record", "--spawn", kTraceDurationArg,
      kChildPath, kChildDurationArg};
  ASSERT_EQ(SpawnProgram(job, argv, ZX_HANDLE_INVALID, &child), ZX_OK);

  int return_code;
  ASSERT_EQ(WaitAndGetExitCode(argv[0], child, &return_code),
            ZX_OK);
  EXPECT_EQ(return_code, 0);

  FXL_LOG(INFO) << "Trace exited, checking for helper presence";

  // The test helper should have been killed.
  zx_koid_t test_helper_pid;
  size_t actual_count, avail_count;
  ASSERT_EQ(job.get_info(ZX_INFO_JOB_PROCESSES, &test_helper_pid,
                         sizeof(test_helper_pid), &actual_count, &avail_count),
            ZX_OK);
  ASSERT_EQ(actual_count, 0u);
  ASSERT_EQ(avail_count, 0u);
}

TEST(DetachTest, SpawnedAppDetached) {
  zx::job job;
  ASSERT_EQ(zx::job::create(*zx::job::default_job(), 0, &job), ZX_OK);

  zx::process child;
  std::vector<std::string> argv{
      kTracePath, "record", "--detach", "--spawn", kTraceDurationArg,
      kChildPath, kChildDurationArg};
  ASSERT_EQ(SpawnProgram(job, argv, ZX_HANDLE_INVALID, &child), ZX_OK);

  int return_code;
  ASSERT_EQ(WaitAndGetExitCode(argv[0], child, &return_code),
            ZX_OK);
  EXPECT_EQ(return_code, 0);

  FXL_LOG(INFO) << "Trace exited, checking for helper presence";

  // The test helper should still be running.
  zx_koid_t test_helper_pid;
  size_t actual_count, avail_count;
  ASSERT_EQ(job.get_info(ZX_INFO_JOB_PROCESSES, &test_helper_pid,
                         sizeof(test_helper_pid), &actual_count, &avail_count),
            ZX_OK);
  ASSERT_EQ(actual_count, 1u);
  ASSERT_EQ(avail_count, 1u);

  FXL_LOG(INFO) << "Process " << test_helper_pid << " present";

  // Don't need the test helper anymore.
  zx::process test_helper;
  ASSERT_EQ(job.get_child(test_helper_pid, ZX_RIGHT_SAME_RIGHTS, &test_helper),
            ZX_OK);
  ASSERT_TRUE(test_helper);
  EXPECT_EQ(test_helper.kill(), ZX_OK);
}

// Provide our own main so that --verbose,etc. are recognized.
int main(int argc, char** argv) {
  fxl::CommandLine cl = fxl::CommandLineFromArgcArgv(argc, argv);
  if (!fxl::SetLogSettingsFromCommandLine(cl))
    return EXIT_FAILURE;

  testing::InitGoogleTest(&argc, argv);

  return RUN_ALL_TESTS();
}
