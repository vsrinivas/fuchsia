// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/syscalls.h>

#include <string>
#include <string_view>

#include <zxtest/zxtest.h>

#include "cxx14.h"

namespace {

TEST(VersionTest, ZxStringView) {
  zx_string_view_t zxsv = zx_system_get_version_string();

  EXPECT_EQ(zxsv.length, strlen(zxsv.c_str));

  EXPECT_EQ(zxsv.length, zxsv.size());
  EXPECT_EQ(zxsv.c_str, zxsv.data());
}

TEST(VersionTest, StdStringView) {
  zx_string_view_t zxsv = zx_system_get_version_string();
  std::string_view sv = zx_system_get_version_string();
  EXPECT_EQ(sv.size(), zxsv.length);
  EXPECT_EQ(sv.data(), zxsv.c_str);
  EXPECT_STR_EQ(sv.data(), zxsv.c_str);
  EXPECT_TRUE(sv == zxsv.c_str);
}

#if __cplusplus > 201703L
TEST(VersionTest, StdU8StringView) {
  zx_string_view_t zxsv = zx_system_get_version_string();
  std::u8string_view sv = zx_system_get_version_string();
  static_assert(sizeof(char8_t) == sizeof(char));
  EXPECT_EQ(sv.size(), zxsv.length);
  EXPECT_EQ(sv.data(), reinterpret_cast<const char8_t*>(zxsv.c_str));
  EXPECT_STR_EQ(reinterpret_cast<const char*>(sv.data()), zxsv.c_str);
  EXPECT_TRUE(sv == reinterpret_cast<const char8_t*>(zxsv.c_str));
}
#endif

TEST(VersionTest, StdString) {
  zx_string_view_t zxsv = zx_system_get_version_string();
  std::string s = zx_system_get_version_string();
  EXPECT_EQ(s.size(), zxsv.length);
  EXPECT_STR_EQ(s.c_str(), zxsv.c_str);
  EXPECT_TRUE(s == zxsv.c_str);
}

// zxtest is not API-compatible with C++14, so just these helper functions
// are compiled separately with -std=c++14.
TEST(VersionTest, CXX14StdString) {
  zx_string_view_t zxsv = zx_system_get_version_string();
  std::string s = AssignSystemGetVersionString();
  EXPECT_EQ(s.size(), zxsv.length);
  EXPECT_STR_EQ(s.c_str(), zxsv.c_str);
  EXPECT_TRUE(s == zxsv.c_str);

  EXPECT_TRUE(ReturnSystemGetVersionString() == s);
}

}  // namespace
