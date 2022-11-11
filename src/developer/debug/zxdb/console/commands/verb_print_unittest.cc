// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/console/commands/verb_print.h"

#include <gtest/gtest.h>

#include "src/developer/debug/zxdb/client/mock_frame.h"
#include "src/developer/debug/zxdb/client/mock_remote_api.h"
#include "src/developer/debug/zxdb/console/console_test.h"
#include "src/developer/debug/zxdb/console/mock_console.h"
#include "src/developer/debug/zxdb/symbols/mock_symbol_data_provider.h"

namespace zxdb {

namespace {

class VerbPrint : public ConsoleTest {};

}  // namespace

TEST_F(VerbPrint, LanguagePreference) {
  console().ProcessInputLine("set language c++");
  console().GetOutputEvent();  // Eat output from the set.
  console().ProcessInputLine("print 3 as u64");
  auto event = console().GetOutputEvent();
  EXPECT_EQ("Unexpected token, did you forget an operator or a semicolon?\n  3 as u64\n    ^",
            event.output.AsString());

  console().ProcessInputLine("set language rust");
  console().GetOutputEvent();  // Eat output from the set.
  console().ProcessInputLine("print 3 as u64");
  event = console().GetOutputEvent();
  EXPECT_EQ("3", event.output.AsString());

  console().ProcessInputLine("set language auto");
  console().GetOutputEvent();  // Eat output from the set.
  console().ProcessInputLine("print 3 as u64");
  event = console().GetOutputEvent();
  EXPECT_EQ("Unexpected token, did you forget an operator or a semicolon?\n  3 as u64\n    ^",
            event.output.AsString());
}

TEST_F(VerbPrint, TypeOverrides) {
  // Decimal (the default).
  console().ProcessInputLine("print 100");
  auto event = console().GetOutputEvent();
  EXPECT_EQ("100", event.output.AsString());

  console().ProcessInputLine("print -d 100");
  event = console().GetOutputEvent();
  EXPECT_EQ("100", event.output.AsString());

  // Hex.
  console().ProcessInputLine("print -x 100");
  event = console().GetOutputEvent();
  EXPECT_EQ("0x64", event.output.AsString());

  // Bin.
  console().ProcessInputLine("print -b 1234");
  event = console().GetOutputEvent();
  EXPECT_EQ("0b100'11010010", event.output.AsString());

  // Unsigned (currently we treat "-1" as a 32-bit integer so the unsigned version is also 32 bits).
  // The "--" is required to mark the end of switches so the "-1" is treated as the expression
  // rather than another switch).
  console().ProcessInputLine("print -u -- -1");
  event = console().GetOutputEvent();
  EXPECT_EQ("4294967295", event.output.AsString());

  // Character.
  console().ProcessInputLine("print -c 100");
  event = console().GetOutputEvent();
  EXPECT_EQ("'d'", event.output.AsString());

  // More than one is an error.
  console().ProcessInputLine("print -d -c 100");
  event = console().GetOutputEvent();
  EXPECT_EQ("More than one type override (-b, -c, -d, -u, -x) specified.", event.output.AsString());
}

// A client end-to-end test for vector register formats.
TEST_F(VerbPrint, VectorRegisterFormat) {
  // Thread needs to be stopped. We can't use InjectExceptionWithStack because we want the real
  // frame implementation which provides the real EvalContext and SymbolDataSource.
  debug_ipc::NotifyException exception;
  exception.type = debug_ipc::ExceptionType::kSingleStep;
  exception.thread.id = {.process = kProcessKoid, .thread = kThreadKoid};
  exception.thread.state = debug_ipc::ThreadRecord::State::kBlocked;
  exception.thread.frames.emplace_back(0x10000, 0x20000, 0x3000);
  InjectException(exception);

  // Don't care about the stop notification.
  loop().RunUntilNoTasks();
  console().FlushOutputEvents();

  // Provide a frame with a value for register xmm0 (the default architecture of the test harness is
  // X64 so use it's vector registers).
  ASSERT_EQ(debug::Arch::kX64, session().arch());
  mock_remote_api()->SetRegisterCategory(
      debug::RegisterCategory::kVector,
      {debug::RegisterValue(
          debug::RegisterID::kX64_xmm0,
          std::vector<uint8_t>{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0xa, 0xb, 0xc, 0xd, 0xe, 0xf})});

  console().ProcessInputLine("set vector-format i64");
  console().GetOutputEvent();  // Eat output from the set.

  // The default architecture of the test harness is ARM64 so use it's vector registers.
  console().ProcessInputLine("print -x xmm0");
  loop().RunUntilNoTasks();
  auto event = console().GetOutputEvent();
  EXPECT_EQ("{0x706050403020100, 0xf0e0d0c0b0a0908}", event.output.AsString());

  console().ProcessInputLine("set vector-format u16");
  console().GetOutputEvent();  // Eat output from the set.
  console().ProcessInputLine("print -x xmm0");
  loop().RunUntilNoTasks();
  event = console().GetOutputEvent();
  EXPECT_EQ(
      "{\n"
      "  [0] = 0x100\n"
      "  [1] = 0x302\n"
      "  [2] = 0x504\n"
      "  [3] = 0x706\n"
      "  [4] = 0x908\n"
      "  [5] = 0xb0a\n"
      "  [6] = 0xd0c\n"
      "  [7] = 0xf0e\n"
      "}",
      event.output.AsString());
}

}  // namespace zxdb
