// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gtest/gtest.h"
#include "src/developer/debug/zxdb/console/console_test.h"

namespace zxdb {

namespace {

class VerbsBreakpointTest : public ConsoleTest {};

}  // namespace

TEST_F(VerbsBreakpointTest, Break) {
  // Process starts out as running. Make an expression breakpoint.
  console().ProcessInputLine("break \"*0x1230 + 4\"");

  auto event = console().GetOutputEvent();
  ASSERT_EQ(MockConsole::OutputEvent::Type::kOutput, event.type);
  EXPECT_EQ("Created Breakpoint 1 @ 0x1234\n", event.output.AsString());

  // Make a new process that's not running and then a breakpoint.
  console().ProcessInputLine("process new");
  console().FlushOutputEvents();
  console().ProcessInputLine("break SomePendingFunc");

  // It should give a pending message.
  event = console().GetOutputEvent();
  ASSERT_EQ(MockConsole::OutputEvent::Type::kOutput, event.type);
  EXPECT_EQ(
      "Created Breakpoint 2 @ SomePendingFunc\n"
      "Pending: No current matches for location. It will be matched against new\n"
      "         processes and shared libraries.\n",
      event.output.AsString());
}

}  // namespace zxdb
