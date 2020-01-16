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
  ASSERT_EQ("Created Breakpoint 1 (Software) on global, Enabled, Stop all, 1 addr @ 0x1234\n",
            event.output.AsString());

  // Make a new process that's not running and then a breakpoint.
  console().ProcessInputLine("process new");
  console().FlushOutputEvents();
  console().ProcessInputLine("break SomePendingFunc");

  // It should give a pending message.
  event = console().GetOutputEvent();
  ASSERT_EQ(MockConsole::OutputEvent::Type::kOutput, event.type);
  ASSERT_EQ(
      "Created Breakpoint 2 (Software) on global, Enabled, Stop all, 0 addrs @ SomePendingFunc\n"
      "Pending: No matches for location, it will be pending library loads.\n",
      event.output.AsString());
}

}  // namespace zxdb
