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

static bool Compiles(const std::string& source_code,
                     std::vector<std::string>* out_errors = nullptr) {
  auto library = TestLibrary("test.fidl", source_code);
  const bool success = library.Compile();

  if (out_errors) {
    *out_errors = library.errors();
  }

  return success;
}

bool compiling() {
  BEGIN_TEST;

  // Keywords as field names.
  EXPECT_TRUE(Compiles(R"FIDL(
library test;

struct struct {
    bool field;
};

union Foo {
    1: int64 union;
    2: bool library;
    3: uint32 uint32;
    4: struct member;
};
)FIDL"));

  // Recursion is allowed.
  EXPECT_TRUE(Compiles(R"FIDL(
library test;

union Value {
  1: bool bool_value;
  2: vector<Value?> list_value;
};
)FIDL"));

  // Mutual recursion is allowed.
  EXPECT_TRUE(Compiles(R"FIDL(
library test;

union Foo {
  1: Bar bar;
};

struct Bar {
  Foo? foo;
};
)FIDL"));

  // Specifying flexible is allowed.
  EXPECT_TRUE(Compiles(R"FIDL(
library test;

flexible union Foo {
  1: string bar;
};
)FIDL"));

  // Specifying strict is allowed.
  EXPECT_TRUE(Compiles(R"FIDL(
library test;

strict union Foo {
  1: string bar;
};
)FIDL"));

  END_TEST;
}

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
  ASSERT_STR_STR(library.errors().at(0).c_str(), "missing ordinal before type");

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
  EXPECT_EQ(member0.ordinal->value, 1);
  auto& member1 = fidl_union->members[1];
  EXPECT_NONNULL(member1.maybe_used);
  EXPECT_EQ(member1.ordinal->value, 2);

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
  EXPECT_EQ(member0.ordinal->value, 1);
  auto& member1 = fidl_union->members[1];
  EXPECT_NONNULL(member1.maybe_used);
  EXPECT_EQ(member1.ordinal->value, 2);
  auto& member2 = fidl_union->members[2];
  EXPECT_NULL(member2.maybe_used);
  EXPECT_EQ(member2.ordinal->value, 3);
  auto& member3 = fidl_union->members[3];
  EXPECT_NONNULL(member3.maybe_used);
  EXPECT_EQ(member3.ordinal->value, 4);
  auto& member4 = fidl_union->members[4];
  EXPECT_NULL(member4.maybe_used);
  EXPECT_EQ(member4.ordinal->value, 5);

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
  EXPECT_EQ(member0.ordinal->value, 5);
  auto& member1 = fidl_union->members[1];
  EXPECT_NONNULL(member1.maybe_used);
  EXPECT_EQ(member1.ordinal->value, 2);
  auto& member2 = fidl_union->members[2];
  EXPECT_NULL(member2.maybe_used);
  EXPECT_EQ(member2.ordinal->value, 3);
  auto& member3 = fidl_union->members[3];
  EXPECT_NULL(member3.maybe_used);
  EXPECT_EQ(member3.ordinal->value, 1);
  auto& member4 = fidl_union->members[4];
  EXPECT_NONNULL(member4.maybe_used);
  EXPECT_EQ(member4.ordinal->value, 4);
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
  ASSERT_STR_STR(library.errors().at(0).c_str(), "missing ordinal before type");

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
  ASSERT_STR_STR(errors[0].c_str(), "union members cannot be nullable");

  END_TEST;
}

bool no_directly_recursive_unions() {
  BEGIN_TEST;

  TestLibrary library(R"FIDL(
library example;

union Value {
  1: Value value;
};

)FIDL");
  ASSERT_FALSE(library.Compile());
  auto errors = library.errors();
  ASSERT_EQ(errors.size(), 1);
  ASSERT_STR_STR(errors[0].c_str(), "There is an includes-cycle in declarations");

  END_TEST;
}

bool invalid_empty_unions() {
  BEGIN_TEST;

  TestLibrary library(R"FIDL(
library example;

union Foo {};

)FIDL");
  ASSERT_FALSE(library.Compile());
  auto errors = library.errors();
  ASSERT_EQ(errors.size(), 1);
  ASSERT_STR_STR(errors[0].c_str(), "must have at least one non reserved member");

  END_TEST;
}

bool error_syntax_explicit_ordinals() {
  BEGIN_TEST;
  TestLibrary error_library(R"FIDL(
library example;
protocol Example {
  Method() -> () error int32;
};
)FIDL");
  ASSERT_TRUE(error_library.Compile());
  const fidl::flat::Union* error_union = error_library.LookupUnion("Example_Method_Result");
  ASSERT_NOT_NULL(error_union);
  ASSERT_EQ(error_union->members.front().ordinal->value, 1);
  ASSERT_EQ(error_union->members.back().ordinal->value, 2);
  END_TEST;
}

bool no_selector() {
  BEGIN_TEST;

  TestLibrary library(R"FIDL(
library example;

union Foo {
  [Selector = "v2"] 1: string v;
};
 
)FIDL");
  ASSERT_FALSE(library.Compile());
  auto errors = library.errors();
  ASSERT_EQ(errors.size(), 1);
  ASSERT_STR_STR(errors[0].c_str(), "placement of attribute 'Selector' disallowed here");

  END_TEST;
}

bool deprecated_xunion_error() {
  BEGIN_TEST;

  TestLibrary xunion_library(R"FIDL(
library test;

xunion Foo {
  1: string foo;
};

)FIDL");
  ASSERT_FALSE(xunion_library.Compile());
  auto errors = xunion_library.errors();
  ASSERT_EQ(errors.size(), 1);
  ASSERT_STR_STR(errors[0].c_str(), "xunion is deprecated, please use `flexible union` instead");

  TestLibrary flexible_xunion_library(R"FIDL(
library test;

flexible xunion FlexibleFoo {
  1: string foo;
};

)FIDL");
  ASSERT_FALSE(flexible_xunion_library.Compile());
  errors = flexible_xunion_library.errors();
  ASSERT_EQ(errors.size(), 1);
  ASSERT_STR_STR(errors[0].c_str(), "xunion is deprecated, please use `flexible union` instead");

  TestLibrary strict_xunion_library(R"FIDL(
library test;

strict xunion StrictFoo {
  1: string foo;
};

)FIDL");
  ASSERT_FALSE(strict_xunion_library.Compile());
  errors = strict_xunion_library.errors();
  ASSERT_EQ(errors.size(), 1);
  ASSERT_STR_STR(errors[0].c_str(),
                 "strict xunion is deprecated, please use `strict union` instead");

  END_TEST;
}

}  // namespace

BEGIN_TEST_CASE(union_tests)
RUN_TEST(compiling)
RUN_TEST(must_have_explicit_ordinals)
RUN_TEST(explicit_ordinals)
RUN_TEST(explicit_ordinals_with_reserved)
RUN_TEST(explicit_ordinals_out_of_order)
RUN_TEST(ordinals_must_be_unique)
RUN_TEST(cannot_mix_explicit_and_hashed_ordinals)
RUN_TEST(cannot_start_at_zero)
RUN_TEST(default_not_allowed)
RUN_TEST(must_be_dense)
RUN_TEST(must_have_at_least_one_non_reserved)
RUN_TEST(no_nullable_members_in_unions)
RUN_TEST(no_directly_recursive_unions)
RUN_TEST(invalid_empty_unions)
RUN_TEST(error_syntax_explicit_ordinals)
RUN_TEST(no_selector)
RUN_TEST(deprecated_xunion_error)
END_TEST_CASE(union_tests)
