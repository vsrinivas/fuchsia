// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/client/setting_schema.h"

#include <gtest/gtest.h>

namespace zxdb {

const char kKey[] = "key";
const char kName[] = "name";
const char kDescription[] = "description";

TEST(SettingSchema, Bool) {
  auto schema = fxl::MakeRefCounted<SettingSchema>();

  bool value = true;
  Setting setting{{kName, kDescription}, SettingValue(value)};

  ASSERT_TRUE(setting.value.is_bool());
  EXPECT_TRUE(setting.value.get_bool());

  schema->AddSetting(kKey, setting);

  Err err;
  // Empty should always fail.
  err = schema->ValidateSetting(kKey, SettingValue());
  EXPECT_TRUE(err.has_error());
  // Same value should be valid.
  err = schema->ValidateSetting(kKey, setting.value);
  EXPECT_FALSE(err.has_error()) << err.msg();
  // Unknown key should fail.
  err = schema->ValidateSetting("wrong", setting.value);
  EXPECT_TRUE(err.has_error());
  // Should validate same value.
  err = schema->ValidateSetting(kKey, SettingValue(value));
  EXPECT_FALSE(err.has_error()) << err.msg();
}

TEST(SettingSchema, Int) {
  auto schema = fxl::MakeRefCounted<SettingSchema>();

  int value = 10;
  Setting setting{{kName, kDescription}, SettingValue(value)};
  ASSERT_TRUE(setting.value.is_int());
  EXPECT_EQ(setting.value.get_int(), value);

  schema->AddSetting(kKey, setting);

  Err err;
  // Empty should always fail.
  err = schema->ValidateSetting(kKey, SettingValue());
  EXPECT_TRUE(err.has_error());
  // Same value should be valid.
  err = schema->ValidateSetting(kKey, setting.value);
  EXPECT_FALSE(err.has_error()) << err.msg();
  // Unknown key should fail.
  err = schema->ValidateSetting("wrong", setting.value);
  EXPECT_TRUE(err.has_error());
  // Should validate same value.
  err = schema->ValidateSetting(kKey, SettingValue(value));
  EXPECT_FALSE(err.has_error()) << err.msg();
}

TEST(SettingSchema, String) {
  auto schema = fxl::MakeRefCounted<SettingSchema>();

  std::string value = "test";
  Setting setting{{kName, kDescription}, SettingValue(value)};
  ASSERT_TRUE(setting.value.is_string());
  EXPECT_EQ(setting.value.get_string(), value);

  schema->AddSetting(kKey, setting);

  Err err;
  // Empty should always fail.
  err = schema->ValidateSetting(kKey, SettingValue());
  EXPECT_TRUE(err.has_error());
  // Same value should be valid.
  err = schema->ValidateSetting(kKey, setting.value);
  EXPECT_FALSE(err.has_error()) << err.msg();
  // Unknown key should fail.
  err = schema->ValidateSetting("wrong", setting.value);
  EXPECT_TRUE(err.has_error());
  // Should validate same value.
  err = schema->ValidateSetting(kKey, SettingValue(value));
  EXPECT_FALSE(err.has_error()) << err.msg();
}

TEST(SettingSchema, List) {
  auto schema = fxl::MakeRefCounted<SettingSchema>();

  std::vector<std::string> value = {"test", "vector"};
  Setting setting{{kName, kDescription}, SettingValue(value)};
  ASSERT_TRUE(setting.value.is_list());
  EXPECT_EQ(setting.value.get_list(), value);

  schema->AddSetting(kKey, setting);

  Err err;
  // Empty should always fail.
  err = schema->ValidateSetting(kKey, SettingValue());
  EXPECT_TRUE(err.has_error());
  // Same value should be valid.
  err = schema->ValidateSetting(kKey, setting.value);
  EXPECT_FALSE(err.has_error()) << err.msg();
  // Unknown key should fail.
  err = schema->ValidateSetting("wrong", setting.value);
  EXPECT_TRUE(err.has_error());
  // Should validate same value.
  err = schema->ValidateSetting(kKey, SettingValue(value));
  EXPECT_FALSE(err.has_error()) << err.msg();
}

}  // namespace zxdb
