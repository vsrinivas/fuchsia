// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/syslog/cpp/log_settings.h>
#include <lib/syslog/cpp/macros.h>

#include <gtest/gtest.h>

#include "garnet/lib/perfmon/controller.h"
#include "run_test.h"
#include "src/lib/fxl/command_line.h"
#include "src/lib/fxl/log_settings_command_line.h"
#include "src/lib/fxl/test/test_settings.h"
#include "verify_test.h"

static syslog::LogSettings g_log_settings;

static void RunAndVerify(const char* spec_path) {
  ASSERT_TRUE(RunSpec(spec_path, g_log_settings));
  VerifySpec(spec_path);
}

#ifdef __x86_64__

TEST(Cpuperf, FixedCounters) { RunAndVerify("/pkg/data/fixed_counters.cpspec"); }

// TODO(fxbug.dev/8501): Re-enable.
TEST(Cpuperf, DISABLED_OsFlag) { RunAndVerify("/pkg/data/os_flag.cpspec"); }

TEST(Cpuperf, ProgrammableCounters) { RunAndVerify("/pkg/data/programmable_counters.cpspec"); }

TEST(Cpuperf, UserFlag) { RunAndVerify("/pkg/data/user_flag.cpspec"); }

// TODO(38183): Re-enable.
TEST(Cpuperf, DISABLED_ValueRecords) { RunAndVerify("/pkg/data/value_records.cpspec"); }

TEST(Cpuperf, LastBranchRecord) {
  perfmon::Properties properties;
  ASSERT_TRUE(perfmon::Controller::GetProperties(&properties));
  if (!(properties.flags & perfmon::Properties::kFlagHasLastBranch)) {
    // Not supported on this h/w. Punt.
    FX_LOGS(INFO) << "Last Branch Records not supported, skipping test";
    return;
  }

  RunAndVerify("/pkg/data/last_branch.cpspec");
}

#endif  // __x86_64__

TEST(Cpuperf, Tally) { RunAndVerify("/pkg/data/tally.cpspec"); }

// Provide our own main so that --verbose,etc. are recognized.
// This is useful because our verbosity is passed on to each test.
int main(int argc, char** argv) {
  auto cl = fxl::CommandLineFromArgcArgv(argc, argv);
  if (!fxl::SetTestSettings(cl))
    return EXIT_FAILURE;
  fxl::ParseLogSettings(cl, &g_log_settings);

  // Early exit if there is no perfmon device. We could be running on QEMU.
  bool is_supported = false;
  is_supported = perfmon::Controller::IsSupported();
  if (!is_supported) {
    FX_LOGS(INFO) << "Perfmon device not supported";
    return EXIT_SUCCESS;
  }

  testing::InitGoogleTest(&argc, argv);

  return RUN_ALL_TESTS();
}
