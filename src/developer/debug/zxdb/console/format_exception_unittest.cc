// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/console/format_exception.h"

#include <gtest/gtest.h>

namespace zxdb {

using debug_ipc::Arch;
using debug_ipc::ExceptionRecord;

TEST(FormatException, X64ExceptionToString) {
  {
    // No exception.
    ExceptionRecord record;
    EXPECT_EQ("No exception information", ExceptionRecordToString(Arch::kX64, record));
  }

  // Divide by 0.
  {
    ExceptionRecord record;
    record.valid = true;
    record.arch.x64.vector = 0;
    EXPECT_EQ("Divide-by-zero exception", ExceptionRecordToString(Arch::kX64, record));
  }

  // Page fault (read).
  {
    ExceptionRecord record;
    record.valid = true;
    record.arch.x64.vector = 14;
    record.arch.x64.err_code = 0;
    record.arch.x64.cr2 = 0x1234;
    EXPECT_EQ("Page fault reading address 0x1234", ExceptionRecordToString(Arch::kX64, record));
  }

  // Page fault (write).
  {
    ExceptionRecord record;
    record.valid = true;
    record.arch.x64.vector = 14;
    record.arch.x64.err_code = 2;
    record.arch.x64.cr2 = 0x5678;
    EXPECT_EQ("Page fault writing address 0x5678", ExceptionRecordToString(Arch::kX64, record));
  }

  // Page fault (write, second-chance).
  {
    ExceptionRecord record;
    record.valid = true;
    record.arch.x64.vector = 14;
    record.arch.x64.err_code = 2;
    record.arch.x64.cr2 = 0x5678;
    record.strategy = debug_ipc::ExceptionStrategy::kSecondChance;
    EXPECT_EQ("Page fault writing address 0x5678 (second chance)",
              ExceptionRecordToString(Arch::kX64, record));
  }

  // Random invalid exception.
  {
    ExceptionRecord record;
    record.valid = true;
    record.arch.x64.vector = 999;
    EXPECT_EQ("Unknown exception (999)", ExceptionRecordToString(Arch::kX64, record));
  }
}

TEST(FormatException, Arm64ExceptionToString) {
  // No exception.
  {
    ExceptionRecord record;
    EXPECT_EQ("No exception information", ExceptionRecordToString(Arch::kX64, record));
  }

  // SP alignment fault
  {
    ExceptionRecord record;
    record.valid = true;
    record.arch.arm64.esr = 0b10011000000000000000000000000000;
    EXPECT_EQ("SP alignment fault exception", ExceptionRecordToString(Arch::kArm64, record));
  }

  // SP alignment fault (second-chance)
  {
    ExceptionRecord record;
    record.valid = true;
    record.arch.arm64.esr = 0b10011000000000000000000000000000;
    record.strategy = debug_ipc::ExceptionStrategy::kSecondChance;
    EXPECT_EQ("SP alignment fault exception (second chance)",
              ExceptionRecordToString(Arch::kArm64, record));
  }

  // Data read fault.
  {
    ExceptionRecord record;
    record.valid = true;
    record.arch.arm64.esr = 0b10010100000000000000000000111110;
    record.arch.arm64.far = 0x1234;
    EXPECT_EQ("Data fault reading address 0x1234 (page domain fault)",
              ExceptionRecordToString(Arch::kArm64, record));
  }

  // Data write fault.
  {
    ExceptionRecord record;
    record.valid = true;
    record.arch.arm64.esr = 0b10010100000000000000000001100001;
    record.arch.arm64.far = 0x1234;
    EXPECT_EQ("Data fault writing address 0x1234 (alignment fault)",
              ExceptionRecordToString(Arch::kArm64, record));
  }
}

}  // namespace zxdb
