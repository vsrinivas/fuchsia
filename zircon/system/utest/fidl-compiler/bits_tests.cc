// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zxtest/zxtest.h>

#include "error_test.h"
#include "test_library.h"

namespace {

TEST(BitsTests, GoodBitsTestSimple) {
  TestLibrary library(R"FIDL(
library example;

bits Fruit : uint64 {
    ORANGE = 1;
    APPLE = 2;
    BANANA = 4;
};
)FIDL");
  ASSERT_TRUE(library.Compile());
}

TEST(BitsTests, BadBitsTestSigned) {
  TestLibrary library(R"FIDL(
library example;

bits Fruit : int64 {
    ORANGE = 1;
    APPLE = 2;
    BANANA = 4;
};
)FIDL");
  ASSERT_FALSE(library.Compile());
  const auto& errors = library.errors();
  ASSERT_ERR(errors[0], fidl::ErrBitsTypeMustBeUnsignedIntegralPrimitive);
}

TEST(BitsTests, BadBitsTestWithNonUniqueValues) {
  TestLibrary library(R"FIDL(
library example;

bits Fruit : uint64 {
    ORANGE = 1;
    APPLE = 1;
};
)FIDL");
  ASSERT_FALSE(library.Compile());
  const auto& errors = library.errors();
  ASSERT_EQ(errors.size(), 1);
  ASSERT_ERR(errors[0], fidl::ErrDuplicateMemberValue);
  ASSERT_SUBSTR(errors[0]->msg.c_str(), "APPLE");
  ASSERT_SUBSTR(errors[0]->msg.c_str(), "ORANGE");
}

TEST(BitsTests, BadBitsTestWithNonUniqueValuesOutOfLine) {
  TestLibrary library(R"FIDL(
library example;

bits Fruit {
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
  ASSERT_SUBSTR(errors[0]->msg.c_str(), "APPLE");
  ASSERT_SUBSTR(errors[0]->msg.c_str(), "ORANGE");
}

TEST(BitsTests, BadBitsTestUnsignedWithNegativeMember) {
  TestLibrary library(R"FIDL(
library example;

bits Fruit : uint64 {
    ORANGE = 1;
    APPLE = -2;
};
)FIDL");
  ASSERT_FALSE(library.Compile());
  const auto& errors = library.errors();
  ASSERT_EQ(errors.size(), 2);
  ASSERT_ERR(errors[0], fidl::ErrConstantCannotBeInterpretedAsType);
  ASSERT_SUBSTR(errors[0]->msg.c_str(), "-2");
  ASSERT_ERR(errors[1], fidl::ErrCouldNotResolveMember);
}

TEST(BitsTests, BadBitsTestMemberOverflow) {
  TestLibrary library(R"FIDL(
library example;

bits Fruit : uint8 {
    ORANGE = 1;
    APPLE = 256;
};
)FIDL");
  ASSERT_FALSE(library.Compile());
  const auto& errors = library.errors();
  ASSERT_EQ(errors.size(), 2);
  ASSERT_ERR(errors[0], fidl::ErrConstantCannotBeInterpretedAsType);
  ASSERT_SUBSTR(errors[0]->msg.c_str(), "256");
  ASSERT_ERR(errors[1], fidl::ErrCouldNotResolveMember);
}

TEST(BitsTests, BadBitsTestDuplicateMember) {
  TestLibrary library(R"FIDL(
library example;

bits Fruit : uint64 {
    ORANGE = 1;
    APPLE = 2;
    ORANGE = 4;
};
)FIDL");
  ASSERT_FALSE(library.Compile());
  const auto& errors = library.errors();
  ASSERT_GE(errors.size(), 1);
  ASSERT_ERR(errors[0], fidl::ErrDuplicateMemberName);
  ASSERT_SUBSTR(errors[0]->msg.c_str(), "ORANGE");
}

TEST(BitsTests, BadBitsTestNoMembers) {
  TestLibrary library(R"FIDL(
library example;

bits B {};
)FIDL");
  ASSERT_FALSE(library.Compile());
  const auto& errors = library.errors();
  ASSERT_EQ(errors.size(), 1);
  ASSERT_ERR(errors[0], fidl::ErrMustHaveOneMember);
}

TEST(BitsTests, GoodBitsTestKeywordNames) {
  TestLibrary library(R"FIDL(
library example;

bits Fruit : uint64 {
    library = 1;
    bits = 2;
    uint64 = 4;
};
)FIDL");
  ASSERT_TRUE(library.Compile());
}

TEST(BitsTests, BadBitsTestNonPowerOfTwo) {
  TestLibrary library(R"FIDL(
library example;

bits non_power_of_two : uint64 {
    three = 3;
};
)FIDL");
  ASSERT_FALSE(library.Compile());
  const auto& errors = library.errors();
  ASSERT_EQ(errors.size(), 1);
  ASSERT_ERR(errors[0], fidl::ErrBitsMemberMustBePowerOfTwo);
}

TEST(BitsTests, GoodBitsTestMask) {
  TestLibrary library(R"FIDL(
library example;

bits Life {
    A = 0b000010;
    B = 0b001000;
    C = 0b100000;
};
)FIDL");
  ASSERT_TRUE(library.Compile());

  auto bits = library.LookupBits("Life");
  ASSERT_NOT_NULL(bits);
  EXPECT_EQ(bits->mask, 42);
}

TEST(EnumsTests, BadBitsShantBeNullable) {
  TestLibrary library(R"FIDL(
library example;

bits NotNullable {
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
  ASSERT_SUBSTR(errors[0]->msg.c_str(), "NotNullable");
}

}  // namespace
