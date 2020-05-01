// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <unittest/unittest.h>

#include "error_test.h"
#include "test_library.h"

namespace {

bool GoodBitsTestSimple() {
  BEGIN_TEST;

  TestLibrary library(R"FIDL(
library example;

bits Fruit : uint64 {
    ORANGE = 1;
    APPLE = 2;
    BANANA = 4;
};
)FIDL");
  ASSERT_TRUE(library.Compile());

  END_TEST;
}

bool BadBitsTestSigned() {
  BEGIN_TEST;

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

  END_TEST;
}

bool BadBitsTestWithNonUniqueValues() {
  BEGIN_TEST;

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
  ASSERT_STR_STR(errors[0]->msg.c_str(), "APPLE");
  ASSERT_STR_STR(errors[0]->msg.c_str(), "ORANGE");

  END_TEST;
}

bool BadBitsTestWithNonUniqueValuesOutOfLine() {
  BEGIN_TEST;

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
  ASSERT_STR_STR(errors[0]->msg.c_str(), "APPLE");
  ASSERT_STR_STR(errors[0]->msg.c_str(), "ORANGE");

  END_TEST;
}

bool BadBitsTestUnsignedWithNegativeMember() {
  BEGIN_TEST;

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
  ASSERT_STR_STR(errors[0]->msg.c_str(), "-2");
  ASSERT_ERR(errors[1], fidl::ErrCouldNotResolveMember);

  END_TEST;
}

bool BadBitsTestMemberOverflow() {
  BEGIN_TEST;

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
  ASSERT_STR_STR(errors[0]->msg.c_str(), "256");
  ASSERT_ERR(errors[1], fidl::ErrCouldNotResolveMember);

  END_TEST;
}

bool BadBitsTestDuplicateMember() {
  BEGIN_TEST;

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
  ASSERT_STR_STR(errors[0]->msg.c_str(), "ORANGE");

  END_TEST;
}

bool BadBitsTestNoMembers() {
  BEGIN_TEST;

  TestLibrary library(R"FIDL(
library example;

bits B {};
)FIDL");
  ASSERT_FALSE(library.Compile());
  const auto& errors = library.errors();
  ASSERT_EQ(errors.size(), 1);
  ASSERT_ERR(errors[0], fidl::ErrMustHaveOneMember);

  END_TEST;
}

bool GoodBitsTestKeywordNames() {
  BEGIN_TEST;

  TestLibrary library(R"FIDL(
library example;

bits Fruit : uint64 {
    library = 1;
    bits = 2;
    uint64 = 4;
};
)FIDL");
  ASSERT_TRUE(library.Compile());

  END_TEST;
}

bool BadBitsTestNonPowerOfTwo() {
  BEGIN_TEST;

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

  END_TEST;
}

bool GoodBitsTestMask() {
  BEGIN_TEST;

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
  ASSERT_NONNULL(bits);
  EXPECT_EQ(bits->mask, 42);

  END_TEST;
}

}  // namespace

BEGIN_TEST_CASE(bits_tests)

RUN_TEST(GoodBitsTestSimple)
RUN_TEST(BadBitsTestSigned)
RUN_TEST(BadBitsTestWithNonUniqueValues)
RUN_TEST(BadBitsTestWithNonUniqueValuesOutOfLine)
RUN_TEST(BadBitsTestUnsignedWithNegativeMember)
RUN_TEST(BadBitsTestMemberOverflow)
RUN_TEST(BadBitsTestDuplicateMember)
RUN_TEST(BadBitsTestNoMembers)
RUN_TEST(GoodBitsTestKeywordNames)
RUN_TEST(BadBitsTestNonPowerOfTwo)
RUN_TEST(GoodBitsTestMask)

END_TEST_CASE(bits_tests)
