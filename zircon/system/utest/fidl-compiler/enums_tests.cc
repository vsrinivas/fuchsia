// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <unittest/unittest.h>

#include "error_test.h"
#include "test_library.h"

namespace {

bool GoodEnumTestSimple() {
  BEGIN_TEST;

  TestLibrary library(R"FIDL(
library example;

enum Fruit : uint64 {
    ORANGE = 1;
    APPLE = 2;
    BANANA = 3;
};
)FIDL");
  ASSERT_TRUE(library.Compile());

  END_TEST;
}

bool BadEnumTestWithNonUniqueValues() {
  BEGIN_TEST;

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

  END_TEST;
}

bool BadEnumTestWithNonUniqueValuesOutOfLine() {
  BEGIN_TEST;

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

  END_TEST;
}

bool BadEnumTestUnsignedWithNegativeMember() {
  BEGIN_TEST;

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

  END_TEST;
}

bool BadEnumTestInferredUnsignedWithNegativeMember() {
  BEGIN_TEST;

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

  END_TEST;
}

bool BadEnumTestMemberOverflow() {
  BEGIN_TEST;

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

  END_TEST;
}

bool BadEnumTestFloatType() {
  BEGIN_TEST;

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

  END_TEST;
}

bool BadEnumTestDuplicateMember() {
  BEGIN_TEST;

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

  END_TEST;
}

bool BadEnumTestNoMembers() {
  BEGIN_TEST;

  TestLibrary library(R"FIDL(
library example;

enum E {};
)FIDL");
  ASSERT_FALSE(library.Compile());
  const auto& errors = library.errors();
  ASSERT_EQ(errors.size(), 1);
  ASSERT_ERR(errors[0], fidl::ErrMustHaveOneMember);

  END_TEST;
}

bool GoodEnumTestKeywordNames() {
  BEGIN_TEST;

  TestLibrary library(R"FIDL(
library example;

enum Fruit : uint64 {
    library = 1;
    enum = 2;
    uint64 = 3;
};
)FIDL");
  ASSERT_TRUE(library.Compile());

  END_TEST;
}

bool BadEnumShantBeNullable() {
  BEGIN_TEST;

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

  END_TEST;
}

}  // namespace

BEGIN_TEST_CASE(enums_tests)

RUN_TEST(GoodEnumTestSimple)
RUN_TEST(BadEnumTestWithNonUniqueValues)
RUN_TEST(BadEnumTestWithNonUniqueValuesOutOfLine)
RUN_TEST(BadEnumTestUnsignedWithNegativeMember)
RUN_TEST(BadEnumTestInferredUnsignedWithNegativeMember)
RUN_TEST(BadEnumTestMemberOverflow)
RUN_TEST(BadEnumTestFloatType)
RUN_TEST(BadEnumTestDuplicateMember)
RUN_TEST(BadEnumTestNoMembers)
RUN_TEST(GoodEnumTestKeywordNames)
RUN_TEST(BadEnumShantBeNullable)

END_TEST_CASE(enums_tests)
