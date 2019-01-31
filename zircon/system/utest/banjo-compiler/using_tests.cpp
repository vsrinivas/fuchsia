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

// Test that using properly allows referring to symbols in dependent library.
bool valid_using_without_alias_test() {
    BEGIN_TEST;

    TestLibrary dependency("dependent.banjo", R"BANJO(
library banjo.test.uzing.dependent;

struct Bar {
    int8 s;
};

)BANJO");
    ASSERT_TRUE(dependency.Compile());

    TestLibrary library("uzing.banjo", R"BANJO(
library banjo.test.uzing;

using banjo.test.uzing.dependent;

struct Foo {
    banjo.test.uzing.dependent.Bar dep;
};

)BANJO");
    ASSERT_TRUE(library.AddDependentLibrary(dependency));
    EXPECT_TRUE(library.Compile());

    END_TEST;
}

// Test that using properly allows referring to symbols in dependent library,
// using the aliased name of the dependent library, or the fully qualified name
// of the dependent library.
bool valid_using_with_alias_test() {
    BEGIN_TEST;

    TestLibrary dependency("dependent.banjo", R"BANJO(
library banjo.test.uzing.dependent;

struct Bar {
    int8 s;
};

)BANJO");
    ASSERT_TRUE(dependency.Compile());

    TestLibrary library("uzing.banjo", R"BANJO(
library banjo.test.uzing;

using banjo.test.uzing.dependent as dependent_alias;

struct Foo {
    banjo.test.uzing.dependent.Bar dep1;
    dependent_alias.Bar dep2;
};

)BANJO");
    ASSERT_TRUE(library.AddDependentLibrary(dependency));
    ASSERT_TRUE(library.Compile());

    END_TEST;
}

// Test that an unknown dependency is properly reported.
bool invalid_did_not_declare_dependency_with_using() {
    BEGIN_TEST;

    TestLibrary library("uzing.banjo", R"BANJO(
library banjo.test.uzing;

// missing using.

struct Foo {
  banjo.test.uzing.dependent.Bar dep;
};

)BANJO");
    ASSERT_FALSE(library.Compile());
    const auto& errors = library.errors();
    ASSERT_EQ(1, errors.size());
    ASSERT_STR_STR(errors[0].c_str(),
        "Unknown dependent library banjo.test.uzing.dependent. Did you require it with `using`?");

    END_TEST;
}

// Test that a duplicated using declaration is the same file is reported as an
// error.
bool invalid_duplicate_using() {
    BEGIN_TEST;

    TestLibrary dependency("dependent.banjo", R"BANJO(
library banjo.test.uzing.dependent;

)BANJO");
    ASSERT_TRUE(dependency.Compile());

    TestLibrary library("uzing.banjo", R"BANJO(
library banjo.test.uzing;

using banjo.test.uzing.dependent;
using banjo.test.uzing.dependent; // duplicated

)BANJO");
    ASSERT_TRUE(library.AddDependentLibrary(dependency));
    ASSERT_FALSE(library.Compile());
    const auto& errors = library.errors();
    ASSERT_EQ(1, errors.size());
    ASSERT_STR_STR(errors[0].c_str(),
        "Library banjo.test.uzing.dependent already imported. Did you require it twice?");

    END_TEST;
}

} // namespace

BEGIN_TEST_CASE(using_tests);
RUN_TEST(valid_using_without_alias_test);
RUN_TEST(valid_using_with_alias_test);
RUN_TEST(invalid_did_not_declare_dependency_with_using);
RUN_TEST(invalid_duplicate_using);
END_TEST_CASE(using_tests);
