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

  schema->AddBool("thread-bool", "Thread bool description");
  schema->AddBool("thread-bool2", "Thread bool description", true);

  schema->AddInt("thread-int", "Thread int description");
  schema->AddInt("thread-int2", "Thread int description", 12334);

  schema->AddString("thread-string", "Thread string description");
  schema->AddString("thread-string2", "Thread string description",
                    "Test string");

  schema->AddList("thread-list", "Thread list description");
  schema->AddList("thread-list2", "Thread list description",
                  {"first", "second", "third"});

  return schema;
}

TEST(FormatSetting, Schema) {
  SettingStore store(SettingStore::Level::kDefault, GetSchema(), nullptr);

  OutputBuffer out;
  Err err = FormatSettings(store, "", &out);
  EXPECT_FALSE(err.has_error()) << err.msg();

  EXPECT_EQ("thread-bool    false\n"
            "thread-bool2   true\n"
            "thread-int     0\n"
            "thread-int2    12334\n"
            "thread-list    <empty>\n"
            "thread-list2   • first\n"
            "               • second\n"
            "               • third\n"
            "thread-string  <empty>\n"
            "thread-string2 Test string\n",
            out.AsString());
}

// TODO(donosoc): Activate these tests when the individual format settings is
//                implemented.
#if 0

const char kName[] = "setting-name";
const char kLongDescriptionText[] = R"(This is a title.

This is a long description text.
This one spans many lines.)";

TEST(FormatSetting, SchemaItem) {
  auto item = SettingSchemaItem(kName, kLongDescriptionText, true);

  EXPECT_EQ("setting-name\n"
            "\n"
            "value(s): true\n"
            "\n"
            "This is a title.\n"
            "\n"
            "This is a long description text.\n"
            "This one spans many lines.",
            FormatSchemaItem(std::move(item)).AsString());
}

TEST(FormatSetting, SchemaItemList) {
  std::vector<std::string> options = {
      "/some/very/long/and/annoying/path/that/actually/leads/nowhere",
      "/another/some/very/long/and/annoying/path/that/actually/leads/nowhere",
      "/yet/another/some/very/long/and/annoying/path/that/actually/leads/"
      "nowhere"};

  auto item =
      SettingSchemaItem(kName, kLongDescriptionText, std::move(options));

  // clang-format makes this one very hard to read.
  // Leave this text easier.
  EXPECT_EQ(
  "setting-name\n"
  "\n"
  "value(s): • /some/very/long/and/annoying/path/that/actually/leads/nowhere\n"
  "          • /another/some/very/long/and/annoying/path/that/actually/leads/nowhere\n"
  "          • /yet/another/some/very/long/and/annoying/path/that/actually/leads/nowhere\n"
  "\n"
  "See \"help set\" for information on the setter text.\n"
  "setter: /some/very/long/and/annoying/path/that/actually/leads/nowhere:/another/some/very/long/and/annoying/path/that/actually/leads/nowhere:/yet/another/some/very/long/and/annoying/path/that/actually/leads/nowhere\n"
  "\n"
  "This is a title.\n"
  "\n"
  "This is a long description text.\n"
  "This one spans many lines.",
  FormatSchemaItem(std::move(item)).AsString());
}

#endif

}  // namespace zxdb
