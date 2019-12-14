// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/flat_ast.h>
#include <fidl/lexer.h>
#include <fidl/parser.h>
#include <fidl/source_file.h>
#include <unittest/unittest.h>

#include "test_library.h"

namespace {

bool placement_of_attributes() {
  BEGIN_TEST;

  SharedAmongstLibraries shared;
  TestLibrary dependency("exampleusing.fidl", R"FIDL(
library exampleusing;

struct Empty {};

)FIDL",
                         &shared);
  ASSERT_TRUE(dependency.Compile());

  TestLibrary library("example.fidl", R"FIDL(
[OnLibrary]
library example;

using exampleusing;

[OnBits]
bits ExampleBits {
    [OnBitsMember]
    MEMBER = 1;
};

[OnConst]
const uint32 EXAMPLE_CONST = 0;

[OnEnum]
enum ExampleEnum {
    [OnEnumMember]
    MEMBER = 1;
};

[OnProtocol]
protocol ExampleProtocol {
    [OnMethod]
    Method([OnParameter] exampleusing.Empty arg);
};

[OnService]
service ExampleService {
    [OnServiceMember]
    ExampleProtocol member;
};

[OnStruct]
struct ExampleStruct {
    [OnStructMember]
    uint32 member;
};

[OnTable]
table ExampleTable {
    [OnTableMember]
    1: uint32 member;
};

[OnTypeAlias]
using ExampleTypeAlias = uint32;

[OnUnion]
union ExampleUnion {
    [OnUnionMember]
    1: uint32 variant;
};

[OnXUnion]
xunion ExampleXUnion {
    [OnXUnionMember]
    1: uint32 variant;
};

)FIDL",
                      &shared);
  ASSERT_TRUE(library.AddDependentLibrary(std::move(dependency)));
  ASSERT_TRUE(library.Compile());

  EXPECT_TRUE(library.library()->HasAttribute("OnLibrary"));

  auto example_bits = library.LookupBits("ExampleBits");
  ASSERT_NONNULL(example_bits);
  EXPECT_TRUE(example_bits->attributes->HasAttribute("OnBits"));
  EXPECT_TRUE(example_bits->members.front().attributes->HasAttribute("OnBitsMember"));

  auto example_const = library.LookupConstant("EXAMPLE_CONST");
  ASSERT_NONNULL(example_const);
  EXPECT_TRUE(example_const->attributes->HasAttribute("OnConst"));

  auto example_enum = library.LookupEnum("ExampleEnum");
  ASSERT_NONNULL(example_enum);
  EXPECT_TRUE(example_enum->attributes->HasAttribute("OnEnum"));
  EXPECT_TRUE(example_enum->members.front().attributes->HasAttribute("OnEnumMember"));

  auto example_protocol = library.LookupProtocol("ExampleProtocol");
  ASSERT_NONNULL(example_protocol);
  EXPECT_TRUE(example_protocol->attributes->HasAttribute("OnProtocol"));
  EXPECT_TRUE(example_protocol->methods.front().attributes->HasAttribute("OnMethod"));
  ASSERT_NONNULL(example_protocol->methods.front().maybe_request);
  EXPECT_TRUE(example_protocol->methods.front().maybe_request->members.front().attributes->HasAttribute("OnParameter"));

  auto example_service = library.LookupService("ExampleService");
  ASSERT_NONNULL(example_service);
  EXPECT_TRUE(example_service->attributes->HasAttribute("OnService"));
  EXPECT_TRUE(example_service->members.front().attributes->HasAttribute("OnServiceMember"));

  auto example_struct = library.LookupStruct("ExampleStruct");
  ASSERT_NONNULL(example_struct);
  EXPECT_TRUE(example_struct->attributes->HasAttribute("OnStruct"));
  EXPECT_TRUE(example_struct->members.front().attributes->HasAttribute("OnStructMember"));

  auto example_table = library.LookupTable("ExampleTable");
  ASSERT_NONNULL(example_table);
  EXPECT_TRUE(example_table->attributes->HasAttribute("OnTable"));
  EXPECT_TRUE(example_table->members.front().maybe_used->attributes->HasAttribute("OnTableMember"));

  auto example_type_alias = library.LookupTypeAlias("ExampleTypeAlias");
  ASSERT_NONNULL(example_type_alias);
  EXPECT_TRUE(example_type_alias->attributes->HasAttribute("OnTypeAlias"));

  auto example_union = library.LookupUnion("ExampleUnion");
  ASSERT_NONNULL(example_union);
  EXPECT_TRUE(example_union->attributes->HasAttribute("OnUnion"));
  EXPECT_TRUE(example_union->members.front().maybe_used->attributes->HasAttribute("OnUnionMember"));

  auto example_xunion = library.LookupXUnion("ExampleXUnion");
  ASSERT_NONNULL(example_xunion);
  EXPECT_TRUE(example_xunion->attributes->HasAttribute("OnXUnion"));
  EXPECT_TRUE(
      example_xunion->members.front().maybe_used->attributes->HasAttribute("OnXUnionMember"));

  END_TEST;
}

bool no_attribute_on_using_not_event_doc() {
  BEGIN_TEST;

  TestLibrary library(R"FIDL(
library example;

/// nope
[NoAttributeOnUsing, EvenDoc]
using we.should.not.care;

)FIDL");
  EXPECT_FALSE(library.Compile());
  auto errors = library.errors();
  ASSERT_EQ(errors.size(), 1);
  ASSERT_STR_STR(
      errors[0].c_str(),
      "no attributes allowed on library import, found: Doc, NoAttributeOnUsing, EvenDoc");

  END_TEST;
}

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

bool syscall_transport() {
  BEGIN_TEST;

  TestLibrary library("transport_attributes.fidl", R"FIDL(
library fidl.test.transportattributes;

[Transport = "Syscall"]
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

[Transport = "Channel, Syscall"]
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

[Transport = "Channel, Bogus, Syscall"]
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
    1: int32 MyMember;
};

[Layout = "Simple"]
table MyTable {
    [Layout = "Simple"]
    1: int32 MyMember;
};

[Layout = "Simple"]
protocol MyProtocol {
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
                          const fidl::raw::Attribute& attribute, const fidl::flat::Decl* decl) {
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
  library.AddAttributeSchema("MustHaveThreeMembers",
                             fidl::flat::AttributeSchema(
                                 {
                                     fidl::flat::AttributeSchema::Placement::kStructDecl,
                                 },
                                 {
                                     "",
                                 },
                                 MustHaveThreeMembers));
  EXPECT_FALSE(library.Compile());
  auto errors = library.errors();
  ASSERT_EQ(errors.size(), 1);
  ASSERT_STR_STR(
      errors[0].c_str(),
      "declaration did not satisfy constraint of attribute 'MustHaveThreeMembers' with value ''");

  END_TEST;
}

bool constraint_only_three_members_on_method() {
  BEGIN_TEST;

  TestLibrary library(R"FIDL(
library fidl.test;

protocol MyProtocol {
    [MustHaveThreeMembers] MyMethod();
};

)FIDL");
  library.AddAttributeSchema("MustHaveThreeMembers",
                             fidl::flat::AttributeSchema(
                                 {
                                     fidl::flat::AttributeSchema::Placement::kMethod,
                                 },
                                 {
                                     "",
                                 },
                                 MustHaveThreeMembers));
  EXPECT_FALSE(library.Compile());
  auto errors = library.errors();
  ASSERT_EQ(errors.size(), 1);
  ASSERT_STR_STR(
      errors[0].c_str(),
      "declaration did not satisfy constraint of attribute 'MustHaveThreeMembers' with value ''");

  END_TEST;
}

bool constraint_only_three_members_on_protocol() {
  BEGIN_TEST;

  TestLibrary library(R"FIDL(
library fidl.test;

[MustHaveThreeMembers]
protocol MyProtocol {
    MyMethod();
    MySecondMethod();
};

)FIDL");
  library.AddAttributeSchema("MustHaveThreeMembers",
                             fidl::flat::AttributeSchema(
                                 {
                                     fidl::flat::AttributeSchema::Placement::kProtocolDecl,
                                 },
                                 {
                                     "",
                                 },
                                 MustHaveThreeMembers));
  EXPECT_FALSE(library.Compile());
  auto errors = library.errors();
  ASSERT_EQ(errors.size(), 2);  // 2 because there are two methods
  ASSERT_STR_STR(
      errors[0].c_str(),
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
  ASSERT_STR_STR(errors[0].c_str(), "too large: only 27 bytes allowed, but 40 bytes found");

  END_TEST;
}

bool max_handles() {
  BEGIN_TEST;

  TestLibrary library(R"FIDL(
library fidl.test;

[MaxHandles = "2"]
union MyUnion {
  1: uint8 hello;
  2: array<uint8>:8 world;
  3: vector<handle>:6 foo;
};

)FIDL");
  EXPECT_FALSE(library.Compile());
  auto errors = library.errors();
  ASSERT_EQ(errors.size(), 1);
  ASSERT_STR_STR(errors[0].c_str(), "too many handles: only 2 allowed, but 6 found");

  END_TEST;
}

bool selector_incorrect_placement() {
  BEGIN_TEST;

  TestLibrary library(R"FIDL(
library fidl.test;

[Selector = "Nonsense"]
union MyUnion {
  1: uint8 hello;
};

)FIDL");
  EXPECT_FALSE(library.Compile());
  auto errors = library.errors();
  ASSERT_EQ(errors.size(), 1);
  ASSERT_STR_STR(errors[0].c_str(), "placement of attribute");
  ASSERT_STR_STR(errors[0].c_str(), "disallowed here");

  END_TEST;
}

bool no_attributes_on_reserved() {
  BEGIN_TEST;

  TestLibrary on_union(R"FIDL(
library fidl.test;

union Foo {
  [Foo]
  1: reserved;
};
)FIDL");
  ASSERT_FALSE(on_union.Compile());
  ASSERT_EQ(on_union.errors().size(), 1);
  ASSERT_STR_STR(on_union.errors()[0].c_str(), "idk");

  TestLibrary on_xunion(R"FIDL(
library fidl.test;

xunion Foo {
  [Foo]
  1: reserved;
};
)FIDL");
  ASSERT_FALSE(on_xunion.Compile());
  ASSERT_EQ(on_xunion.errors().size(), 1);
  ASSERT_STR_STR(on_xunion.errors()[0].c_str(), "idk");

  TestLibrary on_table(R"FIDL(
library fidl.test;

table Foo {
  [Foo]
  1: reserved;
};
)FIDL");
  ASSERT_FALSE(on_table.Compile());
  ASSERT_EQ(on_table.errors().size(), 1);
  ASSERT_STR_STR(on_table.errors()[0].c_str(), "idk");

  END_TEST;
}

bool parameter_attribute_incorrect_placement() {
  BEGIN_TEST;

  TestLibrary library(R"FIDL(
library fidl.test;

protocol ExampleProtocol {
    Method(exampleusing.Empty arg [OnParameter]);
};

)FIDL");
  EXPECT_FALSE(library.Compile());
  auto errors = library.errors();
  ASSERT_EQ(errors.size(), 1);
  ASSERT_STR_STR(errors[0].c_str(), "unexpected token LeftSquare, was expecting RightParen");

  END_TEST;
}
}  // namespace

BEGIN_TEST_CASE(attributes_tests)
RUN_TEST(placement_of_attributes)
RUN_TEST(no_attribute_on_using_not_event_doc)
RUN_TEST(no_two_same_attribute_test)
RUN_TEST(no_two_same_doc_attribute_test)
RUN_TEST(no_two_same_attribute_on_library_test)
RUN_TEST(warn_on_close_attribute_test)
RUN_TEST(warnings_as_errors_test)
RUN_TEST(empty_transport)
RUN_TEST(bogus_transport)
RUN_TEST(channel_transport)
RUN_TEST(syscall_transport)
RUN_TEST(multiple_transports)
RUN_TEST(multiple_transports_with_bogus)
RUN_TEST(incorrect_placement_layout)
RUN_TEST(constraint_only_three_members_on_struct)
RUN_TEST(constraint_only_three_members_on_method)
RUN_TEST(constraint_only_three_members_on_protocol)
RUN_TEST(max_bytes)
RUN_TEST(max_handles)
RUN_TEST(selector_incorrect_placement)
RUN_TEST(parameter_attribute_incorrect_placement)
END_TEST_CASE(attributes_tests)
