// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "args.h"

#include <stdio.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>

#include <string>
#include <variant>

#include <fbl/span.h>
#include <gtest/gtest.h>

namespace hwstress {
namespace {

TEST(Args, ParseHelp) {
  EXPECT_TRUE(ParseArgs({{"hwstress", "--help"}})->help);
  EXPECT_TRUE(ParseArgs({{"hwstress", "-h"}})->help);
}

TEST(Args, ParseSubcommand) {
  // Subcommand given.
  EXPECT_EQ(ParseArgs({{"hwstress", "cpu"}})->subcommand, StressTest::kCpu);
  EXPECT_EQ(ParseArgs({{"hwstress", "memory"}})->subcommand, StressTest::kMemory);

  // No subcommand given.
  EXPECT_TRUE(ParseArgs({{"hwstress"}}).is_error());
  EXPECT_TRUE(ParseArgs({{"hwstress", "bad_subcommand"}}).is_error());
  EXPECT_TRUE(ParseArgs({{"hwstress", "-d", "3"}}).is_error());
}

TEST(Args, ParseDuration) {
  // Good duration specified.
  EXPECT_EQ(ParseArgs({{"hwstress", "cpu", "-d", "5"}})->test_duration_seconds, 5.0);
  EXPECT_EQ(ParseArgs({{"hwstress", "cpu", "-d", "0.1"}})->test_duration_seconds, 0.1);
  EXPECT_EQ(ParseArgs({{"hwstress", "cpu", "--duration", "3"}})->test_duration_seconds, 3.0);

  // Bad durations.
  EXPECT_TRUE(ParseArgs({{"hwstress", "cpu", "-d", "x"}}).is_error());
  EXPECT_TRUE(ParseArgs({{"hwstress", "cpu", "-d", "-3"}}).is_error());
}

TEST(Args, ParseFlash) {
  // Flash subcommand given with FVM path provided
  EXPECT_EQ(ParseArgs({{"hwstress", "flash", "-f", "/path"}})->subcommand, StressTest::kFlash);
  EXPECT_EQ(ParseArgs({{"hwstress", "flash", "--fvm-path", "/path"}})->subcommand, StressTest::kFlash);
  EXPECT_TRUE(ParseArgs({{"hwstress", "flash", "-f", "/path/to/fvm"}})->fvm_path.compare("/path/to/fvm") == 0);
  EXPECT_TRUE(ParseArgs({{"hwstress", "flash", "--fvm-path", "/fvm/path"}})->fvm_path.compare("/fvm/path") == 0);

  // Flash subcommand given without FVM path
  EXPECT_TRUE(ParseArgs({{"hwstress", "flash"}}).is_error());
}

}  // namespace
}  // namespace hwstress
