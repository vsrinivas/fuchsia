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
protocol ExampleProtocol {
    @on_method
    Method(struct { @on_parameter arg exampleusing.Empty; });
};

@on_service
service ExampleService {
    @on_service_member
    member client_end:ExampleProtocol;
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
  EXPECT_TRUE(example_bits->attributes->HasAttribute("on_bits"));
  EXPECT_TRUE(example_bits->members.front().attributes->HasAttribute("on_bits_member"));

  auto example_const = library.LookupConstant("EXAMPLE_CONST");
  ASSERT_NOT_NULL(example_const);
  EXPECT_TRUE(example_const->attributes->HasAttribute("on_const"));

  auto example_enum = library.LookupEnum("ExampleEnum");
  ASSERT_NOT_NULL(example_enum);
  EXPECT_TRUE(example_enum->attributes->HasAttribute("on_enum"));
  EXPECT_TRUE(example_enum->members.front().attributes->HasAttribute("on_enum_member"));

  auto example_protocol = library.LookupProtocol("ExampleProtocol");
  ASSERT_NOT_NULL(example_protocol);
  EXPECT_TRUE(example_protocol->attributes->HasAttribute("on_protocol"));
  EXPECT_TRUE(example_protocol->methods.front().attributes->HasAttribute("on_method"));
  ASSERT_NOT_NULL(example_protocol->methods.front().maybe_request_payload);
  EXPECT_TRUE(example_protocol->methods.front()
                  .maybe_request_payload->members.front()
                  .attributes->HasAttribute("on_parameter"));

  auto example_service = library.LookupService("ExampleService");
  ASSERT_NOT_NULL(example_service);
  EXPECT_TRUE(example_service->attributes->HasAttribute("on_service"));
  EXPECT_TRUE(example_service->members.front().attributes->HasAttribute("on_service_member"));

  auto example_struct = library.LookupStruct("ExampleStruct");
  ASSERT_NOT_NULL(example_struct);
  EXPECT_TRUE(example_struct->attributes->HasAttribute("on_struct"));
  EXPECT_TRUE(example_struct->members.front().attributes->HasAttribute("on_struct_member"));

  auto example_table = library.LookupTable("ExampleTable");
  ASSERT_NOT_NULL(example_table);
  EXPECT_TRUE(example_table->attributes->HasAttribute("on_table"));
  EXPECT_TRUE(
      example_table->members.front().maybe_used->attributes->HasAttribute("on_table_member"));

  auto example_type_alias = library.LookupTypeAlias("ExampleTypeAlias");
  ASSERT_NOT_NULL(example_type_alias);
  EXPECT_TRUE(example_type_alias->attributes->HasAttribute("on_type_alias"));

  auto example_union = library.LookupUnion("ExampleUnion");
  ASSERT_NOT_NULL(example_union);
  EXPECT_TRUE(example_union->attributes->HasAttribute("on_union"));
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
type ExampleStruct = resource struct {};

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
  EXPECT_TRUE(example_const->attributes->HasAttribute("no_doc"));
  EXPECT_TRUE(example_const->HasAttributeArg("doc"));
  auto const_doc_value = static_cast<const fidl::flat::DocCommentConstantValue&>(
      example_const->GetAttributeArg("doc").value().get());
  EXPECT_STR_EQ(const_doc_value.MakeContents(), " For EXAMPLE_CONSTANT\n");
  EXPECT_TRUE(example_const->HasAttributeArg("deprecated"));
  auto const_str_value = static_cast<const fidl::flat::StringConstantValue&>(
      example_const->GetAttributeArg("deprecated").value().get());
  EXPECT_STR_EQ(const_str_value.MakeContents(), "Note");

  auto example_enum = library.LookupEnum("ExampleEnum");
  ASSERT_NOT_NULL(example_enum);
  EXPECT_TRUE(example_enum->attributes->HasAttribute("transitional"));
  EXPECT_TRUE(example_enum->HasAttributeArg("doc"));
  auto enum_doc_value = static_cast<const fidl::flat::DocCommentConstantValue&>(
      example_enum->GetAttributeArg("doc").value().get());
  EXPECT_STR_EQ(enum_doc_value.MakeContents(), " For ExampleEnum\n");
  EXPECT_TRUE(example_enum->HasAttributeArg("deprecated"));
  auto enum_str_value = static_cast<const fidl::flat::StringConstantValue&>(
      example_enum->GetAttributeArg("deprecated").value().get());
  EXPECT_STR_EQ(enum_str_value.MakeContents(), "Reason");
  EXPECT_TRUE(example_enum->members.back().attributes->HasAttribute("unknown"));

  auto example_struct = library.LookupStruct("ExampleStruct");
  ASSERT_NOT_NULL(example_struct);
  EXPECT_TRUE(example_struct->HasAttributeArg("doc"));
  auto struct_doc_value = static_cast<const fidl::flat::DocCommentConstantValue&>(
      example_struct->GetAttributeArg("doc").value().get());
  EXPECT_STR_EQ(struct_doc_value.MakeContents(), " For ExampleStruct\n");
  EXPECT_TRUE(example_struct->HasAttributeArg("max_bytes"));
  auto struct_str_value1 = static_cast<const fidl::flat::StringConstantValue&>(
      example_struct->GetAttributeArg("max_bytes").value().get());
  EXPECT_STR_EQ(struct_str_value1.MakeContents(), "1234");
  EXPECT_TRUE(example_struct->HasAttributeArg("max_handles"));
  auto struct_str_value2 = static_cast<const fidl::flat::StringConstantValue&>(
      example_struct->GetAttributeArg("max_handles").value().get());
  EXPECT_STR_EQ(struct_str_value2.MakeContents(), "5678");

  auto example_protocol = library.LookupProtocol("ExampleProtocol");
  ASSERT_NOT_NULL(example_protocol);
  EXPECT_TRUE(example_protocol->attributes->HasAttribute("discoverable"));
  EXPECT_TRUE(example_protocol->attributes->HasAttribute("for_deprecated_c_bindings"));
  EXPECT_TRUE(example_protocol->HasAttributeArg("doc"));
  auto protocol_doc_value = static_cast<const fidl::flat::DocCommentConstantValue&>(
      example_protocol->GetAttributeArg("doc").value().get());
  EXPECT_STR_EQ(protocol_doc_value.MakeContents(), " For ExampleProtocol\n");
  EXPECT_TRUE(example_protocol->HasAttributeArg("transport"));
  auto protocol_str_value = static_cast<const fidl::flat::StringConstantValue&>(
      example_protocol->GetAttributeArg("transport").value().get());
  EXPECT_STR_EQ(protocol_str_value.MakeContents(), "Syscall");

  auto& example_method = example_protocol->methods.front();
  EXPECT_TRUE(example_method.attributes->HasAttribute("internal"));
  EXPECT_TRUE(example_method.attributes->HasAttribute("transitional"));
  EXPECT_TRUE(example_method.attributes->HasAttributeArg("doc"));
  auto method_doc_value = static_cast<const fidl::flat::DocCommentConstantValue&>(
      example_method.attributes->GetAttributeArg("doc").value().get());
  EXPECT_STR_EQ(method_doc_value.MakeContents(), " For ExampleMethod\n");
  EXPECT_TRUE(example_method.attributes->HasAttributeArg("selector"));
  auto method_str_value = static_cast<const fidl::flat::StringConstantValue&>(
      example_method.attributes->GetAttributeArg("selector").value().get());
  EXPECT_STR_EQ(method_str_value.MakeContents(), "Bar");

  auto example_service = library.LookupService("ExampleService");
  ASSERT_NOT_NULL(example_service);
  EXPECT_TRUE(example_service->attributes->HasAttribute("no_doc"));
  EXPECT_TRUE(example_service->HasAttributeArg("doc"));
  auto service_doc_value = static_cast<const fidl::flat::DocCommentConstantValue&>(
      example_service->GetAttributeArg("doc").value().get());
  EXPECT_STR_EQ(service_doc_value.MakeContents(), " For ExampleService\n");
  EXPECT_TRUE(example_service->HasAttributeArg("foo"));
  auto service_str_value = static_cast<const fidl::flat::StringConstantValue&>(
      example_service->GetAttributeArg("foo").value().get());
  EXPECT_STR_EQ(service_str_value.MakeContents(), "ExampleService");

  auto& example_service_member = example_service->members.front();
  EXPECT_TRUE(example_service_member.attributes->HasAttribute("no_doc"));
  EXPECT_TRUE(example_service_member.attributes->HasAttributeArg("doc"));
  auto service_member_doc_value = static_cast<const fidl::flat::DocCommentConstantValue&>(
      example_service_member.attributes->GetAttributeArg("doc").value().get());
  EXPECT_STR_EQ(service_member_doc_value.MakeContents(), " For ExampleProtocol\n");
  EXPECT_TRUE(example_service_member.attributes->HasAttributeArg("foo"));
  auto service_member_str_value = static_cast<const fidl::flat::StringConstantValue&>(
      example_service_member.attributes->GetAttributeArg("foo").value().get());
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
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrInvalidTransportType);
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

@for_deprecated_c_bindings
protocol MyProtocol {
    @for_deprecated_c_bindings
    MyMethod();
};

)FIDL");
  EXPECT_FALSE(library.Compile());
  const auto& errors = library.errors();
  ASSERT_EQ(errors.size(), 10);
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
                                 {
                                     "",
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
                                 {
                                     "",
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
                                 {
                                     "",
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
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrInvalidAttributeValue);
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
  EXPECT_TRUE(foo->attributes->HasAttribute("foo"));

  auto bar = library.LookupStruct("Bar");
  ASSERT_NOT_NULL(bar);
  EXPECT_TRUE(bar->attributes->HasAttribute("bar"));

  auto req = library.LookupStruct("MyProtocolMyMethodRequest");
  ASSERT_NOT_NULL(req);
  EXPECT_TRUE(req->attributes->HasAttribute("baz"));

  auto inner = library.LookupStruct("InnerLayout");
  ASSERT_NOT_NULL(inner);
  EXPECT_TRUE(inner->attributes->HasAttribute("qux"));
}

}  // namespace
