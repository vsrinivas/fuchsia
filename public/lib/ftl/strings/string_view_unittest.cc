// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FTL_STRINGS_STRING_VIEW_UNITTEST_H_
#define LIB_FTL_STRINGS_STRING_VIEW_UNITTEST_H_

#include "lib/ftl/strings/string_view.h"
#include "gtest/gtest.h"

namespace ftl {
namespace {

#define TEST_STRING "Hello\0u"
#define TEST_STRING_LENGTH 5u

TEST(StringView, Constructors) {
  std::string str1("Hello");
  StringView sw1(str1);
  EXPECT_EQ(str1.data(), sw1.data());
  EXPECT_EQ(str1.size(), sw1.size());
  EXPECT_EQ(TEST_STRING_LENGTH, sw1.size());

  const char* str2 = str1.data();
  StringView sw2(str2);
  EXPECT_EQ(str1.data(), sw2.data());
  EXPECT_EQ(TEST_STRING_LENGTH, sw2.size());
}

TEST(StringView, ConstExprConstructors) {
  constexpr StringView sw1;
  EXPECT_EQ(0u, sw1.size());

  constexpr StringView sw2(sw1);
  EXPECT_EQ(0u, sw2.size());
  EXPECT_EQ(sw1.data(), sw2.data());

  constexpr StringView sw3(TEST_STRING, TEST_STRING_LENGTH);
  EXPECT_EQ(TEST_STRING_LENGTH, sw3.size());

  constexpr StringView sw4(TEST_STRING);
  EXPECT_EQ(TEST_STRING_LENGTH, sw4.size());

  constexpr const char* string_ptr = TEST_STRING;
  constexpr StringView sw5(string_ptr);
  EXPECT_EQ(TEST_STRING_LENGTH, sw4.size());
}

TEST(StringView, CopyOperator) {
  StringView sw1;

  StringView sw2(TEST_STRING);
  sw1 = sw2;
  EXPECT_EQ(sw2.data(), sw1.data());

  sw1 = TEST_STRING;
  EXPECT_EQ(TEST_STRING_LENGTH, sw1.size());

  sw1 = std::string(TEST_STRING);
  EXPECT_EQ(TEST_STRING_LENGTH, sw1.size());
}

TEST(StringView, CapacityMethods) {
  StringView sw1;
  EXPECT_EQ(0u, sw1.size());
  EXPECT_TRUE(sw1.empty());

  StringView sw2(TEST_STRING);
  EXPECT_EQ(TEST_STRING_LENGTH, sw2.size());
  EXPECT_FALSE(sw2.empty());
}

TEST(StringView, AccessMethods) {
  const char* str = TEST_STRING;
  StringView sw1(str);

  EXPECT_EQ('H', sw1.front());
  EXPECT_EQ('e', sw1[1]);
  EXPECT_EQ('l', sw1.at(2));
  EXPECT_EQ('o', sw1.back());
  EXPECT_EQ(str, sw1.data());
}

TEST(StringView, Iterators) {
  StringView sw1(TEST_STRING);

  std::string str1(sw1.begin(), sw1.end());
  EXPECT_EQ(TEST_STRING, str1);

  std::string str2(sw1.cbegin(), sw1.cend());
  EXPECT_EQ(TEST_STRING, str2);

  std::string str3(sw1.rbegin(), sw1.rend());
  EXPECT_EQ("olleH", str3);

  std::string str4(sw1.crbegin(), sw1.crend());
  EXPECT_EQ("olleH", str4);
}

TEST(StringView, Modifiers) {
  StringView sw1(TEST_STRING);

  sw1.remove_prefix(1);
  EXPECT_EQ("ello", sw1.ToString());

  sw1.remove_suffix(1);
  EXPECT_EQ("ell", sw1.ToString());

  sw1.clear();
  EXPECT_EQ(0u, sw1.size());

  StringView sw2(TEST_STRING);
  sw1.swap(sw2);
  EXPECT_EQ(0u, sw2.size());
  EXPECT_EQ(TEST_STRING, sw1.ToString());
}

TEST(StringView, SubString) {
  StringView sw1(TEST_STRING);

  StringView sw2 = sw1.substr(1, 2);
  EXPECT_EQ("el", sw2.ToString());
}

TEST(StringView, Compare) {
  StringView sw1(TEST_STRING);
  StringView sw2(TEST_STRING);

  EXPECT_EQ(0, sw1.compare(sw2));

  sw1 = "a";
  sw2 = "b";
  EXPECT_EQ(-1, sw1.compare(sw2));
  EXPECT_EQ(1, sw2.compare(sw1));

  sw1 = "a";
  sw2 = "aa";
  EXPECT_EQ(-1, sw1.compare(sw2));
  EXPECT_EQ(1, sw2.compare(sw1));
}

TEST(StringView, ComparaisonFunctions) {
  StringView sw1 = "a";
  StringView sw2 = "b";

  EXPECT_TRUE(sw1 == sw1);
  EXPECT_FALSE(sw1 == sw2);
  EXPECT_FALSE(sw1 != sw1);
  EXPECT_TRUE(sw1 != sw2);

  EXPECT_TRUE(sw1 < sw2);
  EXPECT_FALSE(sw2 < sw1);
  EXPECT_TRUE(sw1 <= sw1);
  EXPECT_TRUE(sw1 <= sw2);
  EXPECT_FALSE(sw2 <= sw1);

  EXPECT_TRUE(sw2 > sw1);
  EXPECT_FALSE(sw1 > sw2);
  EXPECT_TRUE(sw1 >= sw1);
  EXPECT_TRUE(sw2 >= sw1);
  EXPECT_FALSE(sw1 >= sw2);
}

TEST(StringView, Stream) {
  StringView sw1(TEST_STRING);

  std::stringstream ss;
  ss << sw1;
  EXPECT_EQ(TEST_STRING, ss.str());
}

}  // namespace
}  // namespace ftl

#endif  // LIB_FTL_STRINGS_STRING_VIEW_UNITTEST_H_
