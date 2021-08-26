// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/flat_ast.h>
#include <fidl/lexer.h>
#include <fidl/names.h>
#include <fidl/parser.h>
#include <fidl/source_file.h>
#include <zxtest/zxtest.h>

#include "error_test.h"
#include "test_library.h"

namespace {

TEST(UnionTests, GoodKeywordsAsFieldNames) {
  TestLibrary library(R"FIDL(
library test;

struct struct {
    bool field;
};

union Foo {
    1: int64 union;
    2: bool library;
    3: uint32 uint32;
    4: struct member;
    5: bool reserved;
};
)FIDL");
  ASSERT_COMPILED_AND_CONVERT(library);
}

TEST(UnionTests, GoodRecursiveUnion) {
  TestLibrary library(R"FIDL(
library test;

union Value {
  1: bool bool_value;
  2: vector<Value?> list_value;
};
)FIDL");
  ASSERT_COMPILED_AND_CONVERT(library);
}

TEST(UnionTests, GoodMutuallyRecursive) {
  TestLibrary library(R"FIDL(
library test;

union Foo {
  1: Bar bar;
};

struct Bar {
  Foo? foo;
};
)FIDL");
  ASSERT_COMPILED_AND_CONVERT(library);
}

TEST(UnionTests, GoodFlexibleUnion) {
  TestLibrary library(R"FIDL(
library test;

flexible union Foo {
  1: string bar;
};
)FIDL");
  ASSERT_COMPILED_AND_CONVERT(library);
}

TEST(UnionTests, GoodStrictUnion) {
  TestLibrary library(R"FIDL(
library test;

strict union Foo {
  1: string bar;
};
)FIDL");
  ASSERT_COMPILED_AND_CONVERT(library);
}

TEST(UnionTests, BadMustHaveExplicitOrdinals) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library(R"FIDL(
library test;

type Foo = strict union {
    foo int64;
    bar vector<uint32>:10;
};

)FIDL",
                      std::move(experimental_flags));
  ASSERT_ERRORED_TWICE_DURING_COMPILE(library, fidl::ErrMissingOrdinalBeforeType,
                                      fidl::ErrMissingOrdinalBeforeType);
}

TEST(UnionTests, GoodExplicitOrdinals) {
  TestLibrary library(R"FIDL(
library test;

union Foo {
  1: int64 foo;
  2: vector<uint32>:10 bar;
};
)FIDL");
  ASSERT_COMPILED_AND_CONVERT(library);

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
  TestLibrary library(R"FIDL(
library test;

union Foo {
  1: reserved;
  2: int64 foo;
  3: reserved;
  4: vector<uint32>:10 bar;
  5: reserved;
};
)FIDL");
  ASSERT_COMPILED_AND_CONVERT(library);

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
  TestLibrary library(R"FIDL(
library test;

union Foo {
  5: int64 foo;
  2: vector<uint32>:10 bar;
  3: reserved;
  1: reserved;
  4: uint32 baz;
};
)FIDL");
  ASSERT_COMPILED_AND_CONVERT(library);

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
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library(R"FIDL(
library test;

type Foo = strict union {
  -1: uint32 foo;
};
)FIDL",
                      std::move(experimental_flags));
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrOrdinalOutOfBound);
}

TEST(UnionTests, BadOrdinalsMustBeUnique) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library(R"FIDL(
library test;

type Foo = strict union {
  1: reserved;
  1: uint64 x;
};
)FIDL",
                      std::move(experimental_flags));
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrDuplicateUnionMemberOrdinal);
}

TEST(UnionTests, BadMemberNamesMustBeUnique) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library(R"FIDL(
library test;

type Duplicates = strict union {
    1: s string;
    2: s int32;
};
)FIDL",
                      std::move(experimental_flags));
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrDuplicateUnionMemberName);
}

TEST(UnionTests, BadCannotStartAtZero) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library(R"FIDL(
library test;

type Foo = strict union {
  0: foo uint32;
  1: bar uint64;
};
)FIDL",
                      std::move(experimental_flags));
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrOrdinalsMustStartAtOne);
}

// NOTE(fxbug.dev/72924): we lose the default specific error in the new syntax.
TEST(UnionTests, BadDefaultNotAllowed) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library(R"FIDL(
library test;

type Foo = strict union {
    1: t int64 = 1;
};

)FIDL",
                      std::move(experimental_flags));
  // NOTE(fxbug.dev/72924): we lose the default specific error in the new syntax.
  // TODO(fxbug.dev/72924): the second error doesn't make any sense
  ASSERT_ERRORED_TWICE_DURING_COMPILE(library, fidl::ErrUnexpectedTokenOfKind,
                                      fidl::ErrMissingOrdinalBeforeType);
}

TEST(UnionTests, BadMustBeDense) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library(R"FIDL(
library example;

type Example = strict union {
    1: first int64;
    3: third int64;
};

)FIDL",
                      std::move(experimental_flags));
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrNonDenseOrdinal);
  ASSERT_SUBSTR(library.errors().at(0)->msg.c_str(), "2");
}

TEST(UnionTests, BadMustHaveNonReservedMember) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library(R"FIDL(
library example;

type Foo = strict union {
  2: reserved;
  1: reserved;
};

)FIDL",
                      std::move(experimental_flags));
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrMustHaveNonReservedMember);
}

TEST(UnionTests, BadNoNullableMembers) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library(R"FIDL(
library example;

type Foo = strict union {
  1: bar string:optional;
};

)FIDL",
                      std::move(experimental_flags));
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrNullableUnionMember);
}

TEST(UnionTests, BadNoDirectlyRecursiveUnions) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library(R"FIDL(
library example;

type Value = strict union {
  1: value Value;
};

)FIDL",
                      std::move(experimental_flags));
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrIncludeCycle);
}

TEST(UnionTests, BadEmptyUnion) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library(R"FIDL(
library example;

type Foo = strict union {};

)FIDL",
                      std::move(experimental_flags));
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrMustHaveNonReservedMember);
}

TEST(UnionTests, GoodErrorSyntaxExplicitOrdinals) {
  TestLibrary library(R"FIDL(
library example;
protocol Example {
  Method() -> () error int32;
};
)FIDL");
  ASSERT_COMPILED_AND_CONVERT(library);
  const fidl::flat::Union* error_union = library.LookupUnion("Example_Method_Result");
  ASSERT_NOT_NULL(error_union);
  ASSERT_EQ(error_union->members.front().ordinal->value, 1);
  ASSERT_EQ(error_union->members.back().ordinal->value, 2);
}

TEST(UnionTests, BadNoSelector) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library(R"FIDL(
library example;

type Foo = strict union {
  @selector("v2") 1: v string;
};

)FIDL",
                      experimental_flags);
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrInvalidAttributePlacement);
}

// TODO(fxbug.dev/70247): as we clean up the migration, it will probably have
// been long enough that we can remove this error and the special handling for
// "xunion"
TEST(UnionTests, BadDeprecatedXUnionError) {
  {
    TestLibrary library(R"FIDL(
  library test;

  xunion Foo {
    1: string foo;
  };

  )FIDL");
    ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrXunionDeprecated);
  }

  {
    TestLibrary library(R"FIDL(
  library test;

  flexible xunion FlexibleFoo {
    1: string foo;
  };

  )FIDL");
    ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrXunionDeprecated);
  }

  {
    TestLibrary library(R"FIDL(
  library test;

  strict xunion StrictFoo {
    1: string foo;
  };

  )FIDL");
    ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrStrictXunionDeprecated);
  }
}

}  // namespace
