// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

#include "src/developer/debug/shared/zx_status_definitions.h"
#include "src/developer/debug/zxdb/client/mock_remote_api.h"
#include "src/developer/debug/zxdb/console/commands/breakpoint_command_test.h"
#include "src/developer/debug/zxdb/console/console_test.h"

namespace zxdb {

namespace {

class VerbClear : public ConsoleTest {};

}  // namespace

TEST_F(VerbClear, Clear) {
  console().ProcessInputLine("break main");
  console().ProcessInputLine("break main");
  console().ProcessInputLine("break $main");
  console().FlushOutputEvents();

  console().ProcessInputLine("clear main");
  EXPECT_EQ("Deleted Breakpoint 1 pending @ main\n", console().GetOutputEvent().output.AsString());
  EXPECT_EQ("Deleted Breakpoint 2 pending @ main\n", console().GetOutputEvent().output.AsString());

  console().ProcessInputLine("clear $main");
  EXPECT_EQ("Deleted Breakpoint 3 pending @ $main\n", console().GetOutputEvent().output.AsString());

  console().ProcessInputLine("clear NonexistantFunction");
  EXPECT_EQ("\"NonexistantFunction\" matches zero breakpoints.",
            console().GetOutputEvent().output.AsString());
}

}  // namespace zxdb
