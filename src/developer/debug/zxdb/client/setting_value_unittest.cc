// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/client/setting_value.h"

#include "gtest/gtest.h"

namespace zxdb {

TEST(SettingValue, Boolean) {
  SettingValue setting(false);
  ASSERT_TRUE(setting.is_bool());
  EXPECT_FALSE(setting.get_bool());

  setting = SettingValue(true);
  EXPECT_TRUE(setting.get_bool());

  setting.set_bool(false);
  EXPECT_FALSE(setting.get_bool());
}

TEST(SettingValue, Int) {
  SettingValue setting(0);
  ASSERT_TRUE(setting.is_int());
  EXPECT_EQ(setting.get_int(), 0);

  constexpr int kTestInt = 43;
  setting = SettingValue(kTestInt);
  EXPECT_EQ(setting.get_int(), kTestInt);

  constexpr int kTestInt2 = 10;
  setting.set_int(kTestInt2);
  EXPECT_EQ(setting.get_int(), kTestInt2);
}

const char kTestString[] = "test_string";
const char kTestString2[] = "test_string2";
const char kTestString3[] = "test_string3";

TEST(SettingValue, String) {
  SettingValue setting(std::string{});
  ASSERT_TRUE(setting.is_string());
  EXPECT_TRUE(setting.get_string().empty());

  setting = SettingValue(kTestString);
  EXPECT_EQ(setting.get_string(), kTestString);

  setting = SettingValue(std::string(kTestString2));
  EXPECT_EQ(setting.get_string(), kTestString2);

  setting.set_string(kTestString3);
  EXPECT_EQ(setting.get_string(), kTestString3);
}

TEST(SettingValue, List) {
  SettingValue setting(std::vector<std::string>{});
  ASSERT_TRUE(setting.is_list());
  EXPECT_TRUE(setting.get_list().empty());

  setting = SettingValue(std::vector<std::string>{kTestString});
  ASSERT_EQ(setting.get_list().size(), 1u);

  setting.set_list({kTestString, kTestString2});
  ASSERT_EQ(setting.get_list().size(), 2u);

  auto it = setting.get_list().begin();
  EXPECT_EQ(*it++, kTestString);
  EXPECT_EQ(*it++, kTestString2);
  EXPECT_EQ(it, setting.get_list().end());
}

}  // namespace zxdb
