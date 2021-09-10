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
  TestLibrary library(R"FIDL(library fidl.test.tables;

type Foo = table {
    1: x int64;
};
)FIDL");
  ASSERT_COMPILED(library);
}

TEST(TableTests, GoodReservedFields) {
  TestLibrary library(R"FIDL(library fidl.test.tables;

type Foo = table {
    1: reserved;
};
)FIDL");
  ASSERT_COMPILED(library);
}

TEST(TableTests, GoodReservedAndPopulatedFields) {
  TestLibrary library(R"FIDL(library fidl.test.tables;

type Foo = table {
    1: x int64;
    2: reserved;
};
)FIDL");
  ASSERT_COMPILED(library);
}

TEST(TableTests, GoodManyReservedFields) {
  TestLibrary library("test.fidl", R"FIDL(library fidl.test.tables;

type Foo = table {
    1: reserved;
    2: reserved;
    3: reserved;
};
)FIDL");
  ASSERT_COMPILED(library);
}

TEST(TableTests, GoodOutOfOrderFields) {
  TestLibrary library(R"FIDL(library fidl.test.tables;

type Foo = table {
    3: reserved;
    1: reserved;
    2: reserved;
};
)FIDL");
  ASSERT_COMPILED(library);
}

TEST(TableTests, GoodAllowEmptyTables) {
  TestLibrary library(R"FIDL(library fidl.test.tables;

type Foo = table {};
)FIDL");
  ASSERT_COMPILED(library);
}

TEST(TableTests, BadMissingOrdinals) {
  TestLibrary library(R"FIDL(
library fidl.test.tables;

type Foo = table {
    x int64;
};
)FIDL");
  // NOTE(fxbug.dev/72924): difference in parser implementation, the old syntax
  // checks for this case specifically.
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrMissingOrdinalBeforeMember)
}

TEST(TableTests, BadDuplicateFieldNames) {
  TestLibrary library(R"FIDL(
library fidl.test.tables;

type Foo = table {
    1: field string;
    2: field uint32;
};
)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrDuplicateTableFieldName);
}

TEST(TableTests, BadDuplicateOrdinals) {
  TestLibrary library(R"FIDL(
library fidl.test.tables;

type Foo = table {
    1: foo string;
    1: bar uint32;
};
)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrDuplicateTableFieldOrdinal);
}

TEST(TableTests, GoodAttributesOnFields) {
  TestLibrary library("test.fidl", R"FIDL(library fidl.test.tables;

type Foo = table {
    @foo_attr("bar")
    1: x int64;
    @bar_attr
    2: bar bool;
};
)FIDL");
  ASSERT_COMPILED(library);
}

TEST(TableTests, GoodAttributesOnTables) {
  TestLibrary library(R"FIDL(library fidl.test.tables;

@foo_attr("bar")
type Foo = table {
    1: x int64;
    2: please bool;
};
)FIDL");
  ASSERT_COMPILED(library);
}

TEST(TableTests, GoodKeywordsAsFieldNames) {
  TestLibrary library(R"FIDL(library fidl.test.tables;

type struct = struct {
    field bool;
};

type Foo = table {
    1: table int64;
    2: library bool;
    3: uint32 uint32;
    4: member struct;
    5: reserved bool;
};
)FIDL");
  ASSERT_COMPILED(library);
}

TEST(TableTests, BadOptionalInStruct) {
  TestLibrary library(R"FIDL(
library fidl.test.tables;

type Foo = table {
    1: t int64;
};

type OptionalTableContainer = struct {
    foo Foo:optional;
};
)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrCannotBeNullable);
}

TEST(TableTests, BadTableMultipleConstraints) {
  TestLibrary library(R"FIDL(
library fidl.test.tables;

type Foo = table {
    1: t int64;
};

type OptionalTableContainer = struct {
    foo Foo:<optional, foo, bar>;
};
)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrTooManyConstraints);
}

TEST(TableTests, BadOptionalInUnion) {
  TestLibrary library(R"FIDL(
library fidl.test.tables;

type Foo = table {
    1: t int64;
};

type OptionalTableContainer = union {
    1: foo Foo:optional;
};
)FIDL");
  // NOTE(fxbug.dev/72924): this pair of tests aims to document a behavior
  // difference between the old and new syntaxes: in the old, we check for
  // ErrNullableTableMember first before determining if the type itself can be
  // nullable. This is not the case in the new syntax (we need to compile the
  // type first to determine if it is nullable). The nullable union member
  // error is tested in UnionTests.BadNoNullableMembers
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrCannotBeNullable);
}

TEST(TableTests, GoodTableInTable) {
  TestLibrary library(R"FIDL(library fidl.test.tables;

type Foo = table {
    1: t int64;
};

type Bar = table {
    1: foo Foo;
};
)FIDL");
  ASSERT_COMPILED(library);
}

TEST(TableTests, GoodTablesInUnions) {
  TestLibrary library(R"FIDL(library fidl.test.tables;

type Foo = table {
    1: t int64;
};

type OptionalTableContainer = flexible union {
    1: foo Foo;
};
)FIDL");
  ASSERT_COMPILED(library);
}

TEST(TableTests, BadOptionalTableMember) {
  TestLibrary library(R"FIDL(
library fidl.test.tables;

type Foo = table {
    1: t string:optional;
};
)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrNullableTableMember);
}

TEST(TableTests, BadOptionalNonNullableTableMember) {
  TestLibrary library(R"FIDL(
library fidl.test.tables;

type Foo = table {
    1: t int64:optional;
};
)FIDL");
  // NOTE(fxbug.dev/72924): this pair of tests aims to document a behavior
  // difference between the old and new syntaxes: in the old, we check for
  // ErrNullableTableMember first before determining if the type itself can be
  // nullable. This is not the case in the new syntax (we need to compile the
  // type first to determine if it is nullable).
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrCannotBeNullable);
}

TEST(TableTests, BadDefaultNotAllowed) {
  TestLibrary library(R"FIDL(
library fidl.test.tables;

type Foo = table {
    1: t int64 = 1;
};

)FIDL");
  // NOTE(fxbug.dev/72924): we lose the default specific error in the new syntax.
  // TODO(fxbug.dev/72924): the second error doesn't make any sense
  ASSERT_ERRORED_TWICE_DURING_COMPILE(library, fidl::ErrUnexpectedTokenOfKind,
                                      fidl::ErrMissingOrdinalBeforeMember);
}

TEST(TableTests, BadMustBeDense) {
  TestLibrary library(R"FIDL(
library example;

type Example = table {
    1: first int64;
    3: third int64;
};

)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrNonDenseOrdinal);
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "2");
}

}  // namespace
