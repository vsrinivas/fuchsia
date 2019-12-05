// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/console/actions.h"

#include "gtest/gtest.h"
#include "src/developer/debug/shared/platform_message_loop.h"
#include "src/developer/debug/zxdb/client/session.h"
#include "src/developer/debug/zxdb/console/console_impl.h"
#include "src/lib/fxl/strings/string_printf.h"

namespace zxdb {

namespace {

using debug_ipc::MessageLoop;

#define STOP_MESSAGE_LOOP() \
  MessageLoop::Current()->PostTask(FROM_HERE, []() { MessageLoop::Current()->QuitNow(); })

// We override the Console in order to be able to override the dispatch
// function
class ConsoleTest : public ConsoleImpl {
 public:
  ConsoleTest(Session* session) : ConsoleImpl(session) {}

  void ProcessInputLine(const std::string& line, CommandCallback callback = nullptr) override {
    // We update the info
    calls.push_back(line);
    if (callback) {
      callback(errors_to_run[call_count]);
    }
    call_count++;
  }

  std::vector<std::string> calls;
  std::vector<Err> errors_to_run;
  size_t call_count = 0;
};

class ActionsTest : public testing::Test {
 protected:
  ActionsTest() {
    loop.Init();
    session = std::make_unique<Session>();
    console = std::make_unique<ConsoleTest>(session.get());
  }
  ~ActionsTest() { loop.Cleanup(); }

  debug_ipc::PlatformMessageLoop loop;
  std::unique_ptr<Session> session;
  std::unique_ptr<ConsoleTest> console;
};

}  // namespace

using debug_ipc::MessageLoop;

TEST_F(ActionsTest, ScriptFile) {
  // We expect 3 calls
  console->errors_to_run = {Err(), Err(), Err()};
  // Setup the mock contents
  std::vector<std::string> mock_commands = {"help", "connect 192.168.0.1 2345", "break main",
                                            "run /path/to/binary"};
  std::stringstream ss;
  ss << mock_commands[0] << "\n" << mock_commands[1] << "\n" << mock_commands[2];
  std::string mock_contents = ss.str();

  auto actions = CommandsToActions(mock_contents);

  ASSERT_EQ(actions.size(), 3u);
  EXPECT_EQ(actions[0].name(), mock_commands[0]);
  EXPECT_EQ(actions[1].name(), mock_commands[1]);
  EXPECT_EQ(actions[2].name(), mock_commands[2]);

  Err callback_err("FAIL");
  auto callback = [&](Err err) {
    callback_err = err;
    STOP_MESSAGE_LOOP();
  };
  // The callback mechanism depends on a global ActionFlow
  ActionFlow& flow = ActionFlow::Singleton();
  flow.ScheduleActions(std::move(actions), session.get(), console.get(), callback);
  loop.Run();

  EXPECT_FALSE(callback_err.has_error()) << callback_err.msg();

  ASSERT_EQ(console->calls.size(), 3u);
  EXPECT_EQ(console->calls[0], mock_commands[0]);
  EXPECT_EQ(console->calls[1], mock_commands[1]);
  EXPECT_EQ(console->calls[2], mock_commands[2]);

  ASSERT_EQ(flow.callbacks().size(), 3u);
  EXPECT_FALSE(flow.callbacks()[0].has_error());
  EXPECT_FALSE(flow.callbacks()[1].has_error());
  EXPECT_FALSE(flow.callbacks()[2].has_error());
}

TEST_F(ActionsTest, ScriptFileWithFailure) {
  // We expect an error
  console->errors_to_run = {Err(), Err("ERROR"), Err()};
  // Setup the mock contents
  std::vector<std::string> mock_commands = {"help", "connect 192.168.0.1 2345",
                                            "run /path/to/binary"};
  std::stringstream ss;
  ss << mock_commands[0] << "\n" << mock_commands[1] << "\n" << mock_commands[2];
  std::string mock_contents = ss.str();

  auto actions = CommandsToActions(mock_contents);

  ASSERT_EQ(actions.size(), 3u);
  EXPECT_EQ(actions[0].name(), mock_commands[0]);
  EXPECT_EQ(actions[1].name(), mock_commands[1]);
  EXPECT_EQ(actions[2].name(), mock_commands[2]);

  Err callback_err("FAIL");
  auto callback = [&](Err err) {
    callback_err = err;
    STOP_MESSAGE_LOOP();
  };

  // The callback mechanism depends on a global ActionFlow
  ActionFlow& flow = ActionFlow::Singleton();
  flow.Clear();
  flow.ScheduleActions(std::move(actions), session.get(), console.get(), callback);
  loop.Run();

  // The error callback was called
  EXPECT_TRUE(callback_err.has_error());

  // The second call fails
  ASSERT_EQ(console->calls.size(), 2u);
  EXPECT_EQ(console->calls[0], mock_commands[0]);
  EXPECT_EQ(console->calls[1], mock_commands[1]);

  ASSERT_EQ(flow.callbacks().size(), 2u);
  EXPECT_FALSE(flow.callbacks()[0].has_error());
  EXPECT_TRUE(flow.callbacks()[1].has_error());
}

}  // namespace zxdb
