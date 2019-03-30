// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/client/setting_value.h"

#include "gtest/gtest.h"

namespace zxdb {

TEST(SettingValue, Boolean) {
  SettingValue setting(false);
  ASSERT_TRUE(setting.valid());
  ASSERT_TRUE(setting.is_bool());
  EXPECT_FALSE(setting.GetBool());

  setting = SettingValue(true);
  EXPECT_TRUE(setting.GetBool());

  setting.GetBool() = false;
  EXPECT_FALSE(setting.GetBool());
}

TEST(SettingValue, Int) {
  SettingValue setting(0);
  ASSERT_TRUE(setting.valid());
  ASSERT_TRUE(setting.is_int());
  EXPECT_EQ(setting.GetInt(), 0);

  constexpr int kTestInt = 43;
  setting = SettingValue(kTestInt);
  EXPECT_EQ(setting.GetInt(), kTestInt);

  constexpr int kTestInt2 = 10;
  setting.GetInt() = kTestInt2;
  EXPECT_EQ(setting.GetInt(), kTestInt2);

  setting.GetInt()++;
  setting.GetInt() += 2;
  setting.GetInt() *= 2;
  EXPECT_EQ(setting.GetInt(), (kTestInt2 + 3) * 2);
}

const char kTestString[] = "test_string";
const char kTestString2[] = "test_string2";
const char kTestString3[] = "test_string3";

TEST(SettingValue, String) {
  SettingValue setting(std::string{});
  ASSERT_TRUE(setting.valid());
  ASSERT_TRUE(setting.is_string());
  EXPECT_TRUE(setting.GetString().empty());

  setting = SettingValue(kTestString);
  EXPECT_EQ(setting.GetString(), kTestString);

  setting = SettingValue(std::string(kTestString2));
  EXPECT_EQ(setting.GetString(), kTestString2);

  setting.GetString() = kTestString3;
  EXPECT_EQ(setting.GetString(), kTestString3);

  setting.GetString().append(kTestString3);
  EXPECT_EQ(setting.GetString(), std::string(kTestString3) + kTestString3);
}

TEST(SettingValue, List) {
  SettingValue setting(std::vector<std::string>{});
  ASSERT_TRUE(setting.valid());
  ASSERT_TRUE(setting.is_list());
  EXPECT_TRUE(setting.GetList().empty());

  setting = SettingValue(std::vector<std::string>{kTestString});
  ASSERT_EQ(setting.GetList().size(), 1u);

  setting.GetList() = {kTestString, kTestString2};
  ASSERT_EQ(setting.GetList().size(), 2u);

  setting.GetList().pop_back();
  setting.GetList().push_back(kTestString3);
  setting.GetList().push_back(kTestString2);
  ASSERT_EQ(setting.GetList().size(), 3u);

  EXPECT_EQ(setting.GetList()[1], kTestString3);

  auto it = setting.GetList().begin();
  EXPECT_EQ(*it++, kTestString);
  EXPECT_EQ(*it++, kTestString3);
  EXPECT_EQ(*it++, kTestString2);
  EXPECT_EQ(it, setting.GetList().end());
}

}  // namespace zxdb
