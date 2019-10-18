// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/flat_ast.h>
#include <fidl/lexer.h>
#include <fidl/parser.h>
#include <fidl/source_file.h>
#include <unittest/unittest.h>

#include "test_library.h"

namespace {

bool Compiles(const std::string& source_code) {
  return TestLibrary("test.fidl", source_code).Compile();
}

bool compiling() {
  BEGIN_TEST;

  // Populated fields.
  EXPECT_TRUE(Compiles(R"FIDL(
library fidl.test.xunions;

xunion Foo {
    int64 i;
};
)FIDL"));

  // Explicit ordinals are invalid.
  EXPECT_FALSE(Compiles(R"FIDL(
library fidl.test.xunions;

xunion Foo {
    1: int64 x;
};
)FIDL"));

  // Attributes on fields.
  EXPECT_TRUE(Compiles(R"FIDL(
library fidl.test.xunions;

xunion Foo {
    [FooAttr="bar"] int64 x;
    [BarAttr] bool bar;
};
)FIDL"));

  // Attributes on xunions.
  EXPECT_TRUE(Compiles(R"FIDL(
library fidl.test.xunions;

[FooAttr="bar"]
xunion Foo {
    int64 x;
    bool please;
};
)FIDL"));

  // Keywords as field names.
  EXPECT_TRUE(Compiles(R"FIDL(
library fidl.test.xunions;

struct struct {
    bool field;
};

xunion Foo {
    int64 xunion;
    bool library;
    uint32 uint32;
    struct member;
};
)FIDL"));

  // Recursion is allowed.
  EXPECT_TRUE(Compiles(R"FIDL(
library fidl.test.xunions;

xunion Value {
  bool bool_value;
  vector<Value?> list_value;
};
)FIDL"));

  // Mutual recursion is allowed.
  EXPECT_TRUE(Compiles(R"FIDL(
library fidl.test.xunions;

xunion Foo {
  Bar bar;
};

struct Bar {
  Foo? foo;
};
)FIDL"));

  END_TEST;
}

bool no_directly_recursive_xunions() {
    BEGIN_TEST;

  TestLibrary library(R"FIDL(
library example;

xunion Value {
  Value value;
};

)FIDL");
  ASSERT_FALSE(library.Compile());
  auto errors = library.errors();
  ASSERT_EQ(errors.size(), 1);
  ASSERT_STR_STR(errors[0].c_str(), "There is an includes-cycle in declarations");

  END_TEST;
}

bool invalid_empty_xunions() {
  BEGIN_TEST;

  TestLibrary library(R"FIDL(
library example;

xunion Foo {};

)FIDL");
  ASSERT_FALSE(library.Compile());
  auto errors = library.errors();
  ASSERT_EQ(errors.size(), 1);
  ASSERT_STR_STR(errors[0].c_str(), "must have at least one member");

  END_TEST;
}

bool union_xunion_same_ordinals() {
  BEGIN_TEST;

  TestLibrary xunion_library(R"FIDL(
library example;

xunion Foo {
  int8 bar;
};

)FIDL");
  ASSERT_TRUE(xunion_library.Compile());

  TestLibrary union_library(R"FIDL(
library example;

union Foo {
  int8 bar;
};

)FIDL");
  ASSERT_TRUE(union_library.Compile());

  const fidl::flat::XUnion* ex_xunion = xunion_library.LookupXUnion("Foo");
  const fidl::flat::Union* ex_union = union_library.LookupUnion("Foo");

  ASSERT_NOT_NULL(ex_xunion);
  ASSERT_NOT_NULL(ex_union);

  ASSERT_EQ(ex_union->members.front().xunion_ordinal->value,
            ex_xunion->members.front().ordinal->value);

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
  ASSERT_EQ(error_union->members.front().xunion_ordinal->value, 1);
  ASSERT_EQ(error_union->members.back().xunion_ordinal->value, 2);
  END_TEST;
}

bool no_nullable_members_in_unions() {
  BEGIN_TEST;

  TestLibrary library(R"FIDL(
library example;

union Foo {
  string? bar;
};

)FIDL");
  ASSERT_FALSE(library.Compile());
  auto errors = library.errors();
  ASSERT_EQ(errors.size(), 1);
  ASSERT_STR_STR(errors[0].c_str(), "Union members cannot be nullable");

  END_TEST;
}

bool no_nullable_members_in_xunions() {
  BEGIN_TEST;

  TestLibrary library(R"FIDL(
library example;

xunion Foo {
  string? bar;
};

)FIDL");
  ASSERT_FALSE(library.Compile());
  auto errors = library.errors();
  ASSERT_EQ(errors.size(), 1);
  ASSERT_STR_STR(errors[0].c_str(), "Extensible union members cannot be nullable");

  END_TEST;
}

}  // namespace

BEGIN_TEST_CASE(xunion_tests)
RUN_TEST(compiling)
RUN_TEST(no_directly_recursive_xunions)
RUN_TEST(invalid_empty_xunions)
RUN_TEST(union_xunion_same_ordinals)
RUN_TEST(error_syntax_explicit_ordinals)
RUN_TEST(no_nullable_members_in_unions)
RUN_TEST(no_nullable_members_in_xunions)
END_TEST_CASE(xunion_tests)
