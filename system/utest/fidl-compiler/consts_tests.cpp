// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <unittest/unittest.h>

// #include <fidl/flat_ast.h>
// #include <fidl/lexer.h>
// #include <fidl/parser.h>
// #include <fidl/source_file.h>

#include "test_library.h"

namespace {

bool good_const_test_bool() {
    BEGIN_TEST;

    TestLibrary library(R"FIDL(
library example;

const bool c = false;
)FIDL");
    ASSERT_TRUE(library.Compile());

    END_TEST;
}

bool bad_const_test_bool_with_string() {
    BEGIN_TEST;

    TestLibrary library(R"FIDL(
library example;

const bool c = "foo";
)FIDL");
    ASSERT_FALSE(library.Compile());
    auto errors = library.errors();
    ASSERT_EQ(errors.size(), 1);
    ASSERT_STR_STR(errors[0].c_str(), "cannot convert \"foo\" (type string:3) to type bool");

    END_TEST;
}

bool bad_const_test_bool_with_numeric() {
    BEGIN_TEST;

    TestLibrary library(R"FIDL(
library example;

const bool c = 6;
)FIDL");
    ASSERT_FALSE(library.Compile());
    auto errors = library.errors();
    ASSERT_EQ(errors.size(), 1);
    ASSERT_STR_STR(errors[0].c_str(), "cannot convert 6 (type int64) to type bool");

    END_TEST;
}

bool good_const_test_int32() {
    BEGIN_TEST;

    TestLibrary library(R"FIDL(
library example;

const int32 c = 42;
)FIDL");
    ASSERT_TRUE(library.Compile());

    END_TEST;
}

bool good_const_test_int32_from_other_const() {
    BEGIN_TEST;

    TestLibrary library(R"FIDL(
library example;

const int32 b = 42;
const int32 c = b;
)FIDL");
    ASSERT_TRUE(library.Compile());

    END_TEST;
}

bool bad_const_test_int32_with_string() {
    BEGIN_TEST;

    TestLibrary library(R"FIDL(
library example;

const int32 c = "foo";
)FIDL");
    ASSERT_FALSE(library.Compile());
    auto errors = library.errors();
    ASSERT_EQ(errors.size(), 1);
    ASSERT_STR_STR(errors[0].c_str(), "cannot convert \"foo\" (type string:3) to type int32");

    END_TEST;
}

bool bad_const_test_int32_with_bool() {
    BEGIN_TEST;

    TestLibrary library(R"FIDL(
library example;

const int32 c = true;
)FIDL");
    ASSERT_FALSE(library.Compile());
    auto errors = library.errors();
    ASSERT_EQ(errors.size(), 1);
    ASSERT_STR_STR(errors[0].c_str(), "cannot convert true (type bool) to type int32");

    END_TEST;
}

bool good_const_test_string() {
    BEGIN_TEST;

    TestLibrary library(R"FIDL(
library example;

const string:4 c = "four";
)FIDL");
    ASSERT_TRUE(library.Compile());

    END_TEST;
}

bool good_const_test_string_from_other_const() {
    BEGIN_TEST;

    TestLibrary library(R"FIDL(
library example;

const string:4 c = "four";
const string:5 d = c;
)FIDL");
    ASSERT_TRUE(library.Compile());

    END_TEST;
}

bool bad_const_test_string_with_numeric() {
    BEGIN_TEST;

    TestLibrary library(R"FIDL(
library example;

const string c = 4;
)FIDL");
    ASSERT_FALSE(library.Compile());
    auto errors = library.errors();
    ASSERT_EQ(errors.size(), 1);
    ASSERT_STR_STR(errors[0].c_str(), "cannot convert 4 (type int64) to type string");

    END_TEST;
}

bool bad_const_test_string_with_bool() {
    BEGIN_TEST;

    TestLibrary library(R"FIDL(
library example;

const string c = true;
)FIDL");
    ASSERT_FALSE(library.Compile());
    auto errors = library.errors();
    ASSERT_EQ(errors.size(), 1);
    ASSERT_STR_STR(errors[0].c_str(), "cannot convert true (type bool) to type string");

    END_TEST;
}

bool bad_const_test_string_with_string_too_long() {
    BEGIN_TEST;

    TestLibrary library(R"FIDL(
library example;

const string:4 c = "hello";
)FIDL");
    ASSERT_FALSE(library.Compile());
    auto errors = library.errors();
    ASSERT_EQ(errors.size(), 1);
    ASSERT_STR_STR(errors[0].c_str(), "cannot convert \"hello\" (type string:5) to type string:4");

    END_TEST;
}

bool good_const_test_using() {
    BEGIN_TEST;

    TestLibrary library(R"FIDL(
library example;

using foo = int32;
const foo c = 2;
)FIDL");
    ASSERT_TRUE(library.Compile());

    END_TEST;
}

bool bad_const_test_using_with_inconvertible_value() {
    BEGIN_TEST;

    TestLibrary library(R"FIDL(
library example;

using foo = int32;
const foo c = "nope";
)FIDL");
    ASSERT_FALSE(library.Compile());
    auto errors = library.errors();
    ASSERT_EQ(errors.size(), 1);
    ASSERT_STR_STR(errors[0].c_str(), "cannot convert \"nope\" (type string:4) to type int32");

    END_TEST;
}

bool bad_const_test_nullable_string() {
    BEGIN_TEST;

    TestLibrary library(R"FIDL(
library example;

const string? c = "";
)FIDL");
    ASSERT_FALSE(library.Compile());
    auto errors = library.errors();
    ASSERT_EQ(errors.size(), 1);
    ASSERT_STR_STR(errors[0].c_str(), "invalid constant type string?");

    END_TEST;
}

bool bad_const_test_enum() {
    BEGIN_TEST;

    TestLibrary library(R"FIDL(
library example;

enum MyEnum : int32 { A = 5; };
const MyEnum c = "";
)FIDL");
    ASSERT_FALSE(library.Compile());
    auto errors = library.errors();
    ASSERT_EQ(errors.size(), 1);
    ASSERT_STR_STR(errors[0].c_str(), "invalid constant type example/MyEnum");

    END_TEST;
}

bool bad_const_test_array() {
    BEGIN_TEST;

    TestLibrary library(R"FIDL(
library example;

const array<int32>:2 c = -1;
)FIDL");
    ASSERT_FALSE(library.Compile());
    auto errors = library.errors();
    ASSERT_EQ(errors.size(), 1);
    ASSERT_STR_STR(errors[0].c_str(), "invalid constant type array<int32>:2");

    END_TEST;
}

bool bad_const_test_vector() {
    BEGIN_TEST;

    TestLibrary library(R"FIDL(
library example;

const vector<int32>:2 c = -1;
)FIDL");
    ASSERT_FALSE(library.Compile());
    auto errors = library.errors();
    ASSERT_EQ(errors.size(), 1);
    ASSERT_STR_STR(errors[0].c_str(), "invalid constant type vector<int32>:2");

    END_TEST;
}

bool bad_const_test_handle_of_thread() {
    BEGIN_TEST;

    TestLibrary library(R"FIDL(
library example;

const handle<thread> c = -1;
)FIDL");
    ASSERT_FALSE(library.Compile());
    auto errors = library.errors();
    ASSERT_EQ(errors.size(), 1);
    ASSERT_STR_STR(errors[0].c_str(), "invalid constant type handle<thread>");

    END_TEST;
}

} // namespace

BEGIN_TEST_CASE(consts_tests);

RUN_TEST(good_const_test_bool);
RUN_TEST(bad_const_test_bool_with_string);
RUN_TEST(bad_const_test_bool_with_numeric);

RUN_TEST(good_const_test_int32);
RUN_TEST(good_const_test_int32_from_other_const);
RUN_TEST(bad_const_test_int32_with_string);
RUN_TEST(bad_const_test_int32_with_bool);

RUN_TEST(good_const_test_string);
RUN_TEST(good_const_test_string_from_other_const);
RUN_TEST(bad_const_test_string_with_numeric);
RUN_TEST(bad_const_test_string_with_bool);
RUN_TEST(bad_const_test_string_with_string_too_long);

RUN_TEST(good_const_test_using);
RUN_TEST(bad_const_test_using_with_inconvertible_value);

RUN_TEST(bad_const_test_nullable_string);
RUN_TEST(bad_const_test_enum);
RUN_TEST(bad_const_test_array);
RUN_TEST(bad_const_test_vector);
RUN_TEST(bad_const_test_handle_of_thread);

END_TEST_CASE(consts_tests);
