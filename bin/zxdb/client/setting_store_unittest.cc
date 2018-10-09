// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/client/setting_store.h"

#include <gtest/gtest.h>

#include "garnet/bin/zxdb/client/setting_schema.h"

namespace zxdb {

namespace {

constexpr int kDefaultInt = 10;
const char kDefaultString[] = "string default";

std::vector<std::string> DefaultList() {
  return {kDefaultString, "list"};
}

fxl::RefPtr<SettingSchema> GetSchema() {
  auto schema = fxl::MakeRefCounted<SettingSchema>();

  SettingSchemaItem item("bool", "bool option", true);
  schema->AddSetting("bool", item);

  item = SettingSchemaItem("int", "int option", kDefaultInt);
  schema->AddSetting("int", item);

  item = SettingSchemaItem("string", "string option", kDefaultString);
  schema->AddSetting("string", item);

  item = SettingSchemaItem::StringWithOptions(
      "string_options", "string with options", kDefaultString, DefaultList());
  schema->AddSetting("string_options", item);

  item = SettingSchemaItem("list", "list option", DefaultList());
  schema->AddSetting("list", item);

  return schema;
}

}  // namespace

TEST(SettingStore, Defaults) {
  SettingStore store(GetSchema(), nullptr);

  EXPECT_TRUE(store.GetBool("bool"));
  EXPECT_EQ(store.GetInt("int"), kDefaultInt);
  EXPECT_EQ(store.GetString("string"), kDefaultString);
  EXPECT_EQ(store.GetList("list"), DefaultList());


  // Not found.
  EXPECT_TRUE(store.GetSetting("unexistent").is_null());
}

TEST(SettingStore, Overrides) {
  SettingStore store(GetSchema(), nullptr);

  Err err;

  // Wrong key.
  err = store.SetInt("wrong", 10);
  EXPECT_TRUE(err.has_error());

  // Wrong type.
  err = store.SetInt("bool", false);
  EXPECT_TRUE(err.has_error());

  constexpr int kNewInt = 15;
  err = store.SetInt("int", kNewInt);
  ASSERT_FALSE(err.has_error());
  EXPECT_EQ(store.GetInt("int"), kNewInt);

  // Valid options.
  err = store.SetString("string_options", "list");
  ASSERT_FALSE(err.has_error()) << err.msg();
  ASSERT_TRUE(store.GetSetting("string_options").is_string());
  EXPECT_EQ(store.GetString("string_options"), "list");

  // Invalid option.
  err = store.SetString("string_options", "invalid");
  EXPECT_TRUE(err.has_error());
}

TEST(SettingStore, Fallback) {
  SettingStore fallback2(GetSchema(), nullptr);
  std::vector<std::string> new_list = {"new", "list"};
  fallback2.SetList("list", new_list);

  SettingStore fallback(GetSchema(), &fallback2);
  std::string new_string = "new string";
  fallback.SetString("string", new_string);

  SettingStore store(GetSchema(), &fallback);

  // Should get default for not overriden.
  ASSERT_TRUE(store.GetSetting("int").is_int());
  EXPECT_EQ(store.GetInt("int"), kDefaultInt);

  // Should get one override hop.
  ASSERT_TRUE(store.GetSetting("string").is_string());
  EXPECT_EQ(store.GetString("string"), new_string);

  // Should fallback through the chain.
  ASSERT_TRUE(store.GetSetting("list").is_list());
  EXPECT_EQ(store.GetList("list"), new_list);
}

}  // namespace zxdb
