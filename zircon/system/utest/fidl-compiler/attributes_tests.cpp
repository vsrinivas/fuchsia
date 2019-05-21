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
protocol A {
    MethodA();
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
protocol A {
    MethodA();
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

    TestLibrary library;
    library.AddSource("dup_attributes.fidl", R"FIDL(
[dup = "first"]
library fidl.test.dupattributes;

)FIDL");
    library.AddSource("dup_attributes_second.fidl", R"FIDL(
[dup = "second"]
library fidl.test.dupattributes;

)FIDL");
    ASSERT_FALSE(library.Compile());
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
protocol A {
    MethodA();
};

)FIDL");
    EXPECT_TRUE(library.Compile());
    auto warnings = library.warnings();
    ASSERT_EQ(warnings.size(), 1);
    ASSERT_STR_STR(warnings[0].c_str(), "suspect attribute with name 'Duc'; did you mean 'Doc'?");

    END_TEST;
}

// This tests our ability to treat warnings as errors.  It is here because this
// is the most convenient warning.
bool warnings_as_errors_test() {
    BEGIN_TEST;

    TestLibrary library("dup_attributes.fidl", R"FIDL(
library fidl.test.dupattributes;

[Duc = "should be Doc"]
protocol A {
    MethodA();
};

)FIDL");
    library.set_warnings_as_errors(true);
    EXPECT_FALSE(library.Compile());
    auto warnings = library.warnings();
    ASSERT_EQ(warnings.size(), 0);
    auto errors = library.errors();
    ASSERT_EQ(errors.size(), 1);
    ASSERT_STR_STR(errors[0].c_str(), "suspect attribute with name 'Duc'; did you mean 'Doc'?");

    END_TEST;
}

bool empty_transport() {
    BEGIN_TEST;

    TestLibrary library("transport_attribuets.fidl", R"FIDL(
library fidl.test.transportattributes;

[Transport]
protocol A {
    MethodA();
};

)FIDL");
    EXPECT_FALSE(library.Compile());
    auto errors = library.errors();
    ASSERT_EQ(errors.size(), 1);
    ASSERT_STR_STR(errors[0].c_str(), "invalid transport");

    END_TEST;
}

bool bogus_transport() {
    BEGIN_TEST;

    TestLibrary library("transport_attribuets.fidl", R"FIDL(
library fidl.test.transportattributes;

[Transport = "Bogus"]
protocol A {
    MethodA();
};

)FIDL");
    EXPECT_FALSE(library.Compile());
    auto errors = library.errors();
    ASSERT_EQ(errors.size(), 1);
    ASSERT_STR_STR(errors[0].c_str(), "invalid transport");

    END_TEST;
}

bool channel_transport() {
    BEGIN_TEST;

    TestLibrary library("transport_attribuets.fidl", R"FIDL(
library fidl.test.transportattributes;

[Transport = "Channel"]
protocol A {
    MethodA();
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
protocol A {
    MethodA();
};

)FIDL");
    EXPECT_TRUE(library.Compile());
    ASSERT_EQ(library.errors().size(), 0);
    ASSERT_EQ(library.warnings().size(), 0);

    END_TEST;
}

bool multiple_transports() {
    BEGIN_TEST;

    TestLibrary library("transport_attribuets.fidl", R"FIDL(
library fidl.test.transportattributes;

[Transport = "SocketControl, OvernetInternal"]
protocol A {
    MethodA();
};

)FIDL");
    EXPECT_TRUE(library.Compile());
    ASSERT_EQ(library.errors().size(), 0);
    ASSERT_EQ(library.warnings().size(), 0);

    END_TEST;
}

bool multiple_transports_with_bogus() {
    BEGIN_TEST;

    TestLibrary library("transport_attribuets.fidl", R"FIDL(
library fidl.test.transportattributes;

[Transport = "SocketControl,Bogus, OvernetInternal"]
protocol A {
    MethodA();
};

)FIDL");
    EXPECT_FALSE(library.Compile());
    auto errors = library.errors();
    ASSERT_EQ(errors.size(), 1);
    ASSERT_STR_STR(errors[0].c_str(), "invalid transport");

    END_TEST;
}

bool incorrect_placement_layout() {
    BEGIN_TEST;

    TestLibrary library(R"FIDL(
[Layout = "Simple"]
library fidl.test;

[Layout = "Simple"]
const int32 MyConst = 0;

[Layout = "Simple"]
enum MyEnum {
    [Layout = "Simple"]
    MyMember = 5;
};

[Layout = "Simple"]
struct MyStruct {
    [Layout = "Simple"]
    int32 MyMember;
};

[Layout = "Simple"]
union MyUnion {
    [Layout = "Simple"]
    int32 MyMember;
};

[Layout = "Simple"]
table MyTable {
    [Layout = "Simple"]
    1: int32 MyMember;
};

[Layout = "Simple"]
protocol MyInterface {
    [Layout = "Simple"]
    MyMethod();
};

)FIDL");
    EXPECT_FALSE(library.Compile());
    auto errors = library.errors();
    ASSERT_EQ(errors.size(), 11);
    ASSERT_STR_STR(errors[0].c_str(), "placement of attribute 'Layout' disallowed here");

    END_TEST;
}

bool MustHaveThreeMembers(fidl::ErrorReporter* error_reporter,
                          const fidl::raw::Attribute& attribute,
                          const fidl::flat::Decl* decl) {
    switch (decl->kind) {
    case fidl::flat::Decl::Kind::kStruct: {
        auto struct_decl = static_cast<const fidl::flat::Struct*>(decl);
        return struct_decl->members.size() == 3;
    }
    default:
        return false;
    }
}

bool constraint_only_three_members_on_struct() {
    BEGIN_TEST;

    TestLibrary library(R"FIDL(
library fidl.test;

[MustHaveThreeMembers]
struct MyStruct {
    int64 one;
    int64 two;
    int64 three;
    int64 oh_no_four;
};

)FIDL");
    library.AddAttributeSchema("MustHaveThreeMembers", fidl::flat::AttributeSchema({
                                                                                       fidl::flat::AttributeSchema::Placement::kStructDecl,
                                                                                   },
                                                                                   {
                                                                                       "",
                                                                                   },
                                                                                   MustHaveThreeMembers));
    EXPECT_FALSE(library.Compile());
    auto errors = library.errors();
    ASSERT_EQ(errors.size(), 1);
    ASSERT_STR_STR(errors[0].c_str(),
                   "declaration did not satisfy constraint of attribute 'MustHaveThreeMembers' with value ''");

    END_TEST;
}

bool constraint_only_three_members_on_method() {
    BEGIN_TEST;

    TestLibrary library(R"FIDL(
library fidl.test;

protocol MyInterface {
    [MustHaveThreeMembers] MyMethod();
};

)FIDL");
    library.AddAttributeSchema("MustHaveThreeMembers", fidl::flat::AttributeSchema({
                                                                                       fidl::flat::AttributeSchema::Placement::kMethod,
                                                                                   },
                                                                                   {
                                                                                       "",
                                                                                   },
                                                                                   MustHaveThreeMembers));
    EXPECT_FALSE(library.Compile());
    auto errors = library.errors();
    ASSERT_EQ(errors.size(), 1);
    ASSERT_STR_STR(errors[0].c_str(),
                   "declaration did not satisfy constraint of attribute 'MustHaveThreeMembers' with value ''");

    END_TEST;
}

bool constraint_only_three_members_on_interface() {
    BEGIN_TEST;

    TestLibrary library(R"FIDL(
library fidl.test;

[MustHaveThreeMembers]
protocol MyInterface {
    MyMethod();
    MySecondMethod();
};

)FIDL");
    library.AddAttributeSchema("MustHaveThreeMembers", fidl::flat::AttributeSchema({
                                                                                       fidl::flat::AttributeSchema::Placement::kInterfaceDecl,
                                                                                   },
                                                                                   {
                                                                                       "",
                                                                                   },
                                                                                   MustHaveThreeMembers));
    EXPECT_FALSE(library.Compile());
    auto errors = library.errors();
    ASSERT_EQ(errors.size(), 2); // 2 because there are two methods
    ASSERT_STR_STR(errors[0].c_str(),
                   "declaration did not satisfy constraint of attribute 'MustHaveThreeMembers' with value ''");

    END_TEST;
}

bool max_bytes() {
    BEGIN_TEST;

    TestLibrary library(R"FIDL(
library fidl.test;

[MaxBytes = "27"]
table MyTable {
  1: bool here;
};

)FIDL");
    EXPECT_FALSE(library.Compile());
    auto errors = library.errors();
    ASSERT_EQ(errors.size(), 1);
    ASSERT_STR_STR(errors[0].c_str(),
                   "too large: only 27 bytes allowed, but 40 bytes found");

    END_TEST;
}

bool max_handles() {
    BEGIN_TEST;

    TestLibrary library(R"FIDL(
library fidl.test;

[MaxHandles = "2"]
union MyUnion {
  uint8 hello;
  array<uint8>:8 world;
  vector<handle>:6 foo;
};

)FIDL");
    EXPECT_FALSE(library.Compile());
    auto errors = library.errors();
    ASSERT_EQ(errors.size(), 1);
    ASSERT_STR_STR(errors[0].c_str(),
                   "too many handles: only 2 allowed, but 6 found");

    END_TEST;
}

bool selector_incorrect_placement() {
    BEGIN_TEST;

    TestLibrary library(R"FIDL(
library fidl.test;

[Selector = "Nonsense"]
union MyUnion {
  uint8 hello;
};

)FIDL");
    EXPECT_FALSE(library.Compile());
    auto errors = library.errors();
    ASSERT_EQ(errors.size(), 1);
    ASSERT_STR_STR(errors[0].c_str(),
                   "placement of attribute");
    ASSERT_STR_STR(errors[0].c_str(),
                   "disallowed here");

    END_TEST;
}

} // namespace

BEGIN_TEST_CASE(attributes_tests)
RUN_TEST(no_two_same_attribute_test)
RUN_TEST(no_two_same_doc_attribute_test)
RUN_TEST(no_two_same_attribute_on_library_test)
RUN_TEST(warn_on_close_attribute_test)
RUN_TEST(warnings_as_errors_test)
RUN_TEST(empty_transport)
RUN_TEST(bogus_transport)
RUN_TEST(channel_transport)
RUN_TEST(socket_control_transport)
RUN_TEST(multiple_transports)
RUN_TEST(multiple_transports_with_bogus)
RUN_TEST(incorrect_placement_layout)
RUN_TEST(constraint_only_three_members_on_struct)
RUN_TEST(constraint_only_three_members_on_method)
RUN_TEST(constraint_only_three_members_on_interface)
RUN_TEST(max_bytes)
RUN_TEST(max_handles)
RUN_TEST(selector_incorrect_placement)
END_TEST_CASE(attributes_tests)
