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

class ValidSuperinterfaces : public TestLibrary {
public:
    ValidSuperinterfaces() : TestLibrary("superinterfaces.fidl", R"FIDL(
library fidl.test.superinterfaces;

interface A {
    1: MethodA();
};

interface B : A {
    2: MethodB();
};

interface C : A {
    3: MethodC();
};

interface D: B, C {
    4: MethodD();
};

)FIDL") {}
};

class InvalidNameSuperinterfaces : public TestLibrary {
public:
    InvalidNameSuperinterfaces() : TestLibrary("superinterfaces.fidl", R"FIDL(
library fidl.test.superinterfaces;

interface A {
    1: MethodA();
};

interface B : A {
    2: MethodB();
};

interface C : A {
    3: MethodC();
};

interface D: B, C {
    4: MethodD();
    5: MethodA();
};

)FIDL") {}
};

class InvalidOrdinalSuperinterfaces : public TestLibrary {
public:
    InvalidOrdinalSuperinterfaces() : TestLibrary("superinterfaces.fidl", R"FIDL(
library fidl.test.superinterfaces;

interface A {
    1: MethodA();
};

interface B : A {
    2: MethodB();
};

interface C : A {
    3: MethodC();
};

interface D: B, C {
    4: MethodD();
    1: MethodE();
};

)FIDL") {}
};

class InvalidSimpleSuperinterfaces : public TestLibrary {
public:
    InvalidSimpleSuperinterfaces() : TestLibrary("superinterfaces.fidl", R"FIDL(
library fidl.test.superinterfaces;

interface A {
    1: MethodA(vector<uint64>);
};

interface B : A {
    2: MethodB();
};

interface C : A {
    3: MethodC();
};

[Layout="Simple"]
interface D: B, C {
    4: MethodD();
};

)FIDL") {}
};

// Test that an interface with a valid diamond dependency has the
// correct number of methods.
bool valid_superinterface_test() {
    BEGIN_TEST;

    ValidSuperinterfaces library;
    EXPECT_TRUE(library.Parse());

    auto interface_d = library.LookupInterface("D");
    EXPECT_NONNULL(interface_d);

    EXPECT_EQ(interface_d->all_methods.size(), 4);

    END_TEST;
}

// Test that an interface with a name collision with a superinterface
// fails to compile.
bool invalid_name_superinterface_test() {
    BEGIN_TEST;

    InvalidNameSuperinterfaces library;
    EXPECT_FALSE(library.Parse());

    END_TEST;
}

// Test that an interface with a ordinal collision with a superinterface
// fails to compile.
bool invalid_ordinal_superinterface_test() {
    BEGIN_TEST;

    InvalidOrdinalSuperinterfaces library;
    EXPECT_FALSE(library.Parse());

    END_TEST;
}

// Test that an interface with a Simple layout constraint violation in
// a superinterface's method fails to compile.
bool invalid_simple_superinterface_test() {
    BEGIN_TEST;

    InvalidSimpleSuperinterfaces library;
    EXPECT_FALSE(library.Parse());

    END_TEST;
}

} // namespace

BEGIN_TEST_CASE(superinterface_tests);
RUN_TEST(valid_superinterface_test);
RUN_TEST(invalid_name_superinterface_test);
RUN_TEST(invalid_ordinal_superinterface_test);
RUN_TEST(invalid_simple_superinterface_test);
END_TEST_CASE(superinterface_tests);
