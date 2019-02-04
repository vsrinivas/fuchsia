// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <unittest/unittest.h>

#include <fidl/flat_ast.h>
#include <fidl/lexer.h>
#include <fidl/parser.h>
#include <fidl/source_file.h>

#include "test_library.h"

// TODO(FIDL-460): Delete this test.

namespace {

class ValidSuperinterfaces : public TestLibrary {
public:
    ValidSuperinterfaces() : TestLibrary("superinterfaces.fidl", R"FIDL(
library fidl.test.superinterfaces;

[FragileBase]
interface A {
    1: MethodA();
};

[FragileBase]
interface B : A {
    2: MethodB();
};

[FragileBase]
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

[FragileBase]
interface A {
    1: MethodA();
};

[FragileBase]
interface B : A {
    2: MethodB();
};

[FragileBase]
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
library a;

// a.b/lo and a.cv/f have colliding computed ordinals, so this is an illegal
// FIDL definition.

[FragileBase]
interface b {
   lo();
};

[FragileBase]
interface cv : b {
    f();
};

)FIDL") {}
};

class InvalidSimpleSuperinterfaces : public TestLibrary {
public:
    InvalidSimpleSuperinterfaces() : TestLibrary("superinterfaces.fidl", R"FIDL(
library fidl.test.superinterfaces;

[FragileBase]
interface A {
    1: MethodA(vector<uint64> arg);
};

[FragileBase]
interface B : A {
    2: MethodB();
};

[FragileBase]
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

bool missing_fragile_base_test() {
    BEGIN_TEST;

    TestLibrary library("fragile_base.fidl", R"FIDL(
library fidl.test.foo;

interface A {
    1: MethodA();
};

interface B : A {
    2: MethodB();
};

)FIDL");
    EXPECT_FALSE(library.Compile());
    auto errors = library.errors();
    ASSERT_EQ(errors.size(), 1);
    ASSERT_STR_STR(errors[0].c_str(),
                   "interface fidl.test.foo/A is not marked by [FragileBase] "
                   "attribute, disallowing interface fidl.test.foo/B from "
                   "inheriting from it");

    END_TEST;
}

} // namespace

BEGIN_TEST_CASE(superinterface_tests);
RUN_TEST(valid_superinterface_test);
RUN_TEST(invalid_name_superinterface_test);
RUN_TEST(invalid_ordinal_superinterface_test);
RUN_TEST(invalid_simple_superinterface_test);
RUN_TEST(missing_fragile_base_test);
END_TEST_CASE(superinterface_tests);
