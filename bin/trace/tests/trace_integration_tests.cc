// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fxl/command_line.h>
#include <lib/fxl/log_settings_command_line.h>

#include "garnet/bin/trace/tests/run_test.h"
#include "gtest/gtest.h"

// Persistent storage is used to assist debugging failures.
const char kOutputFilePath[] = "/data/test.trace";

static void RunAndVerify(const char* tspec_path) {
  ASSERT_TRUE(RunTspec(tspec_path, kOutputFilePath));
  ASSERT_TRUE(VerifyTspec(tspec_path, kOutputFilePath));
}

TEST(Oneshot, FillBuffer) {
  RunAndVerify("/pkg/data/oneshot.tspec");
}

TEST(Circular, FillBuffer) {
  RunAndVerify("/pkg/data/circular.tspec");
}

TEST(Streaming, FillBuffer) {
  RunAndVerify("/pkg/data/streaming.tspec");
}

// Provide our own main so that --verbose,etc. are recognized.
// This is useful because our verbosity is passed on to each test.
int main(int argc, char** argv)
{
  auto cl = fxl::CommandLineFromArgcArgv(argc, argv);
  if (!fxl::SetLogSettingsFromCommandLine(cl))
    return EXIT_FAILURE;

  testing::InitGoogleTest(&argc, argv);

  return RUN_ALL_TESTS();
}
