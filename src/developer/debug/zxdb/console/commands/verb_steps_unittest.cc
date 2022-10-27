// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/console/commands/verb_steps.h"

#include <gtest/gtest.h>

#include "src/developer/debug/zxdb/client/mock_frame.h"
#include "src/developer/debug/zxdb/client/mock_remote_api.h"
#include "src/developer/debug/zxdb/client/substatement.h"
#include "src/developer/debug/zxdb/console/console_test.h"

namespace zxdb {

namespace {

class VerbSteps : public ConsoleTest {};

}  // namespace

TEST_F(VerbSteps, Test) {
  constexpr TargetPointer kLineBegin = 0x10000;

  std::vector<std::unique_ptr<Frame>> frames;
  frames.push_back(std::make_unique<MockFrame>(
      &session(), thread(), Location(Location::State::kSymbolized, kLineBegin), 0x2000));
  InjectExceptionWithStack(ConsoleTest::kProcessKoid, ConsoleTest::kThreadKoid,
                           debug_ipc::ExceptionType::kSingleStep, std::move(frames), true);

  // Don't care about the stop notification.
  loop().RunUntilNoTasks();
  console().FlushOutputEvents();

  std::vector<SubstatementCall> substatements;
  constexpr TargetPointer kCall1At = kLineBegin + 4;
  constexpr TargetPointer kCall1To = 0x20000;
  SubstatementCall& ss1 = substatements.emplace_back();
  ss1.call_addr = kCall1At;
  ss1.call_dest = kCall1To;

  constexpr TargetPointer kCall2At = kCall1At + 4;
  constexpr TargetPointer kCall2To = 0x20020;
  SubstatementCall& ss2 = substatements.emplace_back();
  ss2.call_addr = kCall2At;
  ss2.call_dest = kCall2To;

  // This should show the menu with the above options.
  auto cmd_context = fxl::MakeRefCounted<OfflineCommandContext>(
      &console(), [](OutputBuffer output, std::vector<Err> errors) {});
  RunVerbStepsWithSubstatements(thread(), substatements, cmd_context);

  // We didn't supply any symbols so the destinations aren't symbolized.
  auto event = console().GetOutputEvent();
  EXPECT_EQ(MockConsole::OutputEvent::Type::kOutput, event.type);
  EXPECT_EQ(
      "  1 0x20000\n"
      "  2 0x20020\n"
      "  quit\n",
      event.output.AsString());

  ASSERT_TRUE(console().SendModalReply("2"));

  // That should continue.
  EXPECT_EQ(1, mock_remote_api()->GetAndResetResumeCount());

  // Here we test sending a totally different address outside of the step range and check that
  // execution stopped. We assume the normal execution of the step controller for this is tested
  // by its own test.
  frames.push_back(std::make_unique<MockFrame>(
      &session(), thread(), Location(Location::State::kSymbolized, kLineBegin + 0x1000), 0x2000));
  InjectExceptionWithStack(ConsoleTest::kProcessKoid, ConsoleTest::kThreadKoid,
                           debug_ipc::ExceptionType::kSingleStep, std::move(frames), true);

  // 0 resume count means stop.
  EXPECT_EQ(0, mock_remote_api()->GetAndResetResumeCount());
}

}  // namespace zxdb
