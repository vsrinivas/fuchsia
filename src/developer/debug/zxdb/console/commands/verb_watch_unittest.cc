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
      "This expression's value is stored in a temporary location. Only values\n"
      "stored in memory can be watched.\n"
      "\n"
      "The watch command will implicitly take the address of the result of the\n"
      "expression. To set a breakpoint on a literal address you can do either:\n"
      "\n"
      "  watch *(uint32_t*)0x12345678\n"
      "  break --type=write --size=4 0x12345678\n",
      event.output.AsString());

  // No breakpoints should have been created.
  EXPECT_TRUE(session().system().GetBreakpoints().empty());
}

TEST_F(VerbWatch, Nonexistant) {
  // This variable is not found.
  console().ProcessInputLine("break -t write *nonesitant_var");

  auto event = console().GetOutputEvent();
  ASSERT_EQ(MockConsole::OutputEvent::Type::kOutput, event.type);
  EXPECT_EQ(
      "Unable to evaluate the expression for the command. The result was:\n"
      "  No variable 'nonesitant_var' found.",
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
