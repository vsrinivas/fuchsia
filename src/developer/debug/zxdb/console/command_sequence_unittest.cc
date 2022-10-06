// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/console/command_sequence.h"

#include <gtest/gtest.h>

#include "src/developer/debug/zxdb/console/console_test.h"

namespace zxdb {

namespace {

class CommandSequence : public ConsoleTest {};

}  // namespace

TEST_F(CommandSequence, Empty) {
  bool called = false;
  RunCommandSequence(&console(), {}, [&called](const Err& err) {
    called = true;
    EXPECT_TRUE(err.ok());
  });
  EXPECT_TRUE(called);
}

TEST_F(CommandSequence, Success) {
  // These commands were picked because they don't require a connection.
  bool called = false;
  RunCommandSequence(&console(), {"break main", "get show-stdout"}, [&called](const Err& err) {
    called = true;
    // TODO(brettw) This currently reports an internal error because the command doesn't report
    // callbacks. This "ok" check should be re-enabled when we switch to the command context system.
    // EXPECT_TRUE(err.ok()) << err.msg();
  });

  loop().RunUntilNoTasks();
  EXPECT_TRUE(called);

  // This just searches for some keywords we know are in the output, so we don't depend on the
  // exact wording of the messages.
  auto event = console().GetOutputEvent();
  EXPECT_NE(std::string::npos, event.output.AsString().find("Created Breakpoint"))
      << "Got: " << event.output.AsString();
  event = console().GetOutputEvent();

  EXPECT_NE(std::string::npos, event.output.AsString().find("show-stdout"))
      << "Got: " << event.output.AsString();
}

TEST_F(CommandSequence, Error) {
  // These commands were picked because they don't require a connection.
  bool called = false;
  RunCommandSequence(&console(), {"floofbunny", "break main"}, [&called](const Err& err) {
    called = true;
    EXPECT_TRUE(err.has_error()) << err.msg();
  });

  loop().RunUntilNoTasks();

  // TODO(brettw) currently the callback gets lost in the error case. This check should be
  // uncommented when we finish switching to the ConsoleContext system where the callbacks should be
  // more consistent.
  // EXPECT_TRUE(called);

  auto event = console().GetOutputEvent();
  EXPECT_EQ("The string \"floofbunny\" is not a valid verb.", event.output.AsString())
      << "Got: " << event.output.AsString();

  // The breakpoint should not have set (search for "reak" in case the error capitalization
  // changes).
  EXPECT_FALSE(console().HasOutputEvent());
  EXPECT_EQ(std::string::npos, event.output.AsString().find("reak"))
      << "Got: " << event.output.AsString();
}

}  // namespace zxdb
