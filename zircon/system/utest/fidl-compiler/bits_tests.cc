// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zxtest/zxtest.h>

#include "error_test.h"
#include "test_library.h"

namespace {

TEST(BitsTests, GoodBitsTestSimple) {
  TestLibrary library(R"FIDL(library example;

type Fruit = bits : uint64 {
    ORANGE = 1;
    APPLE = 2;
    BANANA = 4;
};
)FIDL");
  ASSERT_COMPILED(library);
  auto type_decl = library.LookupBits("Fruit");
  ASSERT_NOT_NULL(type_decl);
  EXPECT_EQ(type_decl->members.size(), 3);
  EXPECT_EQ(type_decl->subtype_ctor->name.decl_name(), "uint64");
}

TEST(BitsTests, BadBitsTestSigned) {
  TestLibrary library(R"FIDL(
library example;

type Fruit = bits : int64 {
    ORANGE = 1;
    APPLE = 2;
    BANANA = 4;
};
)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrBitsTypeMustBeUnsignedIntegralPrimitive);
}

TEST(BitsTests, BadBitsTestWithNonUniqueValues) {
  TestLibrary library(R"FIDL(
library example;

type Fruit = bits : uint64 {
    ORANGE = 1;
    APPLE = 1;
};
)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrDuplicateMemberValue);
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "APPLE");
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "ORANGE");
}

TEST(BitsTests, BadBitsTestWithNonUniqueValuesOutOfLine) {
  TestLibrary library(R"FIDL(
library example;

type Fruit = bits {
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

TEST(BitsTests, BadBitsTestUnsignedWithNegativeMember) {
  TestLibrary library(R"FIDL(
library example;

type Fruit = bits : uint64 {
    ORANGE = 1;
    APPLE = -2;
};
)FIDL");
  ASSERT_ERRORED_TWICE_DURING_COMPILE(library, fidl::ErrConstantCannotBeInterpretedAsType,
                                      fidl::ErrCouldNotResolveMember);
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "-2");
}

TEST(BitsTests, BadBitsTestMemberOverflow) {
  TestLibrary library(R"FIDL(
library example;

type Fruit = bits : uint8 {
    ORANGE = 1;
    APPLE = 256;
};
)FIDL");
  ASSERT_ERRORED_TWICE_DURING_COMPILE(library, fidl::ErrConstantCannotBeInterpretedAsType,
                                      fidl::ErrCouldNotResolveMember);
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "256");
}

TEST(BitsTests, BadBitsTestDuplicateMember) {
  TestLibrary library(R"FIDL(
library example;

type Fruit = bits : uint64 {
    ORANGE = 1;
    APPLE = 2;
    ORANGE = 4;
};
)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrDuplicateMemberName);
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "ORANGE");
}

TEST(BitsTests, BadBitsTestNoMembers) {
  TestLibrary library(R"FIDL(
library example;

type B = bits {};
)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrMustHaveOneMember);
}

TEST(BitsTests, GoodBitsTestKeywordNames) {
  TestLibrary library(R"FIDL(library example;

type Fruit = bits : uint64 {
    library = 1;
    bits = 2;
    uint64 = 4;
};
)FIDL");
  ASSERT_COMPILED(library);
}

TEST(BitsTests, BadBitsTestNonPowerOfTwo) {
  TestLibrary library(R"FIDL(
library example;

type non_power_of_two = bits : uint64 {
    three = 3;
};
)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrBitsMemberMustBePowerOfTwo);
}

TEST(BitsTests, GoodBitsTestMask) {
  TestLibrary library(R"FIDL(library example;

type Life = bits {
    A = 0b000010;
    B = 0b001000;
    C = 0b100000;
};
)FIDL");
  ASSERT_COMPILED(library);

  auto bits = library.LookupBits("Life");
  ASSERT_NOT_NULL(bits);
  EXPECT_EQ(bits->mask, 42);
}

TEST(BitsTests, BadBitsShantBeNullable) {
  TestLibrary library(R"FIDL(
library example;

type NotNullable = bits {
    MEMBER = 1;
};

type Struct = struct {
    not_nullable NotNullable:optional;
};
)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrCannotBeNullable);
}

TEST(BitsTests, BadBitsMultipleConstraints) {
  TestLibrary library(R"FIDL(
library example;

type NotNullable = bits {
    MEMBER = 1;
};

type Struct = struct {
    not_nullable NotNullable:<optional, foo, bar>;
};
)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrTooManyConstraints);
}

}  // namespace
