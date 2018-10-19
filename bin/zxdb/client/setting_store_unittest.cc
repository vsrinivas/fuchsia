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
  SettingStore store(SettingStore::Level::kThread, GetSchema(), nullptr);

  auto setting = store.GetSetting("bool");
  ASSERT_TRUE(setting.value.is_bool());
  EXPECT_TRUE(setting.value.GetBool());
  EXPECT_EQ(setting.level, SettingStore::Level::kDefault);

  setting = store.GetSetting("int");
  ASSERT_TRUE(setting.value.is_int());
  EXPECT_EQ(setting.value.GetInt(), kDefaultInt);
  EXPECT_EQ(setting.level, SettingStore::Level::kDefault);

  setting = store.GetSetting("string");
  ASSERT_TRUE(setting.value.is_string());
  EXPECT_EQ(setting.value.GetString(), kDefaultString);
  EXPECT_EQ(setting.level, SettingStore::Level::kDefault);

  setting = store.GetSetting("list");
  ASSERT_TRUE(setting.value.is_list());
  EXPECT_EQ(setting.value.GetList(), DefaultList());
  EXPECT_EQ(setting.level, SettingStore::Level::kDefault);

  // Not found.
  EXPECT_TRUE(store.GetSetting("unexistent").value.is_null());
}

TEST(SettingStore, Overrides) {
  SettingStore store(SettingStore::Level::kDefault, GetSchema(), nullptr);

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
  ASSERT_TRUE(store.GetSetting("string_options").value.is_string());
  EXPECT_EQ(store.GetString("string_options"), "list");

  // Invalid option.
  err = store.SetString("string_options", "invalid");
  EXPECT_TRUE(err.has_error());
}

TEST(SettingStore, Fallback) {
  SettingStore fallback2(SettingStore::Level::kSystem, GetSchema(), nullptr);
  std::vector<std::string> new_list = {"new", "list"};
  fallback2.SetList("list", new_list);

  SettingStore fallback(SettingStore::Level::kTarget, GetSchema(), &fallback2);
  std::string new_string = "new string";
  fallback.SetString("string", new_string);

  SettingStore store(SettingStore::Level::kThread, GetSchema(), &fallback);
  store.SetBool("bool", false);

  // Also test that the correct fallback level is communicated.

  // Should get default for not overriden.
  auto setting = store.GetSetting("int");
  ASSERT_TRUE(setting.value.is_int());
  EXPECT_EQ(setting.value.GetInt(), kDefaultInt);
  EXPECT_EQ(setting.level, SettingStore::Level::kDefault);

  // Should get local level.
  setting = store.GetSetting("bool");
  ASSERT_TRUE(setting.value.is_bool());
  EXPECT_FALSE(setting.value.GetBool());
  EXPECT_EQ(setting.level, SettingStore::Level::kThread);

  // Should get one override hop.
  setting = store.GetSetting("string");
  ASSERT_TRUE(setting.value.is_string());
  EXPECT_EQ(setting.value.GetString(), new_string);
  EXPECT_EQ(setting.level, SettingStore::Level::kTarget);

  // Should fallback through the chain.
  setting = store.GetSetting("list");
  ASSERT_TRUE(setting.value.is_list());
  EXPECT_EQ(setting.value.GetList(), new_list);
  EXPECT_EQ(setting.level, SettingStore::Level::kSystem);
}

}  // namespace zxdb
