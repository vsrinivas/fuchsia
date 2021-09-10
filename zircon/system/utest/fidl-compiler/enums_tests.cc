// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zxtest/zxtest.h>

#include "error_test.h"
#include "fidl/diagnostics.h"
#include "test_library.h"

namespace {

TEST(EnumsTests, GoodEnumTestSimple) {
  TestLibrary library(R"FIDL(library example;

type Fruit = enum : uint64 {
    ORANGE = 1;
    APPLE = 2;
    BANANA = 3;
};
)FIDL");
  ASSERT_COMPILED(library);
  auto type_decl = library.LookupEnum("Fruit");
  ASSERT_NOT_NULL(type_decl);
  EXPECT_EQ(type_decl->members.size(), 3);
  EXPECT_EQ(type_decl->subtype_ctor->name.decl_name(), "uint64");
}

TEST(EnumsTests, BadEnumTestWithNonUniqueValues) {
  TestLibrary library(R"FIDL(
library example;

type Fruit = enum : uint64 {
    ORANGE = 1;
    APPLE = 1;
};
)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrDuplicateMemberValue);
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "APPLE");
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "ORANGE");
}

TEST(EnumsTests, BadEnumTestWithNonUniqueValuesOutOfLine) {
  TestLibrary library(R"FIDL(
library example;

type Fruit = enum {
    ORANGE = FOUR;
    APPLE = TWO_SQUARED;
};

const FOUR uint32 = 4;
const TWO_SQUARED uint32 = 4;
)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrDuplicateMemberValue);
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "APPLE");
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "ORANGE");
}

TEST(EnumsTests, BadEnumTestUnsignedWithNegativeMember) {
  TestLibrary library(R"FIDL(
library example;

type Fruit = enum : uint64 {
    ORANGE = 1;
    APPLE = -2;
};
)FIDL");
  ASSERT_ERRORED_TWICE_DURING_COMPILE(library, fidl::ErrConstantCannotBeInterpretedAsType,
                                      fidl::ErrCouldNotResolveMember);
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "-2");
}

TEST(EnumsTests, BadEnumTestInferredUnsignedWithNegativeMember) {
  TestLibrary library(R"FIDL(
library example;

type Fruit = enum {
    ORANGE = 1;
    APPLE = -2;
};
)FIDL");
  ASSERT_ERRORED_TWICE_DURING_COMPILE(library, fidl::ErrConstantCannotBeInterpretedAsType,
                                      fidl::ErrCouldNotResolveMember);
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "-2");
}

TEST(EnumsTests, BadEnumTestMemberOverflow) {
  TestLibrary library(R"FIDL(
library example;

type Fruit = enum : uint8 {
    ORANGE = 1;
    APPLE = 256;
};
)FIDL");
  ASSERT_ERRORED_TWICE_DURING_COMPILE(library, fidl::ErrConstantCannotBeInterpretedAsType,
                                      fidl::ErrCouldNotResolveMember);
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "256");
}

TEST(EnumsTests, BadEnumTestFloatType) {
  TestLibrary library(R"FIDL(
library example;

type Error = enum: float64 {
    ONE_POINT_FIVE = 1.5;
};
)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrEnumTypeMustBeIntegralPrimitive);
}

TEST(EnumsTests, BadEnumTestDuplicateMember) {
  TestLibrary library(R"FIDL(
library example;

type Fruit = enum : uint64 {
    ORANGE = 1;
    APPLE = 2;
    ORANGE = 3;
};
)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrDuplicateMemberName);
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "ORANGE");
}

TEST(EnumsTests, BadEnumTestNoMembers) {
  TestLibrary library(R"FIDL(
library example;

type E = enum {};
)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrMustHaveOneMember);
}

TEST(EnumsTests, GoodEnumTestKeywordNames) {
  TestLibrary library(R"FIDL(library example;

type Fruit = enum : uint64 {
    library = 1;
    enum = 2;
    uint64 = 3;
};
)FIDL");
  ASSERT_COMPILED(library);
}

TEST(EnumsTests, BadEnumShantBeNullable) {
  TestLibrary library(R"FIDL(
library example;

type NotNullable = enum {
    MEMBER = 1;
};

type Struct = struct {
    not_nullable NotNullable:optional;
};
)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrCannotBeNullable)
}

TEST(EnumsTests, BadEnumMultipleConstraints) {
  TestLibrary library(R"FIDL(
library example;

type NotNullable = enum {
    MEMBER = 1;
};

type Struct = struct {
    not_nullable NotNullable:<optional, foo, bar>;
};
)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrTooManyConstraints)
}

}  // namespace
