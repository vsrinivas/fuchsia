// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <src/lib/fxl/command_line.h>
#include <src/lib/fxl/log_settings_command_line.h>
#include <src/lib/fxl/logging.h>

#include "garnet/bin/trace/tests/run_test.h"
#include "gtest/gtest.h"

const char kTracePath[] = "/bin/trace";
const char kChildPath[] = "/pkg/bin/return_1234";

constexpr const int kChildReturnCode = 1234;

TEST(ReturnChildResult, False) {
  zx::job job{};  // -> default job
  zx::process child;
  std::vector<std::string> argv{
      kTracePath, "record", "--return-child-result=false", "--spawn",
      kChildPath};
  ASSERT_EQ(SpawnProgram(job, argv, ZX_HANDLE_INVALID, &child), ZX_OK);

  int return_code;
  ASSERT_EQ(WaitAndGetExitCode(argv[0], child, &return_code),
            ZX_OK);
  EXPECT_EQ(return_code, 0);
}

TEST(ReturnChildResult, True) {
  zx::job job{};  // -> default job
  zx::process child;
  std::vector<std::string> argv{
      kTracePath, "record", "--return-child-result=true", "--spawn",
      kChildPath};
  ASSERT_EQ(SpawnProgram(job, argv, ZX_HANDLE_INVALID, &child), ZX_OK);

  int return_code;
  ASSERT_EQ(WaitAndGetExitCode(argv[0], child, &return_code),
            ZX_OK);
  EXPECT_EQ(return_code, kChildReturnCode);
}

// Provide our own main so that --verbose,etc. are recognized.
int main(int argc, char** argv) {
  fxl::CommandLine cl = fxl::CommandLineFromArgcArgv(argc, argv);
  if (!fxl::SetLogSettingsFromCommandLine(cl))
    return EXIT_FAILURE;

  testing::InitGoogleTest(&argc, argv);

  return RUN_ALL_TESTS();
}
