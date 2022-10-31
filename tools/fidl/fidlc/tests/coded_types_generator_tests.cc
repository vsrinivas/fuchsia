// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/assert.h>

#include <zxtest/zxtest.h>

#include "tools/fidl/fidlc/include/fidl/coded_ast.h"
#include "tools/fidl/fidlc/include/fidl/coded_types_generator.h"
#include "tools/fidl/fidlc/tests/error_test.h"
#include "tools/fidl/fidlc/tests/test_library.h"

namespace {

const fidl::coded::StructField& field(const fidl::coded::StructElement& element) {
  ZX_ASSERT(std::holds_alternative<const fidl::coded::StructField>(element));
  return std::get<const fidl::coded::StructField>(element);
}
const fidl::coded::StructPadding& padding(const fidl::coded::StructElement& element) {
  ZX_ASSERT(std::holds_alternative<const fidl::coded::StructPadding>(element));
  return std::get<const fidl::coded::StructPadding>(element);
}

TEST(CodedTypesGeneratorTests, GoodCodedTypesOfArrays) {
  TestLibrary library(R"FIDL(library example;

type Arrays = struct {
    prime array<uint8, 7>;
    next_prime array<array<uint8, 7>, 11>;
    next_next_prime array<array<array<uint8, 7>, 11>, 13>;
};
)FIDL");
  ASSERT_COMPILED(library);
  fidl::CodedTypesGenerator gen(library.compilation());
  gen.CompileCodedTypes();

  ASSERT_EQ(4, gen.coded_types().size());

  auto type0 = gen.coded_types().at(0).get();
  EXPECT_STREQ("uint8", type0->coded_name.c_str());
  EXPECT_TRUE(type0->is_coding_needed);
  ASSERT_EQ(fidl::coded::Type::Kind::kPrimitive, type0->kind);
  auto type0_primitive = static_cast<const fidl::coded::PrimitiveType*>(type0);
  EXPECT_EQ(fidl::types::PrimitiveSubtype::kUint8, type0_primitive->subtype);

  auto type1 = gen.coded_types().at(1).get();
  EXPECT_STREQ("Array7_5uint8", type1->coded_name.c_str());
  EXPECT_TRUE(type1->is_coding_needed);
  ASSERT_EQ(fidl::coded::Type::Kind::kArray, type1->kind);
  auto type1_array = static_cast<const fidl::coded::ArrayType*>(type1);
  EXPECT_EQ(1, type1_array->element_size_v2);
  EXPECT_EQ(type0, type1_array->element_type);

  auto type2 = gen.coded_types().at(2).get();
  EXPECT_STREQ("Array77_13Array7_5uint8", type2->coded_name.c_str());
  EXPECT_TRUE(type2->is_coding_needed);
  ASSERT_EQ(fidl::coded::Type::Kind::kArray, type2->kind);
  auto type2_array = static_cast<const fidl::coded::ArrayType*>(type2);
  EXPECT_EQ(7 * 1, type2_array->element_size_v2);
  EXPECT_EQ(type1, type2_array->element_type);

  auto type3 = gen.coded_types().at(3).get();
  EXPECT_STREQ("Array1001_23Array77_13Array7_5uint8", type3->coded_name.c_str());
  EXPECT_TRUE(type3->is_coding_needed);
  ASSERT_EQ(fidl::coded::Type::Kind::kArray, type3->kind);
  auto type3_array = static_cast<const fidl::coded::ArrayType*>(type3);
  EXPECT_EQ(11 * 7 * 1, type3_array->element_size_v2);
  EXPECT_EQ(type2, type3_array->element_type);
}

TEST(CodedTypesGeneratorTests, GoodCodedTypesOfVectors) {
  TestLibrary library(R"FIDL(library example;

type SomeStruct = struct {};

type Vectors = struct {
    bytes1 vector<SomeStruct>:10;
    bytes12 vector<vector<SomeStruct>:10>:20;
};
)FIDL");
  ASSERT_COMPILED(library);
  fidl::CodedTypesGenerator gen(library.compilation());
  gen.CompileCodedTypes();

  auto name_some_struct = fidl::flat::Name::Key(library.LookupLibrary("example"), "SomeStruct");
  auto type_some_struct = gen.CodedTypeFor(name_some_struct);
  ASSERT_NOT_NULL(type_some_struct);
  EXPECT_STREQ("example_SomeStruct", type_some_struct->coded_name.c_str());
  EXPECT_TRUE(type_some_struct->is_coding_needed);
  ASSERT_EQ(fidl::coded::Type::Kind::kStruct, type_some_struct->kind);
  auto type_some_struct_struct = static_cast<const fidl::coded::StructType*>(type_some_struct);
  ASSERT_TRUE(type_some_struct_struct->is_empty);
  ASSERT_EQ(0, type_some_struct_struct->elements.size());
  EXPECT_STREQ("example/SomeStruct", type_some_struct_struct->qname.c_str());
  EXPECT_FALSE(type_some_struct_struct->contains_envelope);
  EXPECT_NULL(type_some_struct_struct->maybe_reference_type);
  EXPECT_EQ(1, type_some_struct_struct->size_v2);

  ASSERT_EQ(2, gen.coded_types().size());

  auto type0 = gen.coded_types().at(0).get();
  EXPECT_STREQ("Vector10nonnullable18example_SomeStruct", type0->coded_name.c_str());
  EXPECT_TRUE(type0->is_coding_needed);
  ASSERT_EQ(fidl::coded::Type::Kind::kVector, type0->kind);
  auto type0_vector = static_cast<const fidl::coded::VectorType*>(type0);
  EXPECT_EQ(type_some_struct, type0_vector->element_type);
  EXPECT_EQ(10, type0_vector->max_count);
  EXPECT_EQ(1, type0_vector->element_size_v2);
  EXPECT_EQ(fidl::types::Nullability::kNonnullable, type0_vector->nullability);
  EXPECT_EQ(fidl::coded::MemcpyCompatibility::kCanMemcpy,
            type0_vector->element_memcpy_compatibility);

  auto type1 = gen.coded_types().at(1).get();
  EXPECT_STREQ("Vector20nonnullable39Vector10nonnullable18example_SomeStruct",
               type1->coded_name.c_str());
  EXPECT_TRUE(type1->is_coding_needed);
  ASSERT_EQ(fidl::coded::Type::Kind::kVector, type1->kind);
  auto type1_vector = static_cast<const fidl::coded::VectorType*>(type1);
  EXPECT_EQ(type0, type1_vector->element_type);
  EXPECT_EQ(20, type1_vector->max_count);
  EXPECT_EQ(16, type1_vector->element_size_v2);
  EXPECT_EQ(fidl::types::Nullability::kNonnullable, type1_vector->nullability);
  EXPECT_EQ(fidl::coded::MemcpyCompatibility::kCannotMemcpy,
            type1_vector->element_memcpy_compatibility);
}

TEST(CodedTypesGeneratorTests, GoodVectorEncodeMightMutate) {
  TestLibrary library(R"FIDL(
library example;

using zx;

type Bits = bits : uint32 {
  A = 1;
};

type Enum = enum : uint32 {
  A = 1;
};

protocol P {};

type EmptyStruct = struct {};

type NeverMutateStruct = struct {
  v1 uint32;
  v2 Bits;
  v3 Enum;
};

type PaddingStruct = struct {
  v1 uint32;
  v2 uint64;
};

type Table = resource table {};
type Union = resource union {
    1: a uint32;
};

type Value = resource struct {
  // The number in the name corresponds to the field index in the assertions below.
  never0 vector<EmptyStruct>;
  never1 vector<NeverMutateStruct>;
  maybe2 vector<box<NeverMutateStruct>>;
  maybe3 vector<PaddingStruct>;
  maybe4 vector<vector<uint32>>;
  maybe5 vector<string>;
  maybe6 vector<zx.handle>;
  maybe7 vector<server_end:P>;
  maybe8 vector<client_end:P>;
  maybe9 vector<Table>;
  maybe10 vector<Union>;
};
)FIDL");
  library.UseLibraryZx();
  ASSERT_COMPILED(library);
  auto str = library.LookupStruct("Value");
  ASSERT_NOT_NULL(str);
  auto elem_might_mutate = [&str](size_t index) {
    const fidl::flat::VectorType* vec =
        static_cast<const fidl::flat::VectorType*>(str->members.at(index).type_ctor->type);
    return fidl::ComputeMemcpyCompatibility(vec->element_type);
  };
  // Note: these EXPECT_EQ are not in a loop so that they give more useful errors.
  EXPECT_EQ(fidl::coded::MemcpyCompatibility::kCanMemcpy, elem_might_mutate(0));
  EXPECT_EQ(fidl::coded::MemcpyCompatibility::kCanMemcpy, elem_might_mutate(1));
  EXPECT_EQ(fidl::coded::MemcpyCompatibility::kCannotMemcpy, elem_might_mutate(2));
  EXPECT_EQ(fidl::coded::MemcpyCompatibility::kCannotMemcpy, elem_might_mutate(3));
  EXPECT_EQ(fidl::coded::MemcpyCompatibility::kCannotMemcpy, elem_might_mutate(4));
  EXPECT_EQ(fidl::coded::MemcpyCompatibility::kCannotMemcpy, elem_might_mutate(5));
  EXPECT_EQ(fidl::coded::MemcpyCompatibility::kCannotMemcpy, elem_might_mutate(6));
  EXPECT_EQ(fidl::coded::MemcpyCompatibility::kCannotMemcpy, elem_might_mutate(7));
  EXPECT_EQ(fidl::coded::MemcpyCompatibility::kCannotMemcpy, elem_might_mutate(8));
  EXPECT_EQ(fidl::coded::MemcpyCompatibility::kCannotMemcpy, elem_might_mutate(9));
  EXPECT_EQ(fidl::coded::MemcpyCompatibility::kCannotMemcpy, elem_might_mutate(10));
}

TEST(CodedTypesGeneratorTests, GoodCodedTypesOfProtocols) {
  TestLibrary library(R"FIDL(library example;

protocol SomeProtocol {};

type OnReceivePayload = resource struct {
    server server_end:SomeProtocol;
};

protocol UseOfProtocol {
    Call(resource struct {
        client client_end:SomeProtocol;
    });
    -> OnReceive(OnReceivePayload);
};
)FIDL");
  ASSERT_COMPILED(library);
  fidl::CodedTypesGenerator gen(library.compilation());
  gen.CompileCodedTypes();

  ASSERT_EQ(3, gen.coded_types().size());

  auto type0 = gen.coded_types().at(0).get();
  EXPECT_STREQ("Protocol20example_SomeProtocolnonnullable", type0->coded_name.c_str());
  EXPECT_TRUE(type0->is_coding_needed);
  EXPECT_EQ(4, type0->size_v2);
  ASSERT_EQ(fidl::coded::Type::Kind::kProtocolHandle, type0->kind);
  auto type0_ihandle = static_cast<const fidl::coded::ProtocolHandleType*>(type0);
  ASSERT_EQ(fidl::types::Nullability::kNonnullable, type0_ihandle->nullability);

  auto type1 = gen.coded_types().at(1).get();
  EXPECT_STREQ("Request20example_SomeProtocolnonnullable", type1->coded_name.c_str());
  EXPECT_TRUE(type1->is_coding_needed);
  EXPECT_EQ(4, type1->size_v2);
  ASSERT_EQ(fidl::coded::Type::Kind::kRequestHandle, type1->kind);
  auto type1_ihandle = static_cast<const fidl::coded::RequestHandleType*>(type1);
  ASSERT_EQ(fidl::types::Nullability::kNonnullable, type1_ihandle->nullability);

  auto type2 = gen.coded_types().at(2).get();
  EXPECT_STREQ("example_UseOfProtocolCallRequestMessage", type2->coded_name.c_str());
  EXPECT_TRUE(type2->is_coding_needed);
  EXPECT_EQ(4, type2->size_v2);
  ASSERT_EQ(fidl::coded::Type::Kind::kStruct, type2->kind);
  auto type2_message = static_cast<const fidl::coded::StructType*>(type2);
  EXPECT_FALSE(type2_message->contains_envelope);
  EXPECT_STREQ("example/UseOfProtocolCallRequestMessage", type2_message->qname.c_str());
  EXPECT_EQ(1, type2_message->elements.size());
  EXPECT_EQ(0, field(type2_message->elements.at(0)).offset_v2);
  EXPECT_EQ(type0, field(type2_message->elements.at(0)).type);

  auto named_payload_name =
      fidl::flat::Name::Key(library.LookupLibrary("example"), "OnReceivePayload");
  auto type_named_payload = gen.CodedTypeFor(named_payload_name);
  ASSERT_NOT_NULL(type_named_payload);
  EXPECT_STREQ("example_OnReceivePayload", type_named_payload->coded_name.c_str());
  EXPECT_TRUE(type_named_payload->is_coding_needed);
  EXPECT_EQ(4, type_named_payload->size_v2);
  ASSERT_EQ(fidl::coded::Type::Kind::kStruct, type_named_payload->kind);
  auto type_named_payload_message = static_cast<const fidl::coded::StructType*>(type_named_payload);
  ASSERT_FALSE(type_named_payload_message->is_empty);
  EXPECT_FALSE(type_named_payload_message->contains_envelope);
  EXPECT_NULL(type_named_payload_message->maybe_reference_type);
  EXPECT_STREQ("example/OnReceivePayload", type_named_payload_message->qname.c_str());
  ASSERT_EQ(1, type_named_payload_message->elements.size());
  EXPECT_EQ(0, field(type_named_payload_message->elements.at(0)).offset_v2);
  EXPECT_EQ(type1, field(type_named_payload_message->elements.at(0)).type);
}

TEST(CodedTypesGeneratorTests, GoodCodedTypesOfProtocolErrorSyntax) {
  TestLibrary library(R"FIDL(library example;

protocol SomeProtocol {};

protocol UseOfProtocol {
    Method() -> (resource struct {
        client client_end:SomeProtocol;
    }) error uint32;
};
)FIDL");
  ASSERT_COMPILED(library);
  fidl::CodedTypesGenerator gen(library.compilation());
  gen.CompileCodedTypes();

  ASSERT_EQ(4, gen.coded_types().size());

  auto type0 = gen.coded_types().at(0).get();
  EXPECT_STREQ("example_UseOfProtocol_Method_ResultNullableRef", type0->coded_name.c_str());
  EXPECT_TRUE(type0->is_coding_needed);
  ASSERT_EQ(fidl::coded::Type::Kind::kXUnion, type0->kind);
  auto type0_union = static_cast<const fidl::coded::XUnionType*>(type0);
  ASSERT_EQ(fidl::types::Nullability::kNullable, type0_union->nullability);
  EXPECT_EQ(16, type0->size_v2);
  ASSERT_EQ(2, type0_union->fields.size());
  auto type0_field0 = type0_union->fields.at(0);
  EXPECT_STREQ("example_UseOfProtocol_Method_Response", type0_field0.type->coded_name.c_str());
  auto type0_field1 = type0_union->fields.at(1);
  EXPECT_STREQ("uint32", type0_field1.type->coded_name.c_str());

  auto type2 = gen.coded_types().at(1).get();
  EXPECT_STREQ("Protocol20example_SomeProtocolnonnullable", type2->coded_name.c_str());
  EXPECT_TRUE(type2->is_coding_needed);
  EXPECT_EQ(4, type2->size_v2);
  ASSERT_EQ(fidl::coded::Type::Kind::kProtocolHandle, type2->kind);
  auto type2_ihandle = static_cast<const fidl::coded::ProtocolHandleType*>(type2);
  ASSERT_EQ(fidl::types::Nullability::kNonnullable, type2_ihandle->nullability);

  auto type3 = gen.coded_types().at(2).get();
  EXPECT_STREQ("uint32", type3->coded_name.c_str());

  auto type5 = gen.coded_types().at(3).get();
  EXPECT_STREQ("example_UseOfProtocolMethodResponseMessage", type5->coded_name.c_str());
  EXPECT_TRUE(type5->is_coding_needed);
  EXPECT_EQ(16, type5->size_v2);
  ASSERT_EQ(fidl::coded::Type::Kind::kStruct, type5->kind);
  auto type5_message = static_cast<const fidl::coded::StructType*>(type5);
  EXPECT_TRUE(type5_message->contains_envelope);
  EXPECT_STREQ("example/UseOfProtocolMethodResponseMessage", type5_message->qname.c_str());
  EXPECT_EQ(1, type5_message->elements.size());

  auto anon_payload_name =
      fidl::flat::Name::Key(library.LookupLibrary("example"), "UseOfProtocol_Method_Response");
  auto type_anon_payload = gen.CodedTypeFor(anon_payload_name);
  ASSERT_NOT_NULL(type_anon_payload);
  EXPECT_STREQ("example_UseOfProtocol_Method_Response", type_anon_payload->coded_name.c_str());
  EXPECT_TRUE(type_anon_payload->is_coding_needed);
  EXPECT_EQ(4, type_anon_payload->size_v2);
  ASSERT_EQ(fidl::coded::Type::Kind::kStruct, type_anon_payload->kind);
  auto type_anon_payload_message = static_cast<const fidl::coded::StructType*>(type_anon_payload);
  ASSERT_FALSE(type_anon_payload_message->is_empty);
  EXPECT_FALSE(type_anon_payload_message->contains_envelope);
  EXPECT_NULL(type_anon_payload_message->maybe_reference_type);
  EXPECT_STREQ("example/UseOfProtocol_Method_Response", type_anon_payload_message->qname.c_str());
  ASSERT_EQ(1, type_anon_payload_message->elements.size());
  EXPECT_EQ(0, field(type_anon_payload_message->elements.at(0)).offset_v2);
  EXPECT_EQ(type2, field(type_anon_payload_message->elements.at(0)).type);
}

TEST(CodedTypesGeneratorTests, GoodCodedTypesGeneratedWrappers) {
  TestLibrary library(R"FIDL(library example;

protocol ErrorSyntaxProtocol {
    ErrorSyntaxMethod() -> (struct{}) error uint32;
};
)FIDL");
  ASSERT_COMPILED(library);
  fidl::CodedTypesGenerator gen(library.compilation());
  gen.CompileCodedTypes();

  ASSERT_EQ(3, gen.coded_types().size());

  auto type0 = gen.coded_types().at(0).get();
  EXPECT_STREQ("example_ErrorSyntaxProtocol_ErrorSyntaxMethod_ResultNullableRef",
               type0->coded_name.c_str());
  EXPECT_EQ(16, type0->size_v2);

  auto type1 = gen.coded_types().at(1).get();
  EXPECT_STREQ("uint32", type1->coded_name.c_str());

  auto type2 = gen.coded_types().at(2).get();
  EXPECT_STREQ("example_ErrorSyntaxProtocolErrorSyntaxMethodResponseMessage",
               type2->coded_name.c_str());
  EXPECT_EQ(16, type2->size_v2);
  auto type2_message = static_cast<const fidl::coded::StructType*>(type2);
  EXPECT_TRUE(type2_message->contains_envelope);
}

TEST(CodedTypesGeneratorTests, GoodCodedTypesOfProtocolEnds) {
  TestLibrary library(R"FIDL(library example;

protocol SomeProtocol {};

protocol UseOfProtocolEnds {
    ClientEnds(resource struct {
        in client_end:SomeProtocol;
    }) -> (resource struct {
        out client_end:<SomeProtocol, optional>;
    });
    ServerEnds(resource struct {
        in server_end:<SomeProtocol, optional>;
    }) -> (resource struct {
        out server_end:SomeProtocol;
    });
};
)FIDL");
  ASSERT_COMPILED(library);
  fidl::CodedTypesGenerator gen(library.compilation());
  gen.CompileCodedTypes();

  ASSERT_EQ(8, gen.coded_types().size());

  // ClientEnd request payload
  auto type0 = gen.coded_types().at(3).get();
  EXPECT_STREQ("Protocol20example_SomeProtocolnonnullable", type0->coded_name.c_str());
  EXPECT_TRUE(type0->is_coding_needed);
  EXPECT_EQ(4, type0->size_v2);
  ASSERT_EQ(fidl::coded::Type::Kind::kProtocolHandle, type0->kind);
  auto type0_ihandle = static_cast<const fidl::coded::ProtocolHandleType*>(type0);
  EXPECT_EQ(fidl::types::Nullability::kNonnullable, type0_ihandle->nullability);

  // ClientEnd request message
  auto type1 = gen.coded_types().at(4).get();
  EXPECT_STREQ("example_UseOfProtocolEndsClientEndsRequestMessage", type1->coded_name.c_str());
  EXPECT_TRUE(type1->is_coding_needed);
  EXPECT_EQ(4, type1->size_v2);
  ASSERT_EQ(fidl::coded::Type::Kind::kStruct, type1->kind);
  auto type1_message = static_cast<const fidl::coded::StructType*>(type1);
  EXPECT_FALSE(type1_message->contains_envelope);
  EXPECT_STREQ("example/UseOfProtocolEndsClientEndsRequestMessage", type1_message->qname.c_str());
  EXPECT_EQ(1, type1_message->elements.size());
  EXPECT_EQ(0, field(type1_message->elements.at(0)).offset_v2);
  EXPECT_EQ(type0, field(type1_message->elements.at(0)).type);

  // ClientEnd response payload
  auto type2 = gen.coded_types().at(2).get();
  EXPECT_STREQ("Protocol20example_SomeProtocolnullable", type2->coded_name.c_str());
  EXPECT_TRUE(type2->is_coding_needed);
  EXPECT_EQ(4, type2->size_v2);
  ASSERT_EQ(fidl::coded::Type::Kind::kProtocolHandle, type2->kind);
  auto type2_ihandle = static_cast<const fidl::coded::ProtocolHandleType*>(type2);
  EXPECT_EQ(fidl::types::Nullability::kNullable, type2_ihandle->nullability);

  // ClientEnd response message
  auto type3 = gen.coded_types().at(5).get();
  EXPECT_STREQ("example_UseOfProtocolEndsClientEndsResponseMessage", type3->coded_name.c_str());
  EXPECT_TRUE(type3->is_coding_needed);
  EXPECT_EQ(4, type3->size_v2);
  ASSERT_EQ(fidl::coded::Type::Kind::kStruct, type3->kind);
  auto type3_message = static_cast<const fidl::coded::StructType*>(type3);
  EXPECT_FALSE(type3_message->contains_envelope);
  EXPECT_STREQ("example/UseOfProtocolEndsClientEndsResponseMessage", type3_message->qname.c_str());
  EXPECT_EQ(1, type3_message->elements.size());
  EXPECT_EQ(0, field(type3_message->elements.at(0)).offset_v2);
  EXPECT_EQ(type2, field(type3_message->elements.at(0)).type);

  // ServerEnd request payload
  auto type4 = gen.coded_types().at(1).get();
  EXPECT_STREQ("Request20example_SomeProtocolnullable", type4->coded_name.c_str());
  EXPECT_TRUE(type4->is_coding_needed);
  EXPECT_EQ(4, type4->size_v2);
  ASSERT_EQ(fidl::coded::Type::Kind::kRequestHandle, type4->kind);
  auto type4_ihandle = static_cast<const fidl::coded::RequestHandleType*>(type4);
  EXPECT_EQ(fidl::types::Nullability::kNullable, type4_ihandle->nullability);

  // ServerEnd request message
  auto type5 = gen.coded_types().at(6).get();
  EXPECT_STREQ("example_UseOfProtocolEndsServerEndsRequestMessage", type5->coded_name.c_str());
  EXPECT_TRUE(type5->is_coding_needed);
  EXPECT_EQ(4, type5->size_v2);
  ASSERT_EQ(fidl::coded::Type::Kind::kStruct, type5->kind);
  auto type5_message = static_cast<const fidl::coded::StructType*>(type5);
  EXPECT_FALSE(type5_message->contains_envelope);
  EXPECT_STREQ("example/UseOfProtocolEndsServerEndsRequestMessage", type5_message->qname.c_str());
  EXPECT_EQ(1, type5_message->elements.size());
  EXPECT_EQ(0, field(type5_message->elements.at(0)).offset_v2);
  EXPECT_EQ(type4, field(type5_message->elements.at(0)).type);

  // ServerEnd response payload
  auto type6 = gen.coded_types().at(0).get();
  EXPECT_STREQ("Request20example_SomeProtocolnonnullable", type6->coded_name.c_str());
  EXPECT_TRUE(type6->is_coding_needed);
  EXPECT_EQ(4, type6->size_v2);
  ASSERT_EQ(fidl::coded::Type::Kind::kRequestHandle, type6->kind);
  auto type6_ihandle = static_cast<const fidl::coded::RequestHandleType*>(type6);
  EXPECT_EQ(fidl::types::Nullability::kNonnullable, type6_ihandle->nullability);

  // ServerEnd response message
  auto type7 = gen.coded_types().at(7).get();
  EXPECT_STREQ("example_UseOfProtocolEndsServerEndsResponseMessage", type7->coded_name.c_str());
  EXPECT_TRUE(type7->is_coding_needed);
  EXPECT_EQ(4, type7->size_v2);
  ASSERT_EQ(fidl::coded::Type::Kind::kStruct, type7->kind);
  auto type7_message = static_cast<const fidl::coded::StructType*>(type7);
  EXPECT_FALSE(type7_message->contains_envelope);
  EXPECT_STREQ("example/UseOfProtocolEndsServerEndsResponseMessage", type7_message->qname.c_str());
  EXPECT_EQ(1, type7_message->elements.size());
  EXPECT_EQ(0, field(type7_message->elements.at(0)).offset_v2);
  EXPECT_EQ(type6, field(type7_message->elements.at(0)).type);
}

// The code between |CodedTypesOfUnions| and |CodedTypesOfNullableUnions| is now very similar
// because the compiler emits both the non-nullable and nullable union types regardless of whether
// it is used in the library in which it was defined.
TEST(CodedTypesGeneratorTests, GoodCodedTypesOfUnions) {
  TestLibrary library(R"FIDL(library example;

type MyXUnion = strict union {
    1: foo bool;
    2: bar int32;
};

type MyXUnionStruct = struct {
  u MyXUnion;
};

)FIDL");
  ASSERT_COMPILED(library);
  fidl::CodedTypesGenerator gen(library.compilation());
  gen.CompileCodedTypes();

  ASSERT_EQ(3, gen.coded_types().size());

  auto type0 = gen.coded_types().at(0).get();
  ASSERT_STREQ("example_MyXUnionNullableRef", type0->coded_name.c_str());
  ASSERT_TRUE(type0->is_coding_needed);
  ASSERT_EQ(fidl::coded::Type::Kind::kXUnion, type0->kind);
  auto nullable_xunion = static_cast<const fidl::coded::XUnionType*>(type0);
  ASSERT_EQ(fidl::types::Nullability::kNullable, nullable_xunion->nullability);

  auto type1 = gen.coded_types().at(1).get();
  ASSERT_STREQ("bool", type1->coded_name.c_str());
  ASSERT_TRUE(type1->is_coding_needed);
  ASSERT_EQ(fidl::coded::Type::Kind::kPrimitive, type1->kind);
  auto type2_primitive = static_cast<const fidl::coded::PrimitiveType*>(type1);
  ASSERT_EQ(fidl::types::PrimitiveSubtype::kBool, type2_primitive->subtype);

  auto type2 = gen.coded_types().at(2).get();
  ASSERT_STREQ("int32", type2->coded_name.c_str());
  ASSERT_TRUE(type2->is_coding_needed);
  ASSERT_EQ(fidl::coded::Type::Kind::kPrimitive, type2->kind);
  auto type1_primitive = static_cast<const fidl::coded::PrimitiveType*>(type2);
  ASSERT_EQ(fidl::types::PrimitiveSubtype::kInt32, type1_primitive->subtype);

  auto name = fidl::flat::Name::Key(library.LookupLibrary("example"), "MyXUnion");
  auto type = gen.CodedTypeFor(name);
  ASSERT_NOT_NULL(type);
  ASSERT_STREQ("example_MyXUnion", type->coded_name.c_str());
  ASSERT_TRUE(type->is_coding_needed);
  ASSERT_EQ(fidl::coded::Type::Kind::kXUnion, type->kind);
  auto coded_xunion = static_cast<const fidl::coded::XUnionType*>(type);
  ASSERT_EQ(2, coded_xunion->fields.size());
  auto xunion_field0 = coded_xunion->fields.at(0);
  ASSERT_EQ(fidl::coded::Type::Kind::kPrimitive, xunion_field0.type->kind);
  auto xunion_field0_primitive = static_cast<const fidl::coded::PrimitiveType*>(xunion_field0.type);
  ASSERT_EQ(fidl::types::PrimitiveSubtype::kBool, xunion_field0_primitive->subtype);
  auto xunion_field1 = coded_xunion->fields.at(1);
  ASSERT_EQ(fidl::coded::Type::Kind::kPrimitive, xunion_field1.type->kind);
  auto xunion_field1_primitive = static_cast<const fidl::coded::PrimitiveType*>(xunion_field1.type);
  ASSERT_EQ(fidl::types::PrimitiveSubtype::kInt32, xunion_field1_primitive->subtype);
  ASSERT_STREQ("example/MyXUnion", coded_xunion->qname.c_str());
  ASSERT_EQ(fidl::types::Nullability::kNonnullable, coded_xunion->nullability);
  ASSERT_NOT_NULL(coded_xunion->maybe_reference_type);

  auto struct_name = fidl::flat::Name::Key(library.LookupLibrary("example"), "MyXUnionStruct");
  auto struct_type = gen.CodedTypeFor(struct_name);
  ASSERT_NOT_NULL(struct_type);
  ASSERT_STREQ("example_MyXUnionStruct", struct_type->coded_name.c_str());
  ASSERT_TRUE(struct_type->is_coding_needed);
  ASSERT_EQ(fidl::coded::Type::Kind::kStruct, struct_type->kind);
  auto struct_type_struct = static_cast<const fidl::coded::StructType*>(struct_type);
  ASSERT_FALSE(struct_type_struct->is_empty);
  EXPECT_TRUE(struct_type_struct->contains_envelope);
}

// The code between |CodedTypesOfUnions| and |CodedTypesOfNullableUnions| is now very similar
// because the compiler emits both the non-nullable and nullable union types regardless of whether
// it is used in the library in which it was defined.
TEST(CodedTypesGeneratorTests, GoodCodedTypesOfNullableUnions) {
  TestLibrary library(R"FIDL(library example;

type MyXUnion = strict union {
    1: foo bool;
    2: bar int32;
};

type Wrapper1 = struct {
    xu MyXUnion:optional;
};

// This ensures that MyXUnion? doesn't show up twice in the coded types.
type Wrapper2 = struct {
    xu MyXUnion:optional;
};
)FIDL");
  ASSERT_COMPILED(library);
  fidl::CodedTypesGenerator gen(library.compilation());
  gen.CompileCodedTypes();

  // 3 == size of {bool, int32, MyXUnion?}, which is all of the types used in
  // the example.
  ASSERT_EQ(3, gen.coded_types().size());

  auto type0 = gen.coded_types().at(0).get();
  ASSERT_STREQ("example_MyXUnionNullableRef", type0->coded_name.c_str());
  ASSERT_TRUE(type0->is_coding_needed);
  ASSERT_EQ(fidl::coded::Type::Kind::kXUnion, type0->kind);
  auto nullable_xunion = static_cast<const fidl::coded::XUnionType*>(type0);
  ASSERT_EQ(fidl::types::Nullability::kNullable, nullable_xunion->nullability);

  auto type1 = gen.coded_types().at(1).get();
  ASSERT_STREQ("bool", type1->coded_name.c_str());
  ASSERT_TRUE(type1->is_coding_needed);
  ASSERT_EQ(fidl::coded::Type::Kind::kPrimitive, type1->kind);
  auto type2_primitive = static_cast<const fidl::coded::PrimitiveType*>(type1);
  ASSERT_EQ(fidl::types::PrimitiveSubtype::kBool, type2_primitive->subtype);

  auto type2 = gen.coded_types().at(2).get();
  ASSERT_STREQ("int32", type2->coded_name.c_str());
  ASSERT_TRUE(type2->is_coding_needed);
  ASSERT_EQ(fidl::coded::Type::Kind::kPrimitive, type2->kind);
  auto type1_primitive = static_cast<const fidl::coded::PrimitiveType*>(type2);
  ASSERT_EQ(fidl::types::PrimitiveSubtype::kInt32, type1_primitive->subtype);
}

// This mostly exists to make sure that the same nullable objects aren't
// represented more than once in the coding tables.
TEST(CodedTypesGeneratorTests, GoodCodedTypesOfNullablePointers) {
  TestLibrary library(R"FIDL(library example;

type MyStruct = struct {
    foo bool;
    bar int32;
};

type MyUnion = strict union {
    1: foo bool;
    2: bar int32;
};

type MyXUnion = flexible union {
    1: foo bool;
    2: bar int32;
};

type Wrapper1 = struct {
    ms box<MyStruct>;
    mu MyUnion:optional;
    xu MyXUnion:optional;
};

// This ensures that MyXUnion? doesn't show up twice in the coded types.
type Wrapper2 = struct {
    ms box<MyStruct>;
    mu MyUnion:optional;
    xu MyXUnion:optional;
};
)FIDL");
  ASSERT_COMPILED(library);
  fidl::CodedTypesGenerator gen(library.compilation());
  gen.CompileCodedTypes();

  // 5 == size of {bool, int32, MyStruct?, MyUnion?, MyXUnion?},
  // which are all the coded types in the example.
  ASSERT_EQ(5, gen.coded_types().size());
}

TEST(CodedTypesGeneratorTests, GoodCodedHandle) {
  TestLibrary library(R"FIDL(library example;

type obj_type = strict enum : uint32 {
    NONE = 0;
    VMO = 3;
};

type rights = strict bits {
    SOME_RIGHT = 1;
};

resource_definition handle : uint32 {
    properties {
        subtype obj_type;
        rights rights;
    };
};

type MyStruct = resource struct {
    h handle:<VMO, rights.SOME_RIGHT>;
};
)FIDL");

  ASSERT_COMPILED(library);
  fidl::CodedTypesGenerator gen(library.compilation());
  gen.CompileCodedTypes();

  auto struct_name = fidl::flat::Name::Key(library.LookupLibrary("example"), "MyStruct");
  auto struct_type = static_cast<const fidl::coded::StructType*>(gen.CodedTypeFor(struct_name));
  auto handle_type =
      static_cast<const fidl::coded::HandleType*>(field(struct_type->elements[0]).type);

  ASSERT_EQ(fidl::types::HandleSubtype::kVmo, handle_type->subtype);
  ASSERT_EQ(1, handle_type->rights);
  ASSERT_EQ(fidl::types::Nullability::kNonnullable, handle_type->nullability);
}

TEST(CodedTypesGeneratorTests, GoodCodedTypesOfStructsWithPaddings) {
  TestLibrary library(R"FIDL(library example;

type BoolAndInt32 = struct {
    foo bool;
    // 3 bytes of padding here.
    bar int32;
};

type Complex = struct {
    i32 int32;
    b1 bool;
    // 3 bytes of padding here.
    i64 int64;
    i16 int16;
// 6 bytes of padding here.
};
)FIDL");
  ASSERT_COMPILED(library);
  fidl::CodedTypesGenerator gen(library.compilation());
  gen.CompileCodedTypes();

  ASSERT_EQ(4, gen.coded_types().size());

  auto type0 = gen.coded_types().at(0).get();
  EXPECT_STREQ("int32", type0->coded_name.c_str());
  EXPECT_TRUE(type0->is_coding_needed);
  auto type1 = gen.coded_types().at(1).get();
  EXPECT_STREQ("bool", type1->coded_name.c_str());
  EXPECT_TRUE(type1->is_coding_needed);
  auto type2 = gen.coded_types().at(2).get();
  EXPECT_STREQ("int64", type2->coded_name.c_str());
  EXPECT_TRUE(type2->is_coding_needed);
  auto type3 = gen.coded_types().at(3).get();
  EXPECT_STREQ("int16", type3->coded_name.c_str());
  EXPECT_TRUE(type3->is_coding_needed);

  auto name_bool_and_int32 =
      fidl::flat::Name::Key(library.LookupLibrary("example"), "BoolAndInt32");
  auto type_bool_and_int32 = gen.CodedTypeFor(name_bool_and_int32);
  ASSERT_NOT_NULL(type_bool_and_int32);
  EXPECT_STREQ("example_BoolAndInt32", type_bool_and_int32->coded_name.c_str());
  auto type_bool_and_int32_struct =
      static_cast<const fidl::coded::StructType*>(type_bool_and_int32);
  ASSERT_FALSE(type_bool_and_int32_struct->is_empty);
  ASSERT_EQ(type_bool_and_int32_struct->elements.size(), 2);
  EXPECT_EQ(field(type_bool_and_int32_struct->elements[0]).type->kind,
            fidl::coded::Type::Kind::kPrimitive);
  EXPECT_EQ(field(type_bool_and_int32_struct->elements[0]).offset_v2, 0);
  EXPECT_EQ(padding(type_bool_and_int32_struct->elements[1]).offset_v2, 0);
  EXPECT_EQ(std::get<uint32_t>(padding(type_bool_and_int32_struct->elements[1]).mask), 0xffffff00);

  auto name_complex = fidl::flat::Name::Key(library.LookupLibrary("example"), "Complex");
  auto type_complex = gen.CodedTypeFor(name_complex);
  ASSERT_NOT_NULL(type_complex);
  EXPECT_STREQ("example_Complex", type_complex->coded_name.c_str());
  auto type_complex_struct = static_cast<const fidl::coded::StructType*>(type_complex);
  ASSERT_FALSE(type_complex_struct->is_empty);
  ASSERT_EQ(type_complex_struct->elements.size(), 3);
  EXPECT_EQ(field(type_complex_struct->elements[0]).type->kind,
            fidl::coded::Type::Kind::kPrimitive);
  EXPECT_EQ(field(type_complex_struct->elements[0]).offset_v2, 4);
  EXPECT_EQ(padding(type_complex_struct->elements[1]).offset_v2, 4);
  EXPECT_EQ(std::get<uint32_t>(padding(type_complex_struct->elements[1]).mask), 0xffffff00);
  EXPECT_EQ(padding(type_complex_struct->elements[2]).offset_v2, 16);
  EXPECT_EQ(std::get<uint64_t>(padding(type_complex_struct->elements[2]).mask),
            0xffffffffffff0000ull);
}

TEST(CodedTypesGeneratorTests, GoodCodedTypesOfMultilevelNestedStructs) {
  TestLibrary library(R"FIDL(library example;

// alignment 4
type Level0 = struct {
    a int8;
    //padding 3
    b int32;
    c int8;
// padding 3;
};

// alignment 8
type Level1 = struct {
    l0 Level0;
    // 4 bytes padding + 3 inside of Level0.
    d uint64;
};

// alignment 8
type Level2 = struct {
    l1 Level1;
    e uint8;
// 7 bytes of padding.
};
)FIDL");
  ASSERT_COMPILED(library);
  fidl::CodedTypesGenerator gen(library.compilation());
  gen.CompileCodedTypes();

  auto name_level0 = fidl::flat::Name::Key(library.LookupLibrary("example"), "Level0");
  auto type_level0 = gen.CodedTypeFor(name_level0);
  ASSERT_NOT_NULL(type_level0);
  auto struct_level0 = static_cast<const fidl::coded::StructType*>(type_level0);
  ASSERT_FALSE(struct_level0->is_empty);
  ASSERT_EQ(struct_level0->elements.size(), 2);
  EXPECT_EQ(padding(struct_level0->elements[0]).offset_v2, 0);
  EXPECT_EQ(std::get<uint32_t>(padding(struct_level0->elements[0]).mask), 0xffffff00);
  EXPECT_EQ(padding(struct_level0->elements[1]).offset_v2, 8);
  EXPECT_EQ(std::get<uint32_t>(padding(struct_level0->elements[1]).mask), 0xffffff00);

  auto name_level1 = fidl::flat::Name::Key(library.LookupLibrary("example"), "Level1");
  auto type_level1 = gen.CodedTypeFor(name_level1);
  ASSERT_NOT_NULL(type_level1);
  auto struct_level1 = static_cast<const fidl::coded::StructType*>(type_level1);
  ASSERT_FALSE(struct_level1->is_empty);
  ASSERT_EQ(struct_level1->elements.size(), 2);
  EXPECT_EQ(padding(struct_level1->elements[0]).offset_v2, 0);
  EXPECT_EQ(std::get<uint32_t>(padding(struct_level1->elements[0]).mask), 0xffffff00);
  EXPECT_EQ(padding(struct_level1->elements[1]).offset_v2, 8);
  EXPECT_EQ(std::get<uint64_t>(padding(struct_level1->elements[1]).mask), 0xffffffffffffff00);

  auto name_level2 = fidl::flat::Name::Key(library.LookupLibrary("example"), "Level2");
  auto type_level2 = gen.CodedTypeFor(name_level2);
  ASSERT_NOT_NULL(type_level2);
  auto struct_level2 = static_cast<const fidl::coded::StructType*>(type_level2);
  ASSERT_FALSE(struct_level2->is_empty);
  ASSERT_EQ(struct_level2->elements.size(), 3);
  EXPECT_EQ(padding(struct_level2->elements[0]).offset_v2, 0);
  EXPECT_EQ(std::get<uint32_t>(padding(struct_level2->elements[0]).mask), 0xffffff00);
  EXPECT_EQ(padding(struct_level2->elements[1]).offset_v2, 8);
  EXPECT_EQ(std::get<uint64_t>(padding(struct_level2->elements[1]).mask), 0xffffffffffffff00);
  EXPECT_EQ(padding(struct_level2->elements[2]).offset_v2, 24);
  EXPECT_EQ(std::get<uint64_t>(padding(struct_level2->elements[2]).mask), 0xffffffffffffff00);
}

TEST(CodedTypesGeneratorTests, GoodCodedTypesOfRecursiveOptionalStructs) {
  TestLibrary library(R"FIDL(library example;

type OneLevelRecursiveOptionalStruct = struct {
    val box<OneLevelRecursiveOptionalStruct>;
};

type TwoLevelRecursiveOptionalStructA = struct {
    b TwoLevelRecursiveOptionalStructB;
};

type TwoLevelRecursiveOptionalStructB = struct {
    a box<TwoLevelRecursiveOptionalStructA>;
};
)FIDL");
  ASSERT_COMPILED(library);
  fidl::CodedTypesGenerator gen(library.compilation());
  gen.CompileCodedTypes();

  auto name_one_level =
      fidl::flat::Name::Key(library.LookupLibrary("example"), "OneLevelRecursiveOptionalStruct");
  auto type_one_level = gen.CodedTypeFor(name_one_level);
  ASSERT_NOT_NULL(type_one_level);
  auto struct_one_level = static_cast<const fidl::coded::StructType*>(type_one_level);
  ASSERT_FALSE(struct_one_level->is_empty);
  ASSERT_EQ(struct_one_level->elements.size(), 1);
  EXPECT_EQ(field(struct_one_level->elements[0]).type->kind,
            fidl::coded::Type::Kind::kStructPointer);
  ASSERT_SUBSTR(field(struct_one_level->elements[0]).type->coded_name.c_str(),
                "OneLevelRecursiveOptionalStruct");
  EXPECT_EQ(field(struct_one_level->elements[0]).offset_v2, 0);

  auto name_two_level_b =
      fidl::flat::Name::Key(library.LookupLibrary("example"), "TwoLevelRecursiveOptionalStructB");
  auto type_two_level_b = gen.CodedTypeFor(name_two_level_b);
  ASSERT_NOT_NULL(type_two_level_b);
  auto struct_two_level_b = static_cast<const fidl::coded::StructType*>(type_two_level_b);
  ASSERT_FALSE(struct_two_level_b->is_empty);
  ASSERT_EQ(struct_two_level_b->elements.size(), 1);
  EXPECT_EQ(field(struct_two_level_b->elements[0]).type->kind,
            fidl::coded::Type::Kind::kStructPointer);
  ASSERT_SUBSTR(field(struct_two_level_b->elements[0]).type->coded_name.c_str(),
                "TwoLevelRecursiveOptionalStructA");
  EXPECT_EQ(field(struct_two_level_b->elements[0]).offset_v2, 0);

  // TwoLevelRecursiveOptionalStructA will be equivalent to TwoLevelRecursiveOptionalStructB
  // because of flattening.
  auto name_two_level_a =
      fidl::flat::Name::Key(library.LookupLibrary("example"), "TwoLevelRecursiveOptionalStructA");
  auto type_two_level_a = gen.CodedTypeFor(name_two_level_a);
  ASSERT_NOT_NULL(type_two_level_a);
  auto struct_two_level_a = static_cast<const fidl::coded::StructType*>(type_two_level_a);
  ASSERT_FALSE(struct_two_level_a->is_empty);
  ASSERT_EQ(struct_two_level_a->elements.size(), 1);
  EXPECT_EQ(field(struct_two_level_a->elements[0]).type->kind,
            fidl::coded::Type::Kind::kStructPointer);
  ASSERT_SUBSTR(field(struct_two_level_a->elements[0]).type->coded_name.c_str(),
                "TwoLevelRecursiveOptionalStructA");
  EXPECT_EQ(field(struct_two_level_a->elements[0]).offset_v2, 0);
}

TEST(CodedTypesGeneratorTests, GoodCodedTypesOfReusedStructs) {
  TestLibrary library(R"FIDL(library example;

// InnerStruct is reused and appears twice.
type InnerStruct = struct{
    a int8;
    // 1 byte padding
    b int16;
};

type OuterStruct = struct {
    a InnerStruct;
    b InnerStruct;
};
)FIDL");
  ASSERT_COMPILED(library);
  fidl::CodedTypesGenerator gen(library.compilation());
  gen.CompileCodedTypes();

  auto name_inner_struct = fidl::flat::Name::Key(library.LookupLibrary("example"), "InnerStruct");
  auto type_inner_struct = gen.CodedTypeFor(name_inner_struct);
  ASSERT_NOT_NULL(type_inner_struct);
  auto struct_inner_struct = static_cast<const fidl::coded::StructType*>(type_inner_struct);
  ASSERT_FALSE(struct_inner_struct->is_empty);
  ASSERT_EQ(struct_inner_struct->elements.size(), 1);
  EXPECT_EQ(padding(struct_inner_struct->elements[0]).offset_v2, 0);
  ASSERT_TRUE(std::get<uint16_t>(padding(struct_inner_struct->elements[0]).mask));
  EXPECT_EQ(std::get<uint16_t>(padding(struct_inner_struct->elements[0]).mask), 0xff00);

  auto name_outer_struct = fidl::flat::Name::Key(library.LookupLibrary("example"), "OuterStruct");
  auto type_outer_struct = gen.CodedTypeFor(name_outer_struct);
  ASSERT_NOT_NULL(type_outer_struct);
  auto struct_outer_struct = static_cast<const fidl::coded::StructType*>(type_outer_struct);
  ASSERT_FALSE(struct_outer_struct->is_empty);
  ASSERT_EQ(struct_outer_struct->elements.size(), 2);
  EXPECT_EQ(padding(struct_outer_struct->elements[0]).offset_v2, 0);
  ASSERT_TRUE(std::get<uint16_t>(padding(struct_outer_struct->elements[0]).mask));
  EXPECT_EQ(std::get<uint16_t>(padding(struct_outer_struct->elements[0]).mask), 0xff00);
  EXPECT_EQ(padding(struct_outer_struct->elements[1]).offset_v2, 4);
  ASSERT_TRUE(std::get<uint16_t>(padding(struct_outer_struct->elements[1]).mask));
  EXPECT_EQ(std::get<uint16_t>(padding(struct_outer_struct->elements[1]).mask), 0xff00);
}

TEST(CodedTypesGeneratorTests, GoodCodedTypesOfOptionals) {
  TestLibrary library(R"FIDL(
library example;
using zx;

type InnerStruct = struct {
  a int8;
  // 1 byte padding
  b int16;
};

type SimpleUnion = union {
    1: a int64;
};

type OuterStruct = resource struct {
  a InnerStruct;
  opt_handle zx.handle:optional;
  opt_union SimpleUnion:optional;
  b InnerStruct;
};

)FIDL");
  library.UseLibraryZx();
  ASSERT_COMPILED(library);
  fidl::CodedTypesGenerator gen(library.compilation());
  gen.CompileCodedTypes();

  auto name_outer_struct = fidl::flat::Name::Key(library.LookupLibrary("example"), "OuterStruct");
  auto type_outer_struct = gen.CodedTypeFor(name_outer_struct);
  ASSERT_NOT_NULL(type_outer_struct);
  auto struct_outer_struct = static_cast<const fidl::coded::StructType*>(type_outer_struct);
  ASSERT_FALSE(struct_outer_struct->is_empty);
  ASSERT_EQ(struct_outer_struct->elements.size(), 5);
  EXPECT_EQ(padding(struct_outer_struct->elements[0]).offset_v2, 0);
  EXPECT_EQ(std::get<uint16_t>(padding(struct_outer_struct->elements[0]).mask), 0xff00);
  EXPECT_EQ(field(struct_outer_struct->elements[1]).type->kind, fidl::coded::Type::Kind::kHandle);
  EXPECT_EQ(field(struct_outer_struct->elements[1]).offset_v2, 4);
  EXPECT_EQ(field(struct_outer_struct->elements[2]).type->kind, fidl::coded::Type::Kind::kXUnion);
  EXPECT_EQ(field(struct_outer_struct->elements[2]).offset_v2, 8);
  EXPECT_EQ(padding(struct_outer_struct->elements[3]).offset_v2, 24);
  EXPECT_EQ(std::get<uint16_t>(padding(struct_outer_struct->elements[3]).mask), 0xff00);
  EXPECT_EQ(padding(struct_outer_struct->elements[4]).offset_v2, 28);
  EXPECT_EQ(std::get<uint32_t>(padding(struct_outer_struct->elements[4]).mask), 0xffffffff);
}

// In the following example, we shadow the builtin `byte` alias to a struct.
// fidlc previous had a scoping bug where the `f1` field's `byte` type referred
// to the builtin rather than the struct. This has since been fixed. Here we
// test that the coding tables take the same interpretation, i.e. that they do
// not do their own lookups with different scoping rules.
TEST(CodedTypesGeneratorTests, GoodCodingTablesMatchScoping) {
  TestLibrary library(R"FIDL(library example;

alias membertype = uint32;

type byte = struct {
    @allow_deprecated_struct_defaults
    member membertype = 1;
};

type container = struct {
    f1 byte;
    f2 vector<uint8>;
};
)FIDL");
  ASSERT_COMPILED(library);
  fidl::CodedTypesGenerator gen(library.compilation());
  gen.CompileCodedTypes();

  auto the_struct_name = fidl::flat::Name::Key(library.LookupLibrary("example"), "container");
  auto the_coded_type = gen.CodedTypeFor(the_struct_name);
  ASSERT_NOT_NULL(the_coded_type);
  auto the_struct_coded_type = static_cast<const fidl::coded::StructType*>(the_coded_type);
  ASSERT_FALSE(the_struct_coded_type->is_empty);
  ASSERT_EQ(the_struct_coded_type->elements.size(), 2);
  EXPECT_EQ(0xffffffff, std::get<uint32_t>(padding(the_struct_coded_type->elements[0]).mask));
  EXPECT_EQ(fidl::coded::Type::Kind::kVector, field(the_struct_coded_type->elements[1]).type->kind);
}

TEST(CodedTypesGeneratorTests, GoodCodedTypesOfTables) {
  TestLibrary library(R"FIDL(library example;

type MyTable = table {
    1: foo bool;
    2: bar int32;
    3: baz array<bool, 42>;
};
)FIDL");
  ASSERT_COMPILED(library);
  fidl::CodedTypesGenerator gen(library.compilation());
  gen.CompileCodedTypes();

  ASSERT_EQ(3, gen.coded_types().size());

  // This bool is used in the coding table of the MyTable table.
  auto type0 = gen.coded_types().at(0).get();
  EXPECT_STREQ("bool", type0->coded_name.c_str());
  EXPECT_TRUE(type0->is_coding_needed);
  ASSERT_EQ(fidl::coded::Type::Kind::kPrimitive, type0->kind);
  auto type0_primitive = static_cast<const fidl::coded::PrimitiveType*>(type0);
  EXPECT_EQ(fidl::types::PrimitiveSubtype::kBool, type0_primitive->subtype);

  auto type1 = gen.coded_types().at(1).get();
  EXPECT_STREQ("int32", type1->coded_name.c_str());
  EXPECT_TRUE(type1->is_coding_needed);
  ASSERT_EQ(fidl::coded::Type::Kind::kPrimitive, type1->kind);
  auto type1_primitive = static_cast<const fidl::coded::PrimitiveType*>(type1);
  EXPECT_EQ(fidl::types::PrimitiveSubtype::kInt32, type1_primitive->subtype);

  auto type3 = gen.coded_types().at(2).get();
  EXPECT_STREQ("Array42_4bool", type3->coded_name.c_str());
  EXPECT_TRUE(type3->is_coding_needed);
  ASSERT_EQ(fidl::coded::Type::Kind::kArray, type3->kind);
  auto type3_array = static_cast<const fidl::coded::ArrayType*>(type3);
  EXPECT_EQ(42, type3_array->size_v2);
  ASSERT_EQ(fidl::coded::Type::Kind::kPrimitive, type3_array->element_type->kind);
  auto type3_array_element_type =
      static_cast<const fidl::coded::PrimitiveType*>(type3_array->element_type);
  EXPECT_EQ(fidl::types::PrimitiveSubtype::kBool, type3_array_element_type->subtype);

  auto name_table = fidl::flat::Name::Key(library.LookupLibrary("example"), "MyTable");
  auto type_table = gen.CodedTypeFor(name_table);
  ASSERT_NOT_NULL(type_table);
  EXPECT_STREQ("example_MyTable", type_table->coded_name.c_str());
  EXPECT_TRUE(type_table->is_coding_needed);
  ASSERT_EQ(fidl::coded::Type::Kind::kTable, type_table->kind);
  auto type_table_table = static_cast<const fidl::coded::TableType*>(type_table);
  EXPECT_EQ(3, type_table_table->fields.size());
  auto table_field0 = type_table_table->fields.at(0);
  ASSERT_EQ(fidl::coded::Type::Kind::kPrimitive, table_field0.type->kind);
  auto table_field0_primitive = static_cast<const fidl::coded::PrimitiveType*>(table_field0.type);
  ASSERT_EQ(fidl::types::PrimitiveSubtype::kBool, table_field0_primitive->subtype);
  auto table_field1 = type_table_table->fields.at(1);
  ASSERT_EQ(fidl::coded::Type::Kind::kPrimitive, table_field1.type->kind);
  auto table_field1_primitive = static_cast<const fidl::coded::PrimitiveType*>(table_field1.type);
  ASSERT_EQ(fidl::types::PrimitiveSubtype::kInt32, table_field1_primitive->subtype);
  auto table_field2 = type_table_table->fields.at(2);
  ASSERT_EQ(fidl::coded::Type::Kind::kArray, table_field2.type->kind);
  EXPECT_STREQ("example/MyTable", type_table_table->qname.c_str());
}

TEST(CodedTypesGeneratorTests, GoodCodedTypesOfBits) {
  TestLibrary library(R"FIDL(library example;

type StrictBits = strict bits : uint8 {
    HELLO = 0x1;
    WORLD = 0x10;
};

type FlexibleBits = flexible bits : uint8 {
    HELLO = 0x1;
    WORLD = 0x10;
};
)FIDL");
  ASSERT_COMPILED(library);
  fidl::CodedTypesGenerator gen(library.compilation());
  gen.CompileCodedTypes();

  ASSERT_EQ(0, gen.coded_types().size());
  {
    auto name_bits = fidl::flat::Name::Key(library.LookupLibrary("example"), "StrictBits");
    auto type_bits = gen.CodedTypeFor(name_bits);
    ASSERT_NOT_NULL(type_bits);
    EXPECT_STREQ("example_StrictBits", type_bits->coded_name.c_str());
    EXPECT_TRUE(type_bits->is_coding_needed);
    ASSERT_EQ(fidl::coded::Type::Kind::kBits, type_bits->kind);
    auto type_bits_bits = static_cast<const fidl::coded::BitsType*>(type_bits);
    EXPECT_EQ(fidl::types::PrimitiveSubtype::kUint8, type_bits_bits->subtype);
    EXPECT_EQ(fidl::types::Strictness::kStrict, type_bits_bits->strictness);
    EXPECT_EQ(0x1u | 0x10u, type_bits_bits->mask);
  }
  {
    auto name_bits = fidl::flat::Name::Key(library.LookupLibrary("example"), "FlexibleBits");
    auto type_bits = gen.CodedTypeFor(name_bits);
    ASSERT_NOT_NULL(type_bits);
    EXPECT_STREQ("example_FlexibleBits", type_bits->coded_name.c_str());
    EXPECT_TRUE(type_bits->is_coding_needed);
    ASSERT_EQ(fidl::coded::Type::Kind::kBits, type_bits->kind);
    auto type_bits_bits = static_cast<const fidl::coded::BitsType*>(type_bits);
    EXPECT_EQ(fidl::types::PrimitiveSubtype::kUint8, type_bits_bits->subtype);
    EXPECT_EQ(fidl::types::Strictness::kFlexible, type_bits_bits->strictness);
    EXPECT_EQ(0x1u | 0x10u, type_bits_bits->mask);
  }
}

TEST(CodedTypesGeneratorTests, GoodCodedTypesOfStrictEnum) {
  TestLibrary library(R"FIDL(library example;

type StrictEnum = strict enum : uint16 {
    HELLO = 0x1;
    WORLD = 0x10;
};

type FlexibleEnum = flexible enum : uint16 {
    HELLO = 0x1;
    WORLD = 0x10;
};
)FIDL");
  ASSERT_COMPILED(library);
  fidl::CodedTypesGenerator gen(library.compilation());
  gen.CompileCodedTypes();

  ASSERT_EQ(0, gen.coded_types().size());
  {
    auto name_enum = fidl::flat::Name::Key(library.LookupLibrary("example"), "StrictEnum");
    auto type_enum = gen.CodedTypeFor(name_enum);
    ASSERT_NOT_NULL(type_enum);
    EXPECT_STREQ("example_StrictEnum", type_enum->coded_name.c_str());
    EXPECT_TRUE(type_enum->is_coding_needed);

    ASSERT_EQ(fidl::coded::Type::Kind::kEnum, type_enum->kind);
    auto type_enum_enum = static_cast<const fidl::coded::EnumType*>(type_enum);
    EXPECT_EQ(fidl::types::PrimitiveSubtype::kUint16, type_enum_enum->subtype);
    EXPECT_EQ(fidl::types::Strictness::kStrict, type_enum_enum->strictness);
    EXPECT_EQ(2, type_enum_enum->members.size());
    EXPECT_EQ(0x1, type_enum_enum->members[0]);
    EXPECT_EQ(0x10, type_enum_enum->members[1]);
  }
  {
    auto name_enum = fidl::flat::Name::Key(library.LookupLibrary("example"), "FlexibleEnum");
    auto type_enum = gen.CodedTypeFor(name_enum);
    ASSERT_NOT_NULL(type_enum);
    EXPECT_STREQ("example_FlexibleEnum", type_enum->coded_name.c_str());
    EXPECT_TRUE(type_enum->is_coding_needed);

    ASSERT_EQ(fidl::coded::Type::Kind::kEnum, type_enum->kind);
    auto type_enum_enum = static_cast<const fidl::coded::EnumType*>(type_enum);
    EXPECT_EQ(fidl::types::PrimitiveSubtype::kUint16, type_enum_enum->subtype);
    EXPECT_EQ(fidl::types::Strictness::kFlexible, type_enum_enum->strictness);
  }
}

TEST(CodedTypesGeneratorTests, GoodCodedTypesOfUnionsWithReverseOrdinals) {
  TestLibrary library(R"FIDL(library example;

type First = struct {};
type Second = struct {};

type MyUnion = strict union {
    3: second Second;
    2: reserved;
    1: first First;
};
)FIDL");
  ASSERT_COMPILED(library);
  fidl::CodedTypesGenerator gen(library.compilation());
  gen.CompileCodedTypes();

  auto name = fidl::flat::Name::Key(library.LookupLibrary("example"), "MyUnion");
  auto type = gen.CodedTypeFor(name);
  ASSERT_NOT_NULL(type);
  EXPECT_STREQ("example_MyUnion", type->coded_name.c_str());
  EXPECT_TRUE(type->is_coding_needed);
  ASSERT_EQ(fidl::coded::Type::Kind::kXUnion, type->kind);

  auto coded_union = static_cast<const fidl::coded::XUnionType*>(type);
  ASSERT_EQ(3, coded_union->fields.size());

  auto union_field0 = coded_union->fields.at(0);
  ASSERT_NOT_NULL(union_field0.type);
  auto union_field0_struct = static_cast<const fidl::coded::StructType*>(union_field0.type);
  ASSERT_TRUE(union_field0_struct->is_empty);
  EXPECT_STREQ("example/First", union_field0_struct->qname.c_str());

  auto union_field1 = coded_union->fields.at(1);
  ASSERT_NULL(union_field1.type);

  auto union_field2 = coded_union->fields.at(2);
  ASSERT_NOT_NULL(union_field2.type);
  auto union_field2_struct = static_cast<const fidl::coded::StructType*>(union_field2.type);
  ASSERT_TRUE(union_field2_struct->is_empty);
  EXPECT_STREQ("example/Second", union_field2_struct->qname.c_str());
}

void check_duplicate_coded_type_names(const fidl::CodedTypesGenerator& gen) {
  const auto types = gen.AllCodedTypes();
  for (auto const& type : types) {
    auto count = std::count_if(types.begin(), types.end(),
                               [&](auto& t) { return t->coded_name == type->coded_name; });
    ASSERT_EQ(count, 1, "Duplicate coded type name.");
  }
}

TEST(CodedTypesGeneratorTests, GoodDuplicateCodedTypesTwoUnions) {
  TestLibrary library(R"FIDL(library example;

type U1 = strict union {
    1: hs array<string, 2>;
};

type U2 = strict union {
    1: hss array<array<string, 2>, 2>;
};
)FIDL");
  ASSERT_COMPILED(library);
  fidl::CodedTypesGenerator gen(library.compilation());
  gen.CompileCodedTypes();
  check_duplicate_coded_type_names(gen);
}

TEST(CodedTypesGeneratorTests, GoodDuplicateCodedTypesUnionArrayArray) {
  TestLibrary library(R"FIDL(library example;

type Union = strict union {
    1: hs array<string, 2>;
    2: hss array<array<string, 2>, 2>;
};
)FIDL");
  ASSERT_COMPILED(library);
  fidl::CodedTypesGenerator gen(library.compilation());
  gen.CompileCodedTypes();
  check_duplicate_coded_type_names(gen);
}

TEST(CodedTypesGeneratorTests, GoodDuplicateCodedTypesUnionVectorArray) {
  TestLibrary library(R"FIDL(library example;

type Union = strict union {
    1: hs array<string, 2>;
    2: hss vector<array<string, 2>>:2;
};
)FIDL");
  ASSERT_COMPILED(library);
  fidl::CodedTypesGenerator gen(library.compilation());
  gen.CompileCodedTypes();
  check_duplicate_coded_type_names(gen);
}

TEST(CodedTypesGeneratorTests, GoodDuplicateCodedTypesTableArrayArray) {
  TestLibrary library(R"FIDL(library example;

type Table = table {
    1: hs array<string, 2>;
    2: hss array<array<string, 2>, 2>;
};
)FIDL");
  ASSERT_COMPILED(library);
  fidl::CodedTypesGenerator gen(library.compilation());
  gen.CompileCodedTypes();
  check_duplicate_coded_type_names(gen);
}

TEST(CodedTypesGeneratorTests, GoodUnionResourceness) {
  TestLibrary library(R"FIDL(library example;

type ResourceUnion = strict resource union {
    1: first bool;
};

type NonResourceUnion = strict union {
    1: first bool;
};
)FIDL");
  ASSERT_COMPILED(library);
  fidl::CodedTypesGenerator gen(library.compilation());
  gen.CompileCodedTypes();

  {
    auto name = fidl::flat::Name::Key(library.LookupLibrary("example"), "ResourceUnion");
    auto type = gen.CodedTypeFor(name);
    ASSERT_NOT_NULL(type);
    ASSERT_EQ(fidl::coded::Type::Kind::kXUnion, type->kind);

    auto coded_union = static_cast<const fidl::coded::XUnionType*>(type);
    EXPECT_EQ(fidl::types::Resourceness::kResource, coded_union->resourceness);
  }

  {
    auto name = fidl::flat::Name::Key(library.LookupLibrary("example"), "NonResourceUnion");
    auto type = gen.CodedTypeFor(name);
    ASSERT_NOT_NULL(type);
    ASSERT_EQ(fidl::coded::Type::Kind::kXUnion, type->kind);

    auto coded_union = static_cast<const fidl::coded::XUnionType*>(type);
    EXPECT_EQ(fidl::types::Resourceness::kValue, coded_union->resourceness);
  }
}

TEST(CodedTypesGeneratorTests, GoodTableResourceness) {
  TestLibrary library(R"FIDL(library example;

type ResourceTable = resource table {
    1: first bool;
};

type NonResourceTable = table {
    1: first bool;
};
)FIDL");
  ASSERT_COMPILED(library);
  fidl::CodedTypesGenerator gen(library.compilation());
  gen.CompileCodedTypes();

  {
    auto name = fidl::flat::Name::Key(library.LookupLibrary("example"), "ResourceTable");
    auto type = gen.CodedTypeFor(name);
    ASSERT_NOT_NULL(type);
    ASSERT_EQ(fidl::coded::Type::Kind::kTable, type->kind);

    auto coded_table = static_cast<const fidl::coded::TableType*>(type);
    EXPECT_EQ(fidl::types::Resourceness::kResource, coded_table->resourceness);
  }

  {
    auto name = fidl::flat::Name::Key(library.LookupLibrary("example"), "NonResourceTable");
    auto type = gen.CodedTypeFor(name);
    ASSERT_NOT_NULL(type);
    ASSERT_EQ(fidl::coded::Type::Kind::kTable, type->kind);

    auto coded_table = static_cast<const fidl::coded::TableType*>(type);
    EXPECT_EQ(fidl::types::Resourceness::kValue, coded_table->resourceness);
  }
}

TEST(CodedTypesGeneratorTests, GoodCodedTypesStructMessage) {
  TestLibrary library(R"FIDL(library example;

type OnReceivePayload = struct {
    arg bool;
};

protocol UseOfProtocol {
    Call(struct {
        arg1 bool;
        arg2 bool;
    });
    -> OnReceive(OnReceivePayload);
};
)FIDL");
  ASSERT_COMPILED(library);
  fidl::CodedTypesGenerator gen(library.compilation());
  gen.CompileCodedTypes();

  ASSERT_EQ(2, gen.coded_types().size());

  auto type0 = gen.coded_types().at(0).get();
  EXPECT_STREQ("bool", type0->coded_name.c_str());

  auto anon_payload = gen.coded_types().at(1).get();
  EXPECT_STREQ("example_UseOfProtocolCallRequestMessage", anon_payload->coded_name.c_str());
  EXPECT_TRUE(anon_payload->is_coding_needed);
  EXPECT_EQ(2, anon_payload->size_v2);
  ASSERT_EQ(fidl::coded::Type::Kind::kStruct, anon_payload->kind);
  auto anon_payload_message = static_cast<const fidl::coded::StructType*>(anon_payload);
  ASSERT_FALSE(anon_payload_message->is_empty);
  EXPECT_FALSE(anon_payload_message->contains_envelope);
  EXPECT_NULL(anon_payload_message->maybe_reference_type);
  EXPECT_STREQ("example/UseOfProtocolCallRequestMessage", anon_payload_message->qname.c_str());
  ASSERT_EQ(2, anon_payload_message->elements.size());
  EXPECT_EQ(0, field(anon_payload_message->elements.at(0)).offset_v2);
  EXPECT_EQ(1, field(anon_payload_message->elements.at(1)).offset_v2);

  auto named_payload_name =
      fidl::flat::Name::Key(library.LookupLibrary("example"), "OnReceivePayload");
  auto type_named_payload = gen.CodedTypeFor(named_payload_name);
  ASSERT_NOT_NULL(type_named_payload);
  EXPECT_STREQ("example_OnReceivePayload", type_named_payload->coded_name.c_str());
  EXPECT_TRUE(type_named_payload->is_coding_needed);
  EXPECT_EQ(1, type_named_payload->size_v2);
  ASSERT_EQ(fidl::coded::Type::Kind::kStruct, type_named_payload->kind);
  auto type_named_payload_message = static_cast<const fidl::coded::StructType*>(type_named_payload);
  ASSERT_FALSE(type_named_payload_message->is_empty);
  EXPECT_FALSE(type_named_payload_message->contains_envelope);
  EXPECT_NULL(type_named_payload_message->maybe_reference_type);
  EXPECT_STREQ("example/OnReceivePayload", type_named_payload_message->qname.c_str());
  EXPECT_EQ(1, type_named_payload_message->elements.size());
  EXPECT_EQ(0, field(type_named_payload_message->elements.at(0)).offset_v2);
}

TEST(CodedTypesGeneratorTests, GoodCodedTypesStructMessageErrorSyntax) {
  TestLibrary library(R"FIDL(library example;

protocol UseOfProtocol {
    Method() -> (struct {
        arg1 bool;
        arg2 bool;
    }) error uint32;
};
)FIDL");
  ASSERT_COMPILED(library);
  fidl::CodedTypesGenerator gen(library.compilation());
  gen.CompileCodedTypes();

  ASSERT_EQ(4, gen.coded_types().size());

  auto type0 = gen.coded_types().at(0).get();
  EXPECT_STREQ("example_UseOfProtocol_Method_ResultNullableRef", type0->coded_name.c_str());
  EXPECT_TRUE(type0->is_coding_needed);
  ASSERT_EQ(fidl::coded::Type::Kind::kXUnion, type0->kind);
  auto type0_union = static_cast<const fidl::coded::XUnionType*>(type0);
  ASSERT_EQ(fidl::types::Nullability::kNullable, type0_union->nullability);
  EXPECT_EQ(16, type0->size_v2);
  ASSERT_EQ(2, type0_union->fields.size());
  auto type0_field0 = type0_union->fields.at(0);
  EXPECT_STREQ("example_UseOfProtocol_Method_Response", type0_field0.type->coded_name.c_str());
  auto type0_field1 = type0_union->fields.at(1);
  EXPECT_STREQ("uint32", type0_field1.type->coded_name.c_str());

  auto type2 = gen.coded_types().at(1).get();
  EXPECT_STREQ("bool", type2->coded_name.c_str());

  auto type3 = gen.coded_types().at(2).get();
  EXPECT_STREQ("uint32", type3->coded_name.c_str());

  auto type4 = gen.coded_types().at(3).get();
  EXPECT_STREQ("example_UseOfProtocolMethodResponseMessage", type4->coded_name.c_str());
  EXPECT_TRUE(type4->is_coding_needed);
  EXPECT_EQ(16, type4->size_v2);
  ASSERT_EQ(fidl::coded::Type::Kind::kStruct, type4->kind);
  auto type4_message = static_cast<const fidl::coded::StructType*>(type4);
  ASSERT_FALSE(type4_message->is_empty);
  EXPECT_TRUE(type4_message->contains_envelope);
  EXPECT_NULL(type4_message->maybe_reference_type);
  EXPECT_STREQ("example/UseOfProtocolMethodResponseMessage", type4_message->qname.c_str());
  ASSERT_EQ(1, type4_message->elements.size());

  auto anon_payload_name =
      fidl::flat::Name::Key(library.LookupLibrary("example"), "UseOfProtocol_Method_Response");
  auto type_anon_payload = gen.CodedTypeFor(anon_payload_name);
  ASSERT_NOT_NULL(type_anon_payload);
  EXPECT_STREQ("example_UseOfProtocol_Method_Response", type_anon_payload->coded_name.c_str());
  EXPECT_TRUE(type_anon_payload->is_coding_needed);
  EXPECT_EQ(2, type_anon_payload->size_v2);
  ASSERT_EQ(fidl::coded::Type::Kind::kStruct, type_anon_payload->kind);
  auto type_anon_payload_message = static_cast<const fidl::coded::StructType*>(type_anon_payload);
  ASSERT_FALSE(type_anon_payload_message->is_empty);
  EXPECT_FALSE(type_anon_payload_message->contains_envelope);
  EXPECT_NULL(type_anon_payload_message->maybe_reference_type);
  EXPECT_STREQ("example/UseOfProtocol_Method_Response", type_anon_payload_message->qname.c_str());
  ASSERT_EQ(2, type_anon_payload_message->elements.size());
  EXPECT_EQ(0, field(type_anon_payload_message->elements.at(0)).offset_v2);
  EXPECT_EQ(1, field(type_anon_payload_message->elements.at(1)).offset_v2);
}

TEST(CodedTypesGeneratorTests, GoodCodedTypesTableMessage) {
  TestLibrary library(R"FIDL(library example;

type OnReceivePayload = table {
    1: arg bool;
};

protocol UseOfProtocol {
    Call(table {
        1: arg1 bool;
        2: arg2 bool;
    });
    -> OnReceive(OnReceivePayload);
};
)FIDL");
  ASSERT_COMPILED(library);
  fidl::CodedTypesGenerator gen(library.compilation());
  gen.CompileCodedTypes();

  ASSERT_EQ(2, gen.coded_types().size());

  auto type0 = gen.coded_types().at(0).get();
  EXPECT_STREQ("bool", type0->coded_name.c_str());

  auto anon_payload = gen.coded_types().at(1).get();
  EXPECT_STREQ("example_UseOfProtocolCallRequestMessage", anon_payload->coded_name.c_str());
  EXPECT_TRUE(anon_payload->is_coding_needed);
  EXPECT_EQ(16, anon_payload->size_v2);
  ASSERT_EQ(fidl::coded::Type::Kind::kTable, anon_payload->kind);
  auto anon_payload_message = static_cast<const fidl::coded::TableType*>(anon_payload);
  EXPECT_EQ(fidl::types::Resourceness::kValue, anon_payload_message->resourceness);
  EXPECT_STREQ("example/UseOfProtocolCallRequestMessage", anon_payload_message->qname.c_str());
  ASSERT_EQ(2, anon_payload_message->fields.size());
  EXPECT_EQ(1, anon_payload_message->fields.at(0).type->size_v2);
  EXPECT_EQ(1, anon_payload_message->fields.at(1).type->size_v2);

  auto named_payload_name =
      fidl::flat::Name::Key(library.LookupLibrary("example"), "OnReceivePayload");
  auto type_named_payload = gen.CodedTypeFor(named_payload_name);
  ASSERT_NOT_NULL(type_named_payload);
  EXPECT_STREQ("example_OnReceivePayload", type_named_payload->coded_name.c_str());
  EXPECT_TRUE(type_named_payload->is_coding_needed);
  EXPECT_EQ(16, type_named_payload->size_v2);
  ASSERT_EQ(fidl::coded::Type::Kind::kTable, type_named_payload->kind);
  auto type_named_payload_message = static_cast<const fidl::coded::TableType*>(type_named_payload);
  EXPECT_EQ(fidl::types::Resourceness::kValue, type_named_payload_message->resourceness);
  EXPECT_STREQ("example/OnReceivePayload", type_named_payload_message->qname.c_str());
  ASSERT_EQ(1, type_named_payload_message->fields.size());
  EXPECT_EQ(1, type_named_payload_message->fields.at(0).type->size_v2);
}

TEST(CodedTypesGeneratorTests, GoodCodedTypesTableMessageErrorSyntax) {
  TestLibrary library(R"FIDL(library example;

protocol UseOfProtocol {
    Method() -> (table {
        1: arg1 bool;
        2: arg2 bool;
    }) error uint32;
};
)FIDL");
  ASSERT_COMPILED(library);
  fidl::CodedTypesGenerator gen(library.compilation());
  gen.CompileCodedTypes();

  ASSERT_EQ(4, gen.coded_types().size());

  auto type0 = gen.coded_types().at(0).get();
  EXPECT_STREQ("example_UseOfProtocol_Method_ResultNullableRef", type0->coded_name.c_str());
  EXPECT_TRUE(type0->is_coding_needed);
  ASSERT_EQ(fidl::coded::Type::Kind::kXUnion, type0->kind);
  auto type0_union = static_cast<const fidl::coded::XUnionType*>(type0);
  ASSERT_EQ(fidl::types::Nullability::kNullable, type0_union->nullability);
  EXPECT_EQ(16, type0->size_v2);
  ASSERT_EQ(2, type0_union->fields.size());
  auto type0_field0 = type0_union->fields.at(0);
  EXPECT_STREQ("example_UseOfProtocol_Method_Response", type0_field0.type->coded_name.c_str());
  auto type0_field1 = type0_union->fields.at(1);
  EXPECT_STREQ("uint32", type0_field1.type->coded_name.c_str());

  auto type2 = gen.coded_types().at(1).get();
  EXPECT_STREQ("bool", type2->coded_name.c_str());

  auto type3 = gen.coded_types().at(2).get();
  EXPECT_STREQ("uint32", type3->coded_name.c_str());

  auto type4 = gen.coded_types().at(3).get();
  EXPECT_STREQ("example_UseOfProtocolMethodResponseMessage", type4->coded_name.c_str());
  EXPECT_TRUE(type4->is_coding_needed);
  EXPECT_EQ(16, type4->size_v2);
  ASSERT_EQ(fidl::coded::Type::Kind::kStruct, type4->kind);
  auto type4_message = static_cast<const fidl::coded::StructType*>(type4);
  EXPECT_TRUE(type4_message->contains_envelope);
  EXPECT_STREQ("example/UseOfProtocolMethodResponseMessage", type4_message->qname.c_str());
  ASSERT_EQ(1, type4_message->elements.size());

  auto anon_payload_name =
      fidl::flat::Name::Key(library.LookupLibrary("example"), "UseOfProtocol_Method_Response");
  auto type_anon_payload = gen.CodedTypeFor(anon_payload_name);
  ASSERT_NOT_NULL(type_anon_payload);
  EXPECT_STREQ("example_UseOfProtocol_Method_Response", type_anon_payload->coded_name.c_str());
  EXPECT_TRUE(type_anon_payload->is_coding_needed);
  EXPECT_EQ(16, type_anon_payload->size_v2);
  ASSERT_EQ(fidl::coded::Type::Kind::kTable, type_anon_payload->kind);
  auto type_anon_payload_message = static_cast<const fidl::coded::TableType*>(type_anon_payload);
  EXPECT_EQ(fidl::types::Resourceness::kValue, type_anon_payload_message->resourceness);
  EXPECT_STREQ("example/UseOfProtocol_Method_Response", type_anon_payload_message->qname.c_str());
  ASSERT_EQ(2, type_anon_payload_message->fields.size());
  EXPECT_EQ(1, type_anon_payload_message->fields.at(0).type->size_v2);
  EXPECT_EQ(1, type_anon_payload_message->fields.at(1).type->size_v2);
}

TEST(CodedTypesGeneratorTests, GoodCodedTypesUnionMessage) {
  TestLibrary library(R"FIDL(library example;

type OnReceivePayload = strict union {
    1: arg bool;
};

protocol UseOfProtocol {
    Call(flexible union {
        1: arg1 bool;
        2: arg2 bool;
    });
    -> OnReceive(OnReceivePayload);
};
)FIDL");
  ASSERT_COMPILED(library);
  fidl::CodedTypesGenerator gen(library.compilation());
  gen.CompileCodedTypes();

  ASSERT_EQ(4, gen.coded_types().size());

  auto type2 = gen.coded_types().at(2).get();
  EXPECT_STREQ("bool", type2->coded_name.c_str());

  auto anon_payload = gen.coded_types().at(3).get();
  EXPECT_STREQ("example_UseOfProtocolCallRequestMessage", anon_payload->coded_name.c_str());
  EXPECT_TRUE(anon_payload->is_coding_needed);
  EXPECT_EQ(16, anon_payload->size_v2);
  ASSERT_EQ(fidl::coded::Type::Kind::kXUnion, anon_payload->kind);
  auto anon_payload_message = static_cast<const fidl::coded::XUnionType*>(anon_payload);
  EXPECT_EQ(fidl::types::Nullability::kNonnullable, anon_payload_message->nullability);
  EXPECT_EQ(fidl::types::Resourceness::kValue, anon_payload_message->resourceness);
  EXPECT_STREQ("example/UseOfProtocolCallRequestMessage", anon_payload_message->qname.c_str());
  ASSERT_EQ(2, anon_payload_message->fields.size());
  EXPECT_EQ(1, anon_payload_message->fields.at(0).type->size_v2);
  EXPECT_EQ(1, anon_payload_message->fields.at(1).type->size_v2);

  auto named_payload_name =
      fidl::flat::Name::Key(library.LookupLibrary("example"), "OnReceivePayload");
  auto type_named_payload = gen.CodedTypeFor(named_payload_name);
  ASSERT_NOT_NULL(type_named_payload);
  EXPECT_STREQ("example_OnReceivePayload", type_named_payload->coded_name.c_str());
  EXPECT_TRUE(type_named_payload->is_coding_needed);
  EXPECT_EQ(16, type_named_payload->size_v2);
  ASSERT_EQ(fidl::coded::Type::Kind::kXUnion, type_named_payload->kind);
  auto type_named_payload_message = static_cast<const fidl::coded::XUnionType*>(type_named_payload);
  EXPECT_EQ(fidl::types::Nullability::kNonnullable, type_named_payload_message->nullability);
  EXPECT_EQ(fidl::types::Resourceness::kValue, type_named_payload_message->resourceness);
  EXPECT_STREQ("example/OnReceivePayload", type_named_payload_message->qname.c_str());
  ASSERT_EQ(1, type_named_payload_message->fields.size());
  EXPECT_EQ(1, type_named_payload_message->fields.at(0).type->size_v2);
}

TEST(CodedTypesGeneratorTests, GoodCodedTypesUnionMessageErrorSyntax) {
  TestLibrary library(R"FIDL(library example;

protocol UseOfProtocol {
    Method() -> (strict union {
        1: arg1 bool;
        2: arg2 bool;
    }) error uint32;
};
)FIDL");
  ASSERT_COMPILED(library);
  fidl::CodedTypesGenerator gen(library.compilation());
  gen.CompileCodedTypes();

  ASSERT_EQ(5, gen.coded_types().size());

  auto type1 = gen.coded_types().at(1).get();
  EXPECT_STREQ("example_UseOfProtocol_Method_ResultNullableRef", type1->coded_name.c_str());
  EXPECT_TRUE(type1->is_coding_needed);
  ASSERT_EQ(fidl::coded::Type::Kind::kXUnion, type1->kind);
  auto type1_union = static_cast<const fidl::coded::XUnionType*>(type1);
  ASSERT_EQ(fidl::types::Nullability::kNullable, type1_union->nullability);
  EXPECT_EQ(16, type1->size_v2);
  ASSERT_EQ(2, type1_union->fields.size());
  auto type1_field0 = type1_union->fields.at(0);
  EXPECT_STREQ("example_UseOfProtocol_Method_Response", type1_field0.type->coded_name.c_str());
  auto type1_field1 = type1_union->fields.at(1);
  EXPECT_STREQ("uint32", type1_field1.type->coded_name.c_str());

  auto type4 = gen.coded_types().at(2).get();
  EXPECT_STREQ("bool", type4->coded_name.c_str());

  auto type5 = gen.coded_types().at(3).get();
  EXPECT_STREQ("uint32", type5->coded_name.c_str());

  auto type6 = gen.coded_types().at(4).get();
  EXPECT_STREQ("example_UseOfProtocolMethodResponseMessage", type6->coded_name.c_str());
  EXPECT_TRUE(type6->is_coding_needed);
  EXPECT_EQ(16, type6->size_v2);
  ASSERT_EQ(fidl::coded::Type::Kind::kStruct, type6->kind);
  auto type6_message = static_cast<const fidl::coded::StructType*>(type6);
  EXPECT_TRUE(type6_message->contains_envelope);
  EXPECT_STREQ("example/UseOfProtocolMethodResponseMessage", type6_message->qname.c_str());
  ASSERT_EQ(1, type6_message->elements.size());

  auto anon_payload_name =
      fidl::flat::Name::Key(library.LookupLibrary("example"), "UseOfProtocol_Method_Response");
  auto type_anon_payload = gen.CodedTypeFor(anon_payload_name);
  ASSERT_NOT_NULL(type_anon_payload);
  EXPECT_STREQ("example_UseOfProtocol_Method_Response", type_anon_payload->coded_name.c_str());
  EXPECT_TRUE(type_anon_payload->is_coding_needed);
  EXPECT_EQ(16, type_anon_payload->size_v2);
  ASSERT_EQ(fidl::coded::Type::Kind::kXUnion, type_anon_payload->kind);
  auto type_anon_payload_message = static_cast<const fidl::coded::XUnionType*>(type_anon_payload);
  EXPECT_EQ(fidl::types::Nullability::kNonnullable, type_anon_payload_message->nullability);
  EXPECT_EQ(fidl::types::Resourceness::kValue, type_anon_payload_message->resourceness);
  EXPECT_STREQ("example/UseOfProtocol_Method_Response", type_anon_payload_message->qname.c_str());
  ASSERT_EQ(2, type_anon_payload_message->fields.size());
  EXPECT_EQ(1, type_anon_payload_message->fields.at(0).type->size_v2);
  EXPECT_EQ(1, type_anon_payload_message->fields.at(1).type->size_v2);
}

}  // namespace
