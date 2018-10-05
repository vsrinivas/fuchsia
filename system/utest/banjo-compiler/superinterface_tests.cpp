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

class ValidSuperinterfaces : public TestLibrary {
public:
    ValidSuperinterfaces() : TestLibrary("superinterfaces.banjo", R"BANJO(
library banjo.test.superinterfaces;

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

)BANJO") {}
};

class InvalidNameSuperinterfaces : public TestLibrary {
public:
    InvalidNameSuperinterfaces() : TestLibrary("superinterfaces.banjo", R"BANJO(
library banjo.test.superinterfaces;

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

)BANJO") {}
};

class InvalidOrdinalSuperinterfaces : public TestLibrary {
public:
    InvalidOrdinalSuperinterfaces() : TestLibrary("superinterfaces.banjo", R"BANJO(
library banjo.test.superinterfaces;

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

)BANJO") {}
};

class InvalidSimpleSuperinterfaces : public TestLibrary {
public:
    InvalidSimpleSuperinterfaces() : TestLibrary("superinterfaces.banjo", R"BANJO(
library banjo.test.superinterfaces;

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

)BANJO") {}
};

// Test that an interface with a valid diamond dependency has the
// correct number of methods.
bool valid_superinterface_test() {
    BEGIN_TEST;

    ValidSuperinterfaces library;
    EXPECT_TRUE(library.Compile());

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
    EXPECT_FALSE(library.Compile());

    END_TEST;
}

// Test that an interface with a ordinal collision with a superinterface
// fails to compile.
bool invalid_ordinal_superinterface_test() {
    BEGIN_TEST;

    InvalidOrdinalSuperinterfaces library;
    EXPECT_FALSE(library.Compile());

    END_TEST;
}

// Test that an interface with a Simple layout constraint violation in
// a superinterface's method fails to compile.
bool invalid_simple_superinterface_test() {
    BEGIN_TEST;

    InvalidSimpleSuperinterfaces library;
    EXPECT_FALSE(library.Compile());

    END_TEST;
}

} // namespace

BEGIN_TEST_CASE(superinterface_tests);
RUN_TEST(valid_superinterface_test);
RUN_TEST(invalid_name_superinterface_test);
RUN_TEST(invalid_ordinal_superinterface_test);
RUN_TEST(invalid_simple_superinterface_test);
END_TEST_CASE(superinterface_tests);
