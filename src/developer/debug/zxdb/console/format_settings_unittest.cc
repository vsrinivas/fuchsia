// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/console/format_settings.h"

#include <gtest/gtest.h>

#include "src/developer/debug/zxdb/client/process.h"
#include "src/developer/debug/zxdb/client/remote_api_test.h"
#include "src/developer/debug/zxdb/client/session.h"
#include "src/developer/debug/zxdb/client/setting_schema.h"
#include "src/developer/debug/zxdb/client/setting_store.h"
#include "src/developer/debug/zxdb/console/console_context.h"
#include "src/developer/debug/zxdb/console/mock_console.h"
#include "src/developer/debug/zxdb/console/output_buffer.h"

namespace zxdb {

namespace {

// Necessary for the ExecutionScope tests which create a thread.
class FormatSettingTest : public RemoteAPITest {};

}  // namespace

TEST_F(FormatSettingTest, Setting) {
  Session session;
  ConsoleContext context(&session);

  OutputBuffer out = FormatSetting(&context, "setting-string2",
                                   "  Setting string description,\n"
                                   "  with many lines.",
                                   SettingValue("Test string"));
  EXPECT_EQ(
      "setting-string2\n"
      "\n"
      "  Setting string description,\n"
      "  with many lines.\n"
      "\n"
      "Type: string\n"
      "\n"
      "Value(s):\n"
      "\"Test string\"\n",
      out.AsString());
}

TEST_F(FormatSettingTest, ExecutionScope) {
  MockConsole console(&session());
  ConsoleContext context(&session());

  constexpr int kProcessKoid = 1234;
  Process* process = InjectProcess(kProcessKoid);
  Thread* thread = InjectThread(kProcessKoid, 5678);

  std::string name = "setting-scope";
  std::string description = "Scope description";

  // Global scope.
  OutputBuffer out = FormatSetting(&context, name, description, SettingValue(ExecutionScope()));
  EXPECT_EQ(
      "setting-scope\n"
      "\n"
      "Scope description\n"
      "\n"
      "Type: scope\n"
      "\n"
      "Value(s):\n"
      "Global\n",
      out.AsString());

  // Target scope.
  out = FormatSetting(&context, name, description,
                      SettingValue(ExecutionScope(process->GetTarget())));
  EXPECT_EQ(
      "setting-scope\n"
      "\n"
      "Scope description\n"
      "\n"
      "Type: scope\n"
      "\n"
      "Value(s):\n"
      "pr 1\n",
      out.AsString());

  // Thread scope.
  out = FormatSetting(&context, name, description, SettingValue(ExecutionScope(thread)));
  EXPECT_EQ(
      "setting-scope\n"
      "\n"
      "Scope description\n"
      "\n"
      "Type: scope\n"
      "\n"
      "Value(s):\n"
      "pr 1 t 1\n",
      out.AsString());
}

TEST_F(FormatSettingTest, InputLocations) {
  MockConsole console(&session());
  ConsoleContext context(&session());

  std::string name = "setting-inputloc";
  std::string description = "Input location description";

  std::vector<InputLocation> inputlocs;

  OutputBuffer out = FormatSetting(&context, name, description, SettingValue(inputlocs));
  EXPECT_EQ(
      "setting-inputloc\n"
      "\n"
      "Input location description\n"
      "\n"
      "Type: locations\n"
      "\n"
      "Value(s):\n"
      "<no location>\n",
      out.AsString());

  // Test with some values. The InputLocation formatter has its own tests for the edge cases.
  inputlocs.emplace_back(Identifier("SomeFunction"));
  inputlocs.emplace_back(FileLine("file.cc", 23));
  out = FormatSetting(&context, name, description, SettingValue(inputlocs));
  EXPECT_EQ(
      "setting-inputloc\n"
      "\n"
      "Input location description\n"
      "\n"
      "Type: locations\n"
      "\n"
      "Value(s):\n"
      "SomeFunction, file.cc:23\n",
      out.AsString());
}

TEST_F(FormatSettingTest, List) {
  Session session;
  ConsoleContext context(&session);

  std::vector<std::string> options = {
      "/some/very/long/and/annoying/path/that/actually/leads/nowhere",
      "/another/some/very/long/and/annoying/path/that/actually/leads/nowhere",
      "this path/needs\tquoting"};

  std::string name = "setting-list2";
  std::string description =
      "  Some very long description about how this setting is very important to the\n"
      "  company and all its customers.";

  OutputBuffer out = FormatSetting(&context, name, description, SettingValue(options));
  EXPECT_EQ(
      "setting-list2\n"
      "\n"
      "  Some very long description about how this setting is very important to the\n"
      "  company and all its customers.\n"
      "\n"
      "Type: list\n"
      "\n"
      "Value(s):\n"
      "• /some/very/long/and/annoying/path/that/actually/leads/nowhere\n"
      "• /another/some/very/long/and/annoying/path/that/actually/leads/nowhere\n"
      "• \"this path/needs\\tquoting\"\n"
      "\n"
      "See \"help set\" about using the set value for lists.\n"
      "To set, type: set setting-list2 "
      "/some/very/long/and/annoying/path/that/actually/leads/nowhere "
      "/another/some/very/long/and/annoying/path/that/actually/leads/nowhere "
      "\"this path/needs\\tquoting\"\n",
      out.AsString());
}

}  // namespace zxdb
