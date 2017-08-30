// Copyright 2014 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <sstream>

#include "gtest/gtest.h"
#include "lib/fidl/cpp/bindings/string.h"

namespace fidl {
namespace test {

TEST(StringTest, DefaultIsNull) {
  String s;
  EXPECT_TRUE(s.is_null());
}

TEST(StringTest, ConstructedWithNull) {
  String s(nullptr);
  EXPECT_TRUE(s.is_null());
}

TEST(StringTest, ConstructedWithNullCharPointer) {
  const char* null = nullptr;
  String s(null);
  EXPECT_TRUE(s.is_null());
}

TEST(StringTest, AssignedNull) {
  String s("");
  EXPECT_FALSE(s.is_null());
  s = nullptr;
  EXPECT_TRUE(s.is_null());
}

TEST(StringTest, AssignedNullCharPointer) {
  String s("");
  EXPECT_FALSE(s.is_null());
  const char* null = nullptr;
  s = null;
  EXPECT_TRUE(s.is_null());
}

TEST(StringTest, Empty) {
  String s("");
  EXPECT_FALSE(s.is_null());
  EXPECT_TRUE(s.get().empty());
}

TEST(StringTest, Basic) {
  String s("hello world");
  EXPECT_EQ(std::string("hello world"), s.get());
}

TEST(StringTest, Assignment) {
  String s("hello world");
  String t = s;  // Makes a copy.
  EXPECT_FALSE(t.is_null());
  EXPECT_EQ(std::string("hello world"), t.get());
  EXPECT_FALSE(s.is_null());
}

TEST(StringTest, ConstAt) {
  const String s("abc");
  EXPECT_EQ('a', s.at(0));
  EXPECT_EQ('b', s.at(1));
  EXPECT_EQ('c', s.at(2));
}

TEST(StringTest, NonConstAt) {
  String s("abc");
  EXPECT_EQ('a', s.at(0));
  EXPECT_EQ('b', s.at(1));
  s.at(0) = 'x';
  s.at(1) = 'y';
  EXPECT_EQ('x', s.at(0));
  EXPECT_EQ('y', s.at(1));
  EXPECT_EQ('c', s.at(2));
}

TEST(StringTest, ConstArraySubscript) {
  const String s("abc");
  EXPECT_EQ('a', s[0]);
  EXPECT_EQ('b', s[1]);
  EXPECT_EQ('c', s[2]);
}

TEST(StringTest, NonConstArraySubscript) {
  String s("abc");
  EXPECT_EQ('a', s[0]);
  EXPECT_EQ('b', s[1]);
  s[0] = 'x';
  s[1] = 'y';
  EXPECT_EQ('x', s[0]);
  EXPECT_EQ('y', s[1]);
  EXPECT_EQ('c', s[2]);
}

TEST(StringTest, Equality) {
  String s("hello world");
  String t("hello world");
  EXPECT_EQ(s, t);
  EXPECT_TRUE(s == t);
  EXPECT_TRUE("hello world" == s);
  EXPECT_TRUE(s == "hello world");
  EXPECT_TRUE("not" != s);
  EXPECT_TRUE(s != "not");
}

TEST(StringTest, LessThanNullness) {
  String null;
  String null2;
  EXPECT_FALSE(null < null2);
  EXPECT_FALSE(null2 < null);

  String real("real");
  EXPECT_TRUE(null < real);
  EXPECT_FALSE(real < null);
}

TEST(StringTest, OutputFormatting) {
  String s("abc");
  String null;

  std::ostringstream so;
  so << "s=" << s << ", null=" << null;
  EXPECT_EQ("s=abc, null=", so.str());
}

}  // namespace test
}  // namespace fidl
