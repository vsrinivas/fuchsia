// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/client/setting.h"
#include "gtest/gtest.h"

namespace zxdb {

TEST(Setting, Boolean) {
  Setting setting(false);
  ASSERT_TRUE(setting.valid());
  ASSERT_TRUE(setting.is_bool());
  EXPECT_FALSE(setting.GetBool());

  setting = Setting(true);
  EXPECT_TRUE(setting.GetBool());

  setting.GetBool() = false;
  EXPECT_FALSE(setting.GetBool());
}

TEST(Setting, Int) {
  Setting setting(0);
  ASSERT_TRUE(setting.valid());
  ASSERT_TRUE(setting.is_int());
  EXPECT_EQ(setting.GetInt(), 0);

  constexpr int kTestInt = 43;
  setting = Setting(kTestInt);
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

TEST(Setting, String) {
  Setting setting(std::string{});
  ASSERT_TRUE(setting.valid());
  ASSERT_TRUE(setting.is_string());
  EXPECT_TRUE(setting.GetString().empty());

  setting = Setting(kTestString);
  EXPECT_EQ(setting.GetString(), kTestString);

  setting = Setting(std::string(kTestString2));
  EXPECT_EQ(setting.GetString(), kTestString2);

  setting.GetString() = kTestString3;
  EXPECT_EQ(setting.GetString(), kTestString3);

  setting.GetString().append(kTestString3);
  EXPECT_EQ(setting.GetString(), std::string(kTestString3) + kTestString3);
}

TEST(Setting, StringList) {
  Setting setting(std::vector<std::string>{});
  ASSERT_TRUE(setting.valid());
  ASSERT_TRUE(setting.is_string_list());
  EXPECT_TRUE(setting.GetStringList().empty());

  setting = Setting(std::vector<std::string>{kTestString});
  ASSERT_EQ(setting.GetStringList().size(), 1u);

  setting.GetStringList() = {kTestString, kTestString2};
  ASSERT_EQ(setting.GetStringList().size(), 2u);

  setting.GetStringList().pop_back();
  setting.GetStringList().push_back(kTestString3);
  setting.GetStringList().push_back(kTestString2);
  ASSERT_EQ(setting.GetStringList().size(), 3u);

  EXPECT_EQ(setting.GetStringList()[1], kTestString3);

  auto it = setting.GetStringList().begin();
  EXPECT_EQ(*it++, kTestString);
  EXPECT_EQ(*it++, kTestString3);
  EXPECT_EQ(*it++, kTestString2);
  EXPECT_EQ(it, setting.GetStringList().end());
}

}  // namespace zxdb
