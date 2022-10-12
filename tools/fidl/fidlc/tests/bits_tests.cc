// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zxtest/zxtest.h>

#include "tools/fidl/fidlc/tests/error_test.h"
#include "tools/fidl/fidlc/tests/test_library.h"

namespace {

TEST(BitsTests, GoodSimple) {
  TestLibrary library;
  library.AddFile("good/fi-0067-a.test.fidl");

  ASSERT_COMPILED(library);
  auto type_decl = library.LookupBits("Fruit");
  ASSERT_NOT_NULL(type_decl);
  EXPECT_EQ(type_decl->members.size(), 3);
  auto underlying = type_decl->subtype_ctor->type;
  ASSERT_EQ(underlying->kind, fidl::flat::Type::Kind::kPrimitive);
  auto underlying_primitive = static_cast<const fidl::flat::PrimitiveType*>(underlying);
  EXPECT_EQ(underlying_primitive->subtype, fidl::types::PrimitiveSubtype::kUint64);
}

TEST(BitsTests, GoodDefaultUint32) {
  TestLibrary library(R"FIDL(library example;

type Fruit = bits {
    ORANGE = 1;
};
)FIDL");
  ASSERT_COMPILED(library);
  auto type_decl = library.LookupBits("Fruit");
  ASSERT_NOT_NULL(type_decl);
  auto underlying = type_decl->subtype_ctor->type;
  ASSERT_EQ(underlying->kind, fidl::flat::Type::Kind::kPrimitive);
  auto underlying_primitive = static_cast<const fidl::flat::PrimitiveType*>(underlying);
  EXPECT_EQ(underlying_primitive->subtype, fidl::types::PrimitiveSubtype::kUint32);
}

TEST(BitsTests, BadSigned) {
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

TEST(BitsTests, BadNonUniqueValues) {
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

TEST(BitsTests, BadNonUniqueValuesOutOfLine) {
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

TEST(BitsTests, BadUnsignedWithNegativeMember) {
  TestLibrary library(R"FIDL(
library example;

type Fruit = bits : uint64 {
    ORANGE = 1;
    APPLE = -2;
};
)FIDL");
  ASSERT_ERRORED_TWICE_DURING_COMPILE(library, fidl::ErrConstantOverflowsType,
                                      fidl::ErrCouldNotResolveMember);
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "-2");
}

TEST(BitsTests, BadMemberOverflow) {
  TestLibrary library(R"FIDL(
library example;

type Fruit = bits : uint8 {
    ORANGE = 1;
    APPLE = 256;
};
)FIDL");
  ASSERT_ERRORED_TWICE_DURING_COMPILE(library, fidl::ErrConstantOverflowsType,
                                      fidl::ErrCouldNotResolveMember);
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "256");
}

TEST(BitsTests, BadDuplicateMember) {
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

TEST(BitsTests, BadNoMembersWhenStrict) {
  TestLibrary library(R"FIDL(
library example;

type B = strict bits {};
)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrMustHaveOneMember);
}

TEST(BitsTests, GoodNoMembersAllowedWhenFlexible) {
  TestLibrary library(R"FIDL(
library example;

type B = flexible bits {};
)FIDL");
  ASSERT_COMPILED(library);
}

TEST(BitsTests, GoodNoMembersAllowedWhenDefaultsToFlexible) {
  TestLibrary library(R"FIDL(
library example;

type B = bits {};
)FIDL");
  ASSERT_COMPILED(library);
}

TEST(BitsTests, GoodKeywordNames) {
  TestLibrary library(R"FIDL(library example;

type Fruit = bits : uint64 {
    library = 1;
    bits = 2;
    uint64 = 4;
};
)FIDL");
  ASSERT_COMPILED(library);
}

TEST(BitsTests, BadNonPowerOfTwo) {
  TestLibrary library;
  library.AddFile("bad/fi-0067.test.fidl");

  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrBitsMemberMustBePowerOfTwo);
}

TEST(BitsTests, GoodWithMask) {
  TestLibrary library;
  library.AddFile("good/fi-0067-b.test.fidl");

  ASSERT_COMPILED(library);

  auto bits = library.LookupBits("Life");
  ASSERT_NOT_NULL(bits);
  EXPECT_EQ(bits->mask, 42);
}

TEST(BitsTests, BadShantBeNullable) {
  TestLibrary library(R"FIDL(
library example;

type NotNullable = bits {
    MEMBER = 1;
};

type Struct = struct {
    not_nullable NotNullable:optional;
};
)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrCannotBeOptional);
}

TEST(BitsTests, BadMultipleConstraints) {
  TestLibrary library(R"FIDL(
library example;

type NotNullable = bits {
    MEMBER = 1;
};

type Struct = struct {
    not_nullable NotNullable:<optional, 1, 2>;
};
)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrTooManyConstraints);
}

}  // namespace
