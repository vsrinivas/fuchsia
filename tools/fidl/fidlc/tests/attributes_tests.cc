// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zxtest/zxtest.h>

#include "tools/fidl/fidlc/include/fidl/diagnostics.h"
#include "tools/fidl/fidlc/include/fidl/flat/attribute_schema.h"
#include "tools/fidl/fidlc/include/fidl/flat_ast.h"
#include "tools/fidl/fidlc/include/fidl/lexer.h"
#include "tools/fidl/fidlc/include/fidl/parser.h"
#include "tools/fidl/fidlc/include/fidl/reporter.h"
#include "tools/fidl/fidlc/include/fidl/source_file.h"
#include "tools/fidl/fidlc/tests/error_test.h"
#include "tools/fidl/fidlc/tests/test_library.h"

namespace {

TEST(AttributesTests, GoodPlacementOfAttributes) {
  SharedAmongstLibraries shared;
  TestLibrary dependency(&shared, "exampleusing.fidl", R"FIDL(
library exampleusing;

@on_dep_struct
type Empty = struct {};
)FIDL");
  ASSERT_COMPILED(dependency);

  TestLibrary library(&shared, "example.fidl", R"FIDL(
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
    @on_reserved_member
    2: reserved;
};

@on_alias
alias ExampleAlias = uint32;

@on_union
type ExampleUnion = union {
    @on_union_member
    1: variant uint32;
    @on_reserved_member
    2: reserved;
};

)FIDL");
  ASSERT_COMPILED(library);

  EXPECT_TRUE(library.attributes()->Get("on_library"));

  auto example_bits = library.LookupBits("ExampleBits");
  ASSERT_NOT_NULL(example_bits);
  EXPECT_TRUE(example_bits->attributes->Get("on_bits"));
  EXPECT_TRUE(example_bits->members.front().attributes->Get("on_bits_member"));

  auto example_const = library.LookupConstant("EXAMPLE_CONST");
  ASSERT_NOT_NULL(example_const);
  EXPECT_TRUE(example_const->attributes->Get("on_const"));

  auto example_enum = library.LookupEnum("ExampleEnum");
  ASSERT_NOT_NULL(example_enum);
  EXPECT_TRUE(example_enum->attributes->Get("on_enum"));
  EXPECT_TRUE(example_enum->members.front().attributes->Get("on_enum_member"));

  auto example_child_protocol = library.LookupProtocol("ExampleChildProtocol");
  ASSERT_NOT_NULL(example_child_protocol);
  EXPECT_TRUE(example_child_protocol->attributes->Get("on_protocol"));
  EXPECT_TRUE(example_child_protocol->methods.front().attributes->Get("on_method"));
  ASSERT_NOT_NULL(example_child_protocol->methods.front().maybe_request.get());

  auto id = static_cast<const fidl::flat::IdentifierType*>(
      example_child_protocol->methods.front().maybe_request->type);
  auto as_struct = static_cast<const fidl::flat::Struct*>(id->type_decl);
  EXPECT_TRUE(as_struct->members.front().attributes->Get("on_parameter"));

  auto example_parent_protocol = library.LookupProtocol("ExampleParentProtocol");
  ASSERT_NOT_NULL(example_parent_protocol);
  EXPECT_TRUE(example_parent_protocol->attributes->Get("on_protocol"));
  EXPECT_TRUE(example_parent_protocol->composed_protocols.front().attributes->Get("on_compose"));

  auto example_service = library.LookupService("ExampleService");
  ASSERT_NOT_NULL(example_service);
  EXPECT_TRUE(example_service->attributes->Get("on_service"));
  EXPECT_TRUE(example_service->members.front().attributes->Get("on_service_member"));

  auto example_struct = library.LookupStruct("ExampleStruct");
  ASSERT_NOT_NULL(example_struct);
  EXPECT_TRUE(example_struct->attributes->Get("on_struct"));
  EXPECT_TRUE(example_struct->members.front().attributes->Get("on_struct_member"));

  auto example_table = library.LookupTable("ExampleTable");
  ASSERT_NOT_NULL(example_table);
  EXPECT_TRUE(example_table->attributes->Get("on_table"));
  EXPECT_TRUE(example_table->members.front().attributes->Get("on_table_member"));
  EXPECT_TRUE(example_table->members.back().attributes->Get("on_reserved_member"));

  auto example_alias = library.LookupAlias("ExampleAlias");
  ASSERT_NOT_NULL(example_alias);
  EXPECT_TRUE(example_alias->attributes->Get("on_alias"));

  auto example_union = library.LookupUnion("ExampleUnion");
  ASSERT_NOT_NULL(example_union);
  EXPECT_TRUE(example_union->attributes->Get("on_union"));
  EXPECT_TRUE(example_union->members.front().attributes->Get("on_union_member"));
  EXPECT_TRUE(example_union->members.back().attributes->Get("on_reserved_member"));
}

TEST(AttributesTests, GoodOfficialAttributes) {
  TestLibrary library(R"FIDL(
@no_doc
library example;

/// For EXAMPLE_CONSTANT
@no_doc
@deprecated("Note")
const EXAMPLE_CONSTANT string = "foo";

/// For ExampleEnum
@deprecated("Reason")
type ExampleEnum = flexible enum {
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

  EXPECT_TRUE(library.attributes()->Get("no_doc"));

  auto example_const = library.LookupConstant("EXAMPLE_CONSTANT");
  ASSERT_NOT_NULL(example_const);
  EXPECT_TRUE(example_const->attributes->Get("no_doc"));
  EXPECT_TRUE(example_const->attributes->Get("doc")->GetArg("value"));
  auto& const_doc_value = static_cast<const fidl::flat::DocCommentConstantValue&>(
      example_const->attributes->Get("doc")->GetArg("value")->value->Value());
  EXPECT_STREQ(const_doc_value.MakeContents(), " For EXAMPLE_CONSTANT\n");
  EXPECT_TRUE(example_const->attributes->Get("deprecated")->GetArg("value"));
  auto& const_str_value = static_cast<const fidl::flat::StringConstantValue&>(
      example_const->attributes->Get("deprecated")->GetArg("value")->value->Value());
  EXPECT_STREQ(const_str_value.MakeContents(), "Note");

  auto example_enum = library.LookupEnum("ExampleEnum");
  ASSERT_NOT_NULL(example_enum);
  EXPECT_TRUE(example_enum->attributes->Get("doc")->GetArg("value"));
  auto& enum_doc_value = static_cast<const fidl::flat::DocCommentConstantValue&>(
      example_enum->attributes->Get("doc")->GetArg("value")->value->Value());
  EXPECT_STREQ(enum_doc_value.MakeContents(), " For ExampleEnum\n");
  EXPECT_TRUE(example_enum->attributes->Get("deprecated")->GetArg("value"));
  auto& enum_str_value = static_cast<const fidl::flat::StringConstantValue&>(
      example_enum->attributes->Get("deprecated")->GetArg("value")->value->Value());
  EXPECT_STREQ(enum_str_value.MakeContents(), "Reason");
  EXPECT_TRUE(example_enum->members.back().attributes->Get("unknown"));

  auto example_struct = library.LookupStruct("ExampleStruct");
  ASSERT_NOT_NULL(example_struct);
  EXPECT_TRUE(example_struct->attributes->Get("doc")->GetArg("value"));
  auto& struct_doc_value = static_cast<const fidl::flat::DocCommentConstantValue&>(
      example_struct->attributes->Get("doc")->GetArg("value")->value->Value());
  EXPECT_STREQ(struct_doc_value.MakeContents(), " For ExampleStruct\n");
  EXPECT_TRUE(example_struct->attributes->Get("max_bytes")->GetArg("value"));
  auto& struct_str_value1 = static_cast<const fidl::flat::StringConstantValue&>(
      example_struct->attributes->Get("max_bytes")->GetArg("value")->value->Value());
  EXPECT_STREQ(struct_str_value1.MakeContents(), "1234");
  EXPECT_TRUE(example_struct->attributes->Get("max_handles")->GetArg("value"));
  auto& struct_str_value2 = static_cast<const fidl::flat::StringConstantValue&>(
      example_struct->attributes->Get("max_handles")->GetArg("value")->value->Value());
  EXPECT_STREQ(struct_str_value2.MakeContents(), "5678");

  auto example_anon = library.LookupTable("CustomName");
  ASSERT_NOT_NULL(example_anon);
  EXPECT_TRUE(example_anon->attributes->Get("generated_name"));

  auto& generated_name_value = static_cast<const fidl::flat::StringConstantValue&>(
      example_anon->attributes->Get("generated_name")->GetArg("value")->value->Value());
  EXPECT_STREQ(generated_name_value.MakeContents(), "CustomName");

  auto example_protocol = library.LookupProtocol("ExampleProtocol");
  ASSERT_NOT_NULL(example_protocol);
  EXPECT_TRUE(example_protocol->attributes->Get("discoverable"));
  EXPECT_TRUE(example_protocol->attributes->Get("for_deprecated_c_bindings"));
  EXPECT_TRUE(example_protocol->attributes->Get("doc")->GetArg("value"));
  auto& protocol_doc_value = static_cast<const fidl::flat::DocCommentConstantValue&>(
      example_protocol->attributes->Get("doc")->GetArg("value")->value->Value());
  EXPECT_STREQ(protocol_doc_value.MakeContents(), " For ExampleProtocol\n");
  EXPECT_TRUE(example_protocol->attributes->Get("transport")->GetArg("value"));
  auto& protocol_str_value = static_cast<const fidl::flat::StringConstantValue&>(
      example_protocol->attributes->Get("transport")->GetArg("value")->value->Value());
  EXPECT_STREQ(protocol_str_value.MakeContents(), "Syscall");

  auto& example_method = example_protocol->methods.front();
  EXPECT_TRUE(example_method.attributes->Get("internal"));
  EXPECT_TRUE(example_method.attributes->Get("transitional"));
  EXPECT_TRUE(example_method.attributes->Get("doc")->GetArg("value"));
  auto& method_doc_value = static_cast<const fidl::flat::DocCommentConstantValue&>(
      example_method.attributes->Get("doc")->GetArg("value")->value->Value());
  EXPECT_STREQ(method_doc_value.MakeContents(), " For ExampleMethod\n");
  EXPECT_TRUE(example_method.attributes->Get("selector")->GetArg("value"));
  auto& method_str_value = static_cast<const fidl::flat::StringConstantValue&>(
      example_method.attributes->Get("selector")->GetArg("value")->value->Value());
  EXPECT_STREQ(method_str_value.MakeContents(), "Bar");

  auto example_service = library.LookupService("ExampleService");
  ASSERT_NOT_NULL(example_service);
  EXPECT_TRUE(example_service->attributes->Get("no_doc"));
  EXPECT_TRUE(example_service->attributes->Get("doc")->GetArg("value"));
  auto& service_doc_value = static_cast<const fidl::flat::DocCommentConstantValue&>(
      example_service->attributes->Get("doc")->GetArg("value")->value->Value());
  EXPECT_STREQ(service_doc_value.MakeContents(), " For ExampleService\n");
  EXPECT_TRUE(example_service->attributes->Get("foo")->GetArg("value"));
  auto& service_str_value = static_cast<const fidl::flat::StringConstantValue&>(
      example_service->attributes->Get("foo")->GetArg("value")->value->Value());
  EXPECT_STREQ(service_str_value.MakeContents(), "ExampleService");

  auto& example_service_member = example_service->members.front();
  EXPECT_TRUE(example_service_member.attributes->Get("no_doc"));
  EXPECT_TRUE(example_service_member.attributes->Get("doc")->GetArg("value"));
  auto& service_member_doc_value = static_cast<const fidl::flat::DocCommentConstantValue&>(
      example_service_member.attributes->Get("doc")->GetArg("value")->value->Value());
  EXPECT_STREQ(service_member_doc_value.MakeContents(), " For ExampleProtocol\n");
  EXPECT_TRUE(example_service_member.attributes->Get("foo")->GetArg("value"));
  auto& service_member_str_value = static_cast<const fidl::flat::StringConstantValue&>(
      example_service_member.attributes->Get("foo")->GetArg("value")->value->Value());
  EXPECT_STREQ(service_member_str_value.MakeContents(), "ExampleProtocol");
}

TEST(AttributesTests, BadNoAttributeOnUsingNotEventDoc) {
  TestLibrary library(R"FIDL(
library example;

/// nope
@no_attribute_on_using
@even_doc
using we.should.not.care;

)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrAttributesNotAllowedOnLibraryImport);
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "doc");
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "no_attribute_on_using");
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "even_doc");
}

// Test that a duplicate attribute is caught, and nicely reported.
TEST(AttributesTests, BadNoTwoSameAttribute) {
  TestLibrary library(R"FIDL(
library fidl.test.dupattributes;

@dup("first")
@dup("second")
protocol A {
    MethodA();
};

)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrDuplicateAttribute);
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "dup");
}

// Test that attributes with the same canonical form are considered duplicates.
TEST(AttributesTests, BadNoTwoSameAttributeCanonical) {
  TestLibrary library(R"FIDL(
library fidl.test.dupattributes;

@TheSame("first")
@The_same("second")
protocol A {
    MethodA();
};

)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrDuplicateAttributeCanonical);
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "canonical form 'the_same'");
}

TEST(AttributesTests, GoodDocAttribute) {
  TestLibrary library;
  library.AddFile("good/fi-0028-b.test.fidl");

  ASSERT_COMPILED(library);
}

// Test that doc comments and doc attributes clash are properly checked.
TEST(AttributesTests, BadNoTwoSameDocAttribute) {
  TestLibrary library(R"FIDL(
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

TEST(AttributesTests, BadNoTwoSameAttributeOnLibrary) {
  TestLibrary library;
  library.AddSource("first.fidl", R"FIDL(
@dup("first")
library fidl.test.dupattributes;

)FIDL");
  library.AddSource("second.fidl", R"FIDL(
@dup("second")
library fidl.test.dupattributes;

)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrDuplicateAttribute);
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "dup");
}

// Test that a close attribute is caught.
TEST(AttributesTests, WarnOnCloseToOfficialAttribute) {
  TestLibrary library;
  library.AddFile("bad/fi-0145.test.fidl");

  ASSERT_WARNED_DURING_COMPILE(library, fidl::WarnAttributeTypo);
  EXPECT_SUBSTR(library.warnings()[0]->msg.c_str(), "duc");
  EXPECT_SUBSTR(library.warnings()[0]->msg.c_str(), "doc");
}

TEST(AttributesTests, GoodNotTooCloseUnofficialAttribute) {
  TestLibrary library;
  library.AddFile("good/fi-0145.test.fidl");

  ASSERT_COMPILED(library);
  auto example_protocol = library.LookupProtocol("Example");
  ASSERT_NOT_NULL(example_protocol);
  EXPECT_TRUE(example_protocol->attributes->Get("duck"));
  auto& struct_str_value1 = static_cast<const fidl::flat::StringConstantValue&>(
      example_protocol->attributes->Get("duck")->GetArg("value")->value->Value());
  EXPECT_STREQ(struct_str_value1.MakeContents(), "quack");
}

// Ensures we detect typos early enough that we still report them, even if there
// were other compilation errors.
TEST(AttributesTests, WarnOnCloseAttributeWithOtherErrors) {
  TestLibrary library(R"FIDL(
@available(added=1)
library fidl.test;

@available(added=1, removed=2)
type Foo = struct {};

// This actually gets added at 1 because we misspelled "available".
@availabe(added=2)
type Foo = resource struct {};

)FIDL");
  ASSERT_FALSE(library.Compile());
  ASSERT_EQ(library.errors().size(), 1);
  EXPECT_ERR(library.errors()[0], fidl::ErrNameOverlap);
  ASSERT_EQ(library.warnings().size(), 1);
  ASSERT_ERR(library.warnings()[0], fidl::WarnAttributeTypo);
  EXPECT_SUBSTR(library.warnings()[0]->msg.c_str(), "availabe");
  EXPECT_SUBSTR(library.warnings()[0]->msg.c_str(), "available");
}

// This tests our ability to treat warnings as errors.  It is here because this
// is the most convenient warning.
TEST(AttributesTests, BadWarningsAsErrors) {
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
}

TEST(AttributesTests, BadEmptyTransport) {
  TestLibrary library(R"FIDL(
library fidl.test.transportattributes;

@transport
protocol A {
    MethodA();
};

)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrMissingRequiredAnonymousAttributeArg);
}

TEST(AttributesTests, BadBogusTransport) {
  TestLibrary library(R"FIDL(
library fidl.test.transportattributes;

@transport("Bogus")
protocol A {
    MethodA();
};

)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrInvalidTransportType);
}

TEST(AttributesTests, GoodChannelTransport) {
  TestLibrary library(R"FIDL(library fidl.test.transportattributes;

@transport("Channel")
protocol A {
    MethodA();
};
)FIDL");
  ASSERT_COMPILED(library);
}

TEST(AttributesTests, GoodSyscallTransport) {
  TestLibrary library(R"FIDL(library fidl.test.transportattributes;

@transport("Syscall")
protocol A {
    MethodA();
};
)FIDL");
  ASSERT_COMPILED(library);
}

TEST(AttributesTests, BadMultipleTransports) {
  TestLibrary library(R"FIDL(library fidl.test.transportattributes;

@transport("Channel, Syscall")
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

TEST(AttributesTests, BadUnknownInvalidPlacementOnUnionMember) {
  TestLibrary library(R"FIDL(
library fidl.test;

type U = flexible union {
  @unknown 1: a int32;
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

TEST(AttributesTests, BadUnknownInvalidOnStrictEnumMember) {
  TestLibrary library;
  library.AddFile("bad/fi-0071.test.fidl");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrUnknownAttributeOnStrictEnumMember);
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "unknown");
}

TEST(AttributesTests, BadTransitionalOnEnum) {
  TestLibrary library(R"FIDL(library fidl.test;

@transitional
type E = strict enum : uint32 {
  A = 1;
};
)FIDL");

  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrInvalidAttributePlacement);
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "transitional");
}

TEST(AttributesTests, BadIncorrectPlacementLayout) {
  TestLibrary library(R"FIDL(
@for_deprecated_c_bindings // 1
library fidl.test;

// No error; placement on simple constants is allowed
@for_deprecated_c_bindings
const MyConst uint32 = 0;

@for_deprecated_c_bindings // 2
type MyEnum = enum {
    @for_deprecated_c_bindings // 3
    MyMember = 5;
};

@for_deprecated_c_bindings // no error, this placement is allowed
type MyStruct = struct {
    @for_deprecated_c_bindings // 4
    MyMember int32;
};

@for_deprecated_c_bindings // 5
type MyUnion = union {
    @for_deprecated_c_bindings // 6
    1: MyMember int32;
};

@for_deprecated_c_bindings // 7
type MyTable = table {
    @for_deprecated_c_bindings // 8
    1: MyMember int32;
};

@for_deprecated_c_bindings // no error, this placement is allowed
protocol MyProtocol {
    @for_deprecated_c_bindings // 9
    MyMethod();
};

)FIDL");
  EXPECT_FALSE(library.Compile());
  const auto& errors = library.errors();
  ASSERT_EQ(errors.size(), 9);
  for (const auto& error : errors) {
    ASSERT_ERR(error, fidl::ErrInvalidAttributePlacement);
    ASSERT_SUBSTR(error->msg.c_str(), "for_deprecated_c_bindings");
  }
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

bool MustHaveThreeMembers(fidl::Reporter* reporter, const fidl::flat::Attribute* attribute,
                          const fidl::flat::Element* element) {
  switch (element->kind) {
    case fidl::flat::Element::Kind::kStruct: {
      auto struct_decl = static_cast<const fidl::flat::Struct*>(element);
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
  library.AddAttributeSchema("must_have_three_members").Constrain(MustHaveThreeMembers);
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
  library.AddAttributeSchema("must_have_three_members").Constrain(MustHaveThreeMembers);
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
  library.AddAttributeSchema("must_have_three_members").Constrain(MustHaveThreeMembers);
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrAttributeConstraintNotSatisfied);
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
  TestLibrary library(R"FIDL(
library fidl.test;

using zx;

@max_handles("2")
type MyUnion = resource union {
  1: hello uint8;
  2: world array<uint8,8>;
  3: foo vector<zx.handle:VMO>:6;
};

)FIDL");
  library.UseLibraryZx();
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
  EXPECT_TRUE(foo->attributes->Get("foo"));

  auto bar = library.LookupStruct("Bar");
  ASSERT_NOT_NULL(bar);
  EXPECT_TRUE(bar->attributes->Get("bar"));

  auto req = library.LookupStruct("MyProtocolMyMethodRequest");
  ASSERT_NOT_NULL(req);
  EXPECT_TRUE(req->attributes->Get("baz"));

  auto inner = library.LookupStruct("InnerLayout");
  ASSERT_NOT_NULL(inner);
  EXPECT_TRUE(inner->attributes->Get("qux"));
}

TEST(AttributesTests, BadNoArgumentsEmptyParens) {
  TestLibrary library(R"FIDL(
library fidl.test;

@for_deprecated_c_bindings()
type MyStruct = struct {};

)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrAttributeWithEmptyParens);
}

TEST(AttributesTests, GoodMultipleArguments) {
  TestLibrary library(R"FIDL(
library example;

@foo(bar="abc", baz="def")
type MyStruct = struct {};

)FIDL");
  ASSERT_COMPILED(library);

  auto example_struct = library.LookupStruct("MyStruct");
  ASSERT_NOT_NULL(example_struct);
  EXPECT_TRUE(example_struct->attributes->Get("foo"));
  EXPECT_TRUE(example_struct->attributes->Get("foo")->GetArg("bar"));
  EXPECT_STREQ(example_struct->attributes->Get("foo")->GetArg("bar")->value->span.data(),
               "\"abc\"");
  EXPECT_TRUE(example_struct->attributes->Get("foo")->GetArg("baz"));
  EXPECT_STREQ(example_struct->attributes->Get("foo")->GetArg("baz")->value->span.data(),
               "\"def\"");
}

TEST(AttributesTests, BadMultipleArgumentsWithNoNames) {
  TestLibrary library(R"FIDL(
library example;

@foo("abc", "def")
type MyStruct = struct {};

)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrAttributeArgsMustAllBeNamed);
}

TEST(AttributesTests, BadMultipleArgumentsDuplicateNames) {
  TestLibrary library(R"FIDL(
library example;

@foo(bar="abc", bar="def")
type MyStruct = struct {};

)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrDuplicateAttributeArg);
}

TEST(AttributesTests, BadMultipleArgumentsDuplicateCanonicalNames) {
  TestLibrary library(R"FIDL(
library example;

@foo(Bar_baz="abc", bar__baz="def")
type MyStruct = struct {};

)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrDuplicateAttributeArgCanonical);
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "canonical form 'bar_baz'");
}

TEST(AttributesTests, GoodSingleArgumentIsNotNamed) {
  TestLibrary library(R"FIDL(
library example;

@foo("bar")
type MyStruct = struct {};

)FIDL");
  ASSERT_COMPILED(library);
}

TEST(AttributesTests, GoodSingleArgumentIsNamedWithoutSchema) {
  TestLibrary library(R"FIDL(
library example;

@foo(a="bar")
type MyStruct = struct {};

)FIDL");
  ASSERT_COMPILED(library);
}

TEST(AttributesTests, GoodSingleSchemaArgument) {
  TestLibrary library(R"FIDL(
library example;

@foo("bar")
type MyStruct = struct {};

)FIDL");
  library.AddAttributeSchema("foo").AddArg(
      "value",
      fidl::flat::AttributeArgSchema(fidl::flat::ConstantValue::Kind::kString,
                                     fidl::flat::AttributeArgSchema::Optionality::kRequired));
  ASSERT_COMPILED(library);
}

TEST(AttributesTests, GoodSingleSchemaArgumentWithInferredName) {
  TestLibrary library(R"FIDL(
library example;

@foo("bar")
type MyStruct = struct {};

)FIDL");
  library.AddAttributeSchema("foo").AddArg(
      "inferrable",
      fidl::flat::AttributeArgSchema(fidl::flat::ConstantValue::Kind::kString,
                                     fidl::flat::AttributeArgSchema::Optionality::kRequired));
  ASSERT_COMPILED(library);

  auto example_struct = library.LookupStruct("MyStruct");
  ASSERT_NOT_NULL(example_struct);
  EXPECT_TRUE(example_struct->attributes->Get("foo"));
  EXPECT_TRUE(example_struct->attributes->Get("foo")->GetArg("inferrable"));
}

// If a schema is provided (ie, this is an "official" FIDL attribute), and it specifies that only
// a single optional argument is allowed, respect both the inclusion and omission of that argument.
TEST(AttributesTests, GoodSingleSchemaArgumentRespectOptionality) {
  TestLibrary library(R"FIDL(
library example;

@foo("bar")
type MyStruct = struct {};

@foo
type MyOtherStruct = struct {};

)FIDL");
  library.AddAttributeSchema("foo").AddArg(
      "value",
      fidl::flat::AttributeArgSchema(fidl::flat::ConstantValue::Kind::kString,
                                     fidl::flat::AttributeArgSchema::Optionality::kOptional));
  ASSERT_COMPILED(library);
}

// If a schema is provided (ie, this is an "official" FIDL attribute), and it specifies that only
// a single argument is allowed, naming that argument is an error.
TEST(AttributesTests, BadSingleSchemaArgumentIsNamed) {
  TestLibrary library(R"FIDL(
library example;

@foo(value="bar")
type MyStruct = struct {};

)FIDL");
  library.AddAttributeSchema("foo").AddArg(
      "value",
      fidl::flat::AttributeArgSchema(fidl::flat::ConstantValue::Kind::kString,
                                     fidl::flat::AttributeArgSchema::Optionality::kRequired));
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrAttributeArgMustNotBeNamed);
}

// If a schema is provided (ie, this is an "official" FIDL attribute), and it specifies that
// multiple arguments are allowed, a single unnamed argument is an error.
TEST(AttributesTests, BadSingleSchemaArgumentIsNotNamed) {
  TestLibrary library(R"FIDL(
library example;

@foo("bar")
type MyStruct = struct {};

)FIDL");
  library.AddAttributeSchema("foo")
      .AddArg("value", fidl::flat::AttributeArgSchema(
                           fidl::flat::ConstantValue::Kind::kString,
                           fidl::flat::AttributeArgSchema::Optionality::kRequired))
      .AddArg("other", fidl::flat::AttributeArgSchema(
                           fidl::flat::ConstantValue::Kind::kString,
                           fidl::flat::AttributeArgSchema::Optionality::kOptional));
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrAttributeArgNotNamed);
}

TEST(AttributesTests, GoodMultipleSchemaArgumentsRequiredOnly) {
  TestLibrary library(R"FIDL(
library fidl.test;

@multiple_args(first="foo", second="bar")
type MyStruct = struct {};

// Order independent.
@multiple_args(second="bar", first="foo")
type MyOtherStruct = struct {};

)FIDL");
  library.AddAttributeSchema("multiple_args")
      .AddArg("first", fidl::flat::AttributeArgSchema(
                           fidl::flat::ConstantValue::Kind::kString,
                           fidl::flat::AttributeArgSchema::Optionality::kRequired))
      .AddArg("second", fidl::flat::AttributeArgSchema(
                            fidl::flat::ConstantValue::Kind::kString,
                            fidl::flat::AttributeArgSchema::Optionality::kRequired));
  ASSERT_COMPILED(library);
}

TEST(AttributesTests, GoodMultipleSchemaArgumentsOptionalOnly) {
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

)FIDL");
  library.AddAttributeSchema("multiple_args")
      .AddArg("first", fidl::flat::AttributeArgSchema(
                           fidl::flat::ConstantValue::Kind::kString,
                           fidl::flat::AttributeArgSchema::Optionality::kOptional))
      .AddArg("second", fidl::flat::AttributeArgSchema(
                            fidl::flat::ConstantValue::Kind::kString,
                            fidl::flat::AttributeArgSchema::Optionality::kOptional));
  ASSERT_COMPILED(library);
}

TEST(AttributesTests, GoodMultipleSchemaArgumentsRequiredAndOptional) {
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

)FIDL");
  library.AddAttributeSchema("multiple_args")
      .AddArg("first", fidl::flat::AttributeArgSchema(
                           fidl::flat::ConstantValue::Kind::kString,
                           fidl::flat::AttributeArgSchema::Optionality::kRequired))
      .AddArg("second", fidl::flat::AttributeArgSchema(
                            fidl::flat::ConstantValue::Kind::kString,
                            fidl::flat::AttributeArgSchema::Optionality::kOptional));
  ASSERT_COMPILED(library);
}

TEST(AttributesTests, BadMultipleSchemaArgumentsRequiredMissing) {
  TestLibrary library(R"FIDL(
library fidl.test;

@multiple_args(optional="foo")
type MyStruct = struct {};

)FIDL");
  library.AddAttributeSchema("multiple_args")
      .AddArg("required", fidl::flat::AttributeArgSchema(
                              fidl::flat::ConstantValue::Kind::kString,
                              fidl::flat::AttributeArgSchema::Optionality::kRequired))
      .AddArg("optional", fidl::flat::AttributeArgSchema(
                              fidl::flat::ConstantValue::Kind::kString,
                              fidl::flat::AttributeArgSchema::Optionality::kOptional));
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrMissingRequiredAttributeArg);
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "multiple_args");
}

TEST(AttributesTests, GoodLiteralTypesWithoutSchema) {
  TestLibrary library(R"FIDL(
library example;

@attr(foo="abc", bar=true, baz=false)
type MyStruct = struct {};

)FIDL");
  ASSERT_COMPILED(library);

  auto example_struct = library.LookupStruct("MyStruct");
  ASSERT_NOT_NULL(example_struct);
  EXPECT_TRUE(example_struct->attributes->Get("attr"));

  // Check `foo` arg.
  EXPECT_TRUE(example_struct->attributes->Get("attr")->GetArg("foo"));
  const auto& foo = example_struct->attributes->Get("attr")->GetArg("foo")->value;
  EXPECT_STREQ(foo->span.data(), "\"abc\"");
  ASSERT_EQ(foo->kind, fidl::flat::Constant::Kind::kLiteral);

  std::unique_ptr<fidl::flat::ConstantValue> resolved_foo;
  EXPECT_TRUE(foo->Value().Convert(fidl::flat::ConstantValue::Kind::kString, &resolved_foo));

  // Check `baz` arg.
  EXPECT_TRUE(example_struct->attributes->Get("attr")->GetArg("baz"));
  const auto& baz = example_struct->attributes->Get("attr")->GetArg("baz")->value;
  EXPECT_STREQ(baz->span.data(), "false");
  ASSERT_EQ(baz->kind, fidl::flat::Constant::Kind::kLiteral);

  std::unique_ptr<fidl::flat::ConstantValue> resolved_baz;
  EXPECT_TRUE(baz->Value().Convert(fidl::flat::ConstantValue::Kind::kBool, &resolved_baz));
}

TEST(AttributesTests, BadLiteralNumericTypesWithoutSchema) {
  TestLibrary library(R"FIDL(
library example;

@attr(foo=1, bar=2.3)
type MyStruct = struct {};

)FIDL");
  ASSERT_ERRORED_TWICE_DURING_COMPILE(library, fidl::ErrCanOnlyUseStringOrBool,
                                      fidl::ErrCanOnlyUseStringOrBool);
}

TEST(AttributesTests, GoodReferencedTypesWithoutSchema) {
  TestLibrary library(R"FIDL(
library example;

const foo string:3 = "abc";
const bar bool = true;
const baz bool = false;

@attr(foo=foo, bar=bar, baz=baz)
type MyStruct = struct {};

)FIDL");
  ASSERT_COMPILED(library);

  auto example_struct = library.LookupStruct("MyStruct");
  ASSERT_NOT_NULL(example_struct);
  EXPECT_TRUE(example_struct->attributes->Get("attr"));

  // Check `foo` arg.
  EXPECT_TRUE(example_struct->attributes->Get("attr")->GetArg("foo"));
  const auto& foo = example_struct->attributes->Get("attr")->GetArg("foo")->value;
  EXPECT_STREQ(foo->span.data(), "foo");
  ASSERT_EQ(foo->kind, fidl::flat::Constant::Kind::kIdentifier);

  std::unique_ptr<fidl::flat::ConstantValue> resolved_foo;
  EXPECT_TRUE(foo->Value().Convert(fidl::flat::ConstantValue::Kind::kString, &resolved_foo));
  EXPECT_STREQ(static_cast<fidl::flat::StringConstantValue*>(resolved_foo.get())->MakeContents(),
               "abc");

  // Check `bar` arg.
  EXPECT_TRUE(example_struct->attributes->Get("attr")->GetArg("bar"));
  const auto& bar = example_struct->attributes->Get("attr")->GetArg("bar")->value;
  EXPECT_STREQ(bar->span.data(), "bar");
  ASSERT_EQ(bar->kind, fidl::flat::Constant::Kind::kIdentifier);

  std::unique_ptr<fidl::flat::ConstantValue> resolved_bar;
  EXPECT_TRUE(bar->Value().Convert(fidl::flat::ConstantValue::Kind::kBool, &resolved_bar));
  EXPECT_TRUE(static_cast<fidl::flat::BoolConstantValue*>(resolved_bar.get())->value);

  // Check `baz` arg.
  EXPECT_TRUE(example_struct->attributes->Get("attr")->GetArg("baz"));
  const auto& baz = example_struct->attributes->Get("attr")->GetArg("baz")->value;
  EXPECT_STREQ(baz->span.data(), "baz");
  ASSERT_EQ(baz->kind, fidl::flat::Constant::Kind::kIdentifier);

  std::unique_ptr<fidl::flat::ConstantValue> resolved_baz;
  EXPECT_TRUE(baz->Value().Convert(fidl::flat::ConstantValue::Kind::kBool, &resolved_baz));
  EXPECT_TRUE(!static_cast<fidl::flat::BoolConstantValue*>(resolved_baz.get())->value);
}

TEST(AttributesTests, BadReferencedNumericTypesWithoutSchema) {
  TestLibrary library(R"FIDL(
library example;

const foo int8 = -1;
const bar float32 = -2.3;

@attr(foo=foo, bar=bar)
type MyStruct = struct {};

)FIDL");
  ASSERT_ERRORED_TWICE_DURING_COMPILE(library, fidl::ErrCanOnlyUseStringOrBool,
                                      fidl::ErrCanOnlyUseStringOrBool);
}

TEST(AttributesTests, GoodLiteralTypesWithSchema) {
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
        usize=5,
        uintptr=6,
        uchar=7,
        float32=1.2,
        float64=-3.4)
type MyStruct = struct {};

)FIDL");
  library.AddAttributeSchema("attr")
      .AddArg("string", fidl::flat::AttributeArgSchema(fidl::flat::ConstantValue::Kind::kString))
      .AddArg("bool", fidl::flat::AttributeArgSchema(fidl::flat::ConstantValue::Kind::kBool))
      .AddArg("int8", fidl::flat::AttributeArgSchema(fidl::flat::ConstantValue::Kind::kInt8))
      .AddArg("int16", fidl::flat::AttributeArgSchema(fidl::flat::ConstantValue::Kind::kInt16))
      .AddArg("int32", fidl::flat::AttributeArgSchema(fidl::flat::ConstantValue::Kind::kInt32))
      .AddArg("int64", fidl::flat::AttributeArgSchema(fidl::flat::ConstantValue::Kind::kInt64))
      .AddArg("uint8", fidl::flat::AttributeArgSchema(fidl::flat::ConstantValue::Kind::kUint8))
      .AddArg("uint16", fidl::flat::AttributeArgSchema(fidl::flat::ConstantValue::Kind::kUint16))
      .AddArg("uint32", fidl::flat::AttributeArgSchema(fidl::flat::ConstantValue::Kind::kUint32))
      .AddArg("uint64", fidl::flat::AttributeArgSchema(fidl::flat::ConstantValue::Kind::kUint64))
      .AddArg("usize", fidl::flat::AttributeArgSchema(fidl::flat::ConstantValue::Kind::kZxUsize))
      .AddArg("uintptr",
              fidl::flat::AttributeArgSchema(fidl::flat::ConstantValue::Kind::kZxUintptr))
      .AddArg("uchar", fidl::flat::AttributeArgSchema(fidl::flat::ConstantValue::Kind::kZxUchar))
      .AddArg("float32", fidl::flat::AttributeArgSchema(fidl::flat::ConstantValue::Kind::kFloat32))
      .AddArg("float64", fidl::flat::AttributeArgSchema(fidl::flat::ConstantValue::Kind::kFloat64));
  ASSERT_COMPILED(library);

  auto example_struct = library.LookupStruct("MyStruct");
  ASSERT_NOT_NULL(example_struct);
  EXPECT_TRUE(example_struct->attributes->Get("attr"));

  // Check `string` arg.
  EXPECT_TRUE(example_struct->attributes->Get("attr")->GetArg("string"));
  const auto& string_val = example_struct->attributes->Get("attr")->GetArg("string")->value;
  EXPECT_STREQ(string_val->span.data(), "\"foo\"");
  ASSERT_EQ(string_val->kind, fidl::flat::Constant::Kind::kLiteral);

  std::unique_ptr<fidl::flat::ConstantValue> resolved_string;
  EXPECT_TRUE(
      string_val->Value().Convert(fidl::flat::ConstantValue::Kind::kString, &resolved_string));
  EXPECT_STREQ(static_cast<fidl::flat::StringConstantValue*>(resolved_string.get())->MakeContents(),
               "foo");

  // Check `bool` arg.
  EXPECT_TRUE(example_struct->attributes->Get("attr")->GetArg("bool"));
  const auto& bool_val = example_struct->attributes->Get("attr")->GetArg("bool")->value;
  EXPECT_STREQ(bool_val->span.data(), "true");
  ASSERT_EQ(bool_val->kind, fidl::flat::Constant::Kind::kLiteral);

  std::unique_ptr<fidl::flat::ConstantValue> resolved_bool;
  EXPECT_TRUE(bool_val->Value().Convert(fidl::flat::ConstantValue::Kind::kBool, &resolved_bool));
  EXPECT_EQ(static_cast<fidl::flat::BoolConstantValue*>(resolved_bool.get())->value, true);

  // Check `int8` arg.
  EXPECT_TRUE(example_struct->attributes->Get("attr")->GetArg("int8"));
  const auto& int8_val = example_struct->attributes->Get("attr")->GetArg("int8")->value;
  EXPECT_STREQ(int8_val->span.data(), "-1");
  ASSERT_EQ(int8_val->kind, fidl::flat::Constant::Kind::kLiteral);

  std::unique_ptr<fidl::flat::ConstantValue> resolved_int8;
  EXPECT_TRUE(int8_val->Value().Convert(fidl::flat::ConstantValue::Kind::kInt8, &resolved_int8));
  EXPECT_EQ(static_cast<fidl::flat::NumericConstantValue<int8_t>*>(resolved_int8.get())->value, -1);

  // Check `int16` arg.
  EXPECT_TRUE(example_struct->attributes->Get("attr")->GetArg("int16"));
  const auto& int16_val = example_struct->attributes->Get("attr")->GetArg("int16")->value;
  EXPECT_STREQ(int16_val->span.data(), "-2");
  ASSERT_EQ(int16_val->kind, fidl::flat::Constant::Kind::kLiteral);

  std::unique_ptr<fidl::flat::ConstantValue> resolved_int16;
  EXPECT_TRUE(int16_val->Value().Convert(fidl::flat::ConstantValue::Kind::kInt16, &resolved_int16));
  EXPECT_EQ(static_cast<fidl::flat::NumericConstantValue<int16_t>*>(resolved_int16.get())->value,
            -2);

  // Check `int32` arg.
  EXPECT_TRUE(example_struct->attributes->Get("attr")->GetArg("int32"));
  const auto& int32_val = example_struct->attributes->Get("attr")->GetArg("int32")->value;
  EXPECT_STREQ(int32_val->span.data(), "-3");
  ASSERT_EQ(int32_val->kind, fidl::flat::Constant::Kind::kLiteral);

  std::unique_ptr<fidl::flat::ConstantValue> resolved_int32;
  EXPECT_TRUE(int32_val->Value().Convert(fidl::flat::ConstantValue::Kind::kInt32, &resolved_int32));
  EXPECT_EQ(static_cast<fidl::flat::NumericConstantValue<int32_t>*>(resolved_int32.get())->value,
            -3);

  // Check `int64` arg.
  EXPECT_TRUE(example_struct->attributes->Get("attr")->GetArg("int64"));
  const auto& int64_val = example_struct->attributes->Get("attr")->GetArg("int64")->value;
  EXPECT_STREQ(int64_val->span.data(), "-4");
  ASSERT_EQ(int64_val->kind, fidl::flat::Constant::Kind::kLiteral);

  std::unique_ptr<fidl::flat::ConstantValue> resolved_int64;
  EXPECT_TRUE(int64_val->Value().Convert(fidl::flat::ConstantValue::Kind::kInt64, &resolved_int64));
  EXPECT_EQ(static_cast<fidl::flat::NumericConstantValue<int64_t>*>(resolved_int64.get())->value,
            -4);

  // Check `uint8` arg.
  EXPECT_TRUE(example_struct->attributes->Get("attr")->GetArg("uint8"));
  const auto& uint8_val = example_struct->attributes->Get("attr")->GetArg("uint8")->value;
  EXPECT_STREQ(uint8_val->span.data(), "1");
  ASSERT_EQ(uint8_val->kind, fidl::flat::Constant::Kind::kLiteral);

  std::unique_ptr<fidl::flat::ConstantValue> resolved_uint8;
  EXPECT_TRUE(uint8_val->Value().Convert(fidl::flat::ConstantValue::Kind::kUint8, &resolved_uint8));
  EXPECT_EQ(static_cast<fidl::flat::NumericConstantValue<uint8_t>*>(resolved_uint8.get())->value,
            1);

  // Check `uint16` arg.
  EXPECT_TRUE(example_struct->attributes->Get("attr")->GetArg("uint16"));
  const auto& uint16_val = example_struct->attributes->Get("attr")->GetArg("uint16")->value;
  EXPECT_STREQ(uint16_val->span.data(), "2");
  ASSERT_EQ(uint16_val->kind, fidl::flat::Constant::Kind::kLiteral);

  std::unique_ptr<fidl::flat::ConstantValue> resolved_uint16;
  EXPECT_TRUE(
      uint16_val->Value().Convert(fidl::flat::ConstantValue::Kind::kUint16, &resolved_uint16));
  EXPECT_EQ(static_cast<fidl::flat::NumericConstantValue<uint16_t>*>(resolved_uint16.get())->value,
            2);

  // Check `uint32` arg.
  EXPECT_TRUE(example_struct->attributes->Get("attr")->GetArg("uint32"));
  const auto& uint32_val = example_struct->attributes->Get("attr")->GetArg("uint32")->value;
  EXPECT_STREQ(uint32_val->span.data(), "3");
  ASSERT_EQ(uint32_val->kind, fidl::flat::Constant::Kind::kLiteral);

  std::unique_ptr<fidl::flat::ConstantValue> resolved_uint32;
  EXPECT_TRUE(
      uint32_val->Value().Convert(fidl::flat::ConstantValue::Kind::kUint32, &resolved_uint32));
  EXPECT_EQ(static_cast<fidl::flat::NumericConstantValue<uint32_t>*>(resolved_uint32.get())->value,
            3);

  // Check `uint64` arg.
  EXPECT_TRUE(example_struct->attributes->Get("attr")->GetArg("uint64"));
  const auto& uint64_val = example_struct->attributes->Get("attr")->GetArg("uint64")->value;
  EXPECT_STREQ(uint64_val->span.data(), "4");
  ASSERT_EQ(uint64_val->kind, fidl::flat::Constant::Kind::kLiteral);

  std::unique_ptr<fidl::flat::ConstantValue> resolved_uint64;
  EXPECT_TRUE(
      uint64_val->Value().Convert(fidl::flat::ConstantValue::Kind::kUint64, &resolved_uint64));
  EXPECT_EQ(static_cast<fidl::flat::NumericConstantValue<uint64_t>*>(resolved_uint64.get())->value,
            4);

  // Check `usize` arg.
  EXPECT_TRUE(example_struct->attributes->Get("attr")->GetArg("usize"));
  const auto& usize_val = example_struct->attributes->Get("attr")->GetArg("usize")->value;
  EXPECT_STREQ(usize_val->span.data(), "5");
  ASSERT_EQ(usize_val->kind, fidl::flat::Constant::Kind::kLiteral);

  std::unique_ptr<fidl::flat::ConstantValue> resolved_usize;
  EXPECT_TRUE(
      usize_val->Value().Convert(fidl::flat::ConstantValue::Kind::kZxUsize, &resolved_usize));
  EXPECT_EQ(static_cast<fidl::flat::NumericConstantValue<uint64_t>*>(resolved_usize.get())->value,
            5);

  // Check `uintptr` arg.
  EXPECT_TRUE(example_struct->attributes->Get("attr")->GetArg("uintptr"));
  const auto& uintptr_val = example_struct->attributes->Get("attr")->GetArg("uintptr")->value;
  EXPECT_STREQ(uintptr_val->span.data(), "6");
  ASSERT_EQ(uintptr_val->kind, fidl::flat::Constant::Kind::kLiteral);

  std::unique_ptr<fidl::flat::ConstantValue> resolved_uintptr;
  EXPECT_TRUE(
      uintptr_val->Value().Convert(fidl::flat::ConstantValue::Kind::kZxUintptr, &resolved_uintptr));
  EXPECT_EQ(static_cast<fidl::flat::NumericConstantValue<uint64_t>*>(resolved_uintptr.get())->value,
            6);

  // Check `uchar` arg.
  EXPECT_TRUE(example_struct->attributes->Get("attr")->GetArg("uchar"));
  const auto& uchar_val = example_struct->attributes->Get("attr")->GetArg("uchar")->value;
  EXPECT_STREQ(uchar_val->span.data(), "7");
  ASSERT_EQ(uchar_val->kind, fidl::flat::Constant::Kind::kLiteral);

  std::unique_ptr<fidl::flat::ConstantValue> resolved_uchar;
  EXPECT_TRUE(
      uchar_val->Value().Convert(fidl::flat::ConstantValue::Kind::kZxUchar, &resolved_uchar));
  EXPECT_EQ(static_cast<fidl::flat::NumericConstantValue<uint8_t>*>(resolved_uchar.get())->value,
            7);

  // Check `float32` arg.
  EXPECT_TRUE(example_struct->attributes->Get("attr")->GetArg("float32"));
  const auto& float32_val = example_struct->attributes->Get("attr")->GetArg("float32")->value;
  EXPECT_STREQ(float32_val->span.data(), "1.2");
  ASSERT_EQ(float32_val->kind, fidl::flat::Constant::Kind::kLiteral);

  std::unique_ptr<fidl::flat::ConstantValue> resolved_float32;
  EXPECT_TRUE(
      float32_val->Value().Convert(fidl::flat::ConstantValue::Kind::kFloat32, &resolved_float32));
  EXPECT_TRUE(static_cast<fidl::flat::NumericConstantValue<float>*>(resolved_float32.get())->value >
              1.1);
  EXPECT_TRUE(static_cast<fidl::flat::NumericConstantValue<float>*>(resolved_float32.get())->value <
              1.3);

  // Check `float64` arg.
  EXPECT_TRUE(example_struct->attributes->Get("attr")->GetArg("float64"));
  const auto& float64_val = example_struct->attributes->Get("attr")->GetArg("float64")->value;
  EXPECT_STREQ(float64_val->span.data(), "-3.4");
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
  TestLibrary library(R"FIDL(
library example;

@attr(true)
type MyStruct = struct {};

)FIDL");
  library.AddAttributeSchema("attr").AddArg(
      "string", fidl::flat::AttributeArgSchema(fidl::flat::ConstantValue::Kind::kString));
  ASSERT_ERRORED_TWICE_DURING_COMPILE(library, fidl::ErrTypeCannotBeConvertedToType,
                                      fidl::ErrCouldNotResolveAttributeArg);
}

TEST(AttributesTests, BadInvalidLiteralBoolTypeWithSchema) {
  TestLibrary library(R"FIDL(
library example;

@attr("foo")
type MyStruct = struct {};

)FIDL");
  library.AddAttributeSchema("attr").AddArg(
      "bool", fidl::flat::AttributeArgSchema(fidl::flat::ConstantValue::Kind::kBool));
  ASSERT_ERRORED_TWICE_DURING_COMPILE(library, fidl::ErrTypeCannotBeConvertedToType,
                                      fidl::ErrCouldNotResolveAttributeArg);
}

TEST(AttributesTests, BadInvalidLiteralNumericTypeWithSchema) {
  TestLibrary library(R"FIDL(
library example;

@attr(-1)
type MyStruct = struct {};

)FIDL");
  library.AddAttributeSchema("attr").AddArg(
      "uint8", fidl::flat::AttributeArgSchema(fidl::flat::ConstantValue::Kind::kUint8));
  ASSERT_ERRORED_TWICE_DURING_COMPILE(library, fidl::ErrConstantOverflowsType,
                                      fidl::ErrCouldNotResolveAttributeArg);
}

TEST(AttributesTests, BadInvalidLiteralWithRealSchema) {
  TestLibrary library;
  library.AddFile("bad/fi-0065-c.test.fidl");
  ASSERT_ERRORED_TWICE_DURING_COMPILE(library, fidl::ErrTypeCannotBeConvertedToType,
                                      fidl::ErrCouldNotResolveAttributeArg);
}

TEST(AttributesTests, GoodReferencedTypesWithSchema) {
  TestLibrary library(R"FIDL(
library fidl.test;

const string fidl.string = "foo";
const bool fidl.bool = true;
const int8 fidl.int8 = -1;
const int16 fidl.int16 = -2;
const int32 fidl.int32 = -3;
type int64 = enum : fidl.int64 {
    MEMBER = -4;
};
const uint8 fidl.uint8 = 1;
const uint16 fidl.uint16 = 2;
const uint32 fidl.uint32 = 3;
type uint64 = bits : fidl.uint64 {
    MEMBER = 4;
};
const usize fidl.usize = 5;
const uintptr fidl.uintptr = 6;
const uchar fidl.uchar = 7;
const float32 fidl.float32 = 1.2;
const float64 fidl.float64 = -3.4;

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
        usize=usize,
        uintptr=uintptr,
        uchar=uchar,
        float32=float32,
        float64=float64)
type MyStruct = struct {};

)FIDL");
  library.AddAttributeSchema("attr")
      .AddArg("string", fidl::flat::AttributeArgSchema(fidl::flat::ConstantValue::Kind::kString))
      .AddArg("bool", fidl::flat::AttributeArgSchema(fidl::flat::ConstantValue::Kind::kBool))
      .AddArg("int8", fidl::flat::AttributeArgSchema(fidl::flat::ConstantValue::Kind::kInt8))
      .AddArg("int16", fidl::flat::AttributeArgSchema(fidl::flat::ConstantValue::Kind::kInt16))
      .AddArg("int32", fidl::flat::AttributeArgSchema(fidl::flat::ConstantValue::Kind::kInt32))
      .AddArg("int64", fidl::flat::AttributeArgSchema(fidl::flat::ConstantValue::Kind::kInt64))
      .AddArg("uint8", fidl::flat::AttributeArgSchema(fidl::flat::ConstantValue::Kind::kUint8))
      .AddArg("uint16", fidl::flat::AttributeArgSchema(fidl::flat::ConstantValue::Kind::kUint16))
      .AddArg("uint32", fidl::flat::AttributeArgSchema(fidl::flat::ConstantValue::Kind::kUint32))
      .AddArg("uint64", fidl::flat::AttributeArgSchema(fidl::flat::ConstantValue::Kind::kUint64))
      .AddArg("usize", fidl::flat::AttributeArgSchema(fidl::flat::ConstantValue::Kind::kZxUsize))
      .AddArg("uintptr",
              fidl::flat::AttributeArgSchema(fidl::flat::ConstantValue::Kind::kZxUintptr))
      .AddArg("uchar", fidl::flat::AttributeArgSchema(fidl::flat::ConstantValue::Kind::kZxUchar))
      .AddArg("float32", fidl::flat::AttributeArgSchema(fidl::flat::ConstantValue::Kind::kFloat32))
      .AddArg("float64", fidl::flat::AttributeArgSchema(fidl::flat::ConstantValue::Kind::kFloat64));

  // For the use of usize, uintptr, and uchar.
  library.EnableFlag(fidl::ExperimentalFlags::Flag::kZxCTypes);

  ASSERT_COMPILED(library);

  auto example_struct = library.LookupStruct("MyStruct");
  ASSERT_NOT_NULL(example_struct);
  EXPECT_TRUE(example_struct->attributes->Get("attr"));

  // Check `string` arg.
  EXPECT_TRUE(example_struct->attributes->Get("attr")->GetArg("string"));
  const auto& string_val = example_struct->attributes->Get("attr")->GetArg("string")->value;
  EXPECT_STREQ(string_val->span.data(), "string");
  ASSERT_EQ(string_val->kind, fidl::flat::Constant::Kind::kIdentifier);

  std::unique_ptr<fidl::flat::ConstantValue> resolved_string;
  EXPECT_TRUE(
      string_val->Value().Convert(fidl::flat::ConstantValue::Kind::kString, &resolved_string));
  EXPECT_STREQ(static_cast<fidl::flat::StringConstantValue*>(resolved_string.get())->MakeContents(),
               "foo");

  // Check `bool` arg.
  EXPECT_TRUE(example_struct->attributes->Get("attr")->GetArg("bool"));
  const auto& bool_val = example_struct->attributes->Get("attr")->GetArg("bool")->value;
  EXPECT_STREQ(bool_val->span.data(), "bool");
  ASSERT_EQ(bool_val->kind, fidl::flat::Constant::Kind::kIdentifier);

  std::unique_ptr<fidl::flat::ConstantValue> resolved_bool;
  EXPECT_TRUE(bool_val->Value().Convert(fidl::flat::ConstantValue::Kind::kBool, &resolved_bool));
  EXPECT_EQ(static_cast<fidl::flat::BoolConstantValue*>(resolved_bool.get())->value, true);

  // Check `int8` arg.
  EXPECT_TRUE(example_struct->attributes->Get("attr")->GetArg("int8"));
  const auto& int8_val = example_struct->attributes->Get("attr")->GetArg("int8")->value;
  EXPECT_STREQ(int8_val->span.data(), "int8");
  ASSERT_EQ(int8_val->kind, fidl::flat::Constant::Kind::kIdentifier);

  std::unique_ptr<fidl::flat::ConstantValue> resolved_int8;
  EXPECT_TRUE(int8_val->Value().Convert(fidl::flat::ConstantValue::Kind::kInt8, &resolved_int8));
  EXPECT_EQ(static_cast<fidl::flat::NumericConstantValue<int8_t>*>(resolved_int8.get())->value, -1);

  // Check `int16` arg.
  EXPECT_TRUE(example_struct->attributes->Get("attr")->GetArg("int16"));
  const auto& int16_val = example_struct->attributes->Get("attr")->GetArg("int16")->value;
  EXPECT_STREQ(int16_val->span.data(), "int16");
  ASSERT_EQ(int16_val->kind, fidl::flat::Constant::Kind::kIdentifier);

  std::unique_ptr<fidl::flat::ConstantValue> resolved_int16;
  EXPECT_TRUE(int16_val->Value().Convert(fidl::flat::ConstantValue::Kind::kInt16, &resolved_int16));
  EXPECT_EQ(static_cast<fidl::flat::NumericConstantValue<int16_t>*>(resolved_int16.get())->value,
            -2);

  // Check `int32` arg.
  EXPECT_TRUE(example_struct->attributes->Get("attr")->GetArg("int32"));
  const auto& int32_val = example_struct->attributes->Get("attr")->GetArg("int32")->value;
  EXPECT_STREQ(int32_val->span.data(), "int32");
  ASSERT_EQ(int32_val->kind, fidl::flat::Constant::Kind::kIdentifier);

  std::unique_ptr<fidl::flat::ConstantValue> resolved_int32;
  EXPECT_TRUE(int32_val->Value().Convert(fidl::flat::ConstantValue::Kind::kInt32, &resolved_int32));
  EXPECT_EQ(static_cast<fidl::flat::NumericConstantValue<int32_t>*>(resolved_int32.get())->value,
            -3);

  // Check `int64` arg.
  EXPECT_TRUE(example_struct->attributes->Get("attr")->GetArg("int64"));
  const auto& int64_val = example_struct->attributes->Get("attr")->GetArg("int64")->value;
  EXPECT_STREQ(int64_val->span.data(), "int64.MEMBER");
  ASSERT_EQ(int64_val->kind, fidl::flat::Constant::Kind::kIdentifier);

  std::unique_ptr<fidl::flat::ConstantValue> resolved_int64;
  EXPECT_TRUE(int64_val->Value().Convert(fidl::flat::ConstantValue::Kind::kInt64, &resolved_int64));
  EXPECT_EQ(static_cast<fidl::flat::NumericConstantValue<int64_t>*>(resolved_int64.get())->value,
            -4);

  // Check `uint8` arg.
  EXPECT_TRUE(example_struct->attributes->Get("attr")->GetArg("uint8"));
  const auto& uint8_val = example_struct->attributes->Get("attr")->GetArg("uint8")->value;
  EXPECT_STREQ(uint8_val->span.data(), "uint8");
  ASSERT_EQ(uint8_val->kind, fidl::flat::Constant::Kind::kIdentifier);

  std::unique_ptr<fidl::flat::ConstantValue> resolved_uint8;
  EXPECT_TRUE(uint8_val->Value().Convert(fidl::flat::ConstantValue::Kind::kUint8, &resolved_uint8));
  EXPECT_EQ(static_cast<fidl::flat::NumericConstantValue<uint8_t>*>(resolved_uint8.get())->value,
            1);

  // Check `uint16` arg.
  EXPECT_TRUE(example_struct->attributes->Get("attr")->GetArg("uint16"));
  const auto& uint16_val = example_struct->attributes->Get("attr")->GetArg("uint16")->value;
  EXPECT_STREQ(uint16_val->span.data(), "uint16");
  ASSERT_EQ(uint16_val->kind, fidl::flat::Constant::Kind::kIdentifier);

  std::unique_ptr<fidl::flat::ConstantValue> resolved_uint16;
  EXPECT_TRUE(
      uint16_val->Value().Convert(fidl::flat::ConstantValue::Kind::kUint16, &resolved_uint16));
  EXPECT_EQ(static_cast<fidl::flat::NumericConstantValue<uint16_t>*>(resolved_uint16.get())->value,
            2);

  // Check `uint32` arg.
  EXPECT_TRUE(example_struct->attributes->Get("attr")->GetArg("uint32"));
  const auto& uint32_val = example_struct->attributes->Get("attr")->GetArg("uint32")->value;
  EXPECT_STREQ(uint32_val->span.data(), "uint32");
  ASSERT_EQ(uint32_val->kind, fidl::flat::Constant::Kind::kIdentifier);

  std::unique_ptr<fidl::flat::ConstantValue> resolved_uint32;
  EXPECT_TRUE(
      uint32_val->Value().Convert(fidl::flat::ConstantValue::Kind::kUint32, &resolved_uint32));
  EXPECT_EQ(static_cast<fidl::flat::NumericConstantValue<uint32_t>*>(resolved_uint32.get())->value,
            3);

  // Check `uint64` arg.
  EXPECT_TRUE(example_struct->attributes->Get("attr")->GetArg("uint64"));
  const auto& uint64_val = example_struct->attributes->Get("attr")->GetArg("uint64")->value;
  EXPECT_STREQ(uint64_val->span.data(), "uint64.MEMBER");
  ASSERT_EQ(uint64_val->kind, fidl::flat::Constant::Kind::kIdentifier);

  std::unique_ptr<fidl::flat::ConstantValue> resolved_uint64;
  EXPECT_TRUE(
      uint64_val->Value().Convert(fidl::flat::ConstantValue::Kind::kUint64, &resolved_uint64));
  EXPECT_EQ(static_cast<fidl::flat::NumericConstantValue<uint64_t>*>(resolved_uint64.get())->value,
            4);

  // Check `usize` arg.
  EXPECT_TRUE(example_struct->attributes->Get("attr")->GetArg("usize"));
  const auto& usize_val = example_struct->attributes->Get("attr")->GetArg("usize")->value;
  EXPECT_STREQ(usize_val->span.data(), "usize");
  ASSERT_EQ(usize_val->kind, fidl::flat::Constant::Kind::kIdentifier);

  std::unique_ptr<fidl::flat::ConstantValue> resolved_usize;
  EXPECT_TRUE(
      usize_val->Value().Convert(fidl::flat::ConstantValue::Kind::kZxUsize, &resolved_usize));
  EXPECT_EQ(static_cast<fidl::flat::NumericConstantValue<uint64_t>*>(resolved_usize.get())->value,
            5);

  // Check `uintptr` arg.
  EXPECT_TRUE(example_struct->attributes->Get("attr")->GetArg("uintptr"));
  const auto& uintptr_val = example_struct->attributes->Get("attr")->GetArg("uintptr")->value;
  EXPECT_STREQ(uintptr_val->span.data(), "uintptr");
  ASSERT_EQ(uintptr_val->kind, fidl::flat::Constant::Kind::kIdentifier);

  std::unique_ptr<fidl::flat::ConstantValue> resolved_uintptr;
  EXPECT_TRUE(
      uintptr_val->Value().Convert(fidl::flat::ConstantValue::Kind::kZxUintptr, &resolved_uintptr));
  EXPECT_EQ(static_cast<fidl::flat::NumericConstantValue<uint64_t>*>(resolved_uintptr.get())->value,
            6);

  // Check `uchar` arg.
  EXPECT_TRUE(example_struct->attributes->Get("attr")->GetArg("uchar"));
  const auto& uchar_val = example_struct->attributes->Get("attr")->GetArg("uchar")->value;
  EXPECT_STREQ(uchar_val->span.data(), "uchar");
  ASSERT_EQ(uchar_val->kind, fidl::flat::Constant::Kind::kIdentifier);

  std::unique_ptr<fidl::flat::ConstantValue> resolved_uchar;
  EXPECT_TRUE(
      uchar_val->Value().Convert(fidl::flat::ConstantValue::Kind::kZxUchar, &resolved_uchar));
  EXPECT_EQ(static_cast<fidl::flat::NumericConstantValue<uint8_t>*>(resolved_uchar.get())->value,
            7);

  // Check `float32` arg.
  EXPECT_TRUE(example_struct->attributes->Get("attr")->GetArg("float32"));
  const auto& float32_val = example_struct->attributes->Get("attr")->GetArg("float32")->value;
  EXPECT_STREQ(float32_val->span.data(), "float32");
  ASSERT_EQ(float32_val->kind, fidl::flat::Constant::Kind::kIdentifier);

  std::unique_ptr<fidl::flat::ConstantValue> resolved_float32;
  EXPECT_TRUE(
      float32_val->Value().Convert(fidl::flat::ConstantValue::Kind::kFloat32, &resolved_float32));
  EXPECT_TRUE(static_cast<fidl::flat::NumericConstantValue<float>*>(resolved_float32.get())->value >
              1.1);
  EXPECT_TRUE(static_cast<fidl::flat::NumericConstantValue<float>*>(resolved_float32.get())->value <
              1.3);

  // Check `float64` arg.
  EXPECT_TRUE(example_struct->attributes->Get("attr")->GetArg("float64"));
  const auto& float64_val = example_struct->attributes->Get("attr")->GetArg("float64")->value;
  EXPECT_STREQ(float64_val->span.data(), "float64");
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
  TestLibrary library(R"FIDL(
library example;

const foo bool = true;

@attr(foo)
type MyStruct = struct {};

)FIDL");
  library.AddAttributeSchema("attr").AddArg(
      "string", fidl::flat::AttributeArgSchema(fidl::flat::ConstantValue::Kind::kString));
  ASSERT_ERRORED_TWICE_DURING_COMPILE(library, fidl::ErrTypeCannotBeConvertedToType,
                                      fidl::ErrCouldNotResolveAttributeArg);
}

TEST(AttributesTests, BadInvalidReferencedBoolTypeWithSchema) {
  TestLibrary library(R"FIDL(
library example;

const foo string:3 = "foo";

@attr(foo)
type MyStruct = struct {};

)FIDL");
  library.AddAttributeSchema("attr").AddArg(
      "bool", fidl::flat::AttributeArgSchema(fidl::flat::ConstantValue::Kind::kBool));
  ASSERT_ERRORED_TWICE_DURING_COMPILE(library, fidl::ErrTypeCannotBeConvertedToType,
                                      fidl::ErrCouldNotResolveAttributeArg);
}

TEST(AttributesTests, BadInvalidReferencedNumericTypeWithSchema) {
  TestLibrary library(R"FIDL(
library example;

const foo uint16 = 259;

@attr(foo)
type MyStruct = struct {};

)FIDL");
  library.AddAttributeSchema("attr").AddArg(
      "int8", fidl::flat::AttributeArgSchema(fidl::flat::ConstantValue::Kind::kInt8));
  ASSERT_ERRORED_TWICE_DURING_COMPILE(library, fidl::ErrTypeCannotBeConvertedToType,
                                      fidl::ErrCouldNotResolveAttributeArg);
}

TEST(AttributesTests, GoodCompileEarlyAttributeLiteralArgument) {
  TestLibrary library(R"FIDL(
library example;

@attr(1)
type MyStruct = struct {};

)FIDL");
  library.AddAttributeSchema("attr")
      .AddArg("int8", fidl::flat::AttributeArgSchema(fidl::flat::ConstantValue::Kind::kUint8))
      .CompileEarly();
  ASSERT_COMPILED(library);
}

TEST(AttributesTests, BadCompileEarlyAttributeReferencedArgument) {
  TestLibrary library(R"FIDL(
library example;

@attr(BAD)
type MyStruct = struct {};

const BAD uint8 = 1;

)FIDL");
  library.AddAttributeSchema("attr")
      .AddArg("int8", fidl::flat::AttributeArgSchema(fidl::flat::ConstantValue::Kind::kUint8))
      .CompileEarly();
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrAttributeArgRequiresLiteral);
}

TEST(AttributesTests, GoodAnonymousArgumentGetsNamedValue) {
  TestLibrary library(R"FIDL(
library example;

@attr("abc")
type MyStruct = struct {};

)FIDL");
  ASSERT_COMPILED(library);

  auto example_struct = library.LookupStruct("MyStruct");
  ASSERT_NOT_NULL(example_struct);
  ASSERT_EQ(example_struct->attributes->attributes.size(), 1);
  ASSERT_EQ(example_struct->attributes->attributes[0]->args.size(), 1);
  EXPECT_EQ(example_struct->attributes->attributes[0]->args[0]->name.value().data(), "value");
}

TEST(AttributesTests, GoodSingleNamedArgumentKeepsName) {
  TestLibrary library(R"FIDL(
library example;

@attr(foo="abc")
type MyStruct = struct {};

)FIDL");
  ASSERT_COMPILED(library);

  auto example_struct = library.LookupStruct("MyStruct");
  ASSERT_NOT_NULL(example_struct);
  ASSERT_EQ(example_struct->attributes->attributes.size(), 1);
  ASSERT_EQ(example_struct->attributes->attributes[0]->args.size(), 1);
  EXPECT_EQ(example_struct->attributes->attributes[0]->args[0]->name.value().data(), "foo");
}

TEST(AttributesTests, BadReferencesNonexistentConstWithoutSchema) {
  TestLibrary library(R"FIDL(
library example;

@foo(nonexistent)
type MyStruct = struct {};

)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrNameNotFound);
}

TEST(AttributesTests, BadReferencesNonexistentConstWithSchema) {
  TestLibrary library(R"FIDL(
library example;

@foo(nonexistent)
type MyStruct = struct {};

)FIDL");
  library.AddAttributeSchema("foo").AddArg(
      "value", fidl::flat::AttributeArgSchema(fidl::flat::ConstantValue::Kind::kBool));
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrNameNotFound);
}

TEST(AttributesTests, BadReferencesInvalidConstWithoutSchema) {
  TestLibrary library(R"FIDL(
library example;

@foo(BAD)
type MyStruct = struct {};

const BAD bool = "not a bool";

)FIDL");
  ASSERT_FALSE(library.Compile());
  ASSERT_EQ(library.errors().size(), 3);
  EXPECT_ERR(library.errors()[0], fidl::ErrTypeCannotBeConvertedToType);
  EXPECT_ERR(library.errors()[1], fidl::ErrCannotResolveConstantValue);
  EXPECT_ERR(library.errors()[2], fidl::ErrCouldNotResolveAttributeArg);
}

TEST(AttributesTests, BadReferencesInvalidConstWithSchema) {
  TestLibrary library(R"FIDL(
library example;

@foo(BAD)
type MyStruct = struct {};

const BAD bool = "not a bool";

)FIDL");
  library.AddAttributeSchema("foo").AddArg(
      "value", fidl::flat::AttributeArgSchema(fidl::flat::ConstantValue::Kind::kBool));
  ASSERT_FALSE(library.Compile());
  ASSERT_EQ(library.errors().size(), 3);
  EXPECT_ERR(library.errors()[0], fidl::ErrTypeCannotBeConvertedToType);
  EXPECT_ERR(library.errors()[1], fidl::ErrCannotResolveConstantValue);
  EXPECT_ERR(library.errors()[2], fidl::ErrCouldNotResolveAttributeArg);
}

TEST(AttributesTests, BadSelfReferenceWithoutSchemaBool) {
  TestLibrary library(R"FIDL(
library example;

@foo(BAR)
const BAR bool = true;

)FIDL");
  ASSERT_ERRORED_TWICE_DURING_COMPILE(library, fidl::ErrIncludeCycle,
                                      fidl::ErrCouldNotResolveAttributeArg);
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "const 'BAR' -> const 'BAR'");
}

TEST(AttributesTests, BadSelfReferenceWithoutSchemaString) {
  TestLibrary library(R"FIDL(
library example;

@foo(BAR)
const BAR string = "bar";

)FIDL");
  ASSERT_ERRORED_TWICE_DURING_COMPILE(library, fidl::ErrIncludeCycle,
                                      fidl::ErrCouldNotResolveAttributeArg);
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "const 'BAR' -> const 'BAR'");
}

TEST(AttributesTests, BadSelfReferenceWithSchema) {
  TestLibrary library(R"FIDL(
library example;

@foo(BAR)
const BAR bool = true;

)FIDL");
  library.AddAttributeSchema("foo").AddArg(
      "value", fidl::flat::AttributeArgSchema(fidl::flat::ConstantValue::Kind::kBool));
  ASSERT_ERRORED_TWICE_DURING_COMPILE(library, fidl::ErrIncludeCycle,
                                      fidl::ErrCouldNotResolveAttributeArg);
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "const 'BAR' -> const 'BAR'");
}

TEST(AttributesTests, BadMutualReferenceWithoutSchemaBool) {
  TestLibrary library(R"FIDL(
library example;

@foo(SECOND)
const FIRST bool = true;
@foo(FIRST)
const SECOND bool = false;

)FIDL");
  ASSERT_ERRORED_TWICE_DURING_COMPILE(library, fidl::ErrIncludeCycle,
                                      fidl::ErrCouldNotResolveAttributeArg);
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(),
                "const 'FIRST' -> const 'SECOND' -> const 'FIRST'");
}

TEST(AttributesTests, BadMutualReferenceWithoutSchemaString) {
  TestLibrary library(R"FIDL(
library example;

@foo(SECOND)
const FIRST string = "first";
@foo(FIRST)
const SECOND string = "second";

)FIDL");
  ASSERT_ERRORED_TWICE_DURING_COMPILE(library, fidl::ErrIncludeCycle,
                                      fidl::ErrCouldNotResolveAttributeArg);
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(),
                "const 'FIRST' -> const 'SECOND' -> const 'FIRST'");
}

TEST(AttributesTests, BadMutualReferenceWithSchema) {
  TestLibrary library(R"FIDL(
library example;

@foo(SECOND)
const FIRST bool = true;
@foo(FIRST)
const SECOND bool = false;

)FIDL");
  library.AddAttributeSchema("foo").AddArg(
      "value", fidl::flat::AttributeArgSchema(fidl::flat::ConstantValue::Kind::kBool));
  ASSERT_ERRORED_TWICE_DURING_COMPILE(library, fidl::ErrIncludeCycle,
                                      fidl::ErrCouldNotResolveAttributeArg);
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(),
                "const 'FIRST' -> const 'SECOND' -> const 'FIRST'");
}

TEST(AttributesTests, BadLibraryReferencesNonexistentConst) {
  TestLibrary library(R"FIDL(
@foo(nonexistent)
library example;
)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrNameNotFound);
}

TEST(AttributesTests, BadLibraryReferencesConst) {
  TestLibrary library(R"FIDL(
@foo(BAR)
library example;

const BAR bool = true;

)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrReferenceInLibraryAttribute);
}

TEST(AttributesTests, BadLibraryReferencesExternalConst) {
  SharedAmongstLibraries shared;
  TestLibrary dependency(&shared, "dependency.fidl", R"FIDL(
library dependency;

const BAR bool = true;
)FIDL");
  ASSERT_COMPILED(dependency);

  TestLibrary library(&shared, "example.fidl", R"FIDL(
@foo(dependency.BAR)
library example;

using dependency;
)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrReferenceInLibraryAttribute);
}

TEST(AttributesTests, GoodDiscoverableImplicitName) {
  TestLibrary library(R"FIDL(
library example;

@discoverable
protocol Foo {};
)FIDL");
  ASSERT_COMPILED(library);
}

TEST(AttributesTests, GoodDiscoverableExplicitName) {
  for (auto name : {"example.Foo", "notexample.NotFoo", "not.example.NotFoo"}) {
    std::string library_str = R"FIDL(
library example;

@discoverable("%1")
protocol Foo {};
)FIDL";
    library_str.replace(library_str.find("%1"), 2, name);
    TestLibrary library(library_str);
    ASSERT_COMPILED(library);
  }
}

TEST(AttributesTests, BadDiscoverableInvalidName) {
  for (auto name : {"", "example/Foo", "Foo", "not example.Not Foo"}) {
    std::string library_str = R"FIDL(
library example;

@discoverable("%1")
protocol Foo {};
)FIDL";
    library_str.replace(library_str.find("%1"), 2, name);
    TestLibrary library(library_str);
    ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrInvalidDiscoverableName);
  }
}

}  // namespace
