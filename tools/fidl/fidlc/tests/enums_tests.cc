// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zxtest/zxtest.h>

#include "tools/fidl/fidlc/include/fidl/diagnostics.h"
#include "tools/fidl/fidlc/tests/error_test.h"
#include "tools/fidl/fidlc/tests/test_library.h"

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
  auto underlying = type_decl->subtype_ctor->type;
  ASSERT_EQ(underlying->kind, fidl::flat::Type::Kind::kPrimitive);
  auto underlying_primitive = static_cast<const fidl::flat::PrimitiveType*>(underlying);
  EXPECT_EQ(underlying_primitive->subtype, fidl::types::PrimitiveSubtype::kUint64);
}

TEST(BitsTests, GoodEnumDefaultUint32) {
  TestLibrary library(R"FIDL(library example;

type Fruit = enum {
    ORANGE = 1;
};
)FIDL");
  ASSERT_COMPILED(library);
  auto type_decl = library.LookupEnum("Fruit");
  ASSERT_NOT_NULL(type_decl);
  auto underlying = type_decl->subtype_ctor->type;
  ASSERT_EQ(underlying->kind, fidl::flat::Type::Kind::kPrimitive);
  auto underlying_primitive = static_cast<const fidl::flat::PrimitiveType*>(underlying);
  EXPECT_EQ(underlying_primitive->subtype, fidl::types::PrimitiveSubtype::kUint32);
}

TEST(EnumsTests, BadEnumTestWithNonUniqueValues) {
  TestLibrary library;
  library.AddFile("bad/fi-0107.test.fidl");
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
  ASSERT_ERRORED_TWICE_DURING_COMPILE(library, fidl::ErrConstantOverflowsType,
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
  ASSERT_ERRORED_TWICE_DURING_COMPILE(library, fidl::ErrConstantOverflowsType,
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
  ASSERT_ERRORED_TWICE_DURING_COMPILE(library, fidl::ErrConstantOverflowsType,
                                      fidl::ErrCouldNotResolveMember);
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "256");
}

TEST(EnumsTests, BadEnumTestFloatType) {
  TestLibrary library;
  library.AddFile("bad/fi-0070.test.fidl");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrEnumTypeMustBeIntegralPrimitive);
}

TEST(EnumsTests, BadEnumTestDuplicateMember) {
  TestLibrary library;
  library.AddFile("bad/fi-0105.test.fidl");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrDuplicateMemberName);
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "ORANGE");
}

TEST(EnumsTests, GoodEnumTestNoMembersAllowedWhenDefaultsToFlexible) {
  TestLibrary library(R"FIDL(
library example;

type E = enum {};
)FIDL");
  ASSERT_COMPILED(library);
}

TEST(EnumsTests, GoodEnumTestNoMembersAllowedWhenFlexible) {
  TestLibrary library;
  library.AddFile("good/fi-0019-a.test.fidl");
  ASSERT_COMPILED(library);
}

TEST(EnumsTests, GoodEnumTestStrictWithMembers) {
  TestLibrary library;
  library.AddFile("good/fi-0019-b.test.fidl");
  ASSERT_COMPILED(library);
}

TEST(EnumsTests, BadEnumTestNoMembersWhenStrict) {
  TestLibrary library;
  library.AddFile("bad/fi-0019.test.fidl");
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
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrCannotBeOptional)
}

TEST(EnumsTests, BadEnumMultipleConstraints) {
  TestLibrary library(R"FIDL(
library example;

type NotNullable = enum {
    MEMBER = 1;
};

type Struct = struct {
    not_nullable NotNullable:<optional, 1, 2>;
};
)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrTooManyConstraints)
}

TEST(EnumsTests, GoodSimpleEnum) {
  TestLibrary library;
  library.AddFile("good/fi-0008.test.fidl");
  ASSERT_COMPILED(library);
}

}  // namespace
