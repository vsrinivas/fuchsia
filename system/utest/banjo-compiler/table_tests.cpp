// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <unittest/unittest.h>

#include <banjo/flat_ast.h>
#include <banjo/lexer.h>
#include <banjo/parser.h>
#include <banjo/source_file.h>

#include "test_library.h"

namespace {

static bool Compiles(const std::string& source_code) {
    return TestLibrary("test.banjo", source_code).Compile();
}

static bool compiling(void) {
    BEGIN_TEST;

    // Populated fields.
    EXPECT_TRUE(Compiles(R"BANJO(
library banjo.test.tables;

table Foo {
    1: int64 x;
};
)BANJO"));

    // Reserved fields.
    EXPECT_TRUE(Compiles(R"BANJO(
library banjo.test.tables;

table Foo {
    1: reserved;
};
)BANJO"));

    // Reserved and populated fields.
    EXPECT_TRUE(Compiles(R"BANJO(
library banjo.test.tables;

table Foo {
    1: reserved;
    2: int64 x;
};
)BANJO"));

    EXPECT_TRUE(Compiles(R"BANJO(
library banjo.test.tables;

table Foo {
    1: int64 x;
    2: reserved;
};
)BANJO"));

    // Many reserved fields.
    EXPECT_TRUE(Compiles(R"BANJO(
library banjo.test.tables;

table Foo {
    1: reserved;
    2: reserved;
    3: reserved;
};
)BANJO"));

    // Out of order fields.
    EXPECT_TRUE(Compiles(R"BANJO(
library banjo.test.tables;

table Foo {
    3: reserved;
    1: reserved;
    2: reserved;
};
)BANJO"));

    // Duplicate ordinals.
    EXPECT_FALSE(Compiles(R"BANJO(
library banjo.test.tables;

table Foo {
    1: reserved;
    1: reserved;
};
)BANJO"));

    // Missing ordinals.
    EXPECT_FALSE(Compiles(R"BANJO(
library banjo.test.tables;

table Foo {
    1: reserved;
    3: reserved;
};
)BANJO"));

    // Empty tables not allowed.
    EXPECT_FALSE(Compiles(R"BANJO(
library banjo.test.tables;

table Foo {
};
)BANJO"));

    // Ordinals required.
    EXPECT_FALSE(Compiles(R"BANJO(
library banjo.test.tables;

table Foo {
    int64 x;
};
)BANJO"));

    // Attributes on fields.
    EXPECT_TRUE(Compiles(R"BANJO(
library banjo.test.tables;

table Foo {
    [FooAttr="bar"]
    1: int64 x;
    [BarAttr]
    2: bool bar;
};
)BANJO"));

    // Attributes on tables.
    EXPECT_TRUE(Compiles(R"BANJO(
library banjo.test.tables;

[FooAttr="bar"]
table Foo {
    1: int64 x;
    2: bool please;
};
)BANJO"));

    // Attributes on reserved.
    EXPECT_FALSE(Compiles(R"BANJO(
library banjo.test.tables;

table Foo {
    [Foo]
    1: reserved;
};
)BANJO"));

    END_TEST;
}

} // namespace

BEGIN_TEST_CASE(table_tests);
RUN_TEST(compiling);
END_TEST_CASE(table_tests);
