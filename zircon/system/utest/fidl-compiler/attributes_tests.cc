// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/diagnostics.h>
#include <fidl/flat_ast.h>
#include <fidl/lexer.h>
#include <fidl/parser.h>
#include <fidl/reporter.h>
#include <fidl/source_file.h>

#include <zxtest/zxtest.h>

#include "error_test.h"
#include "test_library.h"

namespace {

TEST(AttributesTests, GoodPlacementOfAttributes) {
  SharedAmongstLibraries shared;
  TestLibrary dependency("exampleusing.fidl", R"FIDL(library exampleusing;

@on_dep_struct
type Empty = struct {};
)FIDL",
                         &shared);
  ASSERT_COMPILED(dependency);

  TestLibrary library("example.fidl", R"FIDL(
@on_library
library example;

using exampleusing;

@on_bits
type ExampleBits = bits {
    @on_bits_member
    MEMBER = 1;
};

@on_const
const EXAMPLE_CONST uint32 = 0;

@on_enum
type ExampleEnum = enum {
    @on_enum_member
    MEMBER = 1;
};

@on_protocol
protocol ExampleChildProtocol {
    @on_method
    Method(struct { @on_parameter arg exampleusing.Empty; });
};

@on_protocol
protocol ExampleParentProtocol {
    @on_compose
    compose ExampleChildProtocol;
};

@on_service
service ExampleService {
    @on_service_member
    member client_end:ExampleParentProtocol;
};

@on_struct
type ExampleStruct = struct {
    @on_struct_member
    member uint32;
};

@on_table
type ExampleTable = table {
    @on_table_member
    1: member uint32;
};

@on_type_alias
alias ExampleTypeAlias = uint32;

@on_union
type ExampleUnion = union {
    @on_union_member
    1: variant uint32;
};

)FIDL",
                      &shared);
  ASSERT_TRUE(library.AddDependentLibrary(std::move(dependency)));
  ASSERT_COMPILED(library);

  EXPECT_TRUE(library.library()->HasAttribute("on_library"));

  auto example_bits = library.LookupBits("ExampleBits");
  ASSERT_NOT_NULL(example_bits);
  EXPECT_TRUE(example_bits->HasAttribute("on_bits"));
  EXPECT_TRUE(example_bits->members.front().attributes->HasAttribute("on_bits_member"));

  auto example_const = library.LookupConstant("EXAMPLE_CONST");
  ASSERT_NOT_NULL(example_const);
  EXPECT_TRUE(example_const->HasAttribute("on_const"));

  auto example_enum = library.LookupEnum("ExampleEnum");
  ASSERT_NOT_NULL(example_enum);
  EXPECT_TRUE(example_enum->HasAttribute("on_enum"));
  EXPECT_TRUE(example_enum->members.front().attributes->HasAttribute("on_enum_member"));

  auto example_child_protocol = library.LookupProtocol("ExampleChildProtocol");
  ASSERT_NOT_NULL(example_child_protocol);
  EXPECT_TRUE(example_child_protocol->HasAttribute("on_protocol"));
  EXPECT_TRUE(example_child_protocol->methods.front().attributes->HasAttribute("on_method"));
  ASSERT_NOT_NULL(example_child_protocol->methods.front().maybe_request_payload);
  EXPECT_TRUE(example_child_protocol->methods.front()
                  .maybe_request_payload->members.front()
                  .attributes->HasAttribute("on_parameter"));

  auto example_parent_protocol = library.LookupProtocol("ExampleParentProtocol");
  ASSERT_NOT_NULL(example_parent_protocol);
  EXPECT_TRUE(example_parent_protocol->HasAttribute("on_protocol"));
  EXPECT_TRUE(
      example_parent_protocol->composed_protocols.front().attributes->HasAttribute("on_compose"));

  auto example_service = library.LookupService("ExampleService");
  ASSERT_NOT_NULL(example_service);
  EXPECT_TRUE(example_service->HasAttribute("on_service"));
  EXPECT_TRUE(example_service->members.front().attributes->HasAttribute("on_service_member"));

  auto example_struct = library.LookupStruct("ExampleStruct");
  ASSERT_NOT_NULL(example_struct);
  EXPECT_TRUE(example_struct->HasAttribute("on_struct"));
  EXPECT_TRUE(example_struct->members.front().attributes->HasAttribute("on_struct_member"));

  auto example_table = library.LookupTable("ExampleTable");
  ASSERT_NOT_NULL(example_table);
  EXPECT_TRUE(example_table->HasAttribute("on_table"));
  EXPECT_TRUE(
      example_table->members.front().maybe_used->attributes->HasAttribute("on_table_member"));

  auto example_type_alias = library.LookupTypeAlias("ExampleTypeAlias");
  ASSERT_NOT_NULL(example_type_alias);
  EXPECT_TRUE(example_type_alias->HasAttribute("on_type_alias"));

  auto example_union = library.LookupUnion("ExampleUnion");
  ASSERT_NOT_NULL(example_union);
  EXPECT_TRUE(example_union->HasAttribute("on_union"));
  EXPECT_TRUE(
      example_union->members.front().maybe_used->attributes->HasAttribute("on_union_member"));
}

TEST(AttributesTests, GoodOfficialAttributes) {
  TestLibrary library("example.fidl", R"FIDL(@no_doc
library example;

/// For EXAMPLE_CONSTANT
@no_doc
@deprecated("Note")
const EXAMPLE_CONSTANT string = "foo";

/// For ExampleEnum
@deprecated("Reason")
@transitional
type ExampleEnum = strict enum {
    A = 1;
    /// For EnumMember
    @unknown
    B = 2;
};

/// For ExampleStruct
@max_bytes("1234")
@max_handles("5678")
type ExampleStruct = resource struct {
  data @generated_name("CustomName") table {
    1: a uint8;
  };
};

/// For ExampleProtocol
@discoverable
@for_deprecated_c_bindings
@transport("Syscall")
protocol ExampleProtocol {
    /// For ExampleMethod
    @internal
    @selector("Bar")
    @transitional
    ExampleMethod();
};

/// For ExampleService
@foo("ExampleService")
@no_doc
service ExampleService {
    /// For ExampleProtocol
    @foo("ExampleProtocol")
    @no_doc
    p client_end:ExampleProtocol;
};
)FIDL");
  ASSERT_COMPILED(library);

  EXPECT_TRUE(library.library()->HasAttribute("no_doc"));

  auto example_const = library.LookupConstant("EXAMPLE_CONSTANT");
  ASSERT_NOT_NULL(example_const);
  EXPECT_TRUE(example_const->HasAttribute("no_doc"));
  EXPECT_TRUE(example_const->HasAttributeArg("doc", "value"));
  auto const_doc_value = static_cast<const fidl::flat::DocCommentConstantValue&>(
      example_const->GetAttributeArg("doc", "value").value().get().value->Value());
  EXPECT_STR_EQ(const_doc_value.MakeContents(), " For EXAMPLE_CONSTANT\n");
  EXPECT_TRUE(example_const->HasAttributeArg("deprecated", "value"));
  auto const_str_value = static_cast<const fidl::flat::StringConstantValue&>(
      example_const->GetAttributeArg("deprecated", "value").value().get().value->Value());
  EXPECT_STR_EQ(const_str_value.MakeContents(), "Note");

  auto example_enum = library.LookupEnum("ExampleEnum");
  ASSERT_NOT_NULL(example_enum);
  EXPECT_TRUE(example_enum->HasAttribute("transitional"));
  EXPECT_TRUE(example_enum->HasAttributeArg("doc", "value"));
  auto enum_doc_value = static_cast<const fidl::flat::DocCommentConstantValue&>(
      example_enum->GetAttributeArg("doc", "value").value().get().value->Value());
  EXPECT_STR_EQ(enum_doc_value.MakeContents(), " For ExampleEnum\n");
  EXPECT_TRUE(example_enum->HasAttributeArg("deprecated", "value"));
  auto enum_str_value = static_cast<const fidl::flat::StringConstantValue&>(
      example_enum->GetAttributeArg("deprecated", "value").value().get().value->Value());
  EXPECT_STR_EQ(enum_str_value.MakeContents(), "Reason");
  EXPECT_TRUE(example_enum->members.back().attributes->HasAttribute("unknown"));

  auto example_struct = library.LookupStruct("ExampleStruct");
  ASSERT_NOT_NULL(example_struct);
  EXPECT_TRUE(example_struct->HasAttributeArg("doc", "value"));
  auto struct_doc_value = static_cast<const fidl::flat::DocCommentConstantValue&>(
      example_struct->GetAttributeArg("doc", "value").value().get().value->Value());
  EXPECT_STR_EQ(struct_doc_value.MakeContents(), " For ExampleStruct\n");
  EXPECT_TRUE(example_struct->HasAttributeArg("max_bytes", "value"));
  auto struct_str_value1 = static_cast<const fidl::flat::StringConstantValue&>(
      example_struct->GetAttributeArg("max_bytes", "value").value().get().value->Value());
  EXPECT_STR_EQ(struct_str_value1.MakeContents(), "1234");
  EXPECT_TRUE(example_struct->HasAttributeArg("max_handles", "value"));
  auto struct_str_value2 = static_cast<const fidl::flat::StringConstantValue&>(
      example_struct->GetAttributeArg("max_handles", "value").value().get().value->Value());
  EXPECT_STR_EQ(struct_str_value2.MakeContents(), "5678");

  auto example_anon = library.LookupTable("CustomName");
  ASSERT_NOT_NULL(example_anon);
  EXPECT_TRUE(example_anon->HasAttribute("generated_name"));

  auto generated_name_value = static_cast<const fidl::flat::StringConstantValue&>(
      example_anon->GetAttributeArg("generated_name", "value").value().get().value->Value());
  EXPECT_STR_EQ(generated_name_value.MakeContents(), "CustomName");

  auto example_protocol = library.LookupProtocol("ExampleProtocol");
  ASSERT_NOT_NULL(example_protocol);
  EXPECT_TRUE(example_protocol->HasAttribute("discoverable"));
  EXPECT_TRUE(example_protocol->HasAttribute("for_deprecated_c_bindings"));
  EXPECT_TRUE(example_protocol->HasAttributeArg("doc", "value"));
  auto protocol_doc_value = static_cast<const fidl::flat::DocCommentConstantValue&>(
      example_protocol->GetAttributeArg("doc", "value").value().get().value->Value());
  EXPECT_STR_EQ(protocol_doc_value.MakeContents(), " For ExampleProtocol\n");
  EXPECT_TRUE(example_protocol->HasAttributeArg("transport", "value"));
  auto protocol_str_value = static_cast<const fidl::flat::StringConstantValue&>(
      example_protocol->GetAttributeArg("transport", "value").value().get().value->Value());
  EXPECT_STR_EQ(protocol_str_value.MakeContents(), "Syscall");

  auto& example_method = example_protocol->methods.front();
  EXPECT_TRUE(example_method.attributes->HasAttribute("internal"));
  EXPECT_TRUE(example_method.attributes->HasAttribute("transitional"));
  EXPECT_TRUE(example_method.attributes->HasAttributeArg("doc", "value"));
  auto method_doc_value = static_cast<const fidl::flat::DocCommentConstantValue&>(
      example_method.attributes->GetAttributeArg("doc", "value").value().get().value->Value());
  EXPECT_STR_EQ(method_doc_value.MakeContents(), " For ExampleMethod\n");
  EXPECT_TRUE(example_method.attributes->HasAttributeArg("selector", "value"));
  auto method_str_value = static_cast<const fidl::flat::StringConstantValue&>(
      example_method.attributes->GetAttributeArg("selector", "value").value().get().value->Value());
  EXPECT_STR_EQ(method_str_value.MakeContents(), "Bar");

  auto example_service = library.LookupService("ExampleService");
  ASSERT_NOT_NULL(example_service);
  EXPECT_TRUE(example_service->HasAttribute("no_doc"));
  EXPECT_TRUE(example_service->HasAttributeArg("doc", "value"));
  auto service_doc_value = static_cast<const fidl::flat::DocCommentConstantValue&>(
      example_service->GetAttributeArg("doc", "value").value().get().value->Value());
  EXPECT_STR_EQ(service_doc_value.MakeContents(), " For ExampleService\n");
  EXPECT_TRUE(example_service->HasAttributeArg("foo", "value"));
  auto service_str_value = static_cast<const fidl::flat::StringConstantValue&>(
      example_service->GetAttributeArg("foo", "value").value().get().value->Value());
  EXPECT_STR_EQ(service_str_value.MakeContents(), "ExampleService");

  auto& example_service_member = example_service->members.front();
  EXPECT_TRUE(example_service_member.attributes->HasAttribute("no_doc"));
  EXPECT_TRUE(example_service_member.attributes->HasAttributeArg("doc", "value"));
  auto service_member_doc_value = static_cast<const fidl::flat::DocCommentConstantValue&>(
      example_service_member.attributes->GetAttributeArg("doc", "value")
          .value()
          .get()
          .value->Value());
  EXPECT_STR_EQ(service_member_doc_value.MakeContents(), " For ExampleProtocol\n");
  EXPECT_TRUE(example_service_member.attributes->HasAttributeArg("foo", "value"));
  auto service_member_str_value = static_cast<const fidl::flat::StringConstantValue&>(
      example_service_member.attributes->GetAttributeArg("foo", "value")
          .value()
          .get()
          .value->Value());
  EXPECT_STR_EQ(service_member_str_value.MakeContents(), "ExampleProtocol");
}

TEST(AttributesTests, BadNoAttributeOnUsingNotEventDoc) {
  TestLibrary library(R"FIDL(
library example;

/// nope
@no_attribute_on_using
@even_doc
using we.should.not.care;

)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrAttributesNewNotAllowedOnLibraryImport);
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "doc");
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "no_attribute_on_using");
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "even_doc");
}

// Test that a duplicate attribute is caught, and nicely reported.
TEST(AttributesTests, BadNoTwoSameAttributeTest) {
  TestLibrary library("dup_attributes.fidl", R"FIDL(
library fidl.test.dupattributes;

@dup("first")
@Dup("second")
protocol A {
    MethodA();
};

)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrDuplicateAttribute);
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "dup");
}

// Test that doc comments and doc attributes clash are properly checked.
TEST(AttributesTests, BadNoTwoSameDocAttributeTest) {
  TestLibrary library("dup_attributes.fidl", R"FIDL(
library fidl.test.dupattributes;

/// first
@doc("second")
protocol A {
    MethodA();
};

)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrDuplicateAttribute);
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "doc");
}

TEST(AttributesTests, BadNoTwoSameAttributeOnLibraryTest) {
  TestLibrary library("dup_attributes.fidl", R"FIDL(
@dup("first")
library fidl.test.dupattributes;

)FIDL");
  library.AddSource("dup_attributes_second.fidl", R"FIDL(
@dup("second")
 library fidl.test.dupattributes;

)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrDuplicateAttribute);
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "dup");
}

// Test that a close attribute is caught.
TEST(AttributesTests, WarnOnCloseAttributeTest) {
  TestLibrary library(R"FIDL(
library fidl.test;

@duc("should be doc")
protocol A {
    MethodA();
};

)FIDL");
  ASSERT_COMPILED(library);
  const auto& warnings = library.warnings();
  ASSERT_EQ(warnings.size(), 1);
  ASSERT_ERR(warnings[0], fidl::WarnAttributeTypo);
  ASSERT_SUBSTR(warnings[0]->msg.c_str(), "duc");
  ASSERT_SUBSTR(warnings[0]->msg.c_str(), "doc");
}

// This tests our ability to treat warnings as errors.  It is here because this
// is the most convenient warning.
TEST(AttributesTests, BadWarningsAsErrorsTest) {
  TestLibrary library(R"FIDL(
library fidl.test;

@duc("should be doc")
protocol A {
    MethodA();
};

)FIDL");
  library.set_warnings_as_errors(true);
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::WarnAttributeTypo);
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "duc");
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "doc");
  ASSERT_EQ(library.warnings().size(), 0);
}

TEST(AttributesTests, BadEmptyTransport) {
  TestLibrary library("transport_attributes.fidl", R"FIDL(
library fidl.test.transportattributes;

@transport
protocol A {
    MethodA();
};

)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrMissingRequiredAttributeArg);
}

TEST(AttributesTests, BadBogusTransport) {
  TestLibrary library("transport_attributes.fidl", R"FIDL(
library fidl.test.transportattributes;

@transport("Bogus")
protocol A {
    MethodA();
};

)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrInvalidTransportType);
}

TEST(AttributesTests, GoodChannelTransport) {
  TestLibrary library("transport_attributes.fidl", R"FIDL(library fidl.test.transportattributes;

@transport("Channel")
protocol A {
    MethodA();
};
)FIDL");
  ASSERT_COMPILED(library);
  ASSERT_EQ(library.errors().size(), 0);
  ASSERT_EQ(library.warnings().size(), 0);
}

TEST(AttributesTests, GoodSyscallTransport) {
  TestLibrary library("transport_attributes.fidl", R"FIDL(library fidl.test.transportattributes;

@transport("Syscall")
protocol A {
    MethodA();
};
)FIDL");
  ASSERT_COMPILED(library);
  ASSERT_EQ(library.errors().size(), 0);
  ASSERT_EQ(library.warnings().size(), 0);
}

TEST(AttributesTests, GoodMultipleTransports) {
  TestLibrary library("transport_attributes.fidl", R"FIDL(library fidl.test.transportattributes;

@transport("Channel, Syscall")
protocol A {
    MethodA();
};
)FIDL");
  ASSERT_COMPILED(library);
  ASSERT_EQ(library.errors().size(), 0);
  ASSERT_EQ(library.warnings().size(), 0);
}

TEST(AttributesTests, BadMultipleTransportsWithBogus) {
  TestLibrary library("transport_attributes.fidl", R"FIDL(
library fidl.test.transportattributes;

@transport("Channel, Bogus, Syscall")
protocol A {
    MethodA();
};

)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrInvalidTransportType);
}

TEST(AttributesTests, BadTransitionalInvalidPlacement) {
  TestLibrary library(R"FIDL(
library fidl.test;

@transitional
protocol MyProtocol {
  MyMethod();
};
  )FIDL");

  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrInvalidAttributePlacement);
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "transitional");
}

TEST(AttributesTests, BadUnknownInvalidPlacementOnUnion) {
  TestLibrary library(R"FIDL(
library fidl.test;

@unknown
type U = flexible union {
  1: a int32;
};
  )FIDL");

  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrInvalidAttributePlacement);
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "unknown");
}

TEST(AttributesTests, BadUnknownInvalidPlacementOnBitsMember) {
  TestLibrary library(R"FIDL(
library fidl.test;

type B = flexible bits : uint32 {
  @unknown A = 0x1;
};
  )FIDL");

  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrInvalidAttributePlacement);
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "unknown");
}

TEST(AttributesTests, BadUnknownInvalidOnStrictUnionsEnums) {
  {
    TestLibrary library(R"FIDL(
library fidl.test;

type U = strict union {
  @unknown 1: a int32;
};
  )FIDL");
    ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrUnknownAttributeOnInvalidType);
    ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "Unknown");
  }

  {
    TestLibrary library(R"FIDL(
library fidl.test;

type E = strict enum : uint32 {
  @unknown A = 1;
};
  )FIDL");
    ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrUnknownAttributeOnInvalidType);
    ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "Unknown");
  }
}

TEST(AttributesTests, GoodUnknownOkOnFlexibleOrTransitionalEnumsUnionMembers) {
  {
    TestLibrary library(R"FIDL(library fidl.test;

type U = flexible union {
    @unknown
    1: a int32;
};
)FIDL");
    ASSERT_COMPILED(library);
  }

  {
    TestLibrary library(R"FIDL(library fidl.test;

@transitional
type U = strict union {
    @unknown
    1: a int32;
};");
)FIDL");
    ASSERT_COMPILED(library);
  }

  {
    TestLibrary library(R"FIDL(library fidl.test;

type E = flexible enum : uint32 {
    @unknown
    A = 1;
};
)FIDL");
    ASSERT_COMPILED(library);
  }

  {
    TestLibrary library(R"FIDL(library fidl.test;

@transitional
type E = strict enum : uint32 {
    @unknown
    A = 1;
};
)FIDL");
    ASSERT_COMPILED(library);
  }
}

TEST(AttributesTests, BadIncorrectPlacementLayout) {
  TestLibrary library(R"FIDL(
@for_deprecated_c_bindings
library fidl.test;

@for_deprecated_c_bindings
const MyConst int32 = 0;

@for_deprecated_c_bindings
type MyEnum = enum {
    @for_deprecated_c_bindings
    MyMember = 5;
};

type MyStruct = struct {
    @for_deprecated_c_bindings
    MyMember int32;
};

@for_deprecated_c_bindings
type MyUnion = union {
    @for_deprecated_c_bindings
    1: MyMember int32;
};

@for_deprecated_c_bindings
type MyTable = table {
    @for_deprecated_c_bindings
    1: MyMember int32;
};

protocol MyProtocol {
    @for_deprecated_c_bindings
    MyMethod();
};

)FIDL");
  EXPECT_FALSE(library.Compile());
  const auto& errors = library.errors();
  ASSERT_EQ(errors.size(), 9);
  ASSERT_ERR(errors[0], fidl::ErrInvalidAttributePlacement);
  ASSERT_SUBSTR(errors[0]->msg.c_str(), "for_deprecated_c_bindings");
}

TEST(AttributesTests, BadDeprecatedAttributes) {
  TestLibrary library(R"FIDL(
library fidl.test;

@layout("Simple")
type MyStruct = struct {};

@layout("Complex")
protocol MyOtherProtocol {
  MyMethod();
};

@layout("Simple")
protocol MyProtocol {
  MyMethod();
};
)FIDL");
  EXPECT_FALSE(library.Compile());
  const auto& errors = library.errors();
  ASSERT_EQ(errors.size(), 3);
  for (size_t i = 0; i < errors.size(); i++) {
    ASSERT_ERR(errors[i], fidl::ErrDeprecatedAttribute);
  }
}

bool MustHaveThreeMembers(fidl::Reporter* reporter,
                          const std::unique_ptr<fidl::flat::Attribute>& attribute,
                          const fidl::flat::Attributable* attributable) {
  switch (attributable->placement) {
    case fidl::flat::AttributePlacement::kStructDecl: {
      auto struct_decl = static_cast<const fidl::flat::Struct*>(attributable);
      return struct_decl->members.size() == 3;
    }
    default:
      return false;
  }
}

TEST(AttributesTests, BadConstraintOnlyThreeMembersOnStruct) {
  TestLibrary library(R"FIDL(
library fidl.test;

@must_have_three_members
type MyStruct = struct {
    one int64;
    two int64;
    three int64;
    oh_no_four int64;
};

)FIDL");
  library.AddAttributeSchema("must_have_three_members",
                             fidl::flat::AttributeSchema(
                                 {
                                     fidl::flat::AttributePlacement::kStructDecl,
                                 },
                                 MustHaveThreeMembers));
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrAttributeConstraintNotSatisfied);
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "must_have_three_members");
}

TEST(AttributesTests, BadConstraintOnlyThreeMembersOnMethod) {
  TestLibrary library(R"FIDL(
library fidl.test;

protocol MyProtocol {
    @must_have_three_members MyMethod();
};

)FIDL");
  library.AddAttributeSchema("must_have_three_members",
                             fidl::flat::AttributeSchema(
                                 {
                                     fidl::flat::AttributePlacement::kMethod,
                                 },
                                 MustHaveThreeMembers));
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrAttributeConstraintNotSatisfied);
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "must_have_three_members");
}

TEST(AttributesTests, BadConstraintOnlyThreeMembersOnProtocol) {
  TestLibrary library(R"FIDL(
library fidl.test;

@must_have_three_members
protocol MyProtocol {
    MyMethod();
    MySecondMethod();
};

)FIDL");
  library.AddAttributeSchema("must_have_three_members",
                             fidl::flat::AttributeSchema(
                                 {
                                     fidl::flat::AttributePlacement::kProtocolDecl,
                                 },
                                 MustHaveThreeMembers));
  // Twice because there are two methods.
  ASSERT_ERRORED_TWICE_DURING_COMPILE(library, fidl::ErrAttributeConstraintNotSatisfied,
                                      fidl::ErrAttributeConstraintNotSatisfied);
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "must_have_three_members");
}

TEST(AttributesTests, BadMaxBytes) {
  TestLibrary library(R"FIDL(
library fidl.test;

@max_bytes("27")
type MyTable = table {
  1: here bool;
};

)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrTooManyBytes);
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "27");  // 27 allowed
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "40");  // 40 found
}

TEST(AttributesTests, BadMaxBytesBoundTooBig) {
  TestLibrary library(R"FIDL(
library fidl.test;

@max_bytes("4294967296") // 2^32
type MyTable = table {
  1: u uint8;
};
)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrBoundIsTooBig);
}

TEST(AttributesTests, BadMaxBytesUnableToParseBound) {
  TestLibrary library(R"FIDL(
library fidl.test;

@max_bytes("invalid")
type MyTable = table {
  1: u uint8;
};
)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrUnableToParseBound);
}

TEST(AttributesTests, BadMaxHandles) {
  auto library = WithLibraryZx(R"FIDL(
library fidl.test;

using zx;

@max_handles("2")
type MyUnion = resource union {
  1: hello uint8;
  2: world array<uint8,8>;
  3: foo vector<zx.handle:VMO>:6;
};

)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrTooManyHandles);
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "2");  // 2 allowed
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "6");  // 6 found
}

TEST(AttributesTests, BadAttributeValue) {
  TestLibrary library(R"FIDL(
library fidl.test;

@for_deprecated_c_bindings("Complex")
protocol P {
    Method();
};
)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrAttributeDisallowsArgs);
}

TEST(AttributesTests, BadSelectorIncorrectPlacement) {
  TestLibrary library(R"FIDL(
library fidl.test;

@selector("Nonsense")
type MyUnion = union {
  1: hello uint8;
};

)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrInvalidAttributePlacement);
}

TEST(AttributesTests, BadNoAttributesOnReserved) {
  {
    TestLibrary library(R"FIDL(
library fidl.test;

type Foo = union {
  @foo
  1: reserved;
};
)FIDL");
    ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrCannotAttachAttributesToReservedOrdinals);
  }

  {
    TestLibrary library(R"FIDL(
library fidl.test;

type Foo = table {
  @foo
  1: reserved;
};
  )FIDL");
    ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrCannotAttachAttributesToReservedOrdinals);
  }
}

TEST(AttributesTests, BadParameterAttributeIncorrectPlacement) {
  TestLibrary library(R"FIDL(
library fidl.test;

protocol ExampleProtocol {
    Method(struct { arg exampleusing.Empty; } @on_parameter);
};

)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrUnexpectedTokenOfKind);
}

TEST(AttributesTests, BadDuplicateAttributePlacement) {
  TestLibrary library(R"FIDL(
library fidl.test;

@foo
type Foo = @bar struct {};

)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrRedundantAttributePlacement);
}

TEST(AttributesTests, GoodLayoutAttributePlacements) {
  TestLibrary library(R"FIDL(
library fidl.test;

@foo
type Foo = struct {};

type Bar = @bar struct {};

protocol MyProtocol {
  MyMethod(@baz struct {
    inner_layout @qux struct {};
  });
};

)FIDL");
  ASSERT_COMPILED(library);

  auto foo = library.LookupStruct("Foo");
  ASSERT_NOT_NULL(foo);
  EXPECT_TRUE(foo->HasAttribute("foo"));

  auto bar = library.LookupStruct("Bar");
  ASSERT_NOT_NULL(bar);
  EXPECT_TRUE(bar->HasAttribute("bar"));

  auto req = library.LookupStruct("MyProtocolMyMethodRequest");
  ASSERT_NOT_NULL(req);
  EXPECT_TRUE(req->HasAttribute("baz"));

  auto inner = library.LookupStruct("InnerLayout");
  ASSERT_NOT_NULL(inner);
  EXPECT_TRUE(inner->HasAttribute("qux"));
}

TEST(AttributesTests, BadOverrideAttributePlacements) {
  {
    TestLibrary library(R"FIDL(
library fidl.test;

@generated_name("Good")
type Bad = struct {};

)FIDL");
    ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrInvalidAttributePlacement);
  }
  {
    TestLibrary library(R"FIDL(
library fidl.test;

type Bad = @generated_name("Good") struct {};

)FIDL");
    ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrInvalidAttributePlacement);
  }

  {
    TestLibrary library(R"FIDL(
library fidl.test;

type MetaVars = enum {
  FOO = 1;
  @generated_name("BAZ")
  BAR = 2;
}

)FIDL");
    ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrUnexpectedTokenOfKind);
  }

  {
    TestLibrary library(R"FIDL(
library fidl.test;

protocol Foo {};

service Bar {
  @generated_name("One")
  bar_one client_end:Bar;
}

)FIDL");
    ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrUnexpectedTokenOfKind);
  }
}

TEST(AttributesTests, BadMissingOverrideArg) {
  TestLibrary library(R"FIDL(
library fidl.test;

type Foo = struct {
  bad @generated_name struct {};
};

)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrMissingRequiredAttributeArg);
}

TEST(AttributesTests, BadOverrideValue) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library(R"FIDL(
library fidl.test;

type Foo = struct {
  bad @generated_name("ez$") struct {};
};

)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrInvalidNameOverride);
}

TEST(AttributesTests, BadOverrideCausesNameConflict) {
  TestLibrary library(R"FIDL(
library fidl.test;

type Foo = struct {
  foo @generated_name("Baz") struct {};
};

type Baz = struct {};

)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrNameCollision);
}

TEST(AttributesTests, BadNoArgumentsEmptyParens) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kNewSyntaxOnly);
  TestLibrary library(R"FIDL(
library fidl.test;

@for_deprecated_c_bindings()
type MyStruct = struct {};

)FIDL",
                      experimental_flags);
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrAttributeWithEmptyParens);
}

TEST(AttributesTests, GoodMultipleArguments) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kNewSyntaxOnly);
  TestLibrary library(R"FIDL(
library example;

@foo(bar="abc", baz="def")
type MyStruct = struct {};

)FIDL",
                      experimental_flags);
  ASSERT_TRUE(library.Compile());

  auto example_struct = library.LookupStruct("MyStruct");
  ASSERT_NOT_NULL(example_struct);
  EXPECT_TRUE(example_struct->HasAttribute("foo"));
  EXPECT_TRUE(example_struct->HasAttributeArg("foo", "bar"));
  EXPECT_STR_EQ(example_struct->GetAttributeArg("foo", "bar").value().get().value->span.data(),
                "\"abc\"");
  EXPECT_TRUE(example_struct->HasAttributeArg("foo", "baz"));
  EXPECT_STR_EQ(example_struct->GetAttributeArg("foo", "baz").value().get().value->span.data(),
                "\"def\"");
}

TEST(AttributesTests, BadMultipleArgumentsWithNoNames) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kNewSyntaxOnly);
  TestLibrary library(R"FIDL(
library example;

@foo("abc", "def")
type MyStruct = struct {};

)FIDL",
                      experimental_flags);
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrAttributeArgsMustAllBeNamed);
}

TEST(AttributesTests, BadMultipleArgumentsDuplicateNames) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kNewSyntaxOnly);
  TestLibrary library(R"FIDL(
library example;

@foo(bar="abc", bar="def")
type MyStruct = struct {};

)FIDL",
                      experimental_flags);
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrDuplicateAttributeArg);
}

TEST(AttributesTests, BadMultipleArgumentsDuplicateCanonicalNames) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kNewSyntaxOnly);
  TestLibrary library(R"FIDL(
library example;

@foo(bar_baz="abc", bar__baz="def")
type MyStruct = struct {};

)FIDL",
                      experimental_flags);
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrDuplicateAttributeArg);
}

TEST(AttributesTests, GoodSingleArgumentIsNotNamed) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kNewSyntaxOnly);
  TestLibrary library(R"FIDL(
library example;

@foo("bar")
type MyStruct = struct {};

)FIDL",
                      experimental_flags);
  ASSERT_TRUE(library.Compile());
}

TEST(AttributesTests, GoodSingleArgumentIsNamedWithoutSchema) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kNewSyntaxOnly);
  TestLibrary library(R"FIDL(
library example;

@foo(a="bar")
type MyStruct = struct {};

)FIDL",
                      experimental_flags);
  ASSERT_TRUE(library.Compile());
}

TEST(AttributesTests, GoodSingleSchemaArgument) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kNewSyntaxOnly);
  TestLibrary library(R"FIDL(
library example;

@foo("bar")
type MyStruct = struct {};

)FIDL",
                      experimental_flags);
  library.AddAttributeSchema(
      "foo", fidl::flat::AttributeSchema(
                 {
                     fidl::flat::AttributePlacement::kStructDecl,
                 },
                 {
                     {"value", fidl::flat::AttributeArgSchema(
                                   fidl::flat::ConstantValue::Kind::kString,
                                   fidl::flat::AttributeArgSchema::Optionality::kRequired)},
                 }));
  ASSERT_TRUE(library.Compile());
}

TEST(AttributesTests, GoodSingleSchemaArgumentWithInferredName) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kNewSyntaxOnly);
  TestLibrary library(R"FIDL(
library example;

@foo("bar")
type MyStruct = struct {};

)FIDL",
                      experimental_flags);
  library.AddAttributeSchema(
      "foo", fidl::flat::AttributeSchema(
                 {
                     fidl::flat::AttributePlacement::kStructDecl,
                 },
                 {
                     {"inferrable", fidl::flat::AttributeArgSchema(
                                        fidl::flat::ConstantValue::Kind::kString,
                                        fidl::flat::AttributeArgSchema::Optionality::kRequired)},
                 }));
  ASSERT_TRUE(library.Compile());

  auto example_struct = library.LookupStruct("MyStruct");
  ASSERT_NOT_NULL(example_struct);
  EXPECT_TRUE(example_struct->HasAttribute("foo"));
  EXPECT_TRUE(example_struct->HasAttributeArg("foo", "inferrable"));
}

// If a schema is provided (ie, this is an "official" FIDL attribute), and it specifies that only
// a single optional argument is allowed, respect both the inclusion and omission of that argument.
TEST(AttributesTests, GoodSingleSchemaArgumentRespectOptionality) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kNewSyntaxOnly);
  TestLibrary library(R"FIDL(
library example;

@foo("bar")
type MyStruct = struct {};

@foo
type MyOtherStruct = struct {};

)FIDL",
                      experimental_flags);
  library.AddAttributeSchema(
      "foo", fidl::flat::AttributeSchema(
                 {
                     fidl::flat::AttributePlacement::kStructDecl,
                 },
                 {
                     {"value", fidl::flat::AttributeArgSchema(
                                   fidl::flat::ConstantValue::Kind::kString,
                                   fidl::flat::AttributeArgSchema::Optionality::kOptional)},
                 }));
  ASSERT_TRUE(library.Compile());
}

// If a schema is provided (ie, this is an "official" FIDL attribute), and it specifies that only
// a single argument is allowed, naming that argument is an error.
TEST(AttributesTests, BadSingleSchemaArgumentIsNamed) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kNewSyntaxOnly);
  TestLibrary library(R"FIDL(
library example;

@foo(value="bar")
type MyStruct = struct {};

)FIDL",
                      experimental_flags);
  library.AddAttributeSchema(
      "foo", fidl::flat::AttributeSchema(
                 {
                     fidl::flat::AttributePlacement::kStructDecl,
                 },
                 {
                     {"value", fidl::flat::AttributeArgSchema(
                                   fidl::flat::ConstantValue::Kind::kString,
                                   fidl::flat::AttributeArgSchema::Optionality::kRequired)},
                 }));
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrAttributeArgMustNotBeNamed);
}

// If a schema is provided (ie, this is an "official" FIDL attribute), and it specifies that
// multiple arguments are allowed, a single unnamed argument is an error.
TEST(AttributesTests, BadSingleSchemaArgumentIsNotNamed) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kNewSyntaxOnly);
  TestLibrary library(R"FIDL(
library example;

@foo("bar")
type MyStruct = struct {};

)FIDL",
                      experimental_flags);
  library.AddAttributeSchema(
      "foo", fidl::flat::AttributeSchema(
                 {
                     fidl::flat::AttributePlacement::kStructDecl,
                 },
                 {
                     {"value", fidl::flat::AttributeArgSchema(
                                   fidl::flat::ConstantValue::Kind::kString,
                                   fidl::flat::AttributeArgSchema::Optionality::kRequired)},
                     {"other", fidl::flat::AttributeArgSchema(
                                   fidl::flat::ConstantValue::Kind::kString,
                                   fidl::flat::AttributeArgSchema::Optionality::kOptional)},
                 }));
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrAttributeArgNotNamed);
}

TEST(AttributesTests, GoodMultipleSchemaArgumentsRequiredOnly) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kNewSyntaxOnly);
  TestLibrary library(R"FIDL(
library fidl.test;

@multiple_args(first="foo", second="bar")
type MyStruct = struct {};

// Order independent.
@multiple_args(second="bar", first="foo")
type MyOtherStruct = struct {};

)FIDL",
                      experimental_flags);
  library.AddAttributeSchema(
      "multiple_args",
      fidl::flat::AttributeSchema(
          {
              fidl::flat::AttributePlacement::kStructDecl,
          },
          {
              {"first", fidl::flat::AttributeArgSchema(
                            fidl::flat::ConstantValue::Kind::kString,
                            fidl::flat::AttributeArgSchema::Optionality::kRequired)},
              {"second", fidl::flat::AttributeArgSchema(
                             fidl::flat::ConstantValue::Kind::kString,
                             fidl::flat::AttributeArgSchema::Optionality::kRequired)},
          }));
  ASSERT_TRUE(library.Compile());
}

TEST(AttributesTests, GoodMultipleSchemaArgumentsOptionalOnly) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kNewSyntaxOnly);
  TestLibrary library(R"FIDL(
library fidl.test;

@multiple_args(first="foo", second="bar")
type MyStruct = struct {};

// Order independent.
@multiple_args(second="bar", first="foo")
type MyStruct2 = struct {};

// Only 1 argument present.
@multiple_args(first="foo")
type MyStruct3 = struct {};
@multiple_args(second="bar")
type MyStruct4 = struct {};

// No arguments at all.
@multiple_args
type MyStruct5 = struct {};

)FIDL",
                      experimental_flags);
  library.AddAttributeSchema(
      "multiple_args",
      fidl::flat::AttributeSchema(
          {
              fidl::flat::AttributePlacement::kStructDecl,
          },
          {
              {"first", fidl::flat::AttributeArgSchema(
                            fidl::flat::ConstantValue::Kind::kString,
                            fidl::flat::AttributeArgSchema::Optionality::kOptional)},
              {"second", fidl::flat::AttributeArgSchema(
                             fidl::flat::ConstantValue::Kind::kString,
                             fidl::flat::AttributeArgSchema::Optionality::kOptional)},
          }));
  ASSERT_TRUE(library.Compile());
}

TEST(AttributesTests, GoodMultipleSchemaArgumentsRequiredAndOptional) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kNewSyntaxOnly);
  TestLibrary library(R"FIDL(
library fidl.test;

@multiple_args(first="foo", second="bar")
type MyStruct = struct {};

// Order independent.
@multiple_args(second="bar", first="foo")
type MyStruct2 = struct {};

// Only 1 argument present.
@multiple_args(first="foo")
type MyStruct3 = struct {};

)FIDL",
                      experimental_flags);
  library.AddAttributeSchema(
      "multiple_args",
      fidl::flat::AttributeSchema(
          {
              fidl::flat::AttributePlacement::kStructDecl,
          },
          {
              {"first", fidl::flat::AttributeArgSchema(
                            fidl::flat::ConstantValue::Kind::kString,
                            fidl::flat::AttributeArgSchema::Optionality::kRequired)},
              {"second", fidl::flat::AttributeArgSchema(
                             fidl::flat::ConstantValue::Kind::kString,
                             fidl::flat::AttributeArgSchema::Optionality::kOptional)},
          }));
  ASSERT_TRUE(library.Compile());
}

TEST(AttributesTests, BadMultipleSchemaArgumentsRequiredMissing) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kNewSyntaxOnly);
  TestLibrary library(R"FIDL(
library fidl.test;

@multiple_args(optional="foo")
type MyStruct = struct {};

)FIDL",
                      experimental_flags);
  library.AddAttributeSchema(
      "multiple_args",
      fidl::flat::AttributeSchema(
          {
              fidl::flat::AttributePlacement::kStructDecl,
          },
          {
              {"required", fidl::flat::AttributeArgSchema(
                               fidl::flat::ConstantValue::Kind::kString,
                               fidl::flat::AttributeArgSchema::Optionality::kRequired)},
              {"optional", fidl::flat::AttributeArgSchema(
                               fidl::flat::ConstantValue::Kind::kString,
                               fidl::flat::AttributeArgSchema::Optionality::kOptional)},
          }));
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrMissingRequiredAttributeArg);
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "multiple_args");
}

TEST(AttributesTests, GoodLiteralTypesWithoutSchema) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kNewSyntaxOnly);
  TestLibrary library(R"FIDL(
library example;

@attr(foo="abc", bar=true, baz=false)
type MyStruct = struct {};

)FIDL",
                      experimental_flags);
  ASSERT_TRUE(library.Compile());

  auto example_struct = library.LookupStruct("MyStruct");
  ASSERT_NOT_NULL(example_struct);
  EXPECT_TRUE(example_struct->HasAttribute("attr"));

  // Check `foo` arg.
  EXPECT_TRUE(example_struct->HasAttributeArg("attr", "foo"));
  const auto& foo = example_struct->GetAttributeArg("attr", "foo").value().get().value;
  EXPECT_STR_EQ(foo->span.data(), "\"abc\"");
  ASSERT_EQ(foo->kind, fidl::flat::Constant::Kind::kLiteral);

  std::unique_ptr<fidl::flat::ConstantValue> resolved_foo;
  EXPECT_TRUE(foo->Value().Convert(fidl::flat::ConstantValue::Kind::kString, &resolved_foo));

  // Check `baz` arg.
  EXPECT_TRUE(example_struct->HasAttributeArg("attr", "baz"));
  const auto& baz = example_struct->GetAttributeArg("attr", "baz").value().get().value;
  EXPECT_STR_EQ(baz->span.data(), "false");
  ASSERT_EQ(baz->kind, fidl::flat::Constant::Kind::kLiteral);

  std::unique_ptr<fidl::flat::ConstantValue> resolved_baz;
  EXPECT_TRUE(baz->Value().Convert(fidl::flat::ConstantValue::Kind::kBool, &resolved_baz));
}

TEST(AttributesTests, BadLiteralNumericTypesWithoutSchema) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kNewSyntaxOnly);
  TestLibrary library(R"FIDL(
library example;

@attr(foo=1, bar=2.3)
type MyStruct = struct {};

)FIDL",
                      experimental_flags);
  ASSERT_ERRORED_TWICE_DURING_COMPILE(library, fidl::ErrCannotUseNumericArgsOnCustomAttributes,
                                      fidl::ErrCannotUseNumericArgsOnCustomAttributes);
}

TEST(AttributesTests, GoodReferencedTypesWithoutSchema) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kNewSyntaxOnly);
  TestLibrary library(R"FIDL(
library example;

const foo string:3 = "abc";
const bar bool = true;
const baz bool = false;

@attr(foo=foo, bar=bar, baz=baz)
type MyStruct = struct {};

)FIDL",
                      experimental_flags);
  ASSERT_TRUE(library.Compile());

  auto example_struct = library.LookupStruct("MyStruct");
  ASSERT_NOT_NULL(example_struct);
  EXPECT_TRUE(example_struct->HasAttribute("attr"));

  // Check `foo` arg.
  EXPECT_TRUE(example_struct->HasAttributeArg("attr", "foo"));
  const auto& foo = example_struct->GetAttributeArg("attr", "foo").value().get().value;
  EXPECT_STR_EQ(foo->span.data(), "foo");
  ASSERT_EQ(foo->kind, fidl::flat::Constant::Kind::kIdentifier);

  std::unique_ptr<fidl::flat::ConstantValue> resolved_foo;
  EXPECT_TRUE(foo->Value().Convert(fidl::flat::ConstantValue::Kind::kString, &resolved_foo));
  EXPECT_STR_EQ(static_cast<fidl::flat::StringConstantValue*>(resolved_foo.get())->MakeContents(),
                "abc");

  // Check `bar` arg.
  EXPECT_TRUE(example_struct->HasAttributeArg("attr", "bar"));
  const auto& bar = example_struct->GetAttributeArg("attr", "bar").value().get().value;
  EXPECT_STR_EQ(bar->span.data(), "bar");
  ASSERT_EQ(bar->kind, fidl::flat::Constant::Kind::kIdentifier);

  std::unique_ptr<fidl::flat::ConstantValue> resolved_bar;
  EXPECT_TRUE(bar->Value().Convert(fidl::flat::ConstantValue::Kind::kBool, &resolved_bar));
  EXPECT_TRUE(static_cast<fidl::flat::BoolConstantValue*>(resolved_bar.get())->value);

  // Check `baz` arg.
  EXPECT_TRUE(example_struct->HasAttributeArg("attr", "baz"));
  const auto& baz = example_struct->GetAttributeArg("attr", "baz").value().get().value;
  EXPECT_STR_EQ(baz->span.data(), "baz");
  ASSERT_EQ(baz->kind, fidl::flat::Constant::Kind::kIdentifier);

  std::unique_ptr<fidl::flat::ConstantValue> resolved_baz;
  EXPECT_TRUE(baz->Value().Convert(fidl::flat::ConstantValue::Kind::kBool, &resolved_baz));
  EXPECT_TRUE(!static_cast<fidl::flat::BoolConstantValue*>(resolved_baz.get())->value);
}

TEST(AttributesTests, BadReferencedNumericTypesWithoutSchema) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kNewSyntaxOnly);
  TestLibrary library(R"FIDL(
library example;

const foo int8 = -1;
const bar float32 = -2.3;

@attr(foo=foo, bar=bar)
type MyStruct = struct {};

)FIDL",
                      experimental_flags);
  ASSERT_ERRORED_TWICE_DURING_COMPILE(library, fidl::ErrCannotUseNumericArgsOnCustomAttributes,
                                      fidl::ErrCannotUseNumericArgsOnCustomAttributes);
}

TEST(AttributesTests, GoodLiteralTypesWithSchema) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kNewSyntaxOnly);
  TestLibrary library(R"FIDL(
library fidl.test;

@attr(
        string="foo",
        bool=true,
        int8=-1,
        int16=-2,
        int32=-3,
        int64=-4,
        uint8=1,
        uint16=2,
        uint32=3,
        uint64=4,
        float32=1.2,
        float64=-3.4)
type MyStruct = struct {};

)FIDL",
                      experimental_flags);
  library.AddAttributeSchema(
      "attr",
      fidl::flat::AttributeSchema(
          {
              fidl::flat::AttributePlacement::kStructDecl,
          },
          {
              {"string", fidl::flat::AttributeArgSchema(fidl::flat::ConstantValue::Kind::kString)},
              {"bool", fidl::flat::AttributeArgSchema(fidl::flat::ConstantValue::Kind::kBool)},
              {"int8", fidl::flat::AttributeArgSchema(fidl::flat::ConstantValue::Kind::kInt8)},
              {"int16", fidl::flat::AttributeArgSchema(fidl::flat::ConstantValue::Kind::kInt16)},
              {"int32", fidl::flat::AttributeArgSchema(fidl::flat::ConstantValue::Kind::kInt32)},
              {"int64", fidl::flat::AttributeArgSchema(fidl::flat::ConstantValue::Kind::kInt64)},
              {"uint8", fidl::flat::AttributeArgSchema(fidl::flat::ConstantValue::Kind::kUint8)},
              {"uint16", fidl::flat::AttributeArgSchema(fidl::flat::ConstantValue::Kind::kUint16)},
              {"uint32", fidl::flat::AttributeArgSchema(fidl::flat::ConstantValue::Kind::kUint32)},
              {"uint64", fidl::flat::AttributeArgSchema(fidl::flat::ConstantValue::Kind::kUint64)},
              {"float32",
               fidl::flat::AttributeArgSchema(fidl::flat::ConstantValue::Kind::kFloat32)},
              {"float64",
               fidl::flat::AttributeArgSchema(fidl::flat::ConstantValue::Kind::kFloat64)},
          }));
  ASSERT_TRUE(library.Compile());

  auto example_struct = library.LookupStruct("MyStruct");
  ASSERT_NOT_NULL(example_struct);
  EXPECT_TRUE(example_struct->HasAttribute("attr"));

  // Check `string` arg.
  EXPECT_TRUE(example_struct->HasAttributeArg("attr", "string"));
  const auto& string_val = example_struct->GetAttributeArg("attr", "string").value().get().value;
  EXPECT_STR_EQ(string_val->span.data(), "\"foo\"");
  ASSERT_EQ(string_val->kind, fidl::flat::Constant::Kind::kLiteral);

  std::unique_ptr<fidl::flat::ConstantValue> resolved_string;
  EXPECT_TRUE(
      string_val->Value().Convert(fidl::flat::ConstantValue::Kind::kString, &resolved_string));
  EXPECT_STR_EQ(
      static_cast<fidl::flat::StringConstantValue*>(resolved_string.get())->MakeContents(), "foo");

  // Check `bool` arg.
  EXPECT_TRUE(example_struct->HasAttributeArg("attr", "bool"));
  const auto& bool_val = example_struct->GetAttributeArg("attr", "bool").value().get().value;
  EXPECT_STR_EQ(bool_val->span.data(), "true");
  ASSERT_EQ(bool_val->kind, fidl::flat::Constant::Kind::kLiteral);

  std::unique_ptr<fidl::flat::ConstantValue> resolved_bool;
  EXPECT_TRUE(bool_val->Value().Convert(fidl::flat::ConstantValue::Kind::kBool, &resolved_bool));
  EXPECT_EQ(static_cast<fidl::flat::BoolConstantValue*>(resolved_bool.get())->value, true);

  // Check `int8` arg.
  EXPECT_TRUE(example_struct->HasAttributeArg("attr", "int8"));
  const auto& int8_val = example_struct->GetAttributeArg("attr", "int8").value().get().value;
  EXPECT_STR_EQ(int8_val->span.data(), "-1");
  ASSERT_EQ(int8_val->kind, fidl::flat::Constant::Kind::kLiteral);

  std::unique_ptr<fidl::flat::ConstantValue> resolved_int8;
  EXPECT_TRUE(int8_val->Value().Convert(fidl::flat::ConstantValue::Kind::kInt8, &resolved_int8));
  EXPECT_EQ(static_cast<fidl::flat::NumericConstantValue<int8_t>*>(resolved_int8.get())->value, -1);

  // Check `int16` arg.
  EXPECT_TRUE(example_struct->HasAttributeArg("attr", "int16"));
  const auto& int16_val = example_struct->GetAttributeArg("attr", "int16").value().get().value;
  EXPECT_STR_EQ(int16_val->span.data(), "-2");
  ASSERT_EQ(int16_val->kind, fidl::flat::Constant::Kind::kLiteral);

  std::unique_ptr<fidl::flat::ConstantValue> resolved_int16;
  EXPECT_TRUE(int16_val->Value().Convert(fidl::flat::ConstantValue::Kind::kInt16, &resolved_int16));
  EXPECT_EQ(static_cast<fidl::flat::NumericConstantValue<int16_t>*>(resolved_int16.get())->value,
            -2);

  // Check `int32` arg.
  EXPECT_TRUE(example_struct->HasAttributeArg("attr", "int32"));
  const auto& int32_val = example_struct->GetAttributeArg("attr", "int32").value().get().value;
  EXPECT_STR_EQ(int32_val->span.data(), "-3");
  ASSERT_EQ(int32_val->kind, fidl::flat::Constant::Kind::kLiteral);

  std::unique_ptr<fidl::flat::ConstantValue> resolved_int32;
  EXPECT_TRUE(int32_val->Value().Convert(fidl::flat::ConstantValue::Kind::kInt32, &resolved_int32));
  EXPECT_EQ(static_cast<fidl::flat::NumericConstantValue<int32_t>*>(resolved_int32.get())->value,
            -3);

  // Check `int64` arg.
  EXPECT_TRUE(example_struct->HasAttributeArg("attr", "int64"));
  const auto& int64_val = example_struct->GetAttributeArg("attr", "int64").value().get().value;
  EXPECT_STR_EQ(int64_val->span.data(), "-4");
  ASSERT_EQ(int64_val->kind, fidl::flat::Constant::Kind::kLiteral);

  std::unique_ptr<fidl::flat::ConstantValue> resolved_int64;
  EXPECT_TRUE(int64_val->Value().Convert(fidl::flat::ConstantValue::Kind::kInt64, &resolved_int64));
  EXPECT_EQ(static_cast<fidl::flat::NumericConstantValue<int64_t>*>(resolved_int64.get())->value,
            -4);

  // Check `uint8` arg.
  EXPECT_TRUE(example_struct->HasAttributeArg("attr", "uint8"));
  const auto& uint8_val = example_struct->GetAttributeArg("attr", "uint8").value().get().value;
  EXPECT_STR_EQ(uint8_val->span.data(), "1");
  ASSERT_EQ(uint8_val->kind, fidl::flat::Constant::Kind::kLiteral);

  std::unique_ptr<fidl::flat::ConstantValue> resolved_uint8;
  EXPECT_TRUE(uint8_val->Value().Convert(fidl::flat::ConstantValue::Kind::kUint8, &resolved_uint8));
  EXPECT_EQ(static_cast<fidl::flat::NumericConstantValue<uint8_t>*>(resolved_uint8.get())->value,
            1);

  // Check `uint16` arg.
  EXPECT_TRUE(example_struct->HasAttributeArg("attr", "uint16"));
  const auto& uint16_val = example_struct->GetAttributeArg("attr", "uint16").value().get().value;
  EXPECT_STR_EQ(uint16_val->span.data(), "2");
  ASSERT_EQ(uint16_val->kind, fidl::flat::Constant::Kind::kLiteral);

  std::unique_ptr<fidl::flat::ConstantValue> resolved_uint16;
  EXPECT_TRUE(
      uint16_val->Value().Convert(fidl::flat::ConstantValue::Kind::kUint16, &resolved_uint16));
  EXPECT_EQ(static_cast<fidl::flat::NumericConstantValue<uint16_t>*>(resolved_uint16.get())->value,
            2);

  // Check `uint32` arg.
  EXPECT_TRUE(example_struct->HasAttributeArg("attr", "uint32"));
  const auto& uint32_val = example_struct->GetAttributeArg("attr", "uint32").value().get().value;
  EXPECT_STR_EQ(uint32_val->span.data(), "3");
  ASSERT_EQ(uint32_val->kind, fidl::flat::Constant::Kind::kLiteral);

  std::unique_ptr<fidl::flat::ConstantValue> resolved_uint32;
  EXPECT_TRUE(
      uint32_val->Value().Convert(fidl::flat::ConstantValue::Kind::kUint32, &resolved_uint32));
  EXPECT_EQ(static_cast<fidl::flat::NumericConstantValue<uint32_t>*>(resolved_uint32.get())->value,
            3);

  // Check `uint64` arg.
  EXPECT_TRUE(example_struct->HasAttributeArg("attr", "uint64"));
  const auto& uint64_val = example_struct->GetAttributeArg("attr", "uint64").value().get().value;
  EXPECT_STR_EQ(uint64_val->span.data(), "4");
  ASSERT_EQ(uint64_val->kind, fidl::flat::Constant::Kind::kLiteral);

  std::unique_ptr<fidl::flat::ConstantValue> resolved_uint64;
  EXPECT_TRUE(
      uint64_val->Value().Convert(fidl::flat::ConstantValue::Kind::kUint64, &resolved_uint64));
  EXPECT_EQ(static_cast<fidl::flat::NumericConstantValue<uint64_t>*>(resolved_uint64.get())->value,
            4);

  // Check `float32` arg.
  EXPECT_TRUE(example_struct->HasAttributeArg("attr", "float32"));
  const auto& float32_val = example_struct->GetAttributeArg("attr", "float32").value().get().value;
  EXPECT_STR_EQ(float32_val->span.data(), "1.2");
  ASSERT_EQ(float32_val->kind, fidl::flat::Constant::Kind::kLiteral);

  std::unique_ptr<fidl::flat::ConstantValue> resolved_float32;
  EXPECT_TRUE(
      float32_val->Value().Convert(fidl::flat::ConstantValue::Kind::kFloat32, &resolved_float32));
  EXPECT_TRUE(static_cast<fidl::flat::NumericConstantValue<float>*>(resolved_float32.get())->value >
              1.1);
  EXPECT_TRUE(static_cast<fidl::flat::NumericConstantValue<float>*>(resolved_float32.get())->value <
              1.3);

  // Check `float64` arg.
  EXPECT_TRUE(example_struct->HasAttributeArg("attr", "float64"));
  const auto& float64_val = example_struct->GetAttributeArg("attr", "float64").value().get().value;
  EXPECT_STR_EQ(float64_val->span.data(), "-3.4");
  ASSERT_EQ(float64_val->kind, fidl::flat::Constant::Kind::kLiteral);

  std::unique_ptr<fidl::flat::ConstantValue> resolved_float64;
  EXPECT_TRUE(
      float64_val->Value().Convert(fidl::flat::ConstantValue::Kind::kFloat64, &resolved_float64));
  EXPECT_TRUE(
      static_cast<fidl::flat::NumericConstantValue<double>*>(resolved_float64.get())->value > -3.5);
  EXPECT_TRUE(
      static_cast<fidl::flat::NumericConstantValue<double>*>(resolved_float64.get())->value < -3.3);
}

TEST(AttributesTests, BadInvalidLiteralStringTypeWithSchema) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kNewSyntaxOnly);
  TestLibrary library(R"FIDL(
library example;

@attr(true)
type MyStruct = struct {};

)FIDL",
                      experimental_flags);
  library.AddAttributeSchema(
      "attr",
      fidl::flat::AttributeSchema(
          {
              fidl::flat::AttributePlacement::kStructDecl,
          },
          {
              {"string", fidl::flat::AttributeArgSchema(fidl::flat::ConstantValue::Kind::kString)},
          }));
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrConstantCannotBeInterpretedAsType);
}

TEST(AttributesTests, BadInvalidLiteralBoolTypeWithSchema) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kNewSyntaxOnly);
  TestLibrary library(R"FIDL(
library example;

@attr("foo")
type MyStruct = struct {};

)FIDL",
                      experimental_flags);
  library.AddAttributeSchema(
      "attr",
      fidl::flat::AttributeSchema(
          {
              fidl::flat::AttributePlacement::kStructDecl,
          },
          {
              {"bool", fidl::flat::AttributeArgSchema(fidl::flat::ConstantValue::Kind::kBool)},
          }));
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrConstantCannotBeInterpretedAsType);
}

TEST(AttributesTests, BadInvalidLiteralNumericTypeWithSchema) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kNewSyntaxOnly);
  TestLibrary library(R"FIDL(
library example;

@attr(-1)
type MyStruct = struct {};

)FIDL",
                      experimental_flags);
  library.AddAttributeSchema(
      "attr",
      fidl::flat::AttributeSchema(
          {
              fidl::flat::AttributePlacement::kStructDecl,
          },
          {
              {"uint8", fidl::flat::AttributeArgSchema(fidl::flat::ConstantValue::Kind::kUint8)},
          }));
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrConstantCannotBeInterpretedAsType);
}

TEST(AttributesTests, GoodReferencedTypesWithSchema) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kNewSyntaxOnly);
  TestLibrary library(R"FIDL(
library fidl.test;

const string string = "foo";
const bool bool = true;
const int8 int8 = -1;
const int16 int16 = -2;
const int32 int32 = -3;
type int64 = enum : int64 {
    MEMBER = -4;
};
const uint8 uint8 = 1;
const uint16 uint16 = 2;
const uint32 uint32 = 3;
type uint64 = bits : uint64 {
    MEMBER = 4;
};
const float32 float32 = 1.2;
const float64 float64 = -3.4;

@attr(
        string=string,
        bool=bool,
        int8=int8,
        int16=int16,
        int32=int32,
        int64=int64.MEMBER,
        uint8=uint8,
        uint16=uint16,
        uint32=uint32,
        uint64=uint64.MEMBER,
        float32=float32,
        float64=float64)
type MyStruct = struct {};

)FIDL",
                      experimental_flags);
  library.AddAttributeSchema(
      "attr",
      fidl::flat::AttributeSchema(
          {
              fidl::flat::AttributePlacement::kStructDecl,
          },
          {
              {"string", fidl::flat::AttributeArgSchema(fidl::flat::ConstantValue::Kind::kString)},
              {"bool", fidl::flat::AttributeArgSchema(fidl::flat::ConstantValue::Kind::kBool)},
              {"int8", fidl::flat::AttributeArgSchema(fidl::flat::ConstantValue::Kind::kInt8)},
              {"int16", fidl::flat::AttributeArgSchema(fidl::flat::ConstantValue::Kind::kInt16)},
              {"int32", fidl::flat::AttributeArgSchema(fidl::flat::ConstantValue::Kind::kInt32)},
              {"int64", fidl::flat::AttributeArgSchema(fidl::flat::ConstantValue::Kind::kInt64)},
              {"uint8", fidl::flat::AttributeArgSchema(fidl::flat::ConstantValue::Kind::kUint8)},
              {"uint16", fidl::flat::AttributeArgSchema(fidl::flat::ConstantValue::Kind::kUint16)},
              {"uint32", fidl::flat::AttributeArgSchema(fidl::flat::ConstantValue::Kind::kUint32)},
              {"uint64", fidl::flat::AttributeArgSchema(fidl::flat::ConstantValue::Kind::kUint64)},
              {"float32",
               fidl::flat::AttributeArgSchema(fidl::flat::ConstantValue::Kind::kFloat32)},
              {"float64",
               fidl::flat::AttributeArgSchema(fidl::flat::ConstantValue::Kind::kFloat64)},
          }));
  ASSERT_TRUE(library.Compile());

  auto example_struct = library.LookupStruct("MyStruct");
  ASSERT_NOT_NULL(example_struct);
  EXPECT_TRUE(example_struct->HasAttribute("attr"));

  // Check `string` arg.
  EXPECT_TRUE(example_struct->HasAttributeArg("attr", "string"));
  const auto& string_val = example_struct->GetAttributeArg("attr", "string").value().get().value;
  EXPECT_STR_EQ(string_val->span.data(), "string");
  ASSERT_EQ(string_val->kind, fidl::flat::Constant::Kind::kIdentifier);

  std::unique_ptr<fidl::flat::ConstantValue> resolved_string;
  EXPECT_TRUE(
      string_val->Value().Convert(fidl::flat::ConstantValue::Kind::kString, &resolved_string));
  EXPECT_STR_EQ(
      static_cast<fidl::flat::StringConstantValue*>(resolved_string.get())->MakeContents(), "foo");

  // Check `bool` arg.
  EXPECT_TRUE(example_struct->HasAttributeArg("attr", "bool"));
  const auto& bool_val = example_struct->GetAttributeArg("attr", "bool").value().get().value;
  EXPECT_STR_EQ(bool_val->span.data(), "bool");
  ASSERT_EQ(bool_val->kind, fidl::flat::Constant::Kind::kIdentifier);

  std::unique_ptr<fidl::flat::ConstantValue> resolved_bool;
  EXPECT_TRUE(bool_val->Value().Convert(fidl::flat::ConstantValue::Kind::kBool, &resolved_bool));
  EXPECT_EQ(static_cast<fidl::flat::BoolConstantValue*>(resolved_bool.get())->value, true);

  // Check `int8` arg.
  EXPECT_TRUE(example_struct->HasAttributeArg("attr", "int8"));
  const auto& int8_val = example_struct->GetAttributeArg("attr", "int8").value().get().value;
  EXPECT_STR_EQ(int8_val->span.data(), "int8");
  ASSERT_EQ(int8_val->kind, fidl::flat::Constant::Kind::kIdentifier);

  std::unique_ptr<fidl::flat::ConstantValue> resolved_int8;
  EXPECT_TRUE(int8_val->Value().Convert(fidl::flat::ConstantValue::Kind::kInt8, &resolved_int8));
  EXPECT_EQ(static_cast<fidl::flat::NumericConstantValue<int8_t>*>(resolved_int8.get())->value, -1);

  // Check `int16` arg.
  EXPECT_TRUE(example_struct->HasAttributeArg("attr", "int16"));
  const auto& int16_val = example_struct->GetAttributeArg("attr", "int16").value().get().value;
  EXPECT_STR_EQ(int16_val->span.data(), "int16");
  ASSERT_EQ(int16_val->kind, fidl::flat::Constant::Kind::kIdentifier);

  std::unique_ptr<fidl::flat::ConstantValue> resolved_int16;
  EXPECT_TRUE(int16_val->Value().Convert(fidl::flat::ConstantValue::Kind::kInt16, &resolved_int16));
  EXPECT_EQ(static_cast<fidl::flat::NumericConstantValue<int16_t>*>(resolved_int16.get())->value,
            -2);

  // Check `int32` arg.
  EXPECT_TRUE(example_struct->HasAttributeArg("attr", "int32"));
  const auto& int32_val = example_struct->GetAttributeArg("attr", "int32").value().get().value;
  EXPECT_STR_EQ(int32_val->span.data(), "int32");
  ASSERT_EQ(int32_val->kind, fidl::flat::Constant::Kind::kIdentifier);

  std::unique_ptr<fidl::flat::ConstantValue> resolved_int32;
  EXPECT_TRUE(int32_val->Value().Convert(fidl::flat::ConstantValue::Kind::kInt32, &resolved_int32));
  EXPECT_EQ(static_cast<fidl::flat::NumericConstantValue<int32_t>*>(resolved_int32.get())->value,
            -3);

  // Check `int64` arg.
  EXPECT_TRUE(example_struct->HasAttributeArg("attr", "int64"));
  const auto& int64_val = example_struct->GetAttributeArg("attr", "int64").value().get().value;
  EXPECT_STR_EQ(int64_val->span.data(), "int64.MEMBER");
  ASSERT_EQ(int64_val->kind, fidl::flat::Constant::Kind::kIdentifier);

  std::unique_ptr<fidl::flat::ConstantValue> resolved_int64;
  EXPECT_TRUE(int64_val->Value().Convert(fidl::flat::ConstantValue::Kind::kInt64, &resolved_int64));
  EXPECT_EQ(static_cast<fidl::flat::NumericConstantValue<int64_t>*>(resolved_int64.get())->value,
            -4);

  // Check `uint8` arg.
  EXPECT_TRUE(example_struct->HasAttributeArg("attr", "uint8"));
  const auto& uint8_val = example_struct->GetAttributeArg("attr", "uint8").value().get().value;
  EXPECT_STR_EQ(uint8_val->span.data(), "uint8");
  ASSERT_EQ(uint8_val->kind, fidl::flat::Constant::Kind::kIdentifier);

  std::unique_ptr<fidl::flat::ConstantValue> resolved_uint8;
  EXPECT_TRUE(uint8_val->Value().Convert(fidl::flat::ConstantValue::Kind::kUint8, &resolved_uint8));
  EXPECT_EQ(static_cast<fidl::flat::NumericConstantValue<uint8_t>*>(resolved_uint8.get())->value,
            1);

  // Check `uint16` arg.
  EXPECT_TRUE(example_struct->HasAttributeArg("attr", "uint16"));
  const auto& uint16_val = example_struct->GetAttributeArg("attr", "uint16").value().get().value;
  EXPECT_STR_EQ(uint16_val->span.data(), "uint16");
  ASSERT_EQ(uint16_val->kind, fidl::flat::Constant::Kind::kIdentifier);

  std::unique_ptr<fidl::flat::ConstantValue> resolved_uint16;
  EXPECT_TRUE(
      uint16_val->Value().Convert(fidl::flat::ConstantValue::Kind::kUint16, &resolved_uint16));
  EXPECT_EQ(static_cast<fidl::flat::NumericConstantValue<uint16_t>*>(resolved_uint16.get())->value,
            2);

  // Check `uint32` arg.
  EXPECT_TRUE(example_struct->HasAttributeArg("attr", "uint32"));
  const auto& uint32_val = example_struct->GetAttributeArg("attr", "uint32").value().get().value;
  EXPECT_STR_EQ(uint32_val->span.data(), "uint32");
  ASSERT_EQ(uint32_val->kind, fidl::flat::Constant::Kind::kIdentifier);

  std::unique_ptr<fidl::flat::ConstantValue> resolved_uint32;
  EXPECT_TRUE(
      uint32_val->Value().Convert(fidl::flat::ConstantValue::Kind::kUint32, &resolved_uint32));
  EXPECT_EQ(static_cast<fidl::flat::NumericConstantValue<uint32_t>*>(resolved_uint32.get())->value,
            3);

  // Check `uint64` arg.
  EXPECT_TRUE(example_struct->HasAttributeArg("attr", "uint64"));
  const auto& uint64_val = example_struct->GetAttributeArg("attr", "uint64").value().get().value;
  EXPECT_STR_EQ(uint64_val->span.data(), "uint64.MEMBER");
  ASSERT_EQ(uint64_val->kind, fidl::flat::Constant::Kind::kIdentifier);

  std::unique_ptr<fidl::flat::ConstantValue> resolved_uint64;
  EXPECT_TRUE(
      uint64_val->Value().Convert(fidl::flat::ConstantValue::Kind::kUint64, &resolved_uint64));
  EXPECT_EQ(static_cast<fidl::flat::NumericConstantValue<uint64_t>*>(resolved_uint64.get())->value,
            4);

  // Check `float32` arg.
  EXPECT_TRUE(example_struct->HasAttributeArg("attr", "float32"));
  const auto& float32_val = example_struct->GetAttributeArg("attr", "float32").value().get().value;
  EXPECT_STR_EQ(float32_val->span.data(), "float32");
  ASSERT_EQ(float32_val->kind, fidl::flat::Constant::Kind::kIdentifier);

  std::unique_ptr<fidl::flat::ConstantValue> resolved_float32;
  EXPECT_TRUE(
      float32_val->Value().Convert(fidl::flat::ConstantValue::Kind::kFloat32, &resolved_float32));
  EXPECT_TRUE(static_cast<fidl::flat::NumericConstantValue<float>*>(resolved_float32.get())->value >
              1.1);
  EXPECT_TRUE(static_cast<fidl::flat::NumericConstantValue<float>*>(resolved_float32.get())->value <
              1.3);

  // Check `float64` arg.
  EXPECT_TRUE(example_struct->HasAttributeArg("attr", "float64"));
  const auto& float64_val = example_struct->GetAttributeArg("attr", "float64").value().get().value;
  EXPECT_STR_EQ(float64_val->span.data(), "float64");
  ASSERT_EQ(float64_val->kind, fidl::flat::Constant::Kind::kIdentifier);

  std::unique_ptr<fidl::flat::ConstantValue> resolved_float64;
  EXPECT_TRUE(
      float64_val->Value().Convert(fidl::flat::ConstantValue::Kind::kFloat64, &resolved_float64));
  EXPECT_TRUE(
      static_cast<fidl::flat::NumericConstantValue<double>*>(resolved_float64.get())->value > -3.5);
  EXPECT_TRUE(
      static_cast<fidl::flat::NumericConstantValue<double>*>(resolved_float64.get())->value < -3.3);
}

TEST(AttributesTests, BadInvalidReferencedStringTypeWithSchema) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kNewSyntaxOnly);
  TestLibrary library(R"FIDL(
library example;

const foo bool = true;

@attr(foo)
type MyStruct = struct {};

)FIDL",
                      experimental_flags);
  library.AddAttributeSchema(
      "attr",
      fidl::flat::AttributeSchema(
          {
              fidl::flat::AttributePlacement::kStructDecl,
          },
          {
              {"string", fidl::flat::AttributeArgSchema(fidl::flat::ConstantValue::Kind::kString)},
          }));
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrCannotConvertConstantToType);
}

TEST(AttributesTests, BadInvalidReferencedBoolTypeWithSchema) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kNewSyntaxOnly);
  TestLibrary library(R"FIDL(
library example;

const foo string:3 = "foo";

@attr(foo)
type MyStruct = struct {};

)FIDL",
                      experimental_flags);
  library.AddAttributeSchema(
      "attr",
      fidl::flat::AttributeSchema(
          {
              fidl::flat::AttributePlacement::kStructDecl,
          },
          {
              {"bool", fidl::flat::AttributeArgSchema(fidl::flat::ConstantValue::Kind::kBool)},
          }));
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrCannotConvertConstantToType);
}

TEST(AttributesTests, BadInvalidReferencedNumericTypeWithSchema) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kNewSyntaxOnly);
  TestLibrary library(R"FIDL(
library example;

const foo uint16 = 259;

@attr(foo)
type MyStruct = struct {};

)FIDL",
                      experimental_flags);
  library.AddAttributeSchema(
      "attr",
      fidl::flat::AttributeSchema(
          {
              fidl::flat::AttributePlacement::kStructDecl,
          },
          {
              {"int8", fidl::flat::AttributeArgSchema(fidl::flat::ConstantValue::Kind::kInt8)},
          }));
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrCannotConvertConstantToType);
}

}  // namespace
