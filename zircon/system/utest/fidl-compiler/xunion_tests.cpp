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

static bool compiling() {
    BEGIN_TEST;

    // Populated fields.
    EXPECT_TRUE(Compiles(R"FIDL(
library fidl.test.xunions;

xunion Foo {
    int64 i;
};
)FIDL"));

    // Empty xunions are allowed.
    EXPECT_TRUE(Compiles(R"FIDL(
library fidl.test.xunions;

xunion Foo {
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

    END_TEST;
}

} // namespace

BEGIN_TEST_CASE(xunion_tests)
RUN_TEST(compiling)
END_TEST_CASE(xunion_tests)
