// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zxtest/zxtest.h>

#include "tools/fidl/fidlc/include/fidl/flat_ast.h"
#include "tools/fidl/fidlc/include/fidl/lexer.h"
#include "tools/fidl/fidlc/include/fidl/parser.h"
#include "tools/fidl/fidlc/include/fidl/source_file.h"
#include "tools/fidl/fidlc/tests/error_test.h"
#include "tools/fidl/fidlc/tests/test_library.h"

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
  TestLibrary library(R"FIDL(library fidl.test.tables;

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
  TestLibrary library;
  library.AddFile("bad/fi-0016-a.test.fidl");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrMissingOrdinalBeforeMember)
}

TEST(TableTests, BadOrdinalOutOfBoundsNegative) {
  TestLibrary library;
  library.AddFile("bad/fi-0017-a.test.fidl");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrOrdinalOutOfBound);
}

TEST(TableTests, BadOrdinalOutOfBoundsLarge) {
  TestLibrary library(R"FIDL(
library test;

type Foo = union {
  4294967296: foo string;
};
)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrOrdinalOutOfBound);
}

TEST(TableTests, BadDuplicateFieldNames) {
  TestLibrary library;
  library.AddFile("bad/fi-0095.test.fidl");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrDuplicateTableFieldName);
}

TEST(TableTests, BadDuplicateOrdinals) {
  TestLibrary library;
  library.AddFile("bad/fi-0094.test.fidl");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrDuplicateTableFieldOrdinal);
}

TEST(TableTests, GoodAttributesOnFields) {
  TestLibrary library(R"FIDL(library fidl.test.tables;

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
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrCannotBeOptional);
}

TEST(TableTests, BadTableMultipleConstraints) {
  TestLibrary library(R"FIDL(
library fidl.test.tables;

type Foo = table {
    1: t int64;
};

type OptionalTableContainer = struct {
    foo Foo:<optional, 1, 2>;
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
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrCannotBeOptional);
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
  TestLibrary library;
  library.AddFile("bad/fi-0048.test.fidl");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrOptionalTableMember);
}

TEST(TableTests, BadOptionalNonOptionalTableMember) {
  TestLibrary library(R"FIDL(
library fidl.test.tables;

type Foo = table {
    // Integers can never be optional.
    1: t int64:optional;
};
)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrCannotBeOptional);
}

TEST(TableTests, BadDefaultNotAllowed) {
  TestLibrary library(R"FIDL(
library fidl.test.tables;

type Foo = table {
    1: t int64 = 1;
};

)FIDL");
  ASSERT_ERRORED_TWICE_DURING_COMPILE(library, fidl::ErrUnexpectedTokenOfKind,
                                      fidl::ErrMissingOrdinalBeforeMember);
}

TEST(TableTests, BadMustBeDense) {
  TestLibrary library;
  library.AddFile("bad/fi-0100.test.fidl");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrNonDenseOrdinal);
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "2");
}

TEST(TableTests, Good64OrdinalsMaxIsTable) {
  TestLibrary library;
  library.AddFile("good/fi-0093.test.fidl");
  ASSERT_COMPILED(library);
}

TEST(TableTests, BadMaxOrdinalNotTable) {
  TestLibrary library;
  library.AddFile("bad/fi-0093.test.fidl");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrMaxOrdinalNotTable);
}

TEST(TableTests, BadMaxOrdinalNotTableNotPrimitive) {
  TestLibrary library(R"FIDL(
library example;

type MyStruct = struct {};

type Example = table {
    1: v1 int64;
    2: v2 int64;
    3: v3 int64;
    4: v4 int64;
    5: v5 int64;
    6: v6 int64;
    7: v7 int64;
    8: v8 int64;
    9: v9 int64;
    10: v10 int64;
    11: v11 int64;
    12: v12 int64;
    13: v13 int64;
    14: v14 int64;
    15: v15 int64;
    16: v16 int64;
    17: v17 int64;
    18: v18 int64;
    19: v19 int64;
    20: v20 int64;
    21: v21 int64;
    22: v22 int64;
    23: v23 int64;
    24: v24 int64;
    25: v25 int64;
    26: v26 int64;
    27: v27 int64;
    28: v28 int64;
    29: v29 int64;
    30: v30 int64;
    31: v31 int64;
    32: v32 int64;
    33: v33 int64;
    34: v34 int64;
    35: v35 int64;
    36: v36 int64;
    37: v37 int64;
    38: v38 int64;
    39: v39 int64;
    40: v40 int64;
    41: v41 int64;
    42: v42 int64;
    43: v43 int64;
    44: v44 int64;
    45: v45 int64;
    46: v46 int64;
    47: v47 int64;
    48: v48 int64;
    49: v49 int64;
    50: v50 int64;
    51: v51 int64;
    52: v52 int64;
    53: v53 int64;
    54: v54 int64;
    55: v55 int64;
    56: v56 int64;
    57: v57 int64;
    58: v58 int64;
    59: v59 int64;
    60: v60 int64;
    61: v61 int64;
    62: v62 int64;
    63: v63 int64;
    64: v64 MyStruct;
};

)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrMaxOrdinalNotTable);
}

TEST(TableTests, BadTooManyOrdinals) {
  TestLibrary library;
  library.AddFile("bad/fi-0092.test.fidl");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrTooManyTableOrdinals);
}

// TODO(fxbug.dev/35218): This should work once recursive types are fully supported.
TEST(TableTests, BadRecursionDisallowed) {
  TestLibrary library;
  library.AddFile("bad/fi-0057-d.test.fidl");

  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrIncludeCycle);
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "table 'MySelf' -> table 'MySelf'");
}

}  // namespace
