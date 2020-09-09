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
  EXPECT_EQ(ParseArgs({{"hwstress", "flash", "--fvm-path", "/path"}})->subcommand,
            StressTest::kFlash);
  EXPECT_EQ(ParseArgs({{"hwstress", "flash", "-f", "/path/to/fvm"}})->fvm_path, "/path/to/fvm");
  EXPECT_EQ(ParseArgs({{"hwstress", "flash", "--fvm-path", "/fvm/path"}})->fvm_path, "/fvm/path");

  // Flash subcommand given without FVM path
  EXPECT_TRUE(ParseArgs({{"hwstress", "flash"}}).is_error());
}

TEST(Args, ParseMemory) {
  // No optional arguments.
  CommandLineArgs args = ParseArgs({{"hwstress", "memory"}}).value();
  EXPECT_FALSE(args.ram_to_test_percent.has_value());
  EXPECT_FALSE(args.mem_to_test_megabytes.has_value());

  // Arguments given.
  EXPECT_EQ(ParseArgs({{"hwstress", "memory", "--memory", "123"}})->mem_to_test_megabytes,
            cmdline::Optional<int64_t>(123));
  EXPECT_EQ(ParseArgs({{"hwstress", "memory", "--percent-memory", "12"}})->ram_to_test_percent,
            cmdline::Optional<int64_t>(12));

  // Errors
  EXPECT_TRUE(ParseArgs({{"hwstress", "memory", "--memory", "0"}}).is_error());
  EXPECT_TRUE(ParseArgs({{"hwstress", "memory", "--memory", "-5"}}).is_error());
  EXPECT_TRUE(ParseArgs({{"hwstress", "memory", "--memory", "18446744073709551617"}}).is_error());
  EXPECT_TRUE(ParseArgs({{"hwstress", "memory", "--memory", "0.5"}}).is_error());
  EXPECT_TRUE(ParseArgs({{"hwstress", "memory", "--memory", "lots"}}).is_error());
  EXPECT_TRUE(ParseArgs({{"hwstress", "memory", "--memory", ""}}).is_error());
  EXPECT_TRUE(ParseArgs({{"hwstress", "memory", "--percent-memory", "0"}}).is_error());
  EXPECT_TRUE(ParseArgs({{"hwstress", "memory", "--percent-memory", "-5"}}).is_error());
  EXPECT_TRUE(
      ParseArgs({{"hwstress", "memory", "--percent-memory", "18446744073709551617"}}).is_error());
  EXPECT_TRUE(ParseArgs({{"hwstress", "memory", "--percent-memory", "100"}}).is_error());
  EXPECT_TRUE(ParseArgs({{"hwstress", "memory", "--percent-memory", "0.5"}}).is_error());
  EXPECT_TRUE(ParseArgs({{"hwstress", "memory", "--percent-memory", "3%"}}).is_error());
  EXPECT_TRUE(ParseArgs({{"hwstress", "memory", "--percent-memory", ""}}).is_error());
}

TEST(Args, ParseCpu) {
  // Utilization values.
  EXPECT_EQ(ParseArgs({{"hwstress", "cpu"}})->utilization_percent, 100.0);  // default
  EXPECT_EQ(ParseArgs({{"hwstress", "cpu", "-u", "100"}})->utilization_percent, 100.0);
  EXPECT_EQ(ParseArgs({{"hwstress", "cpu", "-u", "50"}})->utilization_percent, 50.0);
  EXPECT_EQ(ParseArgs({{"hwstress", "cpu", "-u", "25.5"}})->utilization_percent, 25.5);

  EXPECT_TRUE(ParseArgs({{"hwstress", "cpu", "-u", "-3"}}).is_error());
  EXPECT_TRUE(ParseArgs({{"hwstress", "cpu", "-u"}}).is_error());
  EXPECT_TRUE(ParseArgs({{"hwstress", "cpu", "-u", "0"}}).is_error());
  EXPECT_TRUE(ParseArgs({{"hwstress", "cpu", "-u", "101"}}).is_error());

  // Workload values
  EXPECT_EQ(ParseArgs({{"hwstress", "cpu"}})->cpu_workload, "");
  EXPECT_EQ(ParseArgs({{"hwstress", "cpu", "-w", "xyz"}})->cpu_workload, "xyz");
}

TEST(Args, ParseLogLevel) {
  EXPECT_EQ(ParseArgs({{"hwstress", "cpu"}})->log_level, "normal");
  EXPECT_EQ(ParseArgs({{"hwstress", "cpu", "--logging-level", "Terse"}})->log_level, "Terse");
  EXPECT_EQ(ParseArgs({{"hwstress", "cpu", "-l", "Verbose"}})->log_level, "Verbose");
  EXPECT_TRUE(ParseArgs({{"hwstress", "cpu", "-l", "Bad"}}).is_error());
}

TEST(Args, ParseLight) {
  EXPECT_TRUE(ParseArgs({{"hwstress", "light"}}).is_ok());
  EXPECT_EQ(ParseArgs({{"hwstress", "light", "--light-on-time=0.25"}})->light_on_time_seconds,
            0.25);
  EXPECT_EQ(ParseArgs({{"hwstress", "light", "--light-off-time=0.25"}})->light_off_time_seconds,
            0.25);

  EXPECT_TRUE(ParseArgs({{"hwstress", "light", "--light-on-time=-3"}}).is_error());
  EXPECT_TRUE(ParseArgs({{"hwstress", "light", "--light-off-time=-3"}}).is_error());
}

TEST(Args, ParseIterations) {
  EXPECT_EQ(ParseArgs({{"hwstress", "flash", "--fvm-path=abc"}})->iterations,
            static_cast<uint64_t>(0));
  EXPECT_EQ(ParseArgs({{"hwstress", "flash", "--fvm-path=abc", "--iterations=7"}})->iterations,
            static_cast<uint64_t>(7));
  EXPECT_EQ(ParseArgs({{"hwstress", "flash", "--fvm-path=abc", "-i", "11"}})->iterations,
            static_cast<uint64_t>(11));

  EXPECT_TRUE(ParseArgs({{"hwstress", "flash", "--fvm-path=abc", "--iterations=1", "--duration=2"}})
                  .is_error());
  EXPECT_TRUE(ParseArgs({{"hwstress", "flash", "--fvm-path=abc", "--iterations=1.5"}}).is_error());
}

TEST(Args, ParseCores) {
  EXPECT_EQ(ParseArgs({{"hwstress", "cpu", "--cpu-cores=0"}})->cores_to_test.cores,
            std::vector<uint32_t>({0}));
  EXPECT_EQ(ParseArgs({{"hwstress", "cpu", "--cpu-cores=2,1"}})->cores_to_test.cores,
            std::vector<uint32_t>({2, 1}));
  EXPECT_EQ(ParseArgs({{"hwstress", "cpu", "-p", "0,3"}})->cores_to_test.cores,
            std::vector<uint32_t>({0, 3}));
  EXPECT_FALSE(ParseArgs({{"hwstress", "cpu"}})->cores_to_test.cores.empty());

  EXPECT_TRUE(ParseArgs({{"hwstress", "cpu", "--cpu-cores=a"}}).is_error());
  EXPECT_TRUE(ParseArgs({{"hwstress", "cpu", "--cpu-cores=1.0"}}).is_error());
}

}  // namespace
}  // namespace hwstress
