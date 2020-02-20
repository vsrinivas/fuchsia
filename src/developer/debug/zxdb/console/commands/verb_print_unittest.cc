// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/console/commands/verb_print.h"

#include "gtest/gtest.h"
#include "src/developer/debug/zxdb/client/mock_frame.h"
#include "src/developer/debug/zxdb/client/mock_remote_api.h"
#include "src/developer/debug/zxdb/console/console_test.h"
#include "src/developer/debug/zxdb/console/mock_console.h"
#include "src/developer/debug/zxdb/symbols/mock_symbol_data_provider.h"

namespace zxdb {

namespace {

class VerbPrint : public ConsoleTest {};

}  // namespace

TEST_F(VerbPrint, LanguagePreference) {
  console().ProcessInputLine("set language c++");
  console().GetOutputEvent();  // Eat output from the set.
  console().ProcessInputLine("print 3 as u64");
  auto event = console().GetOutputEvent();
  EXPECT_EQ("Unexpected input, did you forget an operator?\n  3 as u64\n    ^",
            event.output.AsString());

  console().ProcessInputLine("set language rust");
  console().GetOutputEvent();  // Eat output from the set.
  console().ProcessInputLine("print 3 as u64");
  event = console().GetOutputEvent();
  EXPECT_EQ("3", event.output.AsString());

  console().ProcessInputLine("set language auto");
  console().GetOutputEvent();  // Eat output from the set.
  console().ProcessInputLine("print 3 as u64");
  event = console().GetOutputEvent();
  EXPECT_EQ("Unexpected input, did you forget an operator?\n  3 as u64\n    ^",
            event.output.AsString());
}

TEST_F(VerbPrint, TypeOverrides) {
  // Decimal (the default).
  console().ProcessInputLine("print 100");
  auto event = console().GetOutputEvent();
  EXPECT_EQ("100", event.output.AsString());

  console().ProcessInputLine("print -d 100");
  event = console().GetOutputEvent();
  EXPECT_EQ("100", event.output.AsString());

  // Hex.
  console().ProcessInputLine("print -x 100");
  event = console().GetOutputEvent();
  EXPECT_EQ("0x64", event.output.AsString());

  // Unsigned (currently we treat "-1" as a 32-bit integer so the unsigned version is also 32 bits).
  // The "--" is required to mark the end of switches so the "-1" is treated as the expression
  // rather than another switch).
  console().ProcessInputLine("print -u -- -1");
  event = console().GetOutputEvent();
  EXPECT_EQ("4294967295", event.output.AsString());

  // Character.
  console().ProcessInputLine("print -c 100");
  event = console().GetOutputEvent();
  EXPECT_EQ("'d'", event.output.AsString());

  // More than one is an error.
  console().ProcessInputLine("print -d -c 100");
  event = console().GetOutputEvent();
  EXPECT_EQ("More than one type override (-c, -d, -u, -x) specified.", event.output.AsString());
}

}  // namespace zxdb
