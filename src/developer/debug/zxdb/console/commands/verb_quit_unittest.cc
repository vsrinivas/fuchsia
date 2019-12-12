// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/console/commands/verb_quit.h"

#include "gtest/gtest.h"
#include "src/developer/debug/zxdb/client/remote_api_test.h"
#include "src/developer/debug/zxdb/console/mock_console.h"

namespace zxdb {

namespace {

class VerbQuit : public RemoteAPITest {};

}  // namespace

// Quit with no running processes should exit immediately.
TEST_F(VerbQuit, QuitNoProcs) {
  MockConsole console(&session());

  EXPECT_FALSE(console.has_quit());
  console.ProcessInputLine("quit");
  EXPECT_TRUE(console.has_quit());
}

// Quit with running processes should prompt.
TEST_F(VerbQuit, QuitRunningProcs) {
  MockConsole console(&session());

  InjectProcess(1234);
  console.FlushOutputEvents();  // Process attaching will output some stuff.

  // This should prompt instead of quitting.
  console.ProcessInputLine("quit");
  EXPECT_FALSE(console.has_quit());

  auto output = console.GetOutputEvent();
  ASSERT_EQ(output.type, MockConsole::OutputEvent::Type::kOutput);
  EXPECT_EQ("\nAre you sure you want to quit and detach from the running process?\n",
            output.output.AsString());

  EXPECT_TRUE(console.SendModalReply("y"));
  EXPECT_TRUE(console.has_quit());
}

}  // namespace zxdb
