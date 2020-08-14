// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fbl/string_printf.h>
#include <gtest/gtest.h>

#include "mali-performance-counters.h"

namespace {

std::string log_output;
std::string log_error_output;

void ClearOutputs() {
  log_output = "";
  log_error_output = "";
}
}  // namespace

void Log(const char* format, ...) {
  va_list args;
  va_start(args, format);
  log_output += fbl::StringVPrintf(format, args).c_str();
  va_end(args);
}

void LogError(const char* format, ...) {
  va_list args;
  va_start(args, format);
  log_error_output += fbl::StringVPrintf(format, args).c_str();
  va_end(args);
}

void FlushLog(bool error) {
  // No-op.
}

namespace {

const char* kAppName = "mali-performance-counters";

class PerfCounterClient : public testing::Test {
 public:
  void SetUp() override { ClearOutputs(); }
};

TEST_F(PerfCounterClient, Log) {
  fxl::CommandLine command_line = fxl::CommandLineFromInitializerList({kAppName, "--log"});
  EXPECT_EQ(0, CapturePerformanceCounters(command_line));
  EXPECT_NE(log_output.find("GpuCycles"), std::string::npos);
}

TEST_F(PerfCounterClient, TooManyOptions) {
  EXPECT_NE(0, CapturePerformanceCounters(
                   fxl::CommandLineFromInitializerList({kAppName, "--log", "--log-continuous"})));
  EXPECT_NE(0, CapturePerformanceCounters(
                   fxl::CommandLineFromInitializerList({kAppName, "--trace", "--log"})));
  EXPECT_NE(0, CapturePerformanceCounters(
                   fxl::CommandLineFromInitializerList({kAppName, "--trace", "--log-continuous"})));
}

TEST_F(PerfCounterClient, LogSpecificCounter) {
  fxl::CommandLine command_line =
      fxl::CommandLineFromInitializerList({kAppName, "--log", "--counters=TilerCycles"});
  EXPECT_EQ(0, CapturePerformanceCounters(command_line));
  EXPECT_EQ(log_output.find("GpuCycles"), std::string::npos) << log_output;
  EXPECT_NE(log_output.find("TilerCycles"), std::string::npos) << log_output;
}

TEST_F(PerfCounterClient, BadCounterName) {
  fxl::CommandLine command_line =
      fxl::CommandLineFromInitializerList({kAppName, "--log", "--counters=Blah"});
  EXPECT_NE(0, CapturePerformanceCounters(command_line));
  EXPECT_NE(log_error_output.find("Invalid counter name"), std::string::npos) << log_output;
}

}  // namespace
