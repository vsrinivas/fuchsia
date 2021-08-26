// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zxtest/zxtest.h>

#include "error_test.h"
#include "fidl/diagnostics.h"
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
  ASSERT_COMPILED_AND_CONVERT(library);
}

TEST(EnumsTests, BadEnumTestWithNonUniqueValues) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library(R"FIDL(
library example;

type Fruit = enum : uint64 {
    ORANGE = 1;
    APPLE = 1;
};
)FIDL",
                      experimental_flags);
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrDuplicateMemberValue);
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "APPLE");
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "ORANGE");
}

TEST(EnumsTests, BadEnumTestWithNonUniqueValuesOutOfLine) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library(R"FIDL(
library example;

type Fruit = enum {
    ORANGE = FOUR;
    APPLE = TWO_SQUARED;
};

const FOUR uint32 = 4;
const TWO_SQUARED uint32 = 4;
)FIDL",
                      experimental_flags);
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrDuplicateMemberValue);
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "APPLE");
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "ORANGE");
}

TEST(EnumsTests, BadEnumTestUnsignedWithNegativeMember) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library(R"FIDL(
library example;

type Fruit = enum : uint64 {
    ORANGE = 1;
    APPLE = -2;
};
)FIDL",
                      experimental_flags);
  ASSERT_ERRORED_TWICE_DURING_COMPILE(library, fidl::ErrConstantCannotBeInterpretedAsType,
                                      fidl::ErrCouldNotResolveMember);
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "-2");
}

TEST(EnumsTests, BadEnumTestInferredUnsignedWithNegativeMember) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library(R"FIDL(
library example;

type Fruit = enum {
    ORANGE = 1;
    APPLE = -2;
};
)FIDL",
                      experimental_flags);
  ASSERT_ERRORED_TWICE_DURING_COMPILE(library, fidl::ErrConstantCannotBeInterpretedAsType,
                                      fidl::ErrCouldNotResolveMember);
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "-2");
}

TEST(EnumsTests, BadEnumTestMemberOverflow) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library(R"FIDL(
library example;

type Fruit = enum : uint8 {
    ORANGE = 1;
    APPLE = 256;
};
)FIDL",
                      experimental_flags);
  ASSERT_ERRORED_TWICE_DURING_COMPILE(library, fidl::ErrConstantCannotBeInterpretedAsType,
                                      fidl::ErrCouldNotResolveMember);
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "256");
}

TEST(EnumsTests, BadEnumTestFloatType) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library(R"FIDL(
library example;

type Error = enum: float64 {
    ONE_POINT_FIVE = 1.5;
};
)FIDL",
                      experimental_flags);
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrEnumTypeMustBeIntegralPrimitive);
}

TEST(EnumsTests, BadEnumTestDuplicateMember) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library(R"FIDL(
library example;

type Fruit = enum : uint64 {
    ORANGE = 1;
    APPLE = 2;
    ORANGE = 3;
};
)FIDL",
                      experimental_flags);
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrDuplicateMemberName);
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "ORANGE");
}

TEST(EnumsTests, BadEnumTestNoMembers) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library(R"FIDL(
library example;

type E = enum {};
)FIDL",
                      experimental_flags);
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrMustHaveOneMember);
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
  ASSERT_COMPILED_AND_CONVERT(library);
}

TEST(EnumsTests, BadEnumShantBeNullable) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library(R"FIDL(
library example;

type NotNullable = enum {
    MEMBER = 1;
};

type Struct = struct {
    not_nullable NotNullable:optional;
};
)FIDL",
                      experimental_flags);
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrCannotBeNullable)
}

TEST(EnumsTests, BadEnumMultipleConstraints) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library(R"FIDL(
library example;

type NotNullable = enum {
    MEMBER = 1;
};

type Struct = struct {
    not_nullable NotNullable:<optional, foo, bar>;
};
)FIDL",
                      experimental_flags);
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrTooManyConstraints)
}

}  // namespace
