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

bool test_no_optional_on_primitive() {
    BEGIN_TEST;

    TestLibrary library(R"FIDL(
library test.optionals;

struct Bad {
    int64? opt_num;
};

)FIDL");
    ASSERT_FALSE(library.Compile());
    const auto& errors = library.errors();
    ASSERT_EQ(1, errors.size());
    ASSERT_STR_STR(errors[0].c_str(),
        "int64 cannot be nullable");

    END_TEST;
}

bool test_no_optional_on_aliased_primitive() {
    BEGIN_TEST;

    TestLibrary library(R"FIDL(
library test.optionals;

using alias = int64;

struct Bad {
    alias? opt_num;
};

)FIDL");
    ASSERT_FALSE(library.Compile());
    const auto& errors = library.errors();
    ASSERT_EQ(1, errors.size());
    ASSERT_STR_STR(errors[0].c_str(),
        "int64 cannot be nullable");

    END_TEST;
}

} // namespace

BEGIN_TEST_CASE(optionals_test)
RUN_TEST(test_no_optional_on_primitive)
RUN_TEST(test_no_optional_on_aliased_primitive)
END_TEST_CASE(optionals_test)
