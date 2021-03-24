// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/flat_ast.h>
#include <fidl/lexer.h>
#include <fidl/parser.h>
#include <fidl/source_file.h>
#include <zxtest/zxtest.h>

#include "error_test.h"
#include "test_library.h"

namespace {

TEST(TableTests, PopulatedFields) {
  TestLibrary library(R"FIDL(
library fidl.test.tables;

table Foo {
    1: int64 x;
};
)FIDL");
  ASSERT_COMPILED(library);
}

TEST(TableTests, ReservedFields) {
  TestLibrary library(R"FIDL(
library fidl.test.tables;

table Foo {
    1: reserved;
};
)FIDL");
  ASSERT_COMPILED(library);
}

TEST(TableTests, ReservedAndPopulatedFields) {
  TestLibrary library(R"FIDL(
library fidl.test.tables;

table Foo {
    1: reserved;
    2: int64 x;
};
)FIDL");
  ASSERT_COMPILED(library);
}

TEST(TableTests, ManyReservedFields) {
  TestLibrary library(R"FIDL(
library fidl.test.tables;

table Foo {
    1: reserved;
    2: reserved;
    3: reserved;
};
)FIDL");
  ASSERT_COMPILED(library);
}

TEST(TableTests, OutOfOrderFields) {
  TestLibrary library(R"FIDL(
library fidl.test.tables;

table Foo {
    3: reserved;
    1: reserved;
    2: reserved;
};
)FIDL");
  ASSERT_COMPILED(library);
}

TEST(TableTests, EmptyTables) {
  TestLibrary library(R"FIDL(
library fidl.test.tables;

table Foo {
};
)FIDL");
  ASSERT_COMPILED(library);
}

TEST(TableTests, OrdinalsRequired) {
  TestLibrary library(R"FIDL(
library fidl.test.tables;

table Foo {
    int64 x;
};
)FIDL");
  ASSERT_ERRORED(library, fidl::ErrExpectedOrdinalOrCloseBrace);
}

TEST(TableTests, DuplicateFieldNames) {
  TestLibrary library(R"FIDL(
library fidl.test.tables;

table Duplicates {
    1: string field;
    2: uint32 field;
};
)FIDL");
  ASSERT_ERRORED(library, fidl::ErrDuplicateTableFieldName);
}

TEST(TableTests, DuplicateOrdinals) {
  auto library = TestLibrary(R"FIDL(
library fidl.test.tables;

table Duplicates {
    1: string foo;
    1: uint32 bar;
};
)FIDL");
  ASSERT_ERRORED(library, fidl::ErrDuplicateTableFieldOrdinal);
}

TEST(TableTests, AttributesOnFields) {
  TestLibrary library(R"FIDL(
library fidl.test.tables;

table Foo {
    [FooAttr="bar"]
    1: int64 x;
    [BarAttr]
    2: bool bar;
};
)FIDL");
  ASSERT_COMPILED(library);
}

TEST(TableTests, AttributesOnTables) {
  TestLibrary library(R"FIDL(
library fidl.test.tables;

[FooAttr="bar"]
table Foo {
    1: int64 x;
    2: bool please;
};
)FIDL");
  ASSERT_COMPILED(library);
}

TEST(TableTests, AttribtuesOnReserved) {
  TestLibrary library(R"FIDL(
library fidl.test.tables;

table Foo {
    [Foo]
    1: reserved;
};
)FIDL");
  ASSERT_ERRORED(library, fidl::ErrCannotAttachAttributesToReservedOrdinals);
}

TEST(TableTests, KeywordsAsFieldNames) {
  TestLibrary library(R"FIDL(
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
)FIDL");
  ASSERT_COMPILED(library);
}

TEST(TableTests, OptionalTablesInStructs) {
  auto library = TestLibrary(R"FIDL(
library fidl.test.tables;

table Foo {
    1: int64 t;
};

struct OptionalTableContainer {
    Foo? foo;
};
)FIDL");
  ASSERT_ERRORED(library, fidl::ErrCannotBeNullable);
}

TEST(TableTests, OptionalTablesInUnions) {
  // Optional tables in (static) unions are invalid.
  auto library = TestLibrary(R"FIDL(
library fidl.test.tables;

table Foo {
    1: int64 t;
};

union OptionalTableContainer {
    1: Foo? foo;
};
)FIDL");
  ASSERT_ERRORED(library, fidl::ErrNullableUnionMember);
}

TEST(TableTests, TablesInTables) {
  TestLibrary library(R"FIDL(
library fidl.test.tables;

table Foo {
    1: int64 t;
};

table Bar {
    1: Foo foo;
};

)FIDL");
  ASSERT_COMPILED(library);
}

TEST(TableTests, TablesInUnions) {
  TestLibrary library(R"FIDL(
library fidl.test.tables;

table Foo {
    1: int64 t;
};

flexible union OptionalTableContainer {
    1: Foo foo;
};

)FIDL");
  ASSERT_COMPILED(library);
}

TEST(TableTests, OptionalTableFields) {
  auto library = TestLibrary(R"FIDL(
library fidl.test.tables;

table Foo {
    1: int64? t;
};
)FIDL");
  ASSERT_ERRORED(library, fidl::ErrNullableTableMember);
}

TEST(TableTests, DefaultNotAllowed) {
  auto library = TestLibrary(R"FIDL(
library fidl.test.tables;

table Foo {
    1: int64 t = 1;
};

)FIDL");
  ASSERT_ERRORED(library, fidl::ErrDefaultsOnTablesNotSupported);
}

TEST(TableTests, MustBeDense) {
  auto library = TestLibrary(R"FIDL(
library example;

table Example {
    1: int64 first;
    3: int64 third;
};

)FIDL");
  ASSERT_ERRORED(library, fidl::ErrNonDenseOrdinal);
  ASSERT_SUBSTR(library.errors().at(0)->msg.c_str(), "2");
}

}  // namespace
