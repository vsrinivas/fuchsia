// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gtest/gtest.h"
#include "src/developer/debug/zxdb/client/mock_frame.h"
#include "src/developer/debug/zxdb/client/mock_remote_api.h"
//#include "src/developer/debug/zxdb/client/remote_api_test.h"
#include "src/developer/debug/zxdb/console/console_test.h"
#include "src/developer/debug/zxdb/console/mock_console.h"
#include "src/developer/debug/zxdb/symbols/mock_symbol_data_provider.h"

namespace zxdb {

namespace {

class VerbsThreadTest : public ConsoleTest {};

}  // namespace

// A client end-to-end test for vector register formats.
TEST_F(VerbsThreadTest, VectorRegisterFormat) {
  // Thread needs to be stopped. We can't use InjectExceptionWithStack because we want the real
  // frame implementation which provides the real EvalContext and SymbolDataSource.
  debug_ipc::NotifyException exception;
  exception.type = debug_ipc::ExceptionType::kSingleStep;
  exception.thread.process_koid = kProcessKoid;
  exception.thread.thread_koid = kThreadKoid;
  exception.thread.state = debug_ipc::ThreadRecord::State::kBlocked;
  exception.thread.frames.emplace_back(0x10000, 0x20000, 0x3000);
  InjectException(exception);

  // Don't care about the stop notification.
  loop().RunUntilNoTasks();
  console().FlushOutputEvents();

  // Provide a frame with a value for register xmm0 (the default architecture of the test harness is
  // X64 so use it's vector registers).
  ASSERT_EQ(debug_ipc::Arch::kX64, session().arch());
  mock_remote_api()->SetRegisterCategory(
      debug_ipc::RegisterCategory::kVector,
      {debug_ipc::Register(
          debug_ipc::RegisterID::kX64_xmm0,
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

TEST_F(VerbsThreadTest, LanguagePreference) {
  console().ProcessInputLine("set language c++");
  console().GetOutputEvent();  // Eat output from the set.
  console().ProcessInputLine("print 3 as u64");
  auto event = console().GetOutputEvent();
  EXPECT_EQ("Unexpected input, did you forget an operator?\n  3 as u64\n    ^",
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
  EXPECT_EQ("Unexpected input, did you forget an operator?\n  3 as u64\n    ^",
            event.output.AsString());
}

TEST_F(VerbsThreadTest, Up) {
  std::vector<std::unique_ptr<Frame>> frames;
  constexpr uint64_t kAddress0 = 0x12471253;
  constexpr uint64_t kSP0 = 0x2000;
  frames.push_back(std::make_unique<MockFrame>(
      &session(), thread(), Location(Location::State::kSymbolized, kAddress0), kSP0));

  // Inject a partial stack for an exception the "up" command will have to request more frames.
  InjectExceptionWithStack(kProcessKoid, kThreadKoid, debug_ipc::ExceptionType::kSingleStep,
                           std::move(frames), false);

  // Don't care about the stop notification.
  loop().RunUntilNoTasks();
  console().FlushOutputEvents();

  // This is the reply with the full stack it will get asynchronously. We add three stack
  // frames.
  debug_ipc::ThreadStatusReply thread_status;
  thread_status.record.process_koid = kProcessKoid;
  thread_status.record.thread_koid = kThreadKoid;
  thread_status.record.state = debug_ipc::ThreadRecord::State::kBlocked;
  thread_status.record.stack_amount = debug_ipc::ThreadRecord::StackAmount::kFull;
  thread_status.record.frames.emplace_back(kAddress0, kSP0, kSP0);
  thread_status.record.frames.emplace_back(kAddress0 + 16, kSP0 + 16, kSP0 + 16);
  thread_status.record.frames.emplace_back(kAddress0 + 32, kSP0 + 32, kSP0 + 32);

  mock_remote_api()->set_thread_status_reply(thread_status);

  // This will be at frame "0" initially. Going up should take us to from 2, but it will have to
  // request the frames before these can complete which we respond to asynchronously after.
  console().ProcessInputLine("up");
  console().ProcessInputLine("up");

  loop().RunUntilNoTasks();

  auto event = console().GetOutputEvent();
  EXPECT_EQ("Frame 1 0x12471263", event.output.AsString());
  event = console().GetOutputEvent();
  EXPECT_EQ("Frame 2 0x12471273", event.output.AsString());
}

}  // namespace zxdb
