// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/console/commands/verb_watch.h"

#include "gtest/gtest.h"
#include "src/developer/debug/zxdb/client/mock_frame.h"
#include "src/developer/debug/zxdb/client/mock_remote_api.h"
#include "src/developer/debug/zxdb/console/commands/breakpoint_command_test.h"
#include "src/developer/debug/zxdb/console/console_test.h"

namespace zxdb {

namespace {

class VerbWatch : public BreakpointCommandTest {};

}  // namespace

TEST_F(VerbWatch, Temporary) {
  console().ProcessInputLine("watch 0x12345");
  auto event = console().GetOutputEvent();
  ASSERT_EQ(MockConsole::OutputEvent::Type::kOutput, event.type);
  EXPECT_EQ(
      "This expression's value is stored in a temporary location.\n"
      "Only values stored in memory can be watched.",
      event.output.AsString());

  // No breakpoints should have been created.
  EXPECT_TRUE(session().system().GetBreakpoints().empty());
}

TEST_F(VerbWatch, Value) {
  constexpr uint64_t kAddress = 0x1234;

  // While not strictly necessary, the full expression is evaluated and then we take its address.
  // This simplifies the implementation but does mean that all addresses we use need to map to valid
  // data. Provide that here.
  breakpoint_remote_api()->AddMemory(kAddress, {0, 0});

  console().ProcessInputLine("watch *(uint16_t*)0x1234");
  auto event = console().GetOutputEvent();
  ASSERT_EQ(MockConsole::OutputEvent::Type::kOutput, event.type);
  EXPECT_EQ("Created Breakpoint 1 type=write size=2 @ 0x1234\n", event.output.AsString());

  // One breakpoint should have been created.
  auto breakpoints = session().system().GetBreakpoints();
  ASSERT_EQ(1u, breakpoints.size());

  // Validate the set request.
  ASSERT_TRUE(breakpoint_remote_api()->last_request);
  ASSERT_EQ(1u, breakpoint_remote_api()->last_request->breakpoint.locations.size());
  EXPECT_EQ(kAddress,
            breakpoint_remote_api()->last_request->breakpoint.locations[0].address_range.begin());
  EXPECT_EQ(2u,
            breakpoint_remote_api()->last_request->breakpoint.locations[0].address_range.size());
}

}  // namespace zxdb
