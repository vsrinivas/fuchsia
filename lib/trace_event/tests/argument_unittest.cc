// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/trace_event/argument.h"

#include "gtest/gtest.h"

namespace trace_event {
namespace {

TEST(ArgumentTest, ConstructFromBool) {
  const bool ref_value = true;
  Argument arg("TestBool", ref_value);
  EXPECT_EQ(Argument::Type::kBool, arg.type());
  EXPECT_STREQ("TestBool", arg.name());
  EXPECT_EQ(ref_value, arg.as_bool());
}

TEST(ArgumentTest, ConstructFromInt32) {
  const int32_t ref_value = 42;
  Argument arg("TestInt32", ref_value);
  EXPECT_EQ(Argument::Type::kInt, arg.type());
  EXPECT_STREQ("TestInt32", arg.name());
  EXPECT_EQ(ref_value, arg.as_int());
}

TEST(ArgumentTest, ConstructFromInt64) {
  const int64_t ref_value = 42;
  Argument arg("TestInt64", ref_value);
  EXPECT_EQ(Argument::Type::kInt, arg.type());
  EXPECT_STREQ("TestInt64", arg.name());
  EXPECT_EQ(ref_value, arg.as_int());
}

TEST(ArgumentTest, ConstructFromUInt32) {
  const uint32_t ref_value = 42;
  Argument arg("TestUint32", ref_value);
  EXPECT_EQ(Argument::Type::kUint, arg.type());
  EXPECT_STREQ("TestUint32", arg.name());
  EXPECT_EQ(ref_value, arg.as_uint());
}

TEST(ArgumentTest, ConstructFromUInt64) {
  const uint64_t ref_value = 42;
  Argument arg("TestUint64", ref_value);
  EXPECT_EQ(Argument::Type::kUint, arg.type());
  EXPECT_STREQ("TestUint64", arg.name());
  EXPECT_EQ(ref_value, arg.as_uint());
}

TEST(ArgumentTest, ConstructFromFloat) {
  const float ref_value = 42.;
  Argument arg("TestFloat", ref_value);
  EXPECT_EQ(Argument::Type::kDouble, arg.type());
  EXPECT_STREQ("TestFloat", arg.name());
  EXPECT_FLOAT_EQ(ref_value, arg.as_double());
}

TEST(ArgumentTest, ConstructFromDouble) {
  const double ref_value = 42.;
  Argument arg("TestDouble", ref_value);
  EXPECT_EQ(Argument::Type::kDouble, arg.type());
  EXPECT_STREQ("TestDouble", arg.name());
  EXPECT_DOUBLE_EQ(ref_value, arg.as_double());
}

TEST(ArgumentTest, ConstructFromPointer) {
  int i = 0;
  const void* ref_value = &i;
  Argument arg("TestPointer", ref_value);
  EXPECT_EQ(Argument::Type::kPointer, arg.type());
  EXPECT_STREQ("TestPointer", arg.name());
  EXPECT_EQ(ref_value, arg.as_pointer());
}

TEST(ArgumentTest, ConstructFromStaticString) {
  const char* ref_value = "fuchsia";
  Argument arg("TestString", ref_value);
  EXPECT_EQ(Argument::Type::kString, arg.type());
  EXPECT_STREQ("TestString", arg.name());
  EXPECT_STREQ(ref_value, arg.as_string());
}

TEST(ArgumentTest, ConstructFromDynamicString) {
  std::string ref_value = "fuchsia";
  Argument arg("TestString", ref_value);
  EXPECT_EQ(Argument::Type::kString, arg.type());
  EXPECT_STREQ("TestString", arg.name());
  EXPECT_STREQ(ref_value.c_str(), arg.as_string());
}

TEST(ArgumentTest, EncodeBool) {
  EXPECT_EQ("\"TestArg\":true", EncodeAsJson(Argument("TestArg", true)));
  EXPECT_EQ("\"TestArg\":false", EncodeAsJson(Argument("TestArg", false)));
}

TEST(ArgumentTest, EncodeInt) {
  EXPECT_EQ("\"TestArg\":-42", EncodeAsJson(Argument("TestArg", int32_t(-42))));
  EXPECT_EQ("\"TestArg\":84", EncodeAsJson(Argument("TestArg", int64_t(84))));
}

TEST(ArgumentTest, EncodeUint) {
  EXPECT_EQ("\"TestArg\":21", EncodeAsJson(Argument("TestArg", uint32_t(21))));
  EXPECT_EQ("\"TestArg\":189",
            EncodeAsJson(Argument("TestArg", uint64_t(189))));
}

TEST(ArgumentTest, EncodeDouble) {
  EXPECT_EQ("\"TestArg\":-42.42",
            EncodeAsJson(Argument("TestArg", float(-42.42))));
  EXPECT_EQ("\"TestArg\":3.14",
            EncodeAsJson(Argument("TestArg", double(3.14))));
}

TEST(ArgumentTest, EncodeString) {
  EXPECT_EQ("\"TestArg\":\"TestValue\"",
            EncodeAsJson(Argument("TestArg", "TestValue")));
}

}  // namespace
}  // namespace trace_event
