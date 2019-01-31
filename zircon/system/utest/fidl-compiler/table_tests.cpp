// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <unittest/unittest.h>

#include <fidl/flat_ast.h>
#include <fidl/lexer.h>
#include <fidl/parser.h>
#include <fidl/source_file.h>

#include "test_library.h"

namespace {

static bool Compiles(const std::string& source_code) {
    return TestLibrary("test.fidl", source_code).Compile();
}

static bool compiling(void) {
    BEGIN_TEST;

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
)FIDL"));

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

    END_TEST;
}

} // namespace

BEGIN_TEST_CASE(table_tests);
RUN_TEST(compiling);
END_TEST_CASE(table_tests);
