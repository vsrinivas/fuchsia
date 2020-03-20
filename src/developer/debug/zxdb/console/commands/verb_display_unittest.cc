// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/console/commands/verb_display.h"

#include "gtest/gtest.h"
#include "src/developer/debug/zxdb/client/mock_frame.h"
#include "src/developer/debug/zxdb/client/mock_remote_api.h"
#include "src/developer/debug/zxdb/client/substatement.h"
#include "src/developer/debug/zxdb/console/console_test.h"

namespace zxdb {

namespace {

class VerbDisplay : public ConsoleTest {
 public:
  // Reads one even from the console and returns true if it's the expected stop event at the
  // unsymbolized 0x1000 address.
  bool IsStopEvent() {
    auto event = console().GetOutputEvent();
    if (event.type != MockConsole::OutputEvent::Type::kOutput)
      return false;
    return event.output.AsString() == "🛑 0x1000 (no symbol info)\n";
  }
};

}  // namespace

TEST_F(VerbDisplay, Test) {
  // Use a constant so we don't have to set up a full evaluation environment.
  console().ProcessInputLine("display 99");
  console().ProcessInputLine("display \"hello, world\"");
  console().FlushOutputEvents();

  // This duplicate should be ignored.
  console().ProcessInputLine("display 99");
  auto event = console().GetOutputEvent();
  EXPECT_EQ(MockConsole::OutputEvent::Type::kOutput, event.type);
  EXPECT_EQ("Already watching expression \"99\".", event.output.AsString());

  std::vector<std::unique_ptr<Frame>> frames;
  Location location(Location::State::kSymbolized, 0x1000);
  frames.push_back(std::make_unique<MockFrame>(&session(), thread(), location, 0x2000));
  InjectExceptionWithStack(ConsoleTest::kProcessKoid, ConsoleTest::kThreadKoid,
                           debug_ipc::ExceptionType::kSingleStep, std::move(frames), true);

  loop().RunUntilNoTasks();

  // First should be the stop notification and then the variables should be printed.
  EXPECT_TRUE(IsStopEvent());
  event = console().GetOutputEvent();
  EXPECT_EQ(MockConsole::OutputEvent::Type::kOutput, event.type);
  EXPECT_EQ("99 = 99, \"hello, world\" = \"hello, world\"", event.output.AsString());

  // Remove the number and inject another stop.
  console().ProcessInputLine("set display -= 99");
  console().FlushOutputEvents();
  frames.push_back(std::make_unique<MockFrame>(&session(), thread(), location, 0x2000));
  InjectExceptionWithStack(ConsoleTest::kProcessKoid, ConsoleTest::kThreadKoid,
                           debug_ipc::ExceptionType::kSingleStep, std::move(frames), true);

  // Should have the stop event and just the string.
  EXPECT_TRUE(IsStopEvent());
  event = console().GetOutputEvent();
  EXPECT_EQ(MockConsole::OutputEvent::Type::kOutput, event.type);
  EXPECT_EQ("\"hello, world\" = \"hello, world\"", event.output.AsString());

  // Clear the display variable and now there should be nothing.
  console().ProcessInputLine("set display =");
  console().FlushOutputEvents();
  frames.push_back(std::make_unique<MockFrame>(&session(), thread(), location, 0x2000));
  InjectExceptionWithStack(ConsoleTest::kProcessKoid, ConsoleTest::kThreadKoid,
                           debug_ipc::ExceptionType::kSingleStep, std::move(frames), true);

  EXPECT_TRUE(IsStopEvent());
  EXPECT_FALSE(console().HasOutputEvent());
  event = console().GetOutputEvent();
}

}  // namespace zxdb
