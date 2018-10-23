// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/console/format_settings.h"

#include <gtest/gtest.h>

#include "garnet/bin/zxdb/client/setting_schema.h"
#include "garnet/bin/zxdb/client/setting_store.h"
#include "garnet/bin/zxdb/console/output_buffer.h"

namespace zxdb {

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

  schema->AddList("setting-list", "Setting list description");
  schema->AddList("setting-list2", R"(
  Some very long description about how this setting is very important to the
  company and all its customers.)",
                  {"first", "second", "third"});

  return schema;
}

TEST(FormatSetting, Store) {
  SettingStore store(SettingStore::Level::kDefault, GetSchema(), nullptr);

  OutputBuffer out;
  Err err = FormatSettings(store, "", &out);
  EXPECT_FALSE(err.has_error()) << err.msg();

  EXPECT_EQ("Run get <option> to see detailed information.\n"
            "setting-bool    false\n"
            "setting-bool2   true\n"
            "setting-int     0\n"
            "setting-int2    12334\n"
            "setting-list    <empty>\n"
            "setting-list2   • first\n"
            "                • second\n"
            "                • third\n"
            "setting-string  <empty>\n"
            "setting-string2 Test string\n",
            out.AsString());
}

TEST(FormatSetting, NotFound) {
  SettingStore store(SettingStore::Level::kDefault, GetSchema(), nullptr);

  OutputBuffer out;
  Err err = FormatSettings(store, "invalid", &out);
  EXPECT_TRUE(err.has_error());
}


TEST(FormatSetting, Setting) {
  SettingStore store(SettingStore::Level::kDefault, GetSchema(), nullptr);

  OutputBuffer out;
  Err err = FormatSettings(store, "setting-string2", &out);
  EXPECT_FALSE(err.has_error()) << err.msg();

  EXPECT_EQ("setting-string2\n"
            "\n"
            "  Setting string description,\n"
            "  with many lines.\n"
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

  SettingStore store(SettingStore::Level::kDefault, GetSchema(), nullptr);
  Err err = store.SetList("setting-list2", std::move(options));
  EXPECT_FALSE(err.has_error()) << err.msg();

  OutputBuffer out;
  err = FormatSettings(store, "setting-list2", &out);
  EXPECT_FALSE(err.has_error()) << err.msg();

  // clang-format makes this one very hard to read.
  // Leave this text easier.
  EXPECT_EQ(
  "setting-list2\n"
  "\n"
  "  Some very long description about how this setting is very important to the\n"
  "  company and all its customers.\n"
  "\n"
  "Value(s):\n"
  "• /some/very/long/and/annoying/path/that/actually/leads/nowhere\n"
  "• /another/some/very/long/and/annoying/path/that/actually/leads/nowhere\n"
  "• /yet/another/some/very/long/and/annoying/path/that/actually/leads/nowhere\n"
  "\n"
  "See \"help set\" about using the set value for lists.\n"
  "Set value: /some/very/long/and/annoying/path/that/actually/leads/nowhere:/another/some/very/long/and/annoying/path/that/actually/leads/nowhere:/yet/another/some/very/long/and/annoying/path/that/actually/leads/nowhere\n",
  out.AsString());
}

}  // namespace zxdb
