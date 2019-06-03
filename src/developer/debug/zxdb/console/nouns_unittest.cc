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

class NounsTest : public RemoteAPITest {
 public:
  std::unique_ptr<RemoteAPI> GetRemoteAPIImpl() {
    auto remote_api = std::make_unique<MockRemoteAPI>();
    mock_remote_api_ = remote_api.get();
    return remote_api;
  }

  MockRemoteAPI* mock_remote_api() const { return mock_remote_api_; }

 private:
  MockRemoteAPI* mock_remote_api_ = nullptr;  // Owned by System.
};

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
      R"( # Scope  Stop Enabled Type     Location
 1 Global All  Enabled Software <no location>
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
  in.scope = BreakpointSettings::Scope::kSystem;
  in.location.type = InputLocation::Type::kSymbol;
  in.location.symbol = Identifier(IdentifierComponent("Foo"));
  bp->SetSettings(
      in, [](const Err&) { debug_ipc::MessageLoop::Current()->QuitNow(); });
  loop().Run();

  // List breakpoints now that there are settings.
  console.ProcessInputLine(kListBreakpointsLine);
  event = console.GetOutputEvent();
  ASSERT_EQ(MockConsole::OutputEvent::Type::kOutput, event.type);
  ASSERT_EQ(
      R"( # Scope  Stop Enabled  Type     Location
 1 Global All  Disabled Software Foo
)",
      event.output.AsString());

  // Currently we don't test printing breakpoint locations since that requires
  // injecting a mock process with mock symbols. If we add infrastructure for
  // other noun tests to do this such that this can be easily written, we
  // should add something here.
}

}  // namespace zxdb
