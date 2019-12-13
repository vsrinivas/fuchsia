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

static bool Compiles(const std::string& source_code,
                     std::vector<std::string>* out_errors = nullptr) {
  auto library = TestLibrary("test.fidl", source_code);
  const bool success = library.Compile();

  if (out_errors) {
    *out_errors = library.errors();
  }

  return success;
}

static bool compiling(void) {
  BEGIN_TEST;

  std::vector<std::string> errors;

  // Populated fields.
  EXPECT_TRUE(Compiles(R"FIDL(
library fidl.test.tables;

table Foo {
    1: int64 x;
};
)FIDL"));

  // Reserved fields.
  EXPECT_TRUE(Compiles(R"FIDL(
library fidl.test.tables;

table Foo {
    1: reserved;
};
)FIDL"));

  // Reserved and populated fields.
  EXPECT_TRUE(Compiles(R"FIDL(
library fidl.test.tables;

table Foo {
    1: reserved;
    2: int64 x;
};
)FIDL"));

  EXPECT_TRUE(Compiles(R"FIDL(
library fidl.test.tables;

table Foo {
    1: int64 x;
    2: reserved;
};
)FIDL"));

  // Many reserved fields.
  EXPECT_TRUE(Compiles(R"FIDL(
library fidl.test.tables;

table Foo {
    1: reserved;
    2: reserved;
    3: reserved;
};
)FIDL"));

  // Out of order fields.
  EXPECT_TRUE(Compiles(R"FIDL(
library fidl.test.tables;

table Foo {
    3: reserved;
    1: reserved;
    2: reserved;
};
)FIDL"));

  // Duplicate ordinals.
  EXPECT_FALSE(Compiles(R"FIDL(
library fidl.test.tables;

table Foo {
    1: reserved;
    1: reserved;
};
)FIDL"));

  // Missing ordinals.
  EXPECT_FALSE(Compiles(R"FIDL(
library fidl.test.tables;

table Foo {
    1: reserved;
    3: reserved;
};
)FIDL"));

  // Empty tables are allowed.
  EXPECT_TRUE(Compiles(R"FIDL(
library fidl.test.tables;

table Foo {
};
)FIDL"));

  // Ordinals required.
  EXPECT_FALSE(Compiles(R"FIDL(
library fidl.test.tables;

table Foo {
    int64 x;
};
)FIDL",
                        &errors));

  EXPECT_EQ(errors.size(), 1u);
  ASSERT_STR_STR(errors.at(0).c_str(), "Expected one of ordinal or '}'");

  // Attributes on fields.
  EXPECT_TRUE(Compiles(R"FIDL(
library fidl.test.tables;

table Foo {
    [FooAttr="bar"]
    1: int64 x;
    [BarAttr]
    2: bool bar;
};
)FIDL"));

  // Attributes on tables.
  EXPECT_TRUE(Compiles(R"FIDL(
library fidl.test.tables;

[FooAttr="bar"]
table Foo {
    1: int64 x;
    2: bool please;
};
)FIDL"));

  // Attributes on reserved.
  EXPECT_FALSE(Compiles(R"FIDL(
library fidl.test.tables;

table Foo {
    [Foo]
    1: reserved;
};
)FIDL"));

  // Keywords as field names.
  EXPECT_TRUE(Compiles(R"FIDL(
library fidl.test.tables;

struct struct {
    bool field;
};

table Foo {
    1: int64 table;
    2: bool library;
    3: uint32 uint32;
    4: struct member;
};
)FIDL"));

  // Optional tables in structs are invalid.
  EXPECT_FALSE(Compiles(R"FIDL(
library fidl.test.tables;

table Foo {
    1: int64 t;
};

struct OptionalTableContainer {
    Foo? foo;
};

)FIDL",
                        &errors));
  EXPECT_EQ(errors.size(), 1u);
  ASSERT_STR_STR(errors.at(0).c_str(), "cannot be nullable");

  // Optional tables in (static) unions are invalid.
  EXPECT_FALSE(Compiles(R"FIDL(
library fidl.test.tables;

table Foo {
    1: int64 t;
};

union OptionalTableContainer {
    1: Foo? foo;
};

)FIDL",
                        &errors));
  EXPECT_EQ(errors.size(), 1u);
  ASSERT_STR_STR(errors.at(0).c_str(), "cannot be nullable");

  // Tables in tables are valid.
  EXPECT_TRUE(Compiles(R"FIDL(
library fidl.test.tables;

table Foo {
    1: int64 t;
};

table Bar {
    1: Foo foo;
};

)FIDL"));

  // Tables in xunions are valid.
  EXPECT_TRUE(Compiles(R"FIDL(
library fidl.test.tables;

table Foo {
    1: int64 t;
};

xunion OptionalTableContainer {
    1: Foo foo;
};

)FIDL"));

  END_TEST;
}

bool default_not_allowed() {
  BEGIN_TEST;

  std::vector<std::string> errors;
  EXPECT_FALSE(Compiles(R"FIDL(
library fidl.test.tables;

table Foo {
    1: int64 t = 1;
};

)FIDL",
                        &errors));
  ASSERT_EQ(errors.size(), 1u);
  ASSERT_STR_STR(errors.at(0).c_str(), "Defaults on tables are not yet supported.");

  END_TEST;
}

bool must_be_dense() {
  BEGIN_TEST;

  std::vector<std::string> errors;
  EXPECT_FALSE(Compiles(R"FIDL(
library example;

table Example {
    1: int64 first;
    3: int64 third;
};

)FIDL",
                        &errors));
  ASSERT_EQ(errors.size(), 1u);
  ASSERT_STR_STR(errors.at(0).c_str(),
                 "missing ordinal 2 (ordinals must be dense); consider marking it reserved");

  END_TEST;
}

}  // namespace

BEGIN_TEST_CASE(table_tests)
RUN_TEST(compiling)
RUN_TEST(default_not_allowed)
RUN_TEST(must_be_dense)
END_TEST_CASE(table_tests)
