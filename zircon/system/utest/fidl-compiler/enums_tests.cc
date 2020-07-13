// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zxtest/zxtest.h>

#include "assert_strstr.h"
#include "error_test.h"
#include "test_library.h"

namespace {

TEST(EnumsTests, GoodEnumTestSimple) {
  TestLibrary library(R"FIDL(
library example;

enum Fruit : uint64 {
    ORANGE = 1;
    APPLE = 2;
    BANANA = 3;
};
)FIDL");
  ASSERT_TRUE(library.Compile());
}

TEST(EnumsTests, BadEnumTestWithNonUniqueValues) {
  TestLibrary library(R"FIDL(
library example;

enum Fruit : uint64 {
    ORANGE = 1;
    APPLE = 1;
};
)FIDL");
  ASSERT_FALSE(library.Compile());
  const auto& errors = library.errors();
  ASSERT_EQ(errors.size(), 1);
  ASSERT_ERR(errors[0], fidl::ErrDuplicateMemberValue);
  ASSERT_STR_STR(errors[0]->msg.c_str(), "APPLE");
  ASSERT_STR_STR(errors[0]->msg.c_str(), "ORANGE");
}

TEST(EnumsTests, BadEnumTestWithNonUniqueValuesOutOfLine) {
  TestLibrary library(R"FIDL(
library example;

enum Fruit {
    ORANGE = FOUR;
    APPLE = TWO_SQUARED;
};

const uint32 FOUR = 4;
const uint32 TWO_SQUARED = 4;
)FIDL");
  ASSERT_FALSE(library.Compile());
  const auto& errors = library.errors();
  ASSERT_EQ(errors.size(), 1);
  ASSERT_ERR(errors[0], fidl::ErrDuplicateMemberValue);
  ASSERT_STR_STR(errors[0]->msg.c_str(), "APPLE");
  ASSERT_STR_STR(errors[0]->msg.c_str(), "ORANGE");
}

TEST(EnumsTests, BadEnumTestUnsignedWithNegativeMember) {
  TestLibrary library(R"FIDL(
library example;

enum Fruit : uint64 {
    ORANGE = 1;
    APPLE = -2;
};
)FIDL");
  ASSERT_FALSE(library.Compile());
  const auto& errors = library.errors();
  ASSERT_EQ(errors.size(), 2);
  ASSERT_ERR(errors[0], fidl::ErrConstantCannotBeInterpretedAsType);
  ASSERT_STR_STR(errors[0]->msg.c_str(), "-2");
  ASSERT_ERR(errors[1], fidl::ErrCouldNotResolveMember);
}

TEST(EnumsTests, BadEnumTestInferredUnsignedWithNegativeMember) {
  TestLibrary library(R"FIDL(
library example;

enum Fruit {
    ORANGE = 1;
    APPLE = -2;
};
)FIDL");
  ASSERT_FALSE(library.Compile());
  const auto& errors = library.errors();
  ASSERT_EQ(errors.size(), 2);
  ASSERT_ERR(errors[0], fidl::ErrConstantCannotBeInterpretedAsType);
  ASSERT_STR_STR(errors[0]->msg.c_str(), "-2");
  ASSERT_ERR(errors[1], fidl::ErrCouldNotResolveMember);
}

TEST(EnumsTests, BadEnumTestMemberOverflow) {
  TestLibrary library(R"FIDL(
library example;

enum Fruit : uint8 {
    ORANGE = 1;
    APPLE = 256;
};
)FIDL");
  ASSERT_FALSE(library.Compile());
  const auto& errors = library.errors();
  ASSERT_EQ(errors.size(), 2);
  ASSERT_ERR(errors[0], fidl::ErrConstantCannotBeInterpretedAsType);
  ASSERT_STR_STR(errors[0]->msg.c_str(), "256");
  ASSERT_ERR(errors[1], fidl::ErrCouldNotResolveMember);
}

TEST(EnumsTests, BadEnumTestFloatType) {
  TestLibrary library(R"FIDL(
library example;

enum Error: float64 {
    ONE_POINT_FIVE = 1.5;
};
)FIDL");
  ASSERT_FALSE(library.Compile());
  const auto& errors = library.errors();
  ASSERT_EQ(errors.size(), 1);
  ASSERT_ERR(errors[0], fidl::ErrEnumTypeMustBeIntegralPrimitive);
}

TEST(EnumsTests, BadEnumTestDuplicateMember) {
  TestLibrary library(R"FIDL(
library example;

enum Fruit : uint64 {
    ORANGE = 1;
    APPLE = 2;
    ORANGE = 3;
};
)FIDL");
  ASSERT_FALSE(library.Compile());
  const auto& errors = library.errors();
  ASSERT_GE(errors.size(), 1);
  ASSERT_ERR(errors[0], fidl::ErrDuplicateMemberName);
  ASSERT_STR_STR(errors[0]->msg.c_str(), "ORANGE");
}

TEST(EnumsTests, BadEnumTestNoMembers) {
  TestLibrary library(R"FIDL(
library example;

enum E {};
)FIDL");
  ASSERT_FALSE(library.Compile());
  const auto& errors = library.errors();
  ASSERT_EQ(errors.size(), 1);
  ASSERT_ERR(errors[0], fidl::ErrMustHaveOneMember);
}

TEST(EnumsTests, GoodEnumTestKeywordNames) {
  TestLibrary library(R"FIDL(
library example;

enum Fruit : uint64 {
    library = 1;
    enum = 2;
    uint64 = 3;
};
)FIDL");
  ASSERT_TRUE(library.Compile());
}

TEST(EnumsTests, BadEnumShantBeNullable) {
  TestLibrary library(R"FIDL(
library example;

enum NotNullable {
    MEMBER = 1;
};

struct Struct {
    NotNullable? not_nullable;
};
)FIDL");
  ASSERT_FALSE(library.Compile());
  const auto& errors = library.errors();
  ASSERT_GE(errors.size(), 1);
  ASSERT_ERR(errors[0], fidl::ErrCannotBeNullable);
  ASSERT_STR_STR(errors[0]->msg.c_str(), "NotNullable");
}

}  // namespace
