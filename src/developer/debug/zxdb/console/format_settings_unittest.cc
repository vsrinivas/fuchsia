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

class FormatSettingTest : public RemoteAPITest {};

}  // namespace

fxl::RefPtr<SettingSchema> GetSchema() {
  auto schema = fxl::MakeRefCounted<SettingSchema>();

  schema->AddBool("setting-bool", "Setting bool description");
  schema->AddBool("setting-bool2", "Setting bool description", true);

  schema->AddInt("setting-int", "Setting int description");
  schema->AddInt("setting-int2", "Setting int description", 12334);

  schema->AddString("setting-string", "Setting string description");
  schema->AddString("setting-string2", R"(
  Setting string description,
  with many lines.)",
                    "Test string");

  schema->AddExecutionScope("setting-scope", "Scope description");
  schema->AddInputLocations("setting-inputloc", "Input location description");

  schema->AddList("setting-list", "Setting list description");
  schema->AddList("setting-list2", R"(
  Some very long description about how this setting is very important to the
  company and all its customers.)",
                  {"first", "second", "third"});

  return schema;
}

TEST_F(FormatSettingTest, Setting) {
  Session session;
  ConsoleContext context(&session);

  SettingStore store(GetSchema(), nullptr);

  Setting setting = store.GetSetting("setting-string2");
  OutputBuffer out = FormatSetting(&context, setting);
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

  SettingStore store(GetSchema(), nullptr);

  // Global scope.
  Err err = store.SetExecutionScope("setting-scope", ExecutionScope());
  EXPECT_FALSE(err.has_error()) << err.msg();

  Setting setting = store.GetSetting("setting-scope");
  OutputBuffer out = FormatSetting(&context, setting);
  EXPECT_EQ(
      "setting-scope\n"
      "Scope description\n"
      "\n"
      "Type: scope\n"
      "\n"
      "Value(s):\n"
      "Global\n",
      out.AsString());

  // Target scope.
  setting.value = SettingValue(ExecutionScope(process->GetTarget()));
  out = FormatSetting(&context, setting);
  EXPECT_EQ(
      "setting-scope\n"
      "Scope description\n"
      "\n"
      "Type: scope\n"
      "\n"
      "Value(s):\n"
      "pr 1\n",
      out.AsString());

  // Thread scope.
  setting.value = SettingValue(ExecutionScope(thread));
  out = FormatSetting(&context, setting);
  EXPECT_EQ(
      "setting-scope\n"
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

  SettingStore store(GetSchema(), nullptr);

  // Empty.
  Err err = store.SetInputLocations("setting-inputloc", {});
  EXPECT_FALSE(err.has_error()) << err.msg();

  Setting setting = store.GetSetting("setting-inputloc");
  OutputBuffer out = FormatSetting(&context, setting);
  EXPECT_EQ(
      "setting-inputloc\n"
      "Input location description\n"
      "\n"
      "Type: locations\n"
      "\n"
      "Value(s):\n"
      "<no location>\n",
      out.AsString());

  // Test with some values. The InputLocation formatter has its own tests for the edge cases.
  setting.value = SettingValue(
      {InputLocation(Identifier("SomeFunction")), InputLocation(FileLine("file.cc", 23))});
  out = FormatSetting(&context, setting);
  EXPECT_EQ(
      "setting-inputloc\n"
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

  SettingStore store(GetSchema(), nullptr);
  Err err = store.SetList("setting-list2", std::move(options));
  EXPECT_FALSE(err.has_error()) << err.msg();

  Setting setting = store.GetSetting("setting-list2");

  // clang-format makes this one very hard to read.
  // Leave this text easier.
  OutputBuffer out = FormatSetting(&context, setting);
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
