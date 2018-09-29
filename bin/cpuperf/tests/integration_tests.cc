// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>
#include <lib/fxl/command_line.h>
#include <lib/fxl/log_settings_command_line.h>

#include "garnet/lib/cpuperf/controller.h"

#include "run_test.h"
#include "verify_test.h"

#ifdef __x86_64__

static void RunAndVerify(const char* spec_path) {
  ASSERT_TRUE(RunSpec(spec_path));
  VerifySpec(spec_path);
}

TEST(Cpuperf, FixedCounters) {
  RunAndVerify("/pkg/data/fixed_counters.cpspec");
}

TEST(Cpuperf, OsFlag) {
  RunAndVerify("/pkg/data/os_flag.cpspec");
}

TEST(Cpuperf, UserFlag) {
  RunAndVerify("/pkg/data/user_flag.cpspec");
}

TEST(Cpuperf, ValueRecords) {
  RunAndVerify("/pkg/data/value_records.cpspec");
}

#endif  // __x86_64__

// Provide our own main so that --verbose,etc. are recognized.
// This is useful because our verbosity is passed on to each test.
int main(int argc, char** argv)
{
  auto cl = fxl::CommandLineFromArgcArgv(argc, argv);
  if (!fxl::SetLogSettingsFromCommandLine(cl))
    return EXIT_FAILURE;

  // Early exit if there is no cpuperf device. We could be running on QEMU.
  bool is_supported = false;
#ifdef __x86_64__
  is_supported = cpuperf::Controller::IsSupported();
#endif
  if (!is_supported) {
    FXL_LOG(INFO) << "Cpuperf device not supported";
    return EXIT_SUCCESS;
  }

  testing::InitGoogleTest(&argc, argv);

  return RUN_ALL_TESTS();
}
