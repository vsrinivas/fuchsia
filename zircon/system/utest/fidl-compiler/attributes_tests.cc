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
  TestLibrary dependency("exampleusing.fidl", R"FIDL(
library exampleusing;

[OnDepStruct]
struct Empty {};

)FIDL",
                         &shared);
  TestLibrary converted_dependency;
  ASSERT_COMPILED_AND_CONVERT_INTO(dependency, converted_dependency);

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
alias ExampleTypeAlias = uint32;

[OnUnion]
union ExampleUnion {
    [OnUnionMember]
    1: uint32 variant;
};

)FIDL",
                      &shared);
  ASSERT_TRUE(library.AddDependentLibrary(std::move(dependency)));
  ASSERT_COMPILED_AND_CONVERT_WITH_DEP(library, converted_dependency);

  EXPECT_TRUE(library.library()->HasAttribute("OnLibrary"));

  auto example_bits = library.LookupBits("ExampleBits");
  ASSERT_NOT_NULL(example_bits);
  EXPECT_TRUE(example_bits->attributes->HasAttribute("OnBits"));
  EXPECT_TRUE(example_bits->members.front().attributes->HasAttribute("OnBitsMember"));

  auto example_const = library.LookupConstant("EXAMPLE_CONST");
  ASSERT_NOT_NULL(example_const);
  EXPECT_TRUE(example_const->attributes->HasAttribute("OnConst"));

  auto example_enum = library.LookupEnum("ExampleEnum");
  ASSERT_NOT_NULL(example_enum);
  EXPECT_TRUE(example_enum->attributes->HasAttribute("OnEnum"));
  EXPECT_TRUE(example_enum->members.front().attributes->HasAttribute("OnEnumMember"));

  auto example_protocol = library.LookupProtocol("ExampleProtocol");
  ASSERT_NOT_NULL(example_protocol);
  EXPECT_TRUE(example_protocol->attributes->HasAttribute("OnProtocol"));
  EXPECT_TRUE(example_protocol->methods.front().attributes->HasAttribute("OnMethod"));
  ASSERT_NOT_NULL(example_protocol->methods.front().maybe_request_payload);
  EXPECT_TRUE(example_protocol->methods.front()
                  .maybe_request_payload->members.front()
                  .attributes->HasAttribute("OnParameter"));

  auto example_service = library.LookupService("ExampleService");
  ASSERT_NOT_NULL(example_service);
  EXPECT_TRUE(example_service->attributes->HasAttribute("OnService"));
  EXPECT_TRUE(example_service->members.front().attributes->HasAttribute("OnServiceMember"));

  auto example_struct = library.LookupStruct("ExampleStruct");
  ASSERT_NOT_NULL(example_struct);
  EXPECT_TRUE(example_struct->attributes->HasAttribute("OnStruct"));
  EXPECT_TRUE(example_struct->members.front().attributes->HasAttribute("OnStructMember"));

  auto example_table = library.LookupTable("ExampleTable");
  ASSERT_NOT_NULL(example_table);
  EXPECT_TRUE(example_table->attributes->HasAttribute("OnTable"));
  EXPECT_TRUE(example_table->members.front().maybe_used->attributes->HasAttribute("OnTableMember"));

  auto example_type_alias = library.LookupTypeAlias("ExampleTypeAlias");
  ASSERT_NOT_NULL(example_type_alias);
  EXPECT_TRUE(example_type_alias->attributes->HasAttribute("OnTypeAlias"));

  auto example_union = library.LookupUnion("ExampleUnion");
  ASSERT_NOT_NULL(example_union);
  EXPECT_TRUE(example_union->attributes->HasAttribute("OnUnion"));
  EXPECT_TRUE(example_union->members.front().maybe_used->attributes->HasAttribute("OnUnionMember"));
}

TEST(AttributesTests, GoodPlacementOfAttributesWithOldDep) {
  SharedAmongstLibraries shared;
  TestLibrary dependency("exampleusing.fidl", R"FIDL(
library exampleusing;

[OnDepStruct]
struct Empty {};

)FIDL",
                         &shared);
  TestLibrary cloned_dependency;
  ASSERT_COMPILED_AND_CLONE_INTO(dependency, cloned_dependency);

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
alias ExampleTypeAlias = uint32;

[OnUnion]
union ExampleUnion {
    [OnUnionMember]
    1: uint32 variant;
};

)FIDL",
                      &shared);
  ASSERT_TRUE(library.AddDependentLibrary(std::move(dependency)));
  ASSERT_COMPILED_AND_CONVERT_WITH_DEP(library, cloned_dependency);

  EXPECT_TRUE(library.library()->HasAttribute("OnLibrary"));

  auto example_bits = library.LookupBits("ExampleBits");
  ASSERT_NOT_NULL(example_bits);
  EXPECT_TRUE(example_bits->attributes->HasAttribute("OnBits"));
  EXPECT_TRUE(example_bits->members.front().attributes->HasAttribute("OnBitsMember"));

  auto example_const = library.LookupConstant("EXAMPLE_CONST");
  ASSERT_NOT_NULL(example_const);
  EXPECT_TRUE(example_const->attributes->HasAttribute("OnConst"));

  auto example_enum = library.LookupEnum("ExampleEnum");
  ASSERT_NOT_NULL(example_enum);
  EXPECT_TRUE(example_enum->attributes->HasAttribute("OnEnum"));
  EXPECT_TRUE(example_enum->members.front().attributes->HasAttribute("OnEnumMember"));

  auto example_protocol = library.LookupProtocol("ExampleProtocol");
  ASSERT_NOT_NULL(example_protocol);
  EXPECT_TRUE(example_protocol->attributes->HasAttribute("OnProtocol"));
  EXPECT_TRUE(example_protocol->methods.front().attributes->HasAttribute("OnMethod"));
  ASSERT_NOT_NULL(example_protocol->methods.front().maybe_request_payload);
  EXPECT_TRUE(example_protocol->methods.front()
                  .maybe_request_payload->members.front()
                  .attributes->HasAttribute("OnParameter"));

  auto example_service = library.LookupService("ExampleService");
  ASSERT_NOT_NULL(example_service);
  EXPECT_TRUE(example_service->attributes->HasAttribute("OnService"));
  EXPECT_TRUE(example_service->members.front().attributes->HasAttribute("OnServiceMember"));

  auto example_struct = library.LookupStruct("ExampleStruct");
  ASSERT_NOT_NULL(example_struct);
  EXPECT_TRUE(example_struct->attributes->HasAttribute("OnStruct"));
  EXPECT_TRUE(example_struct->members.front().attributes->HasAttribute("OnStructMember"));

  auto example_table = library.LookupTable("ExampleTable");
  ASSERT_NOT_NULL(example_table);
  EXPECT_TRUE(example_table->attributes->HasAttribute("OnTable"));
  EXPECT_TRUE(example_table->members.front().maybe_used->attributes->HasAttribute("OnTableMember"));

  auto example_type_alias = library.LookupTypeAlias("ExampleTypeAlias");
  ASSERT_NOT_NULL(example_type_alias);
  EXPECT_TRUE(example_type_alias->attributes->HasAttribute("OnTypeAlias"));

  auto example_union = library.LookupUnion("ExampleUnion");
  ASSERT_NOT_NULL(example_union);
  EXPECT_TRUE(example_union->attributes->HasAttribute("OnUnion"));
  EXPECT_TRUE(example_union->members.front().maybe_used->attributes->HasAttribute("OnUnionMember"));
}

TEST(AttributesTests, GoodOfficialAttributes) {
  TestLibrary library("example.fidl", R"FIDL(
[NoDoc]
library example;

/// For EXAMPLE_CONSTANT
[NoDoc, Deprecated = "Note"]
const string EXAMPLE_CONSTANT = "foo";

/// For ExampleEnum
[Deprecated = "Reason", Transitional]
enum ExampleEnum {
    A = 1;
    /// For EnumMember
    [Unknown] B = 2;
};

/// For ExampleStruct
[MaxBytes = "1234", MaxHandles = "5678"]
resource struct ExampleStruct {};

/// For ExampleProtocol
[Discoverable, ForDeprecatedCBindings, Transport = "Syscall"]
protocol ExampleProtocol {
    /// For ExampleMethod
    [Internal, Selector = "Bar", Transitional] ExampleMethod();
};

/// For ExampleService
[Foo = "ExampleService", NoDoc]
service ExampleService {
    /// For ExampleProtocol
    [Foo = "ExampleProtocol", NoDoc]
    ExampleProtocol p;
};
)FIDL");
  ASSERT_COMPILED_AND_CONVERT(library);

  EXPECT_TRUE(library.library()->HasAttribute("NoDoc"));

  auto example_const = library.LookupConstant("EXAMPLE_CONSTANT");
  ASSERT_NOT_NULL(example_const);
  EXPECT_TRUE(example_const->attributes->HasAttribute("NoDoc"));
  EXPECT_TRUE(example_const->HasAttributeArg("Doc"));
  auto const_doc_value = static_cast<const fidl::flat::DocCommentConstantValue&>(
      example_const->GetAttributeArg("Doc").value().get());
  EXPECT_STR_EQ(const_doc_value.MakeContents(), " For EXAMPLE_CONSTANT\n");
  EXPECT_TRUE(example_const->HasAttributeArg("Deprecated"));
  auto const_str_value = static_cast<const fidl::flat::StringConstantValue&>(
      example_const->GetAttributeArg("Deprecated").value().get());
  EXPECT_STR_EQ(const_str_value.MakeContents(), "Note");

  auto example_enum = library.LookupEnum("ExampleEnum");
  ASSERT_NOT_NULL(example_enum);
  EXPECT_TRUE(example_enum->attributes->HasAttribute("Transitional"));
  EXPECT_TRUE(example_enum->HasAttributeArg("Doc"));
  auto enum_doc_value = static_cast<const fidl::flat::DocCommentConstantValue&>(
      example_enum->GetAttributeArg("Doc").value().get());
  EXPECT_STR_EQ(enum_doc_value.MakeContents(), " For ExampleEnum\n");
  EXPECT_TRUE(example_enum->HasAttributeArg("Deprecated"));
  auto enum_str_value = static_cast<const fidl::flat::StringConstantValue&>(
      example_enum->GetAttributeArg("Deprecated").value().get());
  EXPECT_STR_EQ(enum_str_value.MakeContents(), "Reason");
  EXPECT_TRUE(example_enum->members.back().attributes->HasAttribute("Unknown"));

  auto example_struct = library.LookupStruct("ExampleStruct");
  ASSERT_NOT_NULL(example_struct);
  EXPECT_TRUE(example_struct->HasAttributeArg("Doc"));
  auto struct_doc_value = static_cast<const fidl::flat::DocCommentConstantValue&>(
      example_struct->GetAttributeArg("Doc").value().get());
  EXPECT_STR_EQ(struct_doc_value.MakeContents(), " For ExampleStruct\n");
  EXPECT_TRUE(example_struct->HasAttributeArg("MaxBytes"));
  auto struct_str_value1 = static_cast<const fidl::flat::StringConstantValue&>(
      example_struct->GetAttributeArg("MaxBytes").value().get());
  EXPECT_STR_EQ(struct_str_value1.MakeContents(), "1234");
  EXPECT_TRUE(example_struct->HasAttributeArg("MaxHandles"));
  auto struct_str_value2 = static_cast<const fidl::flat::StringConstantValue&>(
      example_struct->GetAttributeArg("MaxHandles").value().get());
  EXPECT_STR_EQ(struct_str_value2.MakeContents(), "5678");

  auto example_protocol = library.LookupProtocol("ExampleProtocol");
  ASSERT_NOT_NULL(example_protocol);
  EXPECT_TRUE(example_protocol->attributes->HasAttribute("Discoverable"));
  EXPECT_TRUE(example_protocol->attributes->HasAttribute("ForDeprecatedCBindings"));
  EXPECT_TRUE(example_protocol->HasAttributeArg("Doc"));
  auto protocol_doc_value = static_cast<const fidl::flat::DocCommentConstantValue&>(
      example_protocol->GetAttributeArg("Doc").value().get());
  EXPECT_STR_EQ(protocol_doc_value.MakeContents(), " For ExampleProtocol\n");
  EXPECT_TRUE(example_protocol->HasAttributeArg("Transport"));
  auto protocol_str_value = static_cast<const fidl::flat::StringConstantValue&>(
      example_protocol->GetAttributeArg("Transport").value().get());
  EXPECT_STR_EQ(protocol_str_value.MakeContents(), "Syscall");

  auto& example_method = example_protocol->methods.front();
  EXPECT_TRUE(example_method.attributes->HasAttribute("Internal"));
  EXPECT_TRUE(example_method.attributes->HasAttribute("Transitional"));
  EXPECT_TRUE(example_method.attributes->HasAttributeArg("Doc"));
  auto method_doc_value = static_cast<const fidl::flat::DocCommentConstantValue&>(
      example_method.attributes->GetAttributeArg("Doc").value().get());
  EXPECT_STR_EQ(method_doc_value.MakeContents(), " For ExampleMethod\n");
  EXPECT_TRUE(example_method.attributes->HasAttributeArg("Selector"));
  auto method_str_value = static_cast<const fidl::flat::StringConstantValue&>(
      example_method.attributes->GetAttributeArg("Selector").value().get());
  EXPECT_STR_EQ(method_str_value.MakeContents(), "Bar");

  auto example_service = library.LookupService("ExampleService");
  ASSERT_NOT_NULL(example_service);
  EXPECT_TRUE(example_service->attributes->HasAttribute("NoDoc"));
  EXPECT_TRUE(example_service->HasAttributeArg("Doc"));
  auto service_doc_value = static_cast<const fidl::flat::DocCommentConstantValue&>(
      example_service->GetAttributeArg("Doc").value().get());
  EXPECT_STR_EQ(service_doc_value.MakeContents(), " For ExampleService\n");
  EXPECT_TRUE(example_service->HasAttributeArg("Foo"));
  auto service_str_value = static_cast<const fidl::flat::StringConstantValue&>(
      example_service->GetAttributeArg("Foo").value().get());
  EXPECT_STR_EQ(service_str_value.MakeContents(), "ExampleService");

  auto& example_service_member = example_service->members.front();
  EXPECT_TRUE(example_service_member.attributes->HasAttribute("NoDoc"));
  EXPECT_TRUE(example_service_member.attributes->HasAttributeArg("Doc"));
  auto service_member_doc_value = static_cast<const fidl::flat::DocCommentConstantValue&>(
      example_service_member.attributes->GetAttributeArg("Doc").value().get());
  EXPECT_STR_EQ(service_member_doc_value.MakeContents(), " For ExampleProtocol\n");
  EXPECT_TRUE(example_service_member.attributes->HasAttributeArg("Foo"));
  auto service_member_str_value = static_cast<const fidl::flat::StringConstantValue&>(
      example_service_member.attributes->GetAttributeArg("Foo").value().get());
  EXPECT_STR_EQ(service_member_str_value.MakeContents(), "ExampleProtocol");
}

TEST(AttributesTests, BadNoAttributeOnUsingNotEventDoc) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library(R"FIDL(
library example;

/// nope
@no_attribute_on_using
@even_doc
using we.should.not.care;

)FIDL",
                      experimental_flags);
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrAttributesNewNotAllowedOnLibraryImport);
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "doc");
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "no_attribute_on_using");
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "even_doc");
}

// Test that a duplicate attribute is caught, and nicely reported.
TEST(AttributesTests, BadNoTwoSameAttributeTest) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library("dup_attributes.fidl", R"FIDL(
library fidl.test.dupattributes;

@dup("first")
@Dup("second")
protocol A {
    MethodA();
};

)FIDL",
                      experimental_flags);
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrDuplicateAttribute);
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "dup");
}

// Test that doc comments and doc attributes clash are properly checked.
TEST(AttributesTests, BadNoTwoSameDocAttributeTest) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library("dup_attributes.fidl", R"FIDL(
library fidl.test.dupattributes;

/// first
@doc("second")
protocol A {
    MethodA();
};

)FIDL",
                      experimental_flags);
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrDuplicateAttribute);
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "doc");
}

TEST(AttributesTests, BadNoTwoSameAttributeOnLibraryTest) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library("dup_attributes.fidl", R"FIDL(
@dup("first")
library fidl.test.dupattributes;

)FIDL",
                      experimental_flags);
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

[Duc = "should be Doc"]
protocol A {
    MethodA();
};

)FIDL");
  ASSERT_COMPILED_AND_CONVERT(library);
  const auto& warnings = library.warnings();
  ASSERT_EQ(warnings.size(), 1);
  ASSERT_ERR(warnings[0], fidl::WarnAttributeTypo);
  ASSERT_SUBSTR(warnings[0]->msg.c_str(), "Duc");
  ASSERT_SUBSTR(warnings[0]->msg.c_str(), "Doc");
}

// Test that a lower_snake_cased attribute doesn't produce a warning in the old
// syntax.
TEST(AttributesTests, GoodAttributeCaseNormalizedOldSyntax) {
  TestLibrary library(R"FIDL(
library fidl.test;

[for_deprecated_c_bindings]
protocol A {
    [transitional]
    MethodA();
};

)FIDL");
  ASSERT_TRUE(library.Compile());
  const auto& warnings = library.warnings();
  ASSERT_EQ(warnings.size(), 0);
}

// This tests our ability to treat warnings as errors.  It is here because this
// is the most convenient warning.
TEST(AttributesTests, BadWarningsAsErrorsTest) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library(R"FIDL(
library fidl.test;

@duc("should be Doc")
protocol A {
    MethodA();
};

)FIDL",
                      experimental_flags);
  library.set_warnings_as_errors(true);
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::WarnAttributeTypo);
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "duc");
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "doc");
  ASSERT_EQ(library.warnings().size(), 0);
}

TEST(AttributesTests, BadEmptyTransport) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library("transport_attributes.fidl", R"FIDL(
library fidl.test.transportattributes;

@transport
protocol A {
    MethodA();
};

)FIDL",
                      experimental_flags);
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrInvalidTransportType);
}

TEST(AttributesTests, BadBogusTransport) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library("transport_attributes.fidl", R"FIDL(
library fidl.test.transportattributes;

@transport("Bogus")
protocol A {
    MethodA();
};

)FIDL",
                      experimental_flags);
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrInvalidTransportType);
}

TEST(AttributesTests, GoodChannelTransport) {
  TestLibrary library("transport_attributes.fidl", R"FIDL(
library fidl.test.transportattributes;

[Transport = "Channel"]
protocol A {
    MethodA();
};

)FIDL");
  ASSERT_COMPILED_AND_CONVERT(library);
  ASSERT_EQ(library.errors().size(), 0);
  ASSERT_EQ(library.warnings().size(), 0);
}

TEST(AttributesTests, GoodSyscallTransport) {
  TestLibrary library("transport_attributes.fidl", R"FIDL(
library fidl.test.transportattributes;

[Transport = "Syscall"]
protocol A {
    MethodA();
};

)FIDL");
  ASSERT_COMPILED_AND_CONVERT(library);
  ASSERT_EQ(library.errors().size(), 0);
  ASSERT_EQ(library.warnings().size(), 0);
}

TEST(AttributesTests, GoodMultipleTransports) {
  TestLibrary library("transport_attributes.fidl", R"FIDL(
library fidl.test.transportattributes;

[Transport = "Channel, Syscall"]
protocol A {
    MethodA();
};

)FIDL");
  ASSERT_COMPILED_AND_CONVERT(library);
  ASSERT_EQ(library.errors().size(), 0);
  ASSERT_EQ(library.warnings().size(), 0);
}

TEST(AttributesTests, BadMultipleTransportsWithBogus) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library("transport_attributes.fidl", R"FIDL(
library fidl.test.transportattributes;

@transport("Channel, Bogus, Syscall")
protocol A {
    MethodA();
};

)FIDL",
                      experimental_flags);
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrInvalidTransportType);
}

TEST(AttributesTests, BadTransitionalInvalidPlacement) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library(R"FIDL(
library fidl.test;

@transitional
protocol MyProtocol {
  MyMethod();
};
  )FIDL",
                      experimental_flags);

  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrInvalidAttributePlacement);
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "transitional");
}

TEST(AttributesTests, BadUnknownInvalidPlacementOnUnion) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library(R"FIDL(
library fidl.test;

@unknown
type U = flexible union {
  1: a int32;
};
  )FIDL",
                      experimental_flags);

  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrInvalidAttributePlacement);
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "unknown");
}

TEST(AttributesTests, BadUnknownInvalidPlacementOnBitsMember) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library(R"FIDL(
library fidl.test;

type B = flexible bits : uint32 {
  @unknown A = 0x1;
};
  )FIDL",
                      experimental_flags);

  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrInvalidAttributePlacement);
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "unknown");
}

TEST(AttributesTests, BadUnknownInvalidOnStrictUnionsEnums) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  {
    TestLibrary library(R"FIDL(
library fidl.test;

type U = strict union {
  @unknown 1: a int32;
};
  )FIDL",
                        experimental_flags);
    ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrUnknownAttributeOnInvalidType);
    ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "Unknown");
  }

  {
    TestLibrary library(R"FIDL(
library fidl.test;

type E = strict enum : uint32 {
  @unknown A = 1;
};
  )FIDL",
                        experimental_flags);
    ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrUnknownAttributeOnInvalidType);
    ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "Unknown");
  }
}

TEST(AttributesTests, GoodUnknownOkOnFlexibleOrTransitionalEnumsUnionMembers) {
  {
    TestLibrary library(R"FIDL(
library fidl.test;

flexible union U {
  [Unknown] 1: int32 a;
};
  )FIDL");
    ASSERT_COMPILED_AND_CONVERT(library);
  }

  {
    TestLibrary library(R"FIDL(
library fidl.test;

[Transitional]
strict union U {
  [Unknown] 1: int32 a;
};");
  )FIDL");
    ASSERT_COMPILED_AND_CONVERT(library);
  }

  {
    TestLibrary library(R"FIDL(
library fidl.test;

flexible enum E :
  uint32 { [Unknown] A = 1;
};
  )FIDL");
    ASSERT_COMPILED_AND_CONVERT(library);
  }

  {
    TestLibrary library(R"FIDL(
library fidl.test;

[Transitional]
strict enum E : uint32 {
  [Unknown] A = 1;
};
  )FIDL");
    ASSERT_COMPILED_AND_CONVERT(library);
  }
}

TEST(AttributesTests, BadIncorrectPlacementLayout) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
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

)FIDL",
                      experimental_flags);
  EXPECT_FALSE(library.Compile());
  const auto& errors = library.errors();
  ASSERT_EQ(errors.size(), 10);
  ASSERT_ERR(errors[0], fidl::ErrInvalidAttributePlacement);
  ASSERT_SUBSTR(errors[0]->msg.c_str(), "for_deprecated_c_bindings");
}

TEST(AttributesTests, BadDeprecatedAttributes) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
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
)FIDL",
                      experimental_flags);
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
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library(R"FIDL(
library fidl.test;

@must_have_three_members
type MyStruct = struct {
    one int64;
    two int64;
    three int64;
    oh_no_four int64;
};

)FIDL",
                      experimental_flags);
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
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library(R"FIDL(
library fidl.test;

protocol MyProtocol {
    @must_have_three_members MyMethod();
};

)FIDL",
                      experimental_flags);
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
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library(R"FIDL(
library fidl.test;

@must_have_three_members
protocol MyProtocol {
    MyMethod();
    MySecondMethod();
};

)FIDL",
                      experimental_flags);
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
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library(R"FIDL(
library fidl.test;

@max_bytes("27")
type MyTable = table {
  1: here bool;
};

)FIDL",
                      experimental_flags);
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrTooManyBytes);
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "27");  // 27 allowed
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "40");  // 40 found
}

TEST(AttributesTests, BadMaxBytesBoundTooBig) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library(R"FIDL(
library fidl.test;

@max_bytes("4294967296") // 2^32
type MyTable = table {
  1: u uint8;
};
)FIDL",
                      experimental_flags);
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrBoundIsTooBig);
}

TEST(AttributesTests, BadMaxBytesUnableToParseBound) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library(R"FIDL(
library fidl.test;

@max_bytes("invalid")
type MyTable = table {
  1: u uint8;
};
)FIDL",
                      experimental_flags);
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrUnableToParseBound);
}

TEST(AttributesTests, BadMaxHandles) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  auto library = WithLibraryZx(R"FIDL(
library fidl.test;

using zx;

@max_handles("2")
type MyUnion = resource union {
  1: hello uint8;
  2: world array<uint8,8>;
  3: foo vector<zx.handle:VMO>:6;
};

)FIDL",
                               std::move(experimental_flags));
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrTooManyHandles);
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "2");  // 2 allowed
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "6");  // 6 found
}

TEST(AttributesTests, BadAttributeValue) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library(R"FIDL(
library fidl.test;

@for_deprecated_c_bindings("Complex")
protocol P {
    Method();
};
)FIDL",
                      experimental_flags);
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrInvalidAttributeValue);
}

TEST(AttributesTests, BadSelectorIncorrectPlacement) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library(R"FIDL(
library fidl.test;

@selector("Nonsense")
type MyUnion = union {
  1: hello uint8;
};

)FIDL",
                      experimental_flags);
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrInvalidAttributePlacement);
}

TEST(AttributesTests, BadNoAttributesOnReserved) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  {
    TestLibrary library(R"FIDL(
library fidl.test;

type Foo = union {
  @foo
  1: reserved;
};
)FIDL",
                        experimental_flags);
    ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrCannotAttachAttributesToReservedOrdinals);
  }

  {
    TestLibrary library(R"FIDL(
library fidl.test;

type Foo = table {
  @foo
  1: reserved;
};
  )FIDL",
                        experimental_flags);
    ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrCannotAttachAttributesToReservedOrdinals);
  }
}

TEST(AttributesTests, BadParameterAttributeIncorrectPlacement) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library(R"FIDL(
library fidl.test;

protocol ExampleProtocol {
    Method(struct { arg exampleusing.Empty; } @on_parameter);
};

)FIDL",
                      experimental_flags);
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrUnexpectedTokenOfKind);
}
}  // namespace
