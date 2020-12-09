// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fbl/string_printf.h>
#include <gtest/gtest.h>

#include "memory-pressure.h"

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

const char* kAppName = "sysmem-memory-pressure";

class MemoryPressure : public testing::Test {
 public:
  void SetUp() override { ClearOutputs(); }
};

TEST_F(MemoryPressure, NoSize) {
  EXPECT_NE(0, MemoryPressureCommand(fxl::CommandLineFromInitializerList({kAppName}), false));
}

TEST_F(MemoryPressure, BadSize) {
  EXPECT_NE(0, MemoryPressureCommand(fxl::CommandLineFromInitializerList({kAppName, "a"}), false));
}

TEST_F(MemoryPressure, Working) {
  EXPECT_EQ(0, MemoryPressureCommand(fxl::CommandLineFromInitializerList({kAppName, "1"}), false));
}

TEST_F(MemoryPressure, WorkingExplicitHeap) {
  // 0 is the system memory heap.
  EXPECT_EQ(0, MemoryPressureCommand(
                   fxl::CommandLineFromInitializerList({kAppName, "--heap=0", "1"}), false));
}

TEST_F(MemoryPressure, BadHeap) {
  EXPECT_NE(0, MemoryPressureCommand(
                   fxl::CommandLineFromInitializerList({kAppName, "--heap=1a", "1"}), false));
}

TEST_F(MemoryPressure, WorkingContiguous) {
  EXPECT_EQ(0, MemoryPressureCommand(
                   fxl::CommandLineFromInitializerList({kAppName, "--contiguous", "1"}), false));
}

}  // namespace
