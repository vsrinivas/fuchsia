// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/console/commands/verb_up.h"

#include "gtest/gtest.h"
#include "src/developer/debug/zxdb/client/mock_frame.h"
#include "src/developer/debug/zxdb/client/mock_remote_api.h"
#include "src/developer/debug/zxdb/console/console_test.h"
#include "src/developer/debug/zxdb/console/mock_console.h"
#include "src/developer/debug/zxdb/symbols/mock_symbol_data_provider.h"

namespace zxdb {

namespace {

class VerbUp : public ConsoleTest {};

}  // namespace

TEST_F(VerbUp, Up) {
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
