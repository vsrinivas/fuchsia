// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/client/setting_schema.h"

#include <gtest/gtest.h>

namespace zxdb {

const char kKey[] = "key";
const char kName[] = "name";
const char kDescription[] = "description";

constexpr SettingSchema::Level kLevel = SettingSchema::Level::kDefault;

TEST(SettingSchema, Bool) {
  auto schema = fxl::MakeRefCounted<SettingSchema>(kLevel);

  bool value = true;
  SettingSchemaItem item(kName, kDescription, value);

  ASSERT_TRUE(item.value().is_bool());
  EXPECT_TRUE(item.value().GetBool());

  schema->AddSetting(kKey, item);

  Err err;
  // Empty should always fail.
  err = schema->ValidateSetting(kKey, SettingValue());
  EXPECT_TRUE(err.has_error());
  // Same value should be valid.
  err = schema->ValidateSetting(kKey, item.value());
  EXPECT_FALSE(err.has_error()) << err.msg();
  // Unknown key should fail.
  err = schema->ValidateSetting("wrong", item.value());
  EXPECT_TRUE(err.has_error());
  // Should validate same value.
  err = schema->ValidateSetting(kKey, SettingValue(value));
  EXPECT_FALSE(err.has_error()) << err.msg();
}

TEST(SettingSchema, Int) {
  auto schema = fxl::MakeRefCounted<SettingSchema>(kLevel);

  int value = 10;
  SettingSchemaItem item(kName, kDescription, value);
  ASSERT_TRUE(item.value().is_int());
  EXPECT_EQ(item.value().GetInt(), value);

  schema->AddSetting(kKey, item);

  Err err;
  // Empty should always fail.
  err = schema->ValidateSetting(kKey, SettingValue());
  EXPECT_TRUE(err.has_error());
  // Same value should be valid.
  err = schema->ValidateSetting(kKey, item.value());
  EXPECT_FALSE(err.has_error()) << err.msg();
  // Unknown key should fail.
  err = schema->ValidateSetting("wrong", item.value());
  EXPECT_TRUE(err.has_error());
  // Should validate same value.
  err = schema->ValidateSetting(kKey, SettingValue(value));
  EXPECT_FALSE(err.has_error()) << err.msg();
}

TEST(SettingSchema, String) {
  auto schema = fxl::MakeRefCounted<SettingSchema>(kLevel);

  std::string value = "test";
  SettingSchemaItem item(kName, kDescription, value);
  ASSERT_TRUE(item.value().is_string());
  EXPECT_EQ(item.value().GetString(), value);

  schema->AddSetting(kKey, item);

  Err err;
  // Empty should always fail.
  err = schema->ValidateSetting(kKey, SettingValue());
  EXPECT_TRUE(err.has_error());
  // Same value should be valid.
  err = schema->ValidateSetting(kKey, item.value());
  EXPECT_FALSE(err.has_error()) << err.msg();
  // Unknown key should fail.
  err = schema->ValidateSetting("wrong", item.value());
  EXPECT_TRUE(err.has_error());
  // Should validate same value.
  err = schema->ValidateSetting(kKey, SettingValue(value));
  EXPECT_FALSE(err.has_error()) << err.msg();
}

TEST(SettingSchema, StringWithOptions) {
  auto schema = fxl::MakeRefCounted<SettingSchema>(kLevel);

  std::string value = "valid";
  std::vector<std::string> valid_values = {value, "another"};

  // Within the values should work.
  SettingSchemaItem item = SettingSchemaItem::StringWithOptions(
      kName, kDescription, value, valid_values);
  ASSERT_TRUE(item.value().is_string());
  EXPECT_EQ(item.value().GetString(), value);

  // Not within options should fail.
  item = SettingSchemaItem::StringWithOptions(kName, kDescription, "invalid",
                                              valid_values);
  EXPECT_TRUE(item.value().is_null());
}

TEST(SettingSchema, List) {
  auto schema = fxl::MakeRefCounted<SettingSchema>(kLevel);

  std::vector<std::string> value = {"test", "vector"};
  SettingSchemaItem item(kName, kDescription, value);
  ASSERT_TRUE(item.value().is_list());
  EXPECT_EQ(item.value().GetList(), value);

  schema->AddSetting(kKey, item);

  Err err;
  // Empty should always fail.
  err = schema->ValidateSetting(kKey, SettingValue());
  EXPECT_TRUE(err.has_error());
  // Same value should be valid.
  err = schema->ValidateSetting(kKey, item.value());
  EXPECT_FALSE(err.has_error()) << err.msg();
  // Unknown key should fail.
  err = schema->ValidateSetting("wrong", item.value());
  EXPECT_TRUE(err.has_error());
  // Should validate same value.
  err = schema->ValidateSetting(kKey, SettingValue(value));
  EXPECT_FALSE(err.has_error()) << err.msg();
}

}  // namespace zxdb
