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

// Test that a duplicate attribute is caught, and nicely reported.
bool no_two_same_attribute_test() {
    BEGIN_TEST;

    TestLibrary library("dup_attributes.fidl", R"FIDL(
library fidl.test.dupattributes;

[dup = "first", dup = "second"]
interface A {
    1: MethodA();
};

)FIDL");
    EXPECT_FALSE(library.Compile());
    auto errors = library.errors();
    ASSERT_EQ(errors.size(), 1);
    ASSERT_STR_STR(errors[0].c_str(), "duplicate attribute with name 'dup'");

    END_TEST;
}

// Test that doc comments and doc attributes clash are properly checked.
bool no_two_same_doc_attribute_test() {
    BEGIN_TEST;

    TestLibrary library("dup_attributes.fidl", R"FIDL(
library fidl.test.dupattributes;

/// first
[Doc = "second"]
interface A {
    1: MethodA();
};

)FIDL");
    EXPECT_FALSE(library.Compile());
    auto errors = library.errors();
    ASSERT_EQ(errors.size(), 1);
    ASSERT_STR_STR(errors[0].c_str(), "duplicate attribute with name 'Doc'");

    END_TEST;
}

// Test that TODO
bool no_two_same_attribute_on_library_test() {
    BEGIN_TEST;

    TestLibrary library("dup_attributes.fidl", R"FIDL(
[dup = "first"]
library fidl.test.dupattributes;

)FIDL");
    EXPECT_TRUE(library.Compile());

    EXPECT_FALSE(library.AddSourceFile("dup_attributes_second.fidl", R"FIDL(
[dup = "second"]
library fidl.test.dupattributes;

)FIDL"));
    auto errors = library.errors();
    ASSERT_EQ(errors.size(), 1);
    ASSERT_STR_STR(errors[0].c_str(), "duplicate attribute with name 'dup'");

    END_TEST;
}

// Test that a close attribute is caught.
bool warn_on_close_attribute_test() {
    BEGIN_TEST;

    TestLibrary library("dup_attributes.fidl", R"FIDL(
library fidl.test.dupattributes;

[Duc = "should be Doc"]
interface A {
    1: MethodA();
};

)FIDL");
    EXPECT_TRUE(library.Compile());
    auto warnings = library.warnings();
    ASSERT_EQ(warnings.size(), 1);
    ASSERT_STR_STR(warnings[0].c_str(), "suspect attribute with name 'Duc'; did you mean 'Doc'?");

    END_TEST;
}

bool empty_transport() {
    BEGIN_TEST;

    TestLibrary library("transport_attribuets.fidl", R"FIDL(
library fidl.test.transportattributes;

[Transport]
interface A {
    1: MethodA();
};

)FIDL");
    EXPECT_FALSE(library.Compile());
    auto errors = library.errors();
    ASSERT_EQ(errors.size(), 1);
    ASSERT_STR_STR(errors[0].c_str(), "invalid attribute value");

    END_TEST;
}

bool bogus_transport() {
    BEGIN_TEST;

    TestLibrary library("transport_attribuets.fidl", R"FIDL(
library fidl.test.transportattributes;

[Transport = "Bogus"]
interface A {
    1: MethodA();
};

)FIDL");
    EXPECT_FALSE(library.Compile());
    auto errors = library.errors();
    ASSERT_EQ(errors.size(), 1);
    ASSERT_STR_STR(errors[0].c_str(), "invalid attribute value");

    END_TEST;
}

bool channel_transport() {
    BEGIN_TEST;

    TestLibrary library("transport_attribuets.fidl", R"FIDL(
library fidl.test.transportattributes;

[Transport = "Channel"]
interface A {
    1: MethodA();
};

)FIDL");
    EXPECT_TRUE(library.Compile());
    ASSERT_EQ(library.errors().size(), 0);
    ASSERT_EQ(library.warnings().size(), 0);

    END_TEST;
}

bool socket_control_transport() {
    BEGIN_TEST;

    TestLibrary library("transport_attribuets.fidl", R"FIDL(
library fidl.test.transportattributes;

[Transport = "SocketControl"]
interface A {
    1: MethodA();
};

)FIDL");
    EXPECT_TRUE(library.Compile());
    ASSERT_EQ(library.errors().size(), 0);
    ASSERT_EQ(library.warnings().size(), 0);

    END_TEST;
}

bool incorrect_placement_layout() {
    BEGIN_TEST;

    TestLibrary library(R"FIDL(
[Layout]
library fidl.test;

[Layout]
const int32 MyConst = 0;

[Layout]
enum MyEnum {
    [Layout]
    MyMember = 5;
};

[Layout]
struct MyStruct {
    [Layout]
    int32 MyMember;
};

[Layout]
union MyUnion {
    [Layout]
    int32 MyMember;
};

[Layout]
table MyTable {
    [Layout]
    1: int32 MyMember;
};

[Layout]
interface MyInterface {
    [Layout]
    1: MyMethod();
};

)FIDL");
    EXPECT_FALSE(library.Compile());
    auto errors = library.errors();
    ASSERT_EQ(errors.size(), 11);
    ASSERT_STR_STR(errors[0].c_str(), "placement of attribute 'Layout' disallowed here");

    END_TEST;
}

} // namespace

BEGIN_TEST_CASE(attributes_tests);
RUN_TEST(no_two_same_attribute_test);
RUN_TEST(no_two_same_doc_attribute_test);
RUN_TEST(no_two_same_attribute_on_library_test);
RUN_TEST(warn_on_close_attribute_test);
RUN_TEST(empty_transport);
RUN_TEST(bogus_transport);
RUN_TEST(channel_transport);
RUN_TEST(socket_control_transport);
RUN_TEST(incorrect_placement_layout);
END_TEST_CASE(attributes_tests);
