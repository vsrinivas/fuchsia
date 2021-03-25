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
  ASSERT_COMPILED_AND_CONVERT(library);
}

TEST(TableTests, ReservedFields) {
  TestLibrary library(R"FIDL(
library fidl.test.tables;

table Foo {
    1: reserved;
};
)FIDL");
  ASSERT_COMPILED_AND_CONVERT(library);
}

TEST(TableTests, ReservedAndPopulatedFields) {
  TestLibrary library(R"FIDL(
library fidl.test.tables;

table Foo {
    1: int64 x;
    2: reserved;
};
)FIDL");
  ASSERT_COMPILED_AND_CONVERT(library);
}

TEST(TableTests, ManyReservedFields) {
  TestLibrary library("test.fidl", R"FIDL(
library fidl.test.tables;

table Foo {
    1: reserved;
    2: reserved;
    3: reserved;
};
)FIDL");
  ASSERT_COMPILED_AND_CONVERT(library);
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
  ASSERT_COMPILED_AND_CONVERT(library);
}

TEST(TableTests, AllowEmptyTables) {
  TestLibrary library(R"FIDL(
library fidl.test.tables;

table Foo {
};
)FIDL");
  ASSERT_COMPILED_AND_CONVERT(library);
}

TEST(TableTests, MissingOrdinalsOld) {
  TestLibrary library(R"FIDL(
library fidl.test.tables;

table Foo {
    int64 x;
};
)FIDL");
  ASSERT_ERRORED(library, fidl::ErrExpectedOrdinalOrCloseBrace);
}

TEST(TableTests, MissingOrdinals) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library(R"FIDL(
library fidl.test.tables;

type Foo = table {
    x int64;
};
)FIDL",
                      std::move(experimental_flags));
  ASSERT_ERRORED(library, fidl::ErrMissingOrdinalBeforeType)
}

TEST(TableTests, DuplicateFieldNamesOld) {
  TestLibrary library(R"FIDL(
library fidl.test.tables;

table Foo {
    1: string field;
    2: uint32 field;
};
)FIDL");
  ASSERT_ERRORED(library, fidl::ErrDuplicateTableFieldName);
}

TEST(TableTests, DuplicateFieldNames) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library(R"FIDL(
library fidl.test.tables;

type Foo = table {
    1: field string;
    2: field uint32;
};
)FIDL",
                      std::move(experimental_flags));
  ASSERT_ERRORED(library, fidl::ErrDuplicateTableFieldName);
}

TEST(TableTests, DuplicateOrdinalsOld) {
  TestLibrary library(R"FIDL(
library fidl.test.tables;

table Foo {
    1: string foo;
    1: uint32 bar;
};
)FIDL");
  ASSERT_ERRORED(library, fidl::ErrDuplicateTableFieldOrdinal);
}

TEST(TableTests, DuplicateOrdinals) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library(R"FIDL(
library fidl.test.tables;

type Foo = table {
    1: foo string;
    1: bar uint32;
};
)FIDL",
                      std::move(experimental_flags));
  ASSERT_ERRORED(library, fidl::ErrDuplicateTableFieldOrdinal);
}

// TODO(fxbug.dev/72924): implement attributes
TEST(TableTests, AttributesOnFields) {
  TestLibrary library("test.fidl", R"FIDL(
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

// TODO(fxbug.dev/72924): implement attributes
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

// TODO(fxbug.dev/72924): implement attributes
TEST(TableTests, AttributesOnReserved) {
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
  ASSERT_COMPILED_AND_CONVERT(library);
}

TEST(TableTests, OptionalInStructOld) {
  TestLibrary library(R"FIDL(
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

TEST(TableTests, OptionalInStruct) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library(R"FIDL(
library fidl.test.tables;

type Foo = table {
    1: t int64;
};

type OptionalTableContainer = struct {
    foo Foo:optional;
};
)FIDL",
                      experimental_flags);
  ASSERT_ERRORED(library, fidl::ErrCannotBeNullable);
}

TEST(TableTests, OptionalInUnionOld) {
  TestLibrary library(R"FIDL(
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

TEST(TableTests, OptionalInUnion) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library(R"FIDL(
library fidl.test.tables;

type Foo = table {
    1: t int64;
};

type OptionalTableContainer = union {
    1: foo Foo:optional;
};
)FIDL",
                      std::move(experimental_flags));
  // NOTE(fxbug.dev/72924): same error is used for tables and unions
  ASSERT_ERRORED(library, fidl::ErrNullableOrdinaledMember);
}

TEST(TableTests, TableInTable) {
  TestLibrary library(R"FIDL(
library fidl.test.tables;

table Foo {
    1: int64 t;
};

table Bar {
    1: Foo foo;
};

)FIDL");
  ASSERT_COMPILED_AND_CONVERT(library);
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
  ASSERT_COMPILED_AND_CONVERT(library);
}

TEST(TableTests, OptionalTableMemberOld) {
  TestLibrary library(R"FIDL(
library fidl.test.tables;

table Foo {
    1: int64? t;
};
)FIDL");
  ASSERT_ERRORED(library, fidl::ErrNullableTableMember);
}

TEST(TableTests, OptionalTableMember) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library(R"FIDL(
library fidl.test.tables;

type Foo = table {
    1: t int64:optional;
};
)FIDL",
                      std::move(experimental_flags));
  // NOTE(fxbug.dev/72924): we lose the default specific error in the new syntax.
  ASSERT_ERRORED(library, fidl::ErrNullableOrdinaledMember);
}

TEST(TableTests, DefaultNotAllowedOld) {
  TestLibrary library(R"FIDL(
library fidl.test.tables;

table Foo {
    1: int64 t = 1;
};

)FIDL");
  ASSERT_ERRORED(library, fidl::ErrDefaultsOnTablesNotSupported);
}

TEST(TableTests, DefaultNotAllowed) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library(R"FIDL(
library fidl.test.tables;

type Foo = table {
    1: t int64 = 1;
};

)FIDL",
                      std::move(experimental_flags));
  // NOTE(fxbug.dev/72924): we lose the default specific error in the new syntax.
  EXPECT_FALSE(library.Compile());
  ASSERT_EQ(library.errors().size(), 2);
  ASSERT_ERR(library.errors()[0], fidl::ErrUnexpectedTokenOfKind);
  // TODO(fxbug.dev/72924): this error doesn't make any sense
  ASSERT_ERR(library.errors()[1], fidl::ErrMissingOrdinalBeforeType);
}

TEST(TableTests, MustBeDenseOld) {
  TestLibrary library(R"FIDL(
library example;

table Example {
    1: int64 first;
    3: int64 third;
};

)FIDL");
  ASSERT_ERRORED(library, fidl::ErrNonDenseOrdinal);
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "2");
}

TEST(TableTests, MustBeDense) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library(R"FIDL(
library example;

type Example = table {
    1: first int64;
    3: third int64;
};

)FIDL",
                      std::move(experimental_flags));
  ASSERT_ERRORED(library, fidl::ErrNonDenseOrdinal);
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "2");
}

}  // namespace
