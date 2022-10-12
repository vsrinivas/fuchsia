// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zxtest/zxtest.h>

#include "tools/fidl/fidlc/include/fidl/flat_ast.h"
#include "tools/fidl/fidlc/include/fidl/lexer.h"
#include "tools/fidl/fidlc/include/fidl/names.h"
#include "tools/fidl/fidlc/include/fidl/parser.h"
#include "tools/fidl/fidlc/include/fidl/source_file.h"
#include "tools/fidl/fidlc/tests/error_test.h"
#include "tools/fidl/fidlc/tests/test_library.h"

namespace {

TEST(UnionTests, GoodKeywordsAsFieldNames) {
  TestLibrary library(R"FIDL(library test;

type struct = struct {
    field bool;
};

type Foo = strict union {
    1: union int64;
    2: library bool;
    3: uint32 uint32;
    4: member struct;
    5: reserved bool;
};
)FIDL");
  ASSERT_COMPILED(library);
  auto type_decl = library.LookupUnion("Foo");
  ASSERT_NOT_NULL(type_decl);
  EXPECT_EQ(type_decl->members.size(), 5);
}

TEST(UnionTests, GoodRecursiveUnion) {
  TestLibrary library(R"FIDL(library test;

type Value = strict union {
    1: bool_value bool;
    2: list_value vector<Value:optional>;
};
)FIDL");
  ASSERT_COMPILED(library);
}

TEST(UnionTests, GoodMutuallyRecursive) {
  TestLibrary library(R"FIDL(library test;

type Foo = strict union {
    1: bar Bar;
};

type Bar = struct {
    foo Foo:optional;
};
)FIDL");
  ASSERT_COMPILED(library);
}

TEST(UnionTests, GoodFlexibleUnion) {
  TestLibrary library(R"FIDL(library test;

type Foo = flexible union {
    1: bar string;
};
)FIDL");
  ASSERT_COMPILED(library);
}

TEST(UnionTests, GoodStrictUnion) {
  TestLibrary library;
  library.AddFile("good/fi-0018.test.fidl");
  ASSERT_COMPILED(library);
}

TEST(UnionTests, BadMustHaveExplicitOrdinals) {
  TestLibrary library(R"FIDL(
library test;

type Foo = strict union {
    foo int64;
    bar vector<uint32>:10;
};

)FIDL");
  ASSERT_ERRORED_TWICE_DURING_COMPILE(library, fidl::ErrMissingOrdinalBeforeMember,
                                      fidl::ErrMissingOrdinalBeforeMember);
}

TEST(UnionTests, GoodExplicitOrdinals) {
  TestLibrary library(R"FIDL(library test;

type Foo = strict union {
    1: foo int64;
    2: bar vector<uint32>:10;
};
)FIDL");
  ASSERT_COMPILED(library);

  auto fidl_union = library.LookupUnion("Foo");
  ASSERT_NOT_NULL(fidl_union);

  ASSERT_EQ(fidl_union->members.size(), 2);
  auto& member0 = fidl_union->members[0];
  EXPECT_NOT_NULL(member0.maybe_used);
  EXPECT_EQ(member0.ordinal->value, 1);
  auto& member1 = fidl_union->members[1];
  EXPECT_NOT_NULL(member1.maybe_used);
  EXPECT_EQ(member1.ordinal->value, 2);
}

TEST(UnionTests, GoodOrdinalsWithReserved) {
  TestLibrary library(R"FIDL(library test;

type Foo = strict union {
    1: reserved;
    2: foo int64;
    3: reserved;
    4: bar vector<uint32>:10;
    5: reserved;
};
)FIDL");
  ASSERT_COMPILED(library);

  auto fidl_union = library.LookupUnion("Foo");
  ASSERT_NOT_NULL(fidl_union);

  ASSERT_EQ(fidl_union->members.size(), 5);
  auto& member0 = fidl_union->members[0];
  EXPECT_NULL(member0.maybe_used);
  EXPECT_EQ(member0.ordinal->value, 1);
  auto& member1 = fidl_union->members[1];
  EXPECT_NOT_NULL(member1.maybe_used);
  EXPECT_EQ(member1.ordinal->value, 2);
  auto& member2 = fidl_union->members[2];
  EXPECT_NULL(member2.maybe_used);
  EXPECT_EQ(member2.ordinal->value, 3);
  auto& member3 = fidl_union->members[3];
  EXPECT_NOT_NULL(member3.maybe_used);
  EXPECT_EQ(member3.ordinal->value, 4);
  auto& member4 = fidl_union->members[4];
  EXPECT_NULL(member4.maybe_used);
  EXPECT_EQ(member4.ordinal->value, 5);
}

TEST(UnionTests, GoodOrdinalsOutOfOrder) {
  TestLibrary library(R"FIDL(library test;

type Foo = strict union {
    5: foo int64;
    2: bar vector<uint32>:10;
    3: reserved;
    1: reserved;
    4: baz uint32;
};
)FIDL");
  ASSERT_COMPILED(library);

  auto fidl_union = library.LookupUnion("Foo");
  ASSERT_NOT_NULL(fidl_union);

  ASSERT_EQ(fidl_union->members.size(), 5);
  auto& member0 = fidl_union->members[0];
  EXPECT_NOT_NULL(member0.maybe_used);
  EXPECT_EQ(member0.ordinal->value, 5);
  auto& member1 = fidl_union->members[1];
  EXPECT_NOT_NULL(member1.maybe_used);
  EXPECT_EQ(member1.ordinal->value, 2);
  auto& member2 = fidl_union->members[2];
  EXPECT_NULL(member2.maybe_used);
  EXPECT_EQ(member2.ordinal->value, 3);
  auto& member3 = fidl_union->members[3];
  EXPECT_NULL(member3.maybe_used);
  EXPECT_EQ(member3.ordinal->value, 1);
  auto& member4 = fidl_union->members[4];
  EXPECT_NOT_NULL(member4.maybe_used);
  EXPECT_EQ(member4.ordinal->value, 4);
}

TEST(UnionTests, BadOrdinalOutOfBounds) {
  TestLibrary library(R"FIDL(
library test;

type Foo = strict union {
  -1: uint32 foo;
};
)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrOrdinalOutOfBound);
}

TEST(UnionTests, BadOrdinalsMustBeUnique) {
  TestLibrary library(R"FIDL(
library test;

type Foo = strict union {
  1: reserved;
  1: x uint64;
};
)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrDuplicateUnionMemberOrdinal);
}

TEST(UnionTests, BadMemberNamesMustBeUnique) {
  TestLibrary library(R"FIDL(
library test;

type Duplicates = strict union {
    1: s string;
    2: s int32;
};
)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrDuplicateUnionMemberName);
}

TEST(UnionTests, BadCannotStartAtZero) {
  TestLibrary library;
  library.AddFile("bad/fi-0018.test.fidl");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrOrdinalsMustStartAtOne);
}

TEST(UnionTests, BadDefaultNotAllowed) {
  TestLibrary library(R"FIDL(
library test;

type Foo = strict union {
    1: t int64 = 1;
};

)FIDL");
  ASSERT_ERRORED_TWICE_DURING_COMPILE(library, fidl::ErrUnexpectedTokenOfKind,
                                      fidl::ErrMissingOrdinalBeforeMember);
}

TEST(UnionTests, BadMustBeDense) {
  TestLibrary library(R"FIDL(
library example;

type Example = strict union {
    1: first int64;
    3: third int64;
};

)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrNonDenseOrdinal);
  ASSERT_SUBSTR(library.errors().at(0)->msg.c_str(), "2");
}

TEST(UnionTests, BadNoNullableMembers) {
  TestLibrary library(R"FIDL(
library example;

type Foo = strict union {
  1: bar string:optional;
};

)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrOptionalUnionMember);
}

TEST(UnionTests, BadNoDirectlyRecursiveUnions) {
  TestLibrary library(R"FIDL(
library example;

type Value = strict union {
  1: value Value;
};

)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrIncludeCycle);
}

TEST(UnionTests, GoodEmptyFlexibleUnion) {
  TestLibrary library(R"FIDL(
library example;

type Foo = flexible union {};

)FIDL");
  ASSERT_COMPILED(library);

  auto fidl_union = library.LookupUnion("Foo");
  ASSERT_NOT_NULL(fidl_union);
  ASSERT_EQ(fidl_union->members.size(), 0);
}

TEST(UnionTests, GoodOnlyReservedFlexibleUnion) {
  TestLibrary library(R"FIDL(
library example;

type Foo = flexible union {
  1: reserved;
};

)FIDL");
  ASSERT_COMPILED(library);

  auto fidl_union = library.LookupUnion("Foo");
  ASSERT_NOT_NULL(fidl_union);

  ASSERT_EQ(fidl_union->members.size(), 1);
  auto& member0 = fidl_union->members[0];
  EXPECT_EQ(member0.ordinal->value, 1);
  EXPECT_NULL(member0.maybe_used);
}

TEST(UnionTests, BadEmptyStrictUnion) {
  TestLibrary library(R"FIDL(
library example;

type Foo = strict union {};

)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrStrictUnionMustHaveNonReservedMember);
}

TEST(UnionTests, BadOnlyReservedStrictUnion) {
  TestLibrary library(R"FIDL(
library example;

type Foo = strict union {
  2: reserved;
  1: reserved;
};

)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrStrictUnionMustHaveNonReservedMember);
}

TEST(UnionTests, GoodErrorSyntaxExplicitOrdinals) {
  TestLibrary library(R"FIDL(library example;
protocol Example {
    Method() -> (struct {}) error int32;
};
)FIDL");
  ASSERT_COMPILED(library);
  const fidl::flat::Union* error_union = library.LookupUnion("Example_Method_Result");
  ASSERT_NOT_NULL(error_union);
  ASSERT_EQ(error_union->members.front().ordinal->value, 1);
  ASSERT_EQ(error_union->members.back().ordinal->value, 2);
}

TEST(UnionTests, BadNoSelector) {
  TestLibrary library(R"FIDL(
library example;

type Foo = strict union {
  @selector("v2") 1: v string;
};

)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrInvalidAttributePlacement);
}

}  // namespace
