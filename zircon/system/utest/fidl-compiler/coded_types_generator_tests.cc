// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/tables_generator.h>

#include <zxtest/zxtest.h>

#include "error_test.h"
#include "fidl/coded_ast.h"
#include "fidl/coded_types_generator.h"
#include "test_library.h"

namespace {

const fidl::coded::StructField& field(const fidl::coded::StructElement& element) {
  assert(std::holds_alternative<const fidl::coded::StructField>(element));
  return std::get<const fidl::coded::StructField>(element);
}
const fidl::coded::StructPadding& padding(const fidl::coded::StructElement& element) {
  assert(std::holds_alternative<const fidl::coded::StructPadding>(element));
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
  fidl::CodedTypesGenerator gen(library.library());
  gen.CompileCodedTypes();

  ASSERT_EQ(4, gen.coded_types().size());

  auto type0 = gen.coded_types().at(0).get();
  EXPECT_STR_EQ("uint8", type0->coded_name.c_str());
  EXPECT_TRUE(type0->is_coding_needed);
  ASSERT_EQ(fidl::coded::Type::Kind::kPrimitive, type0->kind);
  auto type0_primitive = static_cast<const fidl::coded::PrimitiveType*>(type0);
  EXPECT_EQ(fidl::types::PrimitiveSubtype::kUint8, type0_primitive->subtype);

  auto type1 = gen.coded_types().at(1).get();
  EXPECT_STR_EQ("Array7_5uint8", type1->coded_name.c_str());
  EXPECT_TRUE(type1->is_coding_needed);
  ASSERT_EQ(fidl::coded::Type::Kind::kArray, type1->kind);
  auto type1_array = static_cast<const fidl::coded::ArrayType*>(type1);
  EXPECT_EQ(1, type1_array->element_size_v1);
  EXPECT_EQ(1, type1_array->element_size_v2);
  EXPECT_EQ(type0, type1_array->element_type);

  auto type2 = gen.coded_types().at(2).get();
  EXPECT_STR_EQ("Array77_13Array7_5uint8", type2->coded_name.c_str());
  EXPECT_TRUE(type2->is_coding_needed);
  ASSERT_EQ(fidl::coded::Type::Kind::kArray, type2->kind);
  auto type2_array = static_cast<const fidl::coded::ArrayType*>(type2);
  EXPECT_EQ(7 * 1, type2_array->element_size_v1);
  EXPECT_EQ(7 * 1, type2_array->element_size_v2);
  EXPECT_EQ(type1, type2_array->element_type);

  auto type3 = gen.coded_types().at(3).get();
  EXPECT_STR_EQ("Array1001_23Array77_13Array7_5uint8", type3->coded_name.c_str());
  EXPECT_TRUE(type3->is_coding_needed);
  ASSERT_EQ(fidl::coded::Type::Kind::kArray, type3->kind);
  auto type3_array = static_cast<const fidl::coded::ArrayType*>(type3);
  EXPECT_EQ(11 * 7 * 1, type3_array->element_size_v1);
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
  fidl::CodedTypesGenerator gen(library.library());
  gen.CompileCodedTypes();

  auto name_some_struct = fidl::flat::Name::Key(library.library(), "SomeStruct");
  auto type_some_struct = gen.CodedTypeFor(name_some_struct);
  ASSERT_NOT_NULL(type_some_struct);
  EXPECT_STR_EQ("example_SomeStruct", type_some_struct->coded_name.c_str());
  EXPECT_TRUE(type_some_struct->is_coding_needed);
  ASSERT_EQ(fidl::coded::Type::Kind::kStruct, type_some_struct->kind);
  auto type_some_struct_struct = static_cast<const fidl::coded::StructType*>(type_some_struct);
  ASSERT_EQ(0, type_some_struct_struct->elements.size());
  EXPECT_STR_EQ("example/SomeStruct", type_some_struct_struct->qname.c_str());
  EXPECT_FALSE(type_some_struct_struct->contains_envelope);
  EXPECT_NULL(type_some_struct_struct->maybe_reference_type);
  EXPECT_EQ(1, type_some_struct_struct->size_v1);
  EXPECT_EQ(1, type_some_struct_struct->size_v2);

  ASSERT_EQ(2, gen.coded_types().size());

  auto type0 = gen.coded_types().at(0).get();
  EXPECT_STR_EQ("Vector10nonnullable18example_SomeStruct", type0->coded_name.c_str());
  EXPECT_TRUE(type0->is_coding_needed);
  ASSERT_EQ(fidl::coded::Type::Kind::kVector, type0->kind);
  auto type0_vector = static_cast<const fidl::coded::VectorType*>(type0);
  EXPECT_EQ(type_some_struct, type0_vector->element_type);
  EXPECT_EQ(10, type0_vector->max_count);
  EXPECT_EQ(1, type0_vector->element_size_v1);
  EXPECT_EQ(1, type0_vector->element_size_v2);
  EXPECT_EQ(fidl::types::Nullability::kNonnullable, type0_vector->nullability);
  EXPECT_EQ(fidl::coded::MemcpyCompatibility::kCanMemcpy,
            type0_vector->element_memcpy_compatibility);

  auto type1 = gen.coded_types().at(1).get();
  EXPECT_STR_EQ("Vector20nonnullable39Vector10nonnullable18example_SomeStruct",
                type1->coded_name.c_str());
  EXPECT_TRUE(type1->is_coding_needed);
  ASSERT_EQ(fidl::coded::Type::Kind::kVector, type1->kind);
  auto type1_vector = static_cast<const fidl::coded::VectorType*>(type1);
  EXPECT_EQ(type0, type1_vector->element_type);
  EXPECT_EQ(20, type1_vector->max_count);
  EXPECT_EQ(16, type1_vector->element_size_v1);
  EXPECT_EQ(16, type1_vector->element_size_v2);
  EXPECT_EQ(fidl::types::Nullability::kNonnullable, type1_vector->nullability);
  EXPECT_EQ(fidl::coded::MemcpyCompatibility::kCannotMemcpy,
            type1_vector->element_memcpy_compatibility);
}

TEST(CodedTypesGeneratorTests, GoodVectorEncodeMightMutate) {
  auto library = WithLibraryZx(R"FIDL(
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
  ASSERT_COMPILED(library);
  auto decl = library.library()->LookupDeclByName(fidl::flat::Name::CreateSourced(
      library.library(), fidl::SourceSpan("Value", library.source_file())));
  ASSERT_NOT_NULL(decl);
  const fidl::flat::Struct* str = static_cast<fidl::flat::Struct*>(decl);
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

TEST(CodedTypesGeneratorTests, GoodCodedTypesOfProtocol) {
  TestLibrary library(R"FIDL(library example;

protocol SomeProtocol {};

protocol UseOfProtocol {
    Call(resource struct {
        arg client_end:SomeProtocol;
    });
};
)FIDL");
  ASSERT_COMPILED(library);
  fidl::CodedTypesGenerator gen(library.library());
  gen.CompileCodedTypes();

  ASSERT_EQ(2, gen.coded_types().size());

  auto type0 = gen.coded_types().at(0).get();
  EXPECT_STR_EQ("Protocol20example_SomeProtocolnonnullable", type0->coded_name.c_str());
  EXPECT_TRUE(type0->is_coding_needed);
  EXPECT_EQ(4, type0->size_v1);
  EXPECT_EQ(4, type0->size_v2);
  ASSERT_EQ(fidl::coded::Type::Kind::kProtocolHandle, type0->kind);
  auto type0_ihandle = static_cast<const fidl::coded::ProtocolHandleType*>(type0);
  ASSERT_EQ(fidl::types::Nullability::kNonnullable, type0_ihandle->nullability);

  auto type1 = gen.coded_types().at(1).get();
  EXPECT_STR_EQ("example_UseOfProtocolCallRequest", type1->coded_name.c_str());
  EXPECT_TRUE(type1->is_coding_needed);
  EXPECT_EQ(24, type1->size_v1);
  EXPECT_EQ(24, type1->size_v2);
  ASSERT_EQ(fidl::coded::Type::Kind::kMessage, type1->kind);
  auto type1_message = static_cast<const fidl::coded::MessageType*>(type1);
  EXPECT_FALSE(type1_message->contains_envelope);
  EXPECT_STR_EQ("example/UseOfProtocolCallRequest", type1_message->qname.c_str());
  EXPECT_EQ(2, type1_message->elements.size());

  EXPECT_EQ(16, field(type1_message->elements.at(0)).offset_v1);
  EXPECT_EQ(16, field(type1_message->elements.at(0)).offset_v2);
  EXPECT_EQ(type0, field(type1_message->elements.at(0)).type);

  EXPECT_EQ(20, padding(type1_message->elements.at(1)).offset_v1);
  EXPECT_EQ(20, padding(type1_message->elements.at(1)).offset_v2);
  EXPECT_EQ(0xffffffff, std::get<uint32_t>(padding(type1_message->elements.at(1)).mask));
}

TEST(CodedTypesGeneratorTests, GoodCodedTypesOfProtocolErrorSyntax) {
  TestLibrary library(R"FIDL(library example;

protocol ErrorSyntaxProtocol {
    ErrorSyntaxMethod() -> (struct{}) error uint32;
};
)FIDL");
  ASSERT_COMPILED(library);
  fidl::CodedTypesGenerator gen(library.library());
  gen.CompileCodedTypes();

  ASSERT_EQ(4, gen.coded_types().size());

  auto type0 = gen.coded_types().at(0).get();
  EXPECT_STR_EQ("example_ErrorSyntaxProtocol_ErrorSyntaxMethod_ResultNullableRef",
                type0->coded_name.c_str());

  auto type1 = gen.coded_types().at(1).get();
  EXPECT_STR_EQ("uint32", type1->coded_name.c_str());

  auto type2 = gen.coded_types().at(2).get();
  EXPECT_STR_EQ("example_ErrorSyntaxProtocolErrorSyntaxMethodRequest", type2->coded_name.c_str());
  auto type2_message = static_cast<const fidl::coded::MessageType*>(type2);
  EXPECT_FALSE(type2_message->contains_envelope);

  auto type3 = gen.coded_types().at(3).get();
  EXPECT_STR_EQ("example_ErrorSyntaxProtocolErrorSyntaxMethodResponse", type3->coded_name.c_str());
  auto type3_message = static_cast<const fidl::coded::MessageType*>(type3);
  EXPECT_TRUE(type3_message->contains_envelope);
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
  fidl::CodedTypesGenerator gen(library.library());
  gen.CompileCodedTypes();

  ASSERT_EQ(8, gen.coded_types().size());

  // ClientEnd request payload
  auto type0 = gen.coded_types().at(0).get();
  EXPECT_STR_EQ("Protocol20example_SomeProtocolnonnullable", type0->coded_name.c_str());
  EXPECT_TRUE(type0->is_coding_needed);
  EXPECT_EQ(4, type0->size_v1);
  EXPECT_EQ(4, type0->size_v2);
  ASSERT_EQ(fidl::coded::Type::Kind::kProtocolHandle, type0->kind);
  auto type0_ihandle = static_cast<const fidl::coded::ProtocolHandleType*>(type0);
  EXPECT_EQ(fidl::types::Nullability::kNonnullable, type0_ihandle->nullability);

  // ClientEnd request message
  auto type1 = gen.coded_types().at(1).get();
  EXPECT_STR_EQ("example_UseOfProtocolEndsClientEndsRequest", type1->coded_name.c_str());
  EXPECT_TRUE(type1->is_coding_needed);
  EXPECT_EQ(24, type1->size_v1);
  EXPECT_EQ(24, type1->size_v2);
  ASSERT_EQ(fidl::coded::Type::Kind::kMessage, type1->kind);
  auto type1_message = static_cast<const fidl::coded::MessageType*>(type1);
  EXPECT_FALSE(type1_message->contains_envelope);
  EXPECT_STR_EQ("example/UseOfProtocolEndsClientEndsRequest", type1_message->qname.c_str());
  EXPECT_EQ(2, type1_message->elements.size());
  EXPECT_EQ(16, field(type1_message->elements.at(0)).offset_v1);
  EXPECT_EQ(16, field(type1_message->elements.at(0)).offset_v2);
  EXPECT_EQ(type0, field(type1_message->elements.at(0)).type);
  EXPECT_EQ(20, padding(type1_message->elements.at(1)).offset_v1);
  EXPECT_EQ(20, padding(type1_message->elements.at(1)).offset_v2);
  EXPECT_EQ(0xffffffff, std::get<uint32_t>(padding(type1_message->elements.at(1)).mask));

  // ClientEnd response payload
  auto type2 = gen.coded_types().at(2).get();
  EXPECT_STR_EQ("Protocol20example_SomeProtocolnullable", type2->coded_name.c_str());
  EXPECT_TRUE(type2->is_coding_needed);
  EXPECT_EQ(4, type2->size_v1);
  EXPECT_EQ(4, type2->size_v2);
  ASSERT_EQ(fidl::coded::Type::Kind::kProtocolHandle, type2->kind);
  auto type2_ihandle = static_cast<const fidl::coded::ProtocolHandleType*>(type2);
  EXPECT_EQ(fidl::types::Nullability::kNullable, type2_ihandle->nullability);

  // ClientEnd response message
  auto type3 = gen.coded_types().at(3).get();
  EXPECT_STR_EQ("example_UseOfProtocolEndsClientEndsResponse", type3->coded_name.c_str());
  EXPECT_TRUE(type3->is_coding_needed);
  EXPECT_EQ(24, type3->size_v1);
  EXPECT_EQ(24, type3->size_v2);
  ASSERT_EQ(fidl::coded::Type::Kind::kMessage, type3->kind);
  auto type3_message = static_cast<const fidl::coded::MessageType*>(type3);
  EXPECT_FALSE(type3_message->contains_envelope);
  EXPECT_STR_EQ("example/UseOfProtocolEndsClientEndsResponse", type3_message->qname.c_str());
  EXPECT_EQ(2, type3_message->elements.size());
  EXPECT_EQ(16, field(type3_message->elements.at(0)).offset_v1);
  EXPECT_EQ(16, field(type3_message->elements.at(0)).offset_v2);
  EXPECT_EQ(type2, field(type3_message->elements.at(0)).type);
  EXPECT_EQ(20, padding(type3_message->elements.at(1)).offset_v1);
  EXPECT_EQ(20, padding(type3_message->elements.at(1)).offset_v2);
  EXPECT_EQ(0xffffffff, std::get<uint32_t>(padding(type3_message->elements.at(1)).mask));

  // ServerEnd request payload
  auto type4 = gen.coded_types().at(4).get();
  EXPECT_STR_EQ("Request20example_SomeProtocolnullable", type4->coded_name.c_str());
  EXPECT_TRUE(type4->is_coding_needed);
  EXPECT_EQ(4, type4->size_v1);
  EXPECT_EQ(4, type4->size_v2);
  ASSERT_EQ(fidl::coded::Type::Kind::kRequestHandle, type4->kind);
  auto type4_ihandle = static_cast<const fidl::coded::RequestHandleType*>(type4);
  EXPECT_EQ(fidl::types::Nullability::kNullable, type4_ihandle->nullability);

  // ServerEnd request message
  auto type5 = gen.coded_types().at(5).get();
  EXPECT_STR_EQ("example_UseOfProtocolEndsServerEndsRequest", type5->coded_name.c_str());
  EXPECT_TRUE(type5->is_coding_needed);
  EXPECT_EQ(24, type5->size_v1);
  EXPECT_EQ(24, type5->size_v2);
  ASSERT_EQ(fidl::coded::Type::Kind::kMessage, type5->kind);
  auto type5_message = static_cast<const fidl::coded::MessageType*>(type5);
  EXPECT_FALSE(type5_message->contains_envelope);
  EXPECT_STR_EQ("example/UseOfProtocolEndsServerEndsRequest", type5_message->qname.c_str());
  EXPECT_EQ(2, type5_message->elements.size());
  EXPECT_EQ(16, field(type5_message->elements.at(0)).offset_v1);
  EXPECT_EQ(16, field(type5_message->elements.at(0)).offset_v2);
  EXPECT_EQ(type4, field(type5_message->elements.at(0)).type);
  EXPECT_EQ(20, padding(type5_message->elements.at(1)).offset_v1);
  EXPECT_EQ(20, padding(type5_message->elements.at(1)).offset_v2);
  EXPECT_EQ(0xffffffff, std::get<uint32_t>(padding(type5_message->elements.at(1)).mask));

  // ServerEnd response payload
  auto type6 = gen.coded_types().at(6).get();
  EXPECT_STR_EQ("Request20example_SomeProtocolnonnullable", type6->coded_name.c_str());
  EXPECT_TRUE(type6->is_coding_needed);
  EXPECT_EQ(4, type6->size_v1);
  EXPECT_EQ(4, type6->size_v2);
  ASSERT_EQ(fidl::coded::Type::Kind::kRequestHandle, type6->kind);
  auto type6_ihandle = static_cast<const fidl::coded::RequestHandleType*>(type6);
  EXPECT_EQ(fidl::types::Nullability::kNonnullable, type6_ihandle->nullability);

  // ServerEnd response message
  auto type7 = gen.coded_types().at(7).get();
  EXPECT_STR_EQ("example_UseOfProtocolEndsServerEndsResponse", type7->coded_name.c_str());
  EXPECT_TRUE(type7->is_coding_needed);
  EXPECT_EQ(24, type7->size_v1);
  EXPECT_EQ(24, type7->size_v2);
  ASSERT_EQ(fidl::coded::Type::Kind::kMessage, type7->kind);
  auto type7_message = static_cast<const fidl::coded::MessageType*>(type7);
  EXPECT_FALSE(type7_message->contains_envelope);
  EXPECT_STR_EQ("example/UseOfProtocolEndsServerEndsResponse", type7_message->qname.c_str());
  EXPECT_EQ(2, type7_message->elements.size());
  EXPECT_EQ(16, field(type7_message->elements.at(0)).offset_v1);
  EXPECT_EQ(16, field(type7_message->elements.at(0)).offset_v2);
  EXPECT_EQ(type6, field(type7_message->elements.at(0)).type);
  EXPECT_EQ(20, padding(type7_message->elements.at(1)).offset_v1);
  EXPECT_EQ(20, padding(type7_message->elements.at(1)).offset_v2);
  EXPECT_EQ(0xffffffff, std::get<uint32_t>(padding(type7_message->elements.at(1)).mask));
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
  fidl::CodedTypesGenerator gen(library.library());
  gen.CompileCodedTypes();

  ASSERT_EQ(3, gen.coded_types().size());

  auto type0 = gen.coded_types().at(0).get();
  ASSERT_STR_EQ("example_MyXUnionNullableRef", type0->coded_name.c_str());
  ASSERT_TRUE(type0->is_coding_needed);
  ASSERT_EQ(fidl::coded::Type::Kind::kXUnion, type0->kind);
  auto nullable_xunion = static_cast<const fidl::coded::XUnionType*>(type0);
  ASSERT_EQ(fidl::types::Nullability::kNullable, nullable_xunion->nullability);

  auto type1 = gen.coded_types().at(1).get();
  ASSERT_STR_EQ("bool", type1->coded_name.c_str());
  ASSERT_TRUE(type1->is_coding_needed);
  ASSERT_EQ(fidl::coded::Type::Kind::kPrimitive, type1->kind);
  auto type2_primitive = static_cast<const fidl::coded::PrimitiveType*>(type1);
  ASSERT_EQ(fidl::types::PrimitiveSubtype::kBool, type2_primitive->subtype);

  auto type2 = gen.coded_types().at(2).get();
  ASSERT_STR_EQ("int32", type2->coded_name.c_str());
  ASSERT_TRUE(type2->is_coding_needed);
  ASSERT_EQ(fidl::coded::Type::Kind::kPrimitive, type2->kind);
  auto type1_primitive = static_cast<const fidl::coded::PrimitiveType*>(type2);
  ASSERT_EQ(fidl::types::PrimitiveSubtype::kInt32, type1_primitive->subtype);

  auto name = fidl::flat::Name::Key(library.library(), "MyXUnion");
  auto type = gen.CodedTypeFor(name);
  ASSERT_NOT_NULL(type);
  ASSERT_STR_EQ("example_MyXUnion", type->coded_name.c_str());
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
  ASSERT_STR_EQ("example/MyXUnion", coded_xunion->qname.c_str());
  ASSERT_EQ(fidl::types::Nullability::kNonnullable, coded_xunion->nullability);
  ASSERT_NOT_NULL(coded_xunion->maybe_reference_type);

  auto struct_name = fidl::flat::Name::Key(library.library(), "MyXUnionStruct");
  auto struct_type = gen.CodedTypeFor(struct_name);
  ASSERT_NOT_NULL(struct_type);
  ASSERT_STR_EQ("example_MyXUnionStruct", struct_type->coded_name.c_str());
  ASSERT_TRUE(struct_type->is_coding_needed);
  ASSERT_EQ(fidl::coded::Type::Kind::kStruct, struct_type->kind);
  auto struct_type_struct = static_cast<const fidl::coded::StructType*>(struct_type);
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
  fidl::CodedTypesGenerator gen(library.library());
  gen.CompileCodedTypes();

  // 3 == size of {bool, int32, MyXUnion?}, which is all of the types used in
  // the example.
  ASSERT_EQ(3, gen.coded_types().size());

  auto type0 = gen.coded_types().at(0).get();
  ASSERT_STR_EQ("example_MyXUnionNullableRef", type0->coded_name.c_str());
  ASSERT_TRUE(type0->is_coding_needed);
  ASSERT_EQ(fidl::coded::Type::Kind::kXUnion, type0->kind);
  auto nullable_xunion = static_cast<const fidl::coded::XUnionType*>(type0);
  ASSERT_EQ(fidl::types::Nullability::kNullable, nullable_xunion->nullability);

  auto type1 = gen.coded_types().at(1).get();
  ASSERT_STR_EQ("bool", type1->coded_name.c_str());
  ASSERT_TRUE(type1->is_coding_needed);
  ASSERT_EQ(fidl::coded::Type::Kind::kPrimitive, type1->kind);
  auto type2_primitive = static_cast<const fidl::coded::PrimitiveType*>(type1);
  ASSERT_EQ(fidl::types::PrimitiveSubtype::kBool, type2_primitive->subtype);

  auto type2 = gen.coded_types().at(2).get();
  ASSERT_STR_EQ("int32", type2->coded_name.c_str());
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
  fidl::CodedTypesGenerator gen(library.library());
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
  fidl::CodedTypesGenerator gen(library.library());
  gen.CompileCodedTypes();

  auto struct_name = fidl::flat::Name::Key(library.library(), "MyStruct");
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
  fidl::CodedTypesGenerator gen(library.library());
  gen.CompileCodedTypes();

  ASSERT_EQ(4, gen.coded_types().size());

  auto type0 = gen.coded_types().at(0).get();
  EXPECT_STR_EQ("int32", type0->coded_name.c_str());
  EXPECT_TRUE(type0->is_coding_needed);
  auto type1 = gen.coded_types().at(1).get();
  EXPECT_STR_EQ("bool", type1->coded_name.c_str());
  EXPECT_TRUE(type1->is_coding_needed);
  auto type2 = gen.coded_types().at(2).get();
  EXPECT_STR_EQ("int64", type2->coded_name.c_str());
  EXPECT_TRUE(type2->is_coding_needed);
  auto type3 = gen.coded_types().at(3).get();
  EXPECT_STR_EQ("int16", type3->coded_name.c_str());
  EXPECT_TRUE(type3->is_coding_needed);

  auto name_bool_and_int32 = fidl::flat::Name::Key(library.library(), "BoolAndInt32");
  auto type_bool_and_int32 = gen.CodedTypeFor(name_bool_and_int32);
  ASSERT_NOT_NULL(type_bool_and_int32);
  EXPECT_STR_EQ("example_BoolAndInt32", type_bool_and_int32->coded_name.c_str());
  auto type_bool_and_int32_struct =
      static_cast<const fidl::coded::StructType*>(type_bool_and_int32);
  ASSERT_EQ(type_bool_and_int32_struct->elements.size(), 2);
  EXPECT_EQ(field(type_bool_and_int32_struct->elements[0]).type->kind,
            fidl::coded::Type::Kind::kPrimitive);
  EXPECT_EQ(field(type_bool_and_int32_struct->elements[0]).offset_v1, 0);
  EXPECT_EQ(field(type_bool_and_int32_struct->elements[0]).offset_v2, 0);
  EXPECT_EQ(padding(type_bool_and_int32_struct->elements[1]).offset_v1, 0);
  EXPECT_EQ(padding(type_bool_and_int32_struct->elements[1]).offset_v2, 0);
  EXPECT_EQ(std::get<uint32_t>(padding(type_bool_and_int32_struct->elements[1]).mask), 0xffffff00);

  auto name_complex = fidl::flat::Name::Key(library.library(), "Complex");
  auto type_complex = gen.CodedTypeFor(name_complex);
  ASSERT_NOT_NULL(type_complex);
  EXPECT_STR_EQ("example_Complex", type_complex->coded_name.c_str());
  auto type_complex_struct = static_cast<const fidl::coded::StructType*>(type_complex);
  ASSERT_EQ(type_complex_struct->elements.size(), 3);
  EXPECT_EQ(field(type_complex_struct->elements[0]).type->kind,
            fidl::coded::Type::Kind::kPrimitive);
  EXPECT_EQ(field(type_complex_struct->elements[0]).offset_v1, 4);
  EXPECT_EQ(field(type_complex_struct->elements[0]).offset_v2, 4);
  EXPECT_EQ(padding(type_complex_struct->elements[1]).offset_v1, 4);
  EXPECT_EQ(padding(type_complex_struct->elements[1]).offset_v2, 4);
  EXPECT_EQ(std::get<uint32_t>(padding(type_complex_struct->elements[1]).mask), 0xffffff00);
  EXPECT_EQ(padding(type_complex_struct->elements[2]).offset_v1, 16);
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
  fidl::CodedTypesGenerator gen(library.library());
  gen.CompileCodedTypes();

  auto name_level0 = fidl::flat::Name::Key(library.library(), "Level0");
  auto type_level0 = gen.CodedTypeFor(name_level0);
  ASSERT_NOT_NULL(type_level0);
  auto struct_level0 = static_cast<const fidl::coded::StructType*>(type_level0);
  ASSERT_EQ(struct_level0->elements.size(), 2);
  EXPECT_EQ(padding(struct_level0->elements[0]).offset_v1, 0);
  EXPECT_EQ(padding(struct_level0->elements[0]).offset_v2, 0);
  EXPECT_EQ(std::get<uint32_t>(padding(struct_level0->elements[0]).mask), 0xffffff00);
  EXPECT_EQ(padding(struct_level0->elements[1]).offset_v1, 8);
  EXPECT_EQ(padding(struct_level0->elements[1]).offset_v2, 8);
  EXPECT_EQ(std::get<uint32_t>(padding(struct_level0->elements[1]).mask), 0xffffff00);

  auto name_level1 = fidl::flat::Name::Key(library.library(), "Level1");
  auto type_level1 = gen.CodedTypeFor(name_level1);
  ASSERT_NOT_NULL(type_level1);
  auto struct_level1 = static_cast<const fidl::coded::StructType*>(type_level1);
  ASSERT_EQ(struct_level1->elements.size(), 2);
  EXPECT_EQ(padding(struct_level1->elements[0]).offset_v1, 0);
  EXPECT_EQ(padding(struct_level1->elements[0]).offset_v2, 0);
  EXPECT_EQ(std::get<uint32_t>(padding(struct_level1->elements[0]).mask), 0xffffff00);
  EXPECT_EQ(padding(struct_level1->elements[1]).offset_v1, 8);
  EXPECT_EQ(padding(struct_level1->elements[1]).offset_v2, 8);
  EXPECT_EQ(std::get<uint64_t>(padding(struct_level1->elements[1]).mask), 0xffffffffffffff00);

  auto name_level2 = fidl::flat::Name::Key(library.library(), "Level2");
  auto type_level2 = gen.CodedTypeFor(name_level2);
  ASSERT_NOT_NULL(type_level2);
  auto struct_level2 = static_cast<const fidl::coded::StructType*>(type_level2);
  ASSERT_EQ(struct_level2->elements.size(), 3);
  EXPECT_EQ(padding(struct_level2->elements[0]).offset_v1, 0);
  EXPECT_EQ(padding(struct_level2->elements[0]).offset_v2, 0);
  EXPECT_EQ(std::get<uint32_t>(padding(struct_level2->elements[0]).mask), 0xffffff00);
  EXPECT_EQ(padding(struct_level2->elements[1]).offset_v1, 8);
  EXPECT_EQ(padding(struct_level2->elements[1]).offset_v2, 8);
  EXPECT_EQ(std::get<uint64_t>(padding(struct_level2->elements[1]).mask), 0xffffffffffffff00);
  EXPECT_EQ(padding(struct_level2->elements[2]).offset_v1, 24);
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
  fidl::CodedTypesGenerator gen(library.library());
  gen.CompileCodedTypes();

  auto name_one_level = fidl::flat::Name::Key(library.library(), "OneLevelRecursiveOptionalStruct");
  auto type_one_level = gen.CodedTypeFor(name_one_level);
  ASSERT_NOT_NULL(type_one_level);
  auto struct_one_level = static_cast<const fidl::coded::StructType*>(type_one_level);
  ASSERT_EQ(struct_one_level->elements.size(), 1);
  EXPECT_EQ(field(struct_one_level->elements[0]).type->kind,
            fidl::coded::Type::Kind::kStructPointer);
  ASSERT_SUBSTR(field(struct_one_level->elements[0]).type->coded_name.c_str(),
                "OneLevelRecursiveOptionalStruct");
  EXPECT_EQ(field(struct_one_level->elements[0]).offset_v1, 0);
  EXPECT_EQ(field(struct_one_level->elements[0]).offset_v2, 0);

  auto name_two_level_b =
      fidl::flat::Name::Key(library.library(), "TwoLevelRecursiveOptionalStructB");
  auto type_two_level_b = gen.CodedTypeFor(name_two_level_b);
  ASSERT_NOT_NULL(type_two_level_b);
  auto struct_two_level_b = static_cast<const fidl::coded::StructType*>(type_two_level_b);
  ASSERT_EQ(struct_two_level_b->elements.size(), 1);
  EXPECT_EQ(field(struct_two_level_b->elements[0]).type->kind,
            fidl::coded::Type::Kind::kStructPointer);
  ASSERT_SUBSTR(field(struct_two_level_b->elements[0]).type->coded_name.c_str(),
                "TwoLevelRecursiveOptionalStructA");
  EXPECT_EQ(field(struct_two_level_b->elements[0]).offset_v1, 0);
  EXPECT_EQ(field(struct_two_level_b->elements[0]).offset_v2, 0);

  // TwoLevelRecursiveOptionalStructA will be equivalent to TwoLevelRecursiveOptionalStructB
  // because of flattening.
  auto name_two_level_a =
      fidl::flat::Name::Key(library.library(), "TwoLevelRecursiveOptionalStructA");
  auto type_two_level_a = gen.CodedTypeFor(name_two_level_a);
  ASSERT_NOT_NULL(type_two_level_a);
  auto struct_two_level_a = static_cast<const fidl::coded::StructType*>(type_two_level_a);
  ASSERT_EQ(struct_two_level_a->elements.size(), 1);
  EXPECT_EQ(field(struct_two_level_a->elements[0]).type->kind,
            fidl::coded::Type::Kind::kStructPointer);
  ASSERT_SUBSTR(field(struct_two_level_a->elements[0]).type->coded_name.c_str(),
                "TwoLevelRecursiveOptionalStructA");
  EXPECT_EQ(field(struct_two_level_a->elements[0]).offset_v1, 0);
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
  fidl::CodedTypesGenerator gen(library.library());
  gen.CompileCodedTypes();

  auto name_inner_struct = fidl::flat::Name::Key(library.library(), "InnerStruct");
  auto type_inner_struct = gen.CodedTypeFor(name_inner_struct);
  ASSERT_NOT_NULL(type_inner_struct);
  auto struct_inner_struct = static_cast<const fidl::coded::StructType*>(type_inner_struct);
  ASSERT_EQ(struct_inner_struct->elements.size(), 1);
  EXPECT_EQ(padding(struct_inner_struct->elements[0]).offset_v1, 0);
  EXPECT_EQ(padding(struct_inner_struct->elements[0]).offset_v2, 0);
  ASSERT_TRUE(std::get<uint16_t>(padding(struct_inner_struct->elements[0]).mask));
  EXPECT_EQ(std::get<uint16_t>(padding(struct_inner_struct->elements[0]).mask), 0xff00);

  auto name_outer_struct = fidl::flat::Name::Key(library.library(), "OuterStruct");
  auto type_outer_struct = gen.CodedTypeFor(name_outer_struct);
  ASSERT_NOT_NULL(type_outer_struct);
  auto struct_outer_struct = static_cast<const fidl::coded::StructType*>(type_outer_struct);
  ASSERT_EQ(struct_outer_struct->elements.size(), 2);
  EXPECT_EQ(padding(struct_outer_struct->elements[0]).offset_v1, 0);
  EXPECT_EQ(padding(struct_outer_struct->elements[0]).offset_v2, 0);
  ASSERT_TRUE(std::get<uint16_t>(padding(struct_outer_struct->elements[0]).mask));
  EXPECT_EQ(std::get<uint16_t>(padding(struct_outer_struct->elements[0]).mask), 0xff00);
  EXPECT_EQ(padding(struct_outer_struct->elements[1]).offset_v1, 4);
  EXPECT_EQ(padding(struct_outer_struct->elements[1]).offset_v2, 4);
  ASSERT_TRUE(std::get<uint16_t>(padding(struct_outer_struct->elements[1]).mask));
  EXPECT_EQ(std::get<uint16_t>(padding(struct_outer_struct->elements[1]).mask), 0xff00);
}

TEST(CodedTypesGeneratorTests, GoodCodedTypesOfOptionals) {
  auto library = WithLibraryZx(R"FIDL(
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
  ASSERT_COMPILED(library);
  fidl::CodedTypesGenerator gen(library.library());
  gen.CompileCodedTypes();

  auto name_outer_struct = fidl::flat::Name::Key(library.library(), "OuterStruct");
  auto type_outer_struct = gen.CodedTypeFor(name_outer_struct);
  ASSERT_NOT_NULL(type_outer_struct);
  auto struct_outer_struct = static_cast<const fidl::coded::StructType*>(type_outer_struct);
  ASSERT_EQ(struct_outer_struct->elements.size(), 5);
  EXPECT_EQ(padding(struct_outer_struct->elements[0]).offset_v1, 0);
  EXPECT_EQ(padding(struct_outer_struct->elements[0]).offset_v2, 0);
  EXPECT_EQ(std::get<uint16_t>(padding(struct_outer_struct->elements[0]).mask), 0xff00);
  EXPECT_EQ(field(struct_outer_struct->elements[1]).type->kind, fidl::coded::Type::Kind::kHandle);
  EXPECT_EQ(field(struct_outer_struct->elements[1]).offset_v1, 4);
  EXPECT_EQ(field(struct_outer_struct->elements[1]).offset_v2, 4);
  EXPECT_EQ(field(struct_outer_struct->elements[2]).type->kind, fidl::coded::Type::Kind::kXUnion);
  EXPECT_EQ(field(struct_outer_struct->elements[2]).offset_v1, 8);
  EXPECT_EQ(field(struct_outer_struct->elements[2]).offset_v2, 8);
  EXPECT_EQ(padding(struct_outer_struct->elements[3]).offset_v1, 32);
  EXPECT_EQ(padding(struct_outer_struct->elements[3]).offset_v2, 24);
  EXPECT_EQ(std::get<uint16_t>(padding(struct_outer_struct->elements[3]).mask), 0xff00);
  EXPECT_EQ(padding(struct_outer_struct->elements[4]).offset_v1, 36);
  EXPECT_EQ(padding(struct_outer_struct->elements[4]).offset_v2, 28);
  EXPECT_EQ(std::get<uint32_t>(padding(struct_outer_struct->elements[4]).mask), 0xffffffff);
}

// In the following example, we define the `byte` struct. However, fidlc has
// an outstanding scoping bug which causes the `byte` type within the
// `badlookup` struct to resolve to the primitive alias of `uint8`.
//
// When calculating coding tables, we must therefore ensure to follow exactly
// the object graph provided by earlier stages of the compiler rather than
// implementing a lookup which may not be the same as the lookup done earlier.
TEST(CodedTypesGeneratorTests, GoodScopingBugShouldNotAffectCodingTables) {
  TestLibrary library(R"FIDL(library example;

alias membertype = uint32;

type byte = struct {
    member membertype = 1;
};

type badlookup = struct {
    f1 byte;
    f2 bytes;
};
)FIDL");
  ASSERT_COMPILED(library);
  fidl::CodedTypesGenerator gen(library.library());
  gen.CompileCodedTypes();

  auto the_struct_name = fidl::flat::Name::Key(library.library(), "badlookup");
  auto the_coded_type = gen.CodedTypeFor(the_struct_name);
  ASSERT_NOT_NULL(the_coded_type);
  auto the_struct_coded_type = static_cast<const fidl::coded::StructType*>(the_coded_type);
  ASSERT_EQ(the_struct_coded_type->elements.size(), 2);
  EXPECT_EQ(0xffffffffffffff00,
            std::get<uint64_t>(padding(the_struct_coded_type->elements[0]).mask));
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
  fidl::CodedTypesGenerator gen(library.library());
  gen.CompileCodedTypes();

  ASSERT_EQ(3, gen.coded_types().size());

  // This bool is used in the coding table of the MyTable table.
  auto type0 = gen.coded_types().at(0).get();
  EXPECT_STR_EQ("bool", type0->coded_name.c_str());
  EXPECT_TRUE(type0->is_coding_needed);
  ASSERT_EQ(fidl::coded::Type::Kind::kPrimitive, type0->kind);
  auto type0_primitive = static_cast<const fidl::coded::PrimitiveType*>(type0);
  EXPECT_EQ(fidl::types::PrimitiveSubtype::kBool, type0_primitive->subtype);

  auto type1 = gen.coded_types().at(1).get();
  EXPECT_STR_EQ("int32", type1->coded_name.c_str());
  EXPECT_TRUE(type1->is_coding_needed);
  ASSERT_EQ(fidl::coded::Type::Kind::kPrimitive, type1->kind);
  auto type1_primitive = static_cast<const fidl::coded::PrimitiveType*>(type1);
  EXPECT_EQ(fidl::types::PrimitiveSubtype::kInt32, type1_primitive->subtype);

  auto type3 = gen.coded_types().at(2).get();
  EXPECT_STR_EQ("Array42_4bool", type3->coded_name.c_str());
  EXPECT_TRUE(type3->is_coding_needed);
  ASSERT_EQ(fidl::coded::Type::Kind::kArray, type3->kind);
  auto type3_array = static_cast<const fidl::coded::ArrayType*>(type3);
  EXPECT_EQ(42, type3_array->size_v1);
  EXPECT_EQ(42, type3_array->size_v2);
  ASSERT_EQ(fidl::coded::Type::Kind::kPrimitive, type3_array->element_type->kind);
  auto type3_array_element_type =
      static_cast<const fidl::coded::PrimitiveType*>(type3_array->element_type);
  EXPECT_EQ(fidl::types::PrimitiveSubtype::kBool, type3_array_element_type->subtype);

  auto name_table = fidl::flat::Name::Key(library.library(), "MyTable");
  auto type_table = gen.CodedTypeFor(name_table);
  ASSERT_NOT_NULL(type_table);
  EXPECT_STR_EQ("example_MyTable", type_table->coded_name.c_str());
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
  EXPECT_STR_EQ("example/MyTable", type_table_table->qname.c_str());
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
  fidl::CodedTypesGenerator gen(library.library());
  gen.CompileCodedTypes();

  ASSERT_EQ(0, gen.coded_types().size());
  {
    auto name_bits = fidl::flat::Name::Key(library.library(), "StrictBits");
    auto type_bits = gen.CodedTypeFor(name_bits);
    ASSERT_NOT_NULL(type_bits);
    EXPECT_STR_EQ("example_StrictBits", type_bits->coded_name.c_str());
    EXPECT_TRUE(type_bits->is_coding_needed);
    ASSERT_EQ(fidl::coded::Type::Kind::kBits, type_bits->kind);
    auto type_bits_bits = static_cast<const fidl::coded::BitsType*>(type_bits);
    EXPECT_EQ(fidl::types::PrimitiveSubtype::kUint8, type_bits_bits->subtype);
    EXPECT_EQ(fidl::types::Strictness::kStrict, type_bits_bits->strictness);
    EXPECT_EQ(0x1u | 0x10u, type_bits_bits->mask);
  }
  {
    auto name_bits = fidl::flat::Name::Key(library.library(), "FlexibleBits");
    auto type_bits = gen.CodedTypeFor(name_bits);
    ASSERT_NOT_NULL(type_bits);
    EXPECT_STR_EQ("example_FlexibleBits", type_bits->coded_name.c_str());
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
  fidl::CodedTypesGenerator gen(library.library());
  gen.CompileCodedTypes();

  ASSERT_EQ(0, gen.coded_types().size());
  {
    auto name_enum = fidl::flat::Name::Key(library.library(), "StrictEnum");
    auto type_enum = gen.CodedTypeFor(name_enum);
    ASSERT_NOT_NULL(type_enum);
    EXPECT_STR_EQ("example_StrictEnum", type_enum->coded_name.c_str());
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
    auto name_enum = fidl::flat::Name::Key(library.library(), "FlexibleEnum");
    auto type_enum = gen.CodedTypeFor(name_enum);
    ASSERT_NOT_NULL(type_enum);
    EXPECT_STR_EQ("example_FlexibleEnum", type_enum->coded_name.c_str());
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
  fidl::CodedTypesGenerator gen(library.library());
  gen.CompileCodedTypes();

  auto name = fidl::flat::Name::Key(library.library(), "MyUnion");
  auto type = gen.CodedTypeFor(name);
  ASSERT_NOT_NULL(type);
  EXPECT_STR_EQ("example_MyUnion", type->coded_name.c_str());
  EXPECT_TRUE(type->is_coding_needed);
  ASSERT_EQ(fidl::coded::Type::Kind::kXUnion, type->kind);

  auto coded_union = static_cast<const fidl::coded::XUnionType*>(type);
  ASSERT_EQ(3, coded_union->fields.size());

  auto union_field0 = coded_union->fields.at(0);
  ASSERT_NOT_NULL(union_field0.type);
  auto union_field0_struct = static_cast<const fidl::coded::StructType*>(union_field0.type);
  EXPECT_STR_EQ("example/First", union_field0_struct->qname.c_str());

  auto union_field1 = coded_union->fields.at(1);
  ASSERT_NULL(union_field1.type);

  auto union_field2 = coded_union->fields.at(2);
  ASSERT_NOT_NULL(union_field2.type);
  auto union_field2_struct = static_cast<const fidl::coded::StructType*>(union_field2.type);
  EXPECT_STR_EQ("example/Second", union_field2_struct->qname.c_str());
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
  fidl::CodedTypesGenerator gen(library.library());
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
  fidl::CodedTypesGenerator gen(library.library());
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
  fidl::CodedTypesGenerator gen(library.library());
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
  fidl::CodedTypesGenerator gen(library.library());
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
  fidl::CodedTypesGenerator gen(library.library());
  gen.CompileCodedTypes();

  {
    auto name = fidl::flat::Name::Key(library.library(), "ResourceUnion");
    auto type = gen.CodedTypeFor(name);
    ASSERT_NOT_NULL(type);
    ASSERT_EQ(fidl::coded::Type::Kind::kXUnion, type->kind);

    auto coded_union = static_cast<const fidl::coded::XUnionType*>(type);
    EXPECT_EQ(fidl::types::Resourceness::kResource, coded_union->resourceness);
  }

  {
    auto name = fidl::flat::Name::Key(library.library(), "NonResourceUnion");
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
  fidl::CodedTypesGenerator gen(library.library());
  gen.CompileCodedTypes();

  {
    auto name = fidl::flat::Name::Key(library.library(), "ResourceTable");
    auto type = gen.CodedTypeFor(name);
    ASSERT_NOT_NULL(type);
    ASSERT_EQ(fidl::coded::Type::Kind::kTable, type->kind);

    auto coded_table = static_cast<const fidl::coded::TableType*>(type);
    EXPECT_EQ(fidl::types::Resourceness::kResource, coded_table->resourceness);
  }

  {
    auto name = fidl::flat::Name::Key(library.library(), "NonResourceTable");
    auto type = gen.CodedTypeFor(name);
    ASSERT_NOT_NULL(type);
    ASSERT_EQ(fidl::coded::Type::Kind::kTable, type->kind);

    auto coded_table = static_cast<const fidl::coded::TableType*>(type);
    EXPECT_EQ(fidl::types::Resourceness::kValue, coded_table->resourceness);
  }
}

}  // namespace
