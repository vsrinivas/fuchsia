// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/flat_ast.h>
#include <fidl/lexer.h>
#include <fidl/names.h>
#include <fidl/parser.h>
#include <fidl/source_file.h>
#include <unittest/unittest.h>

#include "test_library.h"

namespace {

bool must_have_explicit_ordinals() {
  BEGIN_TEST;

  TestLibrary library(R"FIDL(
library test;

union Foo {
    int64 foo;
    vector<uint32>:10 bar;
};

)FIDL");
  ASSERT_FALSE(library.Compile());
  ASSERT_EQ(library.errors().size(), 1u);
  ASSERT_STR_STR(library.errors().at(0).c_str(), "expecting NumericLiteral");

  END_TEST;
}

bool explicit_ordinals() {
  BEGIN_TEST;

  TestLibrary library(R"FIDL(
library test;

union Foo {
  1: int64 foo;
  2: vector<uint32>:10 bar;
};
)FIDL");
  ASSERT_TRUE(library.Compile());

  auto fidl_union = library.LookupUnion("Foo");
  ASSERT_NONNULL(fidl_union);

  ASSERT_EQ(fidl_union->members.size(), 2);
  auto& member0 = fidl_union->members[0];
  EXPECT_NONNULL(member0.maybe_used);
  EXPECT_EQ(member0.xunion_ordinal->value, 1);
  auto& member1 = fidl_union->members[1];
  EXPECT_NONNULL(member1.maybe_used);
  EXPECT_EQ(member1.xunion_ordinal->value, 2);

  END_TEST;
}

bool explicit_ordinals_with_reserved() {
  BEGIN_TEST;

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
  ASSERT_TRUE(library.Compile());

  auto fidl_union = library.LookupUnion("Foo");
  ASSERT_NONNULL(fidl_union);

  ASSERT_EQ(fidl_union->members.size(), 5);
  auto& member0 = fidl_union->members[0];
  EXPECT_NULL(member0.maybe_used);
  EXPECT_EQ(member0.xunion_ordinal->value, 1);
  auto& member1 = fidl_union->members[1];
  EXPECT_NONNULL(member1.maybe_used);
  EXPECT_EQ(member1.xunion_ordinal->value, 2);
  auto& member2 = fidl_union->members[2];
  EXPECT_NULL(member2.maybe_used);
  EXPECT_EQ(member2.xunion_ordinal->value, 3);
  auto& member3 = fidl_union->members[3];
  EXPECT_NONNULL(member3.maybe_used);
  EXPECT_EQ(member3.xunion_ordinal->value, 4);
  auto& member4 = fidl_union->members[4];
  EXPECT_NULL(member4.maybe_used);
  EXPECT_EQ(member4.xunion_ordinal->value, 5);

  END_TEST;
}

bool explicit_ordinals_out_of_order() {
  BEGIN_TEST;

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
  ASSERT_TRUE(library.Compile());

  auto fidl_union = library.LookupUnion("Foo");
  ASSERT_NONNULL(fidl_union);

  ASSERT_EQ(fidl_union->members.size(), 5);
  auto& member0 = fidl_union->members[0];
  EXPECT_NONNULL(member0.maybe_used);
  EXPECT_EQ(member0.xunion_ordinal->value, 5);
  auto& member1 = fidl_union->members[1];
  EXPECT_NONNULL(member1.maybe_used);
  EXPECT_EQ(member1.xunion_ordinal->value, 2);
  auto& member2 = fidl_union->members[2];
  EXPECT_NULL(member2.maybe_used);
  EXPECT_EQ(member2.xunion_ordinal->value, 3);
  auto& member3 = fidl_union->members[3];
  EXPECT_NULL(member3.maybe_used);
  EXPECT_EQ(member3.xunion_ordinal->value, 1);
  auto& member4 = fidl_union->members[4];
  EXPECT_NONNULL(member4.maybe_used);
  EXPECT_EQ(member4.xunion_ordinal->value, 4);
  END_TEST;
}

bool ordinals_must_be_unique() {
  BEGIN_TEST;

  TestLibrary library(R"FIDL(
library test;

union Foo {
  1: reserved;
  1: uint64 x;
};
)FIDL");
  ASSERT_FALSE(library.Compile());
  ASSERT_EQ(library.errors().size(), 1u);
  ASSERT_STR_STR(library.errors().at(0).c_str(), "Multiple union fields with the same ordinal");

  END_TEST;
}

bool cannot_mix_explicit_and_hashed_ordinals() {
  BEGIN_TEST;

  TestLibrary library(R"FIDL(
library test;

union Foo {
    1: int64 foo;
    vector<uint32>:10 bar;
};

)FIDL");
  ASSERT_FALSE(library.Compile());
  ASSERT_EQ(library.errors().size(), 1u);
  ASSERT_STR_STR(library.errors().at(0).c_str(), "expecting NumericLiteral");

  END_TEST;
}

bool cannot_start_at_zero() {
  BEGIN_TEST;

  TestLibrary library(R"FIDL(
library test;

union Foo {
  0: uint32 foo;
  1: uint64 bar;
};
)FIDL");
  ASSERT_FALSE(library.Compile());
  ASSERT_EQ(library.errors().size(), 1u);
  ASSERT_STR_STR(library.errors().at(0).c_str(), "ordinals must start at 1");

  END_TEST;
}

bool default_not_allowed() {
  BEGIN_TEST;

  TestLibrary library(R"FIDL(
library test;

union Foo {
    1: int64 t = 1;
};

)FIDL");
  ASSERT_FALSE(library.Compile());
  ASSERT_EQ(library.errors().size(), 1u);
  ASSERT_STR_STR(library.errors().at(0).c_str(), "unexpected token Equal");

  END_TEST;
}

bool must_be_dense() {
  BEGIN_TEST;

  TestLibrary library(R"FIDL(
library example;

union Example {
    1: int64 first;
    3: int64 third;
};

)FIDL");
  ASSERT_FALSE(library.Compile());
  ASSERT_EQ(library.errors().size(), 1u);
  ASSERT_STR_STR(library.errors().at(0).c_str(),
                 "missing ordinal 2 (ordinals must be dense); consider marking it reserved");

  END_TEST;
}

bool must_have_at_least_one_non_reserved() {
  BEGIN_TEST;

  TestLibrary library(R"FIDL(
library example;

union Foo {
  2: reserved;
  1: reserved;
};

)FIDL");
  ASSERT_FALSE(library.Compile());
  ASSERT_EQ(library.errors().size(), 1u);
  ASSERT_STR_STR(library.errors().at(0).c_str(), "must have at least one non reserved member");

  END_TEST;
}

bool no_nullable_members_in_unions() {
  BEGIN_TEST;

  TestLibrary library(R"FIDL(
library example;

union Foo {
  1: string? bar;
};

)FIDL");
  ASSERT_FALSE(library.Compile());
  auto errors = library.errors();
  ASSERT_EQ(errors.size(), 1);
  ASSERT_STR_STR(errors[0].c_str(), "Union members cannot be nullable");

  END_TEST;
}

bool ordinal_cutoff() {
  BEGIN_TEST;

  TestLibrary below_cutoff(R"FIDL(
library example;

union Foo {
  512: string bar;
};

)FIDL");
  ASSERT_FALSE(below_cutoff.Compile());
  auto errors = below_cutoff.errors();
  ASSERT_EQ(errors.size(), 1);
  // the ordinal cutoff is enforced before checking for a dense ordinal space.
  ASSERT_STR_STR(errors[0].c_str(), "missing ordinal 1 (ordinals must be dense)");

  TestLibrary above_cutoff(R"FIDL(
library example;

union Foo {
  513: string bar;
};

)FIDL");
  ASSERT_FALSE(above_cutoff.Compile());
  errors = above_cutoff.errors();
  ASSERT_EQ(errors.size(), 1);
  // the ordinal cutoff is enforced before checking for a dense ordinal space.
  ASSERT_STR_STR(errors[0].c_str(), "explicit union ordinal must be <= 512");

  END_TEST;
}

}  // namespace

BEGIN_TEST_CASE(union_tests)
RUN_TEST(must_have_explicit_ordinals)
RUN_TEST(explicit_ordinals)
RUN_TEST(explicit_ordinals_with_reserved)
RUN_TEST(explicit_ordinals_out_of_order)
RUN_TEST(ordinals_must_be_unique)
RUN_TEST(cannot_mix_explicit_and_hashed_ordinals)
RUN_TEST(cannot_start_at_zero)
RUN_TEST(default_not_allowed)
RUN_TEST(must_be_dense)
RUN_TEST(no_nullable_members_in_unions)
RUN_TEST(ordinal_cutoff)
END_TEST_CASE(union_tests)
