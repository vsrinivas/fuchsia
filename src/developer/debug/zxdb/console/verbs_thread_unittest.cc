// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gtest/gtest.h"
#include "src/developer/debug/zxdb/client/mock_frame.h"
#include "src/developer/debug/zxdb/client/mock_remote_api.h"
#include "src/developer/debug/zxdb/client/remote_api_test.h"
#include "src/developer/debug/zxdb/console/mock_console.h"
#include "src/developer/debug/zxdb/symbols/mock_symbol_data_provider.h"

namespace zxdb {

namespace {

class VerbsThreadTest : public RemoteAPITest {};

}  // namespace

// A client end-to-end test for vector register formats.
TEST_F(VerbsThreadTest, VectorRegisterFormat) {
  MockConsole console(&session());

  // Inject a fake running process.
  constexpr uint64_t kProcessKoid = 1234;
  InjectProcess(kProcessKoid);
  constexpr uint64_t kThreadKoid = 5678;
  InjectThread(kProcessKoid, kThreadKoid);

  // Thread needs to be stopped. We can't use InjectExceptionWithStack because we want the real
  // frame implementation which provides the real EvalContext and SymbolDataSource.
  debug_ipc::NotifyException exception;
  exception.type = debug_ipc::ExceptionType::kSingleStep;
  exception.thread.process_koid = kProcessKoid;
  exception.thread.thread_koid = kThreadKoid;
  exception.thread.state = debug_ipc::ThreadRecord::State::kBlocked;
  exception.thread.frames.emplace_back(0x10000, 0x20000, 0x3000);
  InjectException(exception);
  console.Clear();

  // Provide a frame with a value for register xmm0 (the default architecture of the test harness is
  // X64 so use it's vector registers).
  ASSERT_EQ(debug_ipc::Arch::kX64, session().arch());
  mock_remote_api()->SetRegisterCategory(
      debug_ipc::RegisterCategory::kVector,
      {debug_ipc::Register(
          debug_ipc::RegisterID::kX64_xmm0,
          std::vector<uint8_t>{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0xa, 0xb, 0xc, 0xd, 0xe, 0xf})});

  console.ProcessInputLine("set vector-format i64");
  console.GetOutputEvent();  // Eat output from the set.

  // The default architecture of the test harness is ARM64 so use it's vector registers.
  console.ProcessInputLine("print -x xmm0");
  loop().RunUntilNoTasks();
  auto event = console.GetOutputEvent();
  EXPECT_EQ("{0x706050403020100, 0xf0e0d0c0b0a0908}", event.output.AsString());

  console.ProcessInputLine("set vector-format u16");
  console.GetOutputEvent();  // Eat output from the set.
  console.ProcessInputLine("print -x xmm0");
  loop().RunUntilNoTasks();
  event = console.GetOutputEvent();
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

TEST_F(VerbsThreadTest, LanguagePreference) {
  MockConsole console(&session());

  console.ProcessInputLine("set language c++");
  console.GetOutputEvent();  // Eat output from the set.
  console.ProcessInputLine("print 3 as u64");
  auto event = console.GetOutputEvent();
  EXPECT_EQ("Unexpected input, did you forget an operator?\n  3 as u64\n    ^",
            event.output.AsString());

  console.ProcessInputLine("set language rust");
  console.GetOutputEvent();  // Eat output from the set.
  console.ProcessInputLine("print 3 as u64");
  event = console.GetOutputEvent();
  EXPECT_EQ("3", event.output.AsString());

  console.ProcessInputLine("set language auto");
  console.GetOutputEvent();  // Eat output from the set.
  console.ProcessInputLine("print 3 as u64");
  event = console.GetOutputEvent();
  EXPECT_EQ("Unexpected input, did you forget an operator?\n  3 as u64\n    ^",
            event.output.AsString());
}

}  // namespace zxdb
