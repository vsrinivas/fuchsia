// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gtest/gtest.h"
#include "src/developer/debug/zxdb/client/breakpoint.h"
#include "src/developer/debug/zxdb/client/breakpoint_settings.h"
#include "src/developer/debug/zxdb/client/mock_remote_api.h"
#include "src/developer/debug/zxdb/client/remote_api_test.h"
#include "src/developer/debug/zxdb/client/system.h"
#include "src/developer/debug/zxdb/console/mock_console.h"

namespace zxdb {

namespace {

class NounsTest : public RemoteAPITest {};

}  // namespace

TEST_F(NounsTest, BreakpointList) {
  MockConsole console(&session());

  const char kListBreakpointsLine[] = "bp";
  const char kListBreakpointsVerboseLine[] = "bp -v";

  // List breakpoints when there are none.
  console.ProcessInputLine(kListBreakpointsLine);
  auto event = console.GetOutputEvent();
  ASSERT_EQ(MockConsole::OutputEvent::Type::kOutput, event.type);
  ASSERT_EQ("No breakpoints.\n", event.output.AsString());

  // Create a breakpoint with no settings.
  const char kExpectedNoSettings[] =
      R"( # Scope  Stop Enabled Type     # Addrs Location
 1 Global All  Enabled Software       0 <no location>
)";
  Breakpoint* bp = session().system().CreateNewBreakpoint();
  console.ProcessInputLine(kListBreakpointsLine);
  event = console.GetOutputEvent();
  ASSERT_EQ(MockConsole::OutputEvent::Type::kOutput, event.type);
  ASSERT_EQ(kExpectedNoSettings, event.output.AsString());

  // Verbose list, there are no locations so this should be the same.
  console.ProcessInputLine(kListBreakpointsVerboseLine);
  event = console.GetOutputEvent();
  ASSERT_EQ(MockConsole::OutputEvent::Type::kOutput, event.type);
  ASSERT_EQ(kExpectedNoSettings, event.output.AsString());

  // Set location.
  BreakpointSettings in;
  in.enabled = false;
  in.locations.emplace_back(Identifier(IdentifierComponent("Foo")));
  bp->SetSettings(in, [](const Err&) { debug_ipc::MessageLoop::Current()->QuitNow(); });
  loop().Run();

  // List breakpoints now that there are settings.
  console.ProcessInputLine(kListBreakpointsLine);
  event = console.GetOutputEvent();
  ASSERT_EQ(MockConsole::OutputEvent::Type::kOutput, event.type);
  ASSERT_EQ(
      R"( # Scope  Stop Enabled  Type     # Addrs Location
 1 Global All  Disabled Software       0 Foo
)",
      event.output.AsString());

  // Currently we don't test printing breakpoint locations since that requires
  // injecting a mock process with mock symbols. If we add infrastructure for
  // other noun tests to do this such that this can be easily written, we
  // should add something here.
}

TEST_F(NounsTest, FilterTest) {
  MockConsole console(&session());

  console.ProcessInputLine("filter");
  auto event = console.GetOutputEvent();
  ASSERT_EQ(MockConsole::OutputEvent::Type::kOutput, event.type);
  ASSERT_EQ("No filters.\n", event.output.AsString());

  console.ProcessInputLine("filter attach foo");
  event = console.GetOutputEvent();
  ASSERT_EQ(MockConsole::OutputEvent::Type::kOutput, event.type);
  ASSERT_EQ("\"filter\" may not be specified for this command.",
            event.output.AsString());

  console.ProcessInputLine("attach foobar");
  event = console.GetOutputEvent();
  ASSERT_EQ(MockConsole::OutputEvent::Type::kOutput, event.type);
  ASSERT_EQ(
      "Waiting for process matching \"foobar\".\n"
      "Type \"filter\" to see the current filters.",
      event.output.AsString());

  console.ProcessInputLine("job 1 attach boofar");
  event = console.GetOutputEvent();
  ASSERT_EQ(MockConsole::OutputEvent::Type::kOutput, event.type);
  ASSERT_EQ(
      "Waiting for process matching \"boofar\".\n"
      "Type \"filter\" to see the current filters.",
      event.output.AsString());

  console.ProcessInputLine("filter");
  event = console.GetOutputEvent();
  ASSERT_EQ(MockConsole::OutputEvent::Type::kOutput, event.type);
  ASSERT_EQ(
      "  # Pattern Job\n"
      "  1 foobar    *\n"
      "▶ 2 boofar    1\n",
      event.output.AsString());

  // Delete the wildcard filters.
  console.ProcessInputLine("filter 1 rm");
  console.ProcessInputLine("filter 3 rm");
  console.FlushOutputEvents();

  // Job-specific filter listing.
  console.ProcessInputLine("job 1 filter");
  event = console.GetOutputEvent();
  ASSERT_EQ(MockConsole::OutputEvent::Type::kOutput, event.type);
  ASSERT_EQ(
      "Filters for job 1 only:\n"
      "  # Pattern Job\n"
      "▶ 2 boofar    1\n",
      event.output.AsString());

  // List current job's filters when there aren't any.
  console.ProcessInputLine("job new");
  console.FlushOutputEvents();
  console.ProcessInputLine("job filter");
  event = console.GetOutputEvent();
  ASSERT_EQ(MockConsole::OutputEvent::Type::kOutput, event.type);
  ASSERT_EQ("No filters for job 2.\n", event.output.AsString());
}

}  // namespace zxdb
