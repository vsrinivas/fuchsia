// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/client/setting_schema.h"

#include <gtest/gtest.h>

namespace zxdb {

const char kName[] = "name";
const char kDescription[] = "description";

TEST(SettingSchema, Bool) {
  auto schema = fxl::MakeRefCounted<SettingSchema>();

  bool value = true;
  schema->AddBool(kName, kDescription, value);

  Err err;
  // Empty should always fail.
  err = schema->ValidateSetting(kName, SettingValue());
  EXPECT_TRUE(err.has_error());
  // Same value should be valid.
  err = schema->ValidateSetting(kName, SettingValue(value));
  EXPECT_FALSE(err.has_error()) << err.msg();
  // Unknown key should fail.
  err = schema->ValidateSetting("wrong", SettingValue(value));
  EXPECT_TRUE(err.has_error());
  // Should validate same value.
  err = schema->ValidateSetting(kName, SettingValue(value));
  EXPECT_FALSE(err.has_error()) << err.msg();
}

TEST(SettingSchema, Int) {
  auto schema = fxl::MakeRefCounted<SettingSchema>();

  int value = 10;
  schema->AddSetting(kName, kDescription, SettingValue(value));

  Err err;
  // Empty should always fail.
  err = schema->ValidateSetting(kName, SettingValue());
  EXPECT_TRUE(err.has_error());
  // Same value should be valid.
  err = schema->ValidateSetting(kName, SettingValue(value));
  EXPECT_FALSE(err.has_error()) << err.msg();
  // Unknown key should fail.
  err = schema->ValidateSetting("wrong", SettingValue(value));
  EXPECT_TRUE(err.has_error());
  // Should validate same value.
  err = schema->ValidateSetting(kName, SettingValue(value));
  EXPECT_FALSE(err.has_error()) << err.msg();
}

TEST(SettingSchema, String) {
  auto schema = fxl::MakeRefCounted<SettingSchema>();

  std::string value = "test";
  schema->AddSetting(kName, kDescription, SettingValue(value));

  Err err;
  // Empty should always fail.
  err = schema->ValidateSetting(kName, SettingValue());
  EXPECT_TRUE(err.has_error());
  // Same value should be valid.
  err = schema->ValidateSetting(kName, SettingValue(value));
  EXPECT_FALSE(err.has_error()) << err.msg();
  // Unknown key should fail.
  err = schema->ValidateSetting("wrong", SettingValue(value));
  EXPECT_TRUE(err.has_error());
  // Should validate same value.
  err = schema->ValidateSetting(kName, SettingValue(value));
  EXPECT_FALSE(err.has_error()) << err.msg();
}

TEST(SettingSchema, StringWithOptions) {
  auto schema = fxl::MakeRefCounted<SettingSchema>();
  const char kOne[] = "one";
  const char kTwo[] = "Two";
  const char kThree[] = "THREE";
  schema->AddString(kName, kDescription, "", {kOne, kTwo, kThree});

  EXPECT_TRUE(schema->ValidateSetting(kName, SettingValue(kOne)).ok());
  EXPECT_FALSE(schema->ValidateSetting(kName, SettingValue("ONE")).ok());
  EXPECT_TRUE(schema->ValidateSetting(kName, SettingValue(kTwo)).ok());
  EXPECT_TRUE(schema->ValidateSetting(kName, SettingValue(kThree)).ok());
  EXPECT_FALSE(schema->ValidateSetting(kName, SettingValue("random")).ok());
}

TEST(SettingSchema, List) {
  auto schema = fxl::MakeRefCounted<SettingSchema>();

  std::vector<std::string> value = {"test", "vector"};
  schema->AddList(kName, kDescription, value);

  Err err;
  // Empty should always fail.
  err = schema->ValidateSetting(kName, SettingValue());
  EXPECT_TRUE(err.has_error());
  // Same value should be valid.
  err = schema->ValidateSetting(kName, SettingValue(value));
  EXPECT_FALSE(err.has_error()) << err.msg();
  // Unknown key should fail.
  err = schema->ValidateSetting("wrong", SettingValue(value));
  EXPECT_TRUE(err.has_error());
  // Should validate same value.
  err = schema->ValidateSetting(kName, SettingValue(value));
  EXPECT_FALSE(err.has_error()) << err.msg();
}

TEST(SettingSchema, ListWithOptions) {
  auto schema = fxl::MakeRefCounted<SettingSchema>();
  std::vector<std::string> value = {"test", "vector"};
  std::vector<std::string> options = {"test", "vector", "another"};

  ASSERT_TRUE(schema->AddList("valid", "description", value, options));
  {
    const SettingSchema::Record* record = schema->GetSetting("valid");
    ASSERT_TRUE(record);
    ASSERT_TRUE(record->default_value.is_list());
    ASSERT_EQ(record->options.size(), 3u);
    EXPECT_EQ(record->options[0], options[0]);
    EXPECT_EQ(record->options[1], options[1]);
    EXPECT_EQ(record->options[2], options[2]);
  }

  ASSERT_FALSE(schema->AddList("invalid", "description", {"non"}, options));
  {
    const SettingSchema::Record* record = schema->GetSetting("invalid");
    EXPECT_FALSE(record);
  }

  // Check validation.
  EXPECT_TRUE(
      schema->ValidateSetting("valid", SettingValue(std::vector<std::string>{"vector"})).ok());
  EXPECT_FALSE(
      schema->ValidateSetting("valid", SettingValue(std::vector<std::string>{"Vector"})).ok());
}

}  // namespace zxdb
