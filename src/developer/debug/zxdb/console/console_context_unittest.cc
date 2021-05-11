// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/console/console_context.h"

#include <gtest/gtest.h>

#include "src/developer/debug/zxdb/client/mock_remote_api.h"
#include "src/developer/debug/zxdb/console/console_test.h"

namespace zxdb {

namespace {

class ConsoleContextTest : public ConsoleTest {};

}  // namespace

// Verifies that the breakpoint is set to the current one when it's hit.
TEST_F(ConsoleContextTest, CurrentBreakpoint) {
  // Breakpoint 1.
  console().ProcessInputLine("b 0x10000");
  auto event = console().GetOutputEvent();
  ASSERT_NE(std::string::npos, event.output.AsString().find("Created Breakpoint 1"))
      << event.output.AsString();
  int breakpoint_1_backend_id = mock_remote_api()->last_breakpoint_id();

  // Breakpoint 2.
  console().ProcessInputLine("b 0x20000");
  event = console().GetOutputEvent();
  ASSERT_NE(std::string::npos, event.output.AsString().find("Created Breakpoint 2"))
      << event.output.AsString();

  // Breakpoint 2 should be active, so its address should be returned when we ask for the location.
  console().ProcessInputLine("bp get location");
  event = console().GetOutputEvent();
  ASSERT_NE(std::string::npos, event.output.AsString().find("location = 0x20000"))
      << event.output.AsString();

  // Provide a stop at breakpoint 1.
  debug_ipc::NotifyException notify;
  notify.type = debug_ipc::ExceptionType::kSoftwareBreakpoint;
  notify.thread.id = {.process = kProcessKoid, .thread = kThreadKoid};
  notify.thread.state = debug_ipc::ThreadRecord::State::kBlocked;
  notify.thread.frames.emplace_back(0x10000, 0);
  notify.hit_breakpoints.emplace_back();
  notify.hit_breakpoints[0].id = breakpoint_1_backend_id;
  notify.hit_breakpoints[0].hit_count = 1;
  InjectException(notify);

  // Should have issued a stop at the first location.
  event = console().GetOutputEvent();
  ASSERT_NE(std::string::npos, event.output.AsString().find("0x10000 (no symbol info)"))
      << event.output.AsString();

  // Breakpoint 1 should now be active.
  console().ProcessInputLine("bp get location");
  event = console().GetOutputEvent();
  ASSERT_NE(std::string::npos, event.output.AsString().find("location = 0x10000"))
      << event.output.AsString();
}

}  // namespace zxdb
