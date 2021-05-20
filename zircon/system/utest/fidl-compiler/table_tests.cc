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

TEST(TableTests, GoodPopulatedFields) {
  TestLibrary library(R"FIDL(
library fidl.test.tables;

table Foo {
    1: int64 x;
};
)FIDL");
  ASSERT_COMPILED_AND_CONVERT(library);
}

TEST(TableTests, GoodReservedFields) {
  TestLibrary library(R"FIDL(
library fidl.test.tables;

table Foo {
    1: reserved;
};
)FIDL");
  ASSERT_COMPILED_AND_CONVERT(library);
}

TEST(TableTests, GoodReservedAndPopulatedFields) {
  TestLibrary library(R"FIDL(
library fidl.test.tables;

table Foo {
    1: int64 x;
    2: reserved;
};
)FIDL");
  ASSERT_COMPILED_AND_CONVERT(library);
}

TEST(TableTests, GoodManyReservedFields) {
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

TEST(TableTests, GoodOutOfOrderFields) {
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

TEST(TableTests, GoodAllowEmptyTables) {
  TestLibrary library(R"FIDL(
library fidl.test.tables;

table Foo {
};
)FIDL");
  ASSERT_COMPILED_AND_CONVERT(library);
}

TEST(TableTests, BadMissingOrdinalsOld) {
  TestLibrary library(R"FIDL(
library fidl.test.tables;

table Foo {
    int64 x;
};
)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrExpectedOrdinalOrCloseBrace);
}

TEST(TableTests, BadMissingOrdinals) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library(R"FIDL(
library fidl.test.tables;

type Foo = table {
    x int64;
};
)FIDL",
                      std::move(experimental_flags));
  // NOTE(fxbug.dev/72924): difference in parser implementation, the old syntax
  // checks for this case specifically.
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrMissingOrdinalBeforeType)
}

TEST(TableTests, BadDuplicateFieldNamesOld) {
  TestLibrary library(R"FIDL(
library fidl.test.tables;

table Foo {
    1: string field;
    2: uint32 field;
};
)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrDuplicateTableFieldName);
}

TEST(TableTests, BadDuplicateFieldNames) {
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
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrDuplicateTableFieldName);
}

TEST(TableTests, BadDuplicateOrdinalsOld) {
  TestLibrary library(R"FIDL(
library fidl.test.tables;

table Foo {
    1: string foo;
    1: uint32 bar;
};
)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrDuplicateTableFieldOrdinal);
}

TEST(TableTests, BadDuplicateOrdinals) {
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
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrDuplicateTableFieldOrdinal);
}

TEST(TableTests, GoodAttributesOnFields) {
  TestLibrary library("test.fidl", R"FIDL(
library fidl.test.tables;

table Foo {
    [FooAttr="bar"]
    1: int64 x;
    [BarAttr]
    2: bool bar;
};
)FIDL");
  ASSERT_COMPILED_AND_CONVERT(library);
}

TEST(TableTests, GoodAttributesOnTables) {
  TestLibrary library(R"FIDL(
library fidl.test.tables;

[FooAttr="bar"]
table Foo {
    1: int64 x;
    2: bool please;
};
)FIDL");
  ASSERT_COMPILED_AND_CONVERT(library);
}

TEST(TableTests, GoodKeywordsAsFieldNames) {
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
    5: bool reserved;
};
)FIDL");
  ASSERT_COMPILED_AND_CONVERT(library);
}

TEST(TableTests, BadOptionalInStructOld) {
  TestLibrary library(R"FIDL(
library fidl.test.tables;

table Foo {
    1: int64 t;
};

struct OptionalTableContainer {
    Foo? foo;
};
)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrCannotBeNullable);
}

TEST(TableTests, BadOptionalInStruct) {
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
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrCannotBeNullable);
}

TEST(TableTests, BadTableMultipleConstraints) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library(R"FIDL(
library fidl.test.tables;

type Foo = table {
    1: t int64;
};

type OptionalTableContainer = struct {
    foo Foo:<optional, foo, bar>;
};
)FIDL",
                      experimental_flags);
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrTooManyConstraints);
}

TEST(TableTests, BadOptionalInUnionOld) {
  TestLibrary library(R"FIDL(
library fidl.test.tables;

table Foo {
    1: int64 t;
};

union OptionalTableContainer {
    1: Foo? foo;
};
)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrNullableUnionMember);
}

TEST(TableTests, BadOptionalInUnion) {
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
  // NOTE(fxbug.dev/72924): this pair of tests aims to document a behavior
  // difference between the old and new syntaxes: in the old, we check for
  // ErrNullableTableMember first before determining if the type itself can be
  // nullable. This is not the case in the new syntax (we need to compile the
  // type first to determine if it is nullable). The nullable union member
  // error is tested in UnionTests.BadNoNullableMembers
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrCannotBeNullable);
}

TEST(TableTests, GoodTableInTable) {
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

TEST(TableTests, GoodTablesInUnions) {
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

TEST(TableTests, BadOptionalTableMemberOld) {
  TestLibrary library(R"FIDL(
library fidl.test.tables;

table Foo {
    1: string? t;
};
)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrNullableTableMember);
}

TEST(TableTests, BadOptionalTableMember) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library(R"FIDL(
library fidl.test.tables;

type Foo = table {
    1: t string:optional;
};
)FIDL",
                      std::move(experimental_flags));
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrNullableTableMember);
}

TEST(TableTests, BadOptionalNonNullableTableMemberOld) {
  TestLibrary library(R"FIDL(
library fidl.test.tables;

table Foo {
    1: int64? t;
};
)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrNullableTableMember);
}

TEST(TableTests, BadOptionalNonNullableTableMember) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library(R"FIDL(
library fidl.test.tables;

type Foo = table {
    1: t int64:optional;
};
)FIDL",
                      std::move(experimental_flags));
  // NOTE(fxbug.dev/72924): this pair of tests aims to document a behavior
  // difference between the old and new syntaxes: in the old, we check for
  // ErrNullableTableMember first before determining if the type itself can be
  // nullable. This is not the case in the new syntax (we need to compile the
  // type first to determine if it is nullable).
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrCannotBeNullable);
}

TEST(TableTests, BadDefaultNotAllowedOld) {
  TestLibrary library(R"FIDL(
library fidl.test.tables;

table Foo {
    1: int64 t = 1;
};

)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrDefaultsOnTablesNotSupported);
}

TEST(TableTests, BadDefaultNotAllowed) {
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
  // TODO(fxbug.dev/72924): the second error doesn't make any sense
  ASSERT_ERRORED_TWICE_DURING_COMPILE(library, fidl::ErrUnexpectedTokenOfKind,
                                      fidl::ErrMissingOrdinalBeforeType);
}

TEST(TableTests, BadMustBeDenseOld) {
  TestLibrary library(R"FIDL(
library example;

table Example {
    1: int64 first;
    3: int64 third;
};

)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrNonDenseOrdinal);
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "2");
}

TEST(TableTests, BadMustBeDense) {
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
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrNonDenseOrdinal);
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "2");
}

}  // namespace
