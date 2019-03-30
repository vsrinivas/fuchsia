// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/console/format_settings.h"

#include <gtest/gtest.h>

#include "garnet/bin/zxdb/console/output_buffer.h"
#include "src/developer/debug/zxdb/client/setting_schema.h"
#include "src/developer/debug/zxdb/client/setting_store.h"

namespace zxdb {

fxl::RefPtr<SettingSchema> GetSchema(
    SettingSchema::Level level = SettingSchema::Level::kDefault) {
  auto schema = fxl::MakeRefCounted<SettingSchema>(level);

  schema->AddBool("setting-bool", "Setting bool description");
  schema->AddBool("setting-bool2", "Setting bool description", true);

  schema->AddInt("setting-int", "Setting int description");
  schema->AddInt("setting-int2", "Setting int description", 12334);

  schema->AddString("setting-string", "Setting string description");
  schema->AddString("setting-string2", R"(
  Setting string description,
  with many lines.)",
                    "Test string");

  schema->AddList("setting-list", "Setting list description");
  schema->AddList("setting-list2", R"(
  Some very long description about how this setting is very important to the
  company and all its customers.)",
                  {"first", "second", "third"});

  return schema;
}

TEST(FormatSetting, NotFound) {
  SettingStore store(GetSchema(), nullptr);

  OutputBuffer out;
  Err err = FormatSetting(store, "invalid", &out);
  EXPECT_TRUE(err.has_error());
}

TEST(FormatSetting, Setting) {
  SettingStore store(GetSchema(), nullptr);

  OutputBuffer out;
  Err err = FormatSetting(store, "setting-string2", &out);
  EXPECT_FALSE(err.has_error()) << err.msg();

  EXPECT_EQ(
      "setting-string2\n"
      "\n"
      "  Setting string description,\n"
      "  with many lines.\n"
      "\n"
      "Type: string\n"
      "\n"
      "Value(s):\n"
      "Test string\n",
      out.AsString());
}

TEST(FormatSetting, SchemaItemList) {
  std::vector<std::string> options = {
      "/some/very/long/and/annoying/path/that/actually/leads/nowhere",
      "/another/some/very/long/and/annoying/path/that/actually/leads/nowhere",
      "/yet/another/some/very/long/and/annoying/path/that/actually/leads/"
      "nowhere"};

  SettingStore store(GetSchema(), nullptr);
  Err err = store.SetList("setting-list2", std::move(options));
  EXPECT_FALSE(err.has_error()) << err.msg();

  OutputBuffer out;
  err = FormatSetting(store, "setting-list2", &out);
  EXPECT_FALSE(err.has_error()) << err.msg();

  // clang-format makes this one very hard to read.
  // Leave this text easier.
  EXPECT_EQ(
      "setting-list2\n"
      "\n"
      "  Some very long description about how this setting is very important "
      "to the\n"
      "  company and all its customers.\n"
      "\n"
      "Type: list\n"
      "\n"
      "Value(s):\n"
      "• /some/very/long/and/annoying/path/that/actually/leads/nowhere\n"
      "• "
      "/another/some/very/long/and/annoying/path/that/actually/leads/nowhere\n"
      "• "
      "/yet/another/some/very/long/and/annoying/path/that/actually/leads/"
      "nowhere\n"
      "\n"
      "See \"help set\" about using the set value for lists.\n"
      "Set value: "
      "/some/very/long/and/annoying/path/that/actually/leads/nowhere:/another/"
      "some/very/long/and/annoying/path/that/actually/leads/nowhere:/yet/"
      "another/some/very/long/and/annoying/path/that/actually/leads/nowhere\n",
      out.AsString());
}

}  // namespace zxdb
