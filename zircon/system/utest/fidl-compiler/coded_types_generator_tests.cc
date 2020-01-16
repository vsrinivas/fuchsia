// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/tables_generator.h>
#include <unittest/unittest.h>

#include "fidl/coded_types_generator.h"
#include "test_library.h"

namespace {

bool CodedTypesOfArrays() {
  BEGIN_TEST;

  TestLibrary library(R"FIDL(
library example;

struct Arrays {
  array<uint8>:7 prime;
  array<array<uint8>:7>:11 next_prime;
  array<array<array<uint8>:7>:11>:13 next_next_prime;
};
)FIDL");
  ASSERT_TRUE(library.Compile());
  fidl::CodedTypesGenerator gen(library.library());
  gen.CompileCodedTypes(fidl::WireFormat::kOld);

  ASSERT_EQ(4, gen.coded_types().size());

  auto type0 = gen.coded_types().at(0).get();
  ASSERT_STR_EQ("uint8", type0->coded_name.c_str());
  ASSERT_FALSE(type0->coding_needed);
  ASSERT_EQ(fidl::coded::Type::Kind::kPrimitive, type0->kind);
  auto type0_primitive = static_cast<const fidl::coded::PrimitiveType*>(type0);
  ASSERT_EQ(fidl::types::PrimitiveSubtype::kUint8, type0_primitive->subtype);

  auto type1 = gen.coded_types().at(1).get();
  ASSERT_STR_EQ("Array7_5uint8", type1->coded_name.c_str());
  ASSERT_FALSE(type1->coding_needed);
  ASSERT_EQ(fidl::coded::Type::Kind::kArray, type1->kind);
  auto type1_array = static_cast<const fidl::coded::ArrayType*>(type1);
  ASSERT_EQ(1, type1_array->element_size);
  ASSERT_EQ(type0, type1_array->element_type);

  auto type2 = gen.coded_types().at(2).get();
  ASSERT_STR_EQ("Array77_13Array7_5uint8", type2->coded_name.c_str());
  ASSERT_FALSE(type2->coding_needed);
  ASSERT_EQ(fidl::coded::Type::Kind::kArray, type2->kind);
  auto type2_array = static_cast<const fidl::coded::ArrayType*>(type2);
  ASSERT_EQ(7 * 1, type2_array->element_size);
  ASSERT_EQ(type1, type2_array->element_type);

  auto type3 = gen.coded_types().at(3).get();
  ASSERT_STR_EQ("Array1001_23Array77_13Array7_5uint8", type3->coded_name.c_str());
  ASSERT_FALSE(type3->coding_needed);
  ASSERT_EQ(fidl::coded::Type::Kind::kArray, type3->kind);
  auto type3_array = static_cast<const fidl::coded::ArrayType*>(type3);
  ASSERT_EQ(11 * 7 * 1, type3_array->element_size);
  ASSERT_EQ(type2, type3_array->element_type);

  END_TEST;
}

bool CodedTypesOfVectors() {
  BEGIN_TEST;

  TestLibrary library(R"FIDL(
library example;

struct SomeStruct {};

struct Vectors {
  vector<SomeStruct>:10 bytes1;
  vector<vector<SomeStruct>:10>:20 bytes12;
};
)FIDL");
  ASSERT_TRUE(library.Compile());
  fidl::CodedTypesGenerator gen(library.library());
  gen.CompileCodedTypes(fidl::WireFormat::kOld);

  auto name_some_struct = fidl::flat::Name(library.library(), "SomeStruct");
  auto type_some_struct = gen.CodedTypeFor(&name_some_struct);
  ASSERT_NONNULL(type_some_struct);
  ASSERT_STR_EQ("example_SomeStruct", type_some_struct->coded_name.c_str());
  ASSERT_TRUE(type_some_struct->coding_needed);
  ASSERT_EQ(fidl::coded::Type::Kind::kStruct, type_some_struct->kind);
  auto type_some_struct_struct = static_cast<const fidl::coded::StructType*>(type_some_struct);
  ASSERT_EQ(0, type_some_struct_struct->fields.size());
  ASSERT_STR_EQ("example/SomeStruct", type_some_struct_struct->qname.c_str());
  ASSERT_NULL(type_some_struct_struct->maybe_reference_type);
  ASSERT_EQ(1, type_some_struct_struct->size);
  ASSERT_EQ(0, type_some_struct_struct->max_out_of_line);
  ASSERT_EQ(false, type_some_struct_struct->contains_union);

  ASSERT_EQ(2, gen.coded_types().size());

  auto type0 = gen.coded_types().at(0).get();
  ASSERT_STR_EQ("Vector10nonnullable18example_SomeStruct", type0->coded_name.c_str());
  ASSERT_TRUE(type0->coding_needed);
  ASSERT_EQ(fidl::coded::Type::Kind::kVector, type0->kind);
  auto type0_vector = static_cast<const fidl::coded::VectorType*>(type0);
  ASSERT_EQ(type_some_struct, type0_vector->element_type);
  ASSERT_EQ(10, type0_vector->max_count);
  ASSERT_EQ(1, type0_vector->element_size);
  ASSERT_EQ(fidl::types::Nullability::kNonnullable, type0_vector->nullability);

  auto type1 = gen.coded_types().at(1).get();
  ASSERT_STR_EQ("Vector20nonnullable39Vector10nonnullable18example_SomeStruct",
                type1->coded_name.c_str());
  ASSERT_TRUE(type1->coding_needed);
  ASSERT_EQ(fidl::coded::Type::Kind::kVector, type1->kind);
  auto type1_vector = static_cast<const fidl::coded::VectorType*>(type1);
  ASSERT_EQ(type0, type1_vector->element_type);
  ASSERT_EQ(20, type1_vector->max_count);
  ASSERT_EQ(16, type1_vector->element_size);
  ASSERT_EQ(fidl::types::Nullability::kNonnullable, type1_vector->nullability);

  END_TEST;
}

bool CodedTypesOfProtocol() {
  BEGIN_TEST;

  TestLibrary library(R"FIDL(
library example;

protocol SomeProtocol {};

protocol UseOfProtocol {
    Call(SomeProtocol arg);
};
)FIDL");
  ASSERT_TRUE(library.Compile());
  fidl::CodedTypesGenerator gen(library.library());
  gen.CompileCodedTypes(fidl::WireFormat::kOld);

  ASSERT_EQ(2, gen.coded_types().size());

  auto type0 = gen.coded_types().at(0).get();
  ASSERT_STR_EQ("Protocol20example_SomeProtocolnonnullable", type0->coded_name.c_str());
  ASSERT_TRUE(type0->coding_needed);
  ASSERT_EQ(fidl::coded::Type::Kind::kProtocolHandle, type0->kind);
  ASSERT_EQ(4, type0->size);
  auto type0_ihandle = static_cast<const fidl::coded::ProtocolHandleType*>(type0);
  ASSERT_EQ(fidl::types::Nullability::kNonnullable, type0_ihandle->nullability);

  auto type1 = gen.coded_types().at(1).get();
  ASSERT_STR_EQ("example_UseOfProtocolCallRequest", type1->coded_name.c_str());
  ASSERT_TRUE(type1->coding_needed);
  ASSERT_EQ(fidl::coded::Type::Kind::kMessage, type1->kind);
  ASSERT_EQ(24, type1->size);
  auto type1_message = static_cast<const fidl::coded::MessageType*>(type1);
  ASSERT_STR_EQ("example/UseOfProtocolCallRequest", type1_message->qname.c_str());
  ASSERT_EQ(1, type1_message->fields.size());

  auto type1_message_field0 = type1_message->fields.at(0);
  ASSERT_EQ(16, type1_message_field0.offset);
  ASSERT_EQ(type0, type1_message_field0.type);

  END_TEST;
}

bool CodedTypesOfRequestOfProtocol() {
  BEGIN_TEST;

  TestLibrary library(R"FIDL(
library example;

protocol SomeProtocol {};

protocol UseOfRequestOfProtocol {
    Call(request<SomeProtocol> arg);
};
)FIDL");
  ASSERT_TRUE(library.Compile());
  fidl::CodedTypesGenerator gen(library.library());
  gen.CompileCodedTypes(fidl::WireFormat::kOld);

  ASSERT_EQ(2, gen.coded_types().size());

  auto type0 = gen.coded_types().at(0).get();
  ASSERT_STR_EQ("Request20example_SomeProtocolnonnullable", type0->coded_name.c_str());
  ASSERT_TRUE(type0->coding_needed);
  ASSERT_EQ(fidl::coded::Type::Kind::kRequestHandle, type0->kind);
  ASSERT_EQ(4, type0->size);
  auto type0_ihandle = static_cast<const fidl::coded::RequestHandleType*>(type0);
  ASSERT_EQ(fidl::types::Nullability::kNonnullable, type0_ihandle->nullability);

  auto type1 = gen.coded_types().at(1).get();
  ASSERT_STR_EQ("example_UseOfRequestOfProtocolCallRequest", type1->coded_name.c_str());
  ASSERT_TRUE(type1->coding_needed);
  ASSERT_EQ(fidl::coded::Type::Kind::kMessage, type1->kind);
  ASSERT_EQ(24, type1->size);
  auto type1_message = static_cast<const fidl::coded::MessageType*>(type1);
  ASSERT_STR_EQ("example/UseOfRequestOfProtocolCallRequest", type1_message->qname.c_str());
  ASSERT_EQ(1, type1_message->fields.size());

  auto type1_message_field0 = type1_message->fields.at(0);
  ASSERT_EQ(16, type1_message_field0.offset);
  ASSERT_EQ(type0, type1_message_field0.type);

  END_TEST;
}

// The code between |CodedTypesOfXUnions| and |CodedTypesOfNullableXUnions| is now very similar
// because the compiler emits both the non-nullable and nullable xunion types regardless of whether
// it is used in the library in which it was defined.
bool CodedTypesOfXUnions() {
  BEGIN_TEST;

  TestLibrary library(R"FIDL(
library example;

xunion MyXUnion {
  1: bool foo;
  2: int32 bar;
};
)FIDL");
  ASSERT_TRUE(library.Compile());
  fidl::CodedTypesGenerator gen(library.library());
  gen.CompileCodedTypes(fidl::WireFormat::kOld);

  ASSERT_EQ(3, gen.coded_types().size());

  auto type0 = gen.coded_types().at(0).get();
  ASSERT_STR_EQ("example_MyXUnionNullableRef", type0->coded_name.c_str());
  ASSERT_TRUE(type0->coding_needed);
  ASSERT_EQ(fidl::coded::Type::Kind::kXUnion, type0->kind);
  auto nullable_xunion = static_cast<const fidl::coded::XUnionType*>(type0);
  ASSERT_EQ(fidl::types::Nullability::kNullable, nullable_xunion->nullability);

  auto type1 = gen.coded_types().at(1).get();
  ASSERT_STR_EQ("bool", type1->coded_name.c_str());
  ASSERT_TRUE(type1->coding_needed);
  ASSERT_EQ(fidl::coded::Type::Kind::kPrimitive, type1->kind);
  auto type2_primitive = static_cast<const fidl::coded::PrimitiveType*>(type1);
  ASSERT_EQ(fidl::types::PrimitiveSubtype::kBool, type2_primitive->subtype);

  auto type2 = gen.coded_types().at(2).get();
  ASSERT_STR_EQ("int32", type2->coded_name.c_str());
  ASSERT_TRUE(type2->coding_needed);
  ASSERT_EQ(fidl::coded::Type::Kind::kPrimitive, type2->kind);
  auto type1_primitive = static_cast<const fidl::coded::PrimitiveType*>(type2);
  ASSERT_EQ(fidl::types::PrimitiveSubtype::kInt32, type1_primitive->subtype);

  auto name = fidl::flat::Name(library.library(), "MyXUnion");
  auto type = gen.CodedTypeFor(&name);
  ASSERT_NONNULL(type);
  ASSERT_STR_EQ("example_MyXUnion", type->coded_name.c_str());
  ASSERT_TRUE(type->coding_needed);
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
  ASSERT_NONNULL(coded_xunion->maybe_reference_type);

  END_TEST;
}

// This mostly exists to make sure that the same nullable objects aren't
// represented more than once in the coding tables.
bool CodedTypesOfNullablePointers() {
  BEGIN_TEST;

  TestLibrary library(R"FIDL(
library example;

struct MyStruct {
  bool foo;
  int32 bar;
};

union MyUnion {
  1: bool foo;
  2: int32 bar;
};

xunion MyXUnion {
  1: bool foo;
  2: int32 bar;
};

struct Wrapper1 {
  MyStruct? ms;
  MyUnion? mu;
  MyXUnion? xu;
};

// This ensures that MyXUnion? doesn't show up twice in the coded types.
struct Wrapper2 {
  MyStruct? ms;
  MyUnion? mu;
  MyXUnion? xu;
};

)FIDL");
  ASSERT_TRUE(library.Compile());
  fidl::CodedTypesGenerator gen(library.library());
  gen.CompileCodedTypes(fidl::WireFormat::kOld);

  // 7 == size of {bool-outside-of-envelope, bool-inside-of-envelope,
  // int32-outside-of-envelope, int32-inside-of-envelope, MyStruct?, MyUnion?,
  // MyXUnion?}, which is all the coded types in the example.
  ASSERT_EQ(7, gen.coded_types().size());

  END_TEST;
}

bool UnboundedOutOfLineContainsUnion() {
  BEGIN_TEST;

  TestLibrary library(R"FIDL(
library example;

union Foo {
  1: int32 bar;
  2: int16 baz;
};

struct WithString {
  Foo foo;
  string bar;
};

)FIDL");
  ASSERT_TRUE(library.Compile());
  fidl::CodedTypesGenerator gen(library.library());
  gen.CompileCodedTypes(fidl::WireFormat::kV1NoEe);

  auto name_with_string = fidl::flat::Name(library.library(), "WithString");
  auto type_with_string = gen.CodedTypeFor(&name_with_string);
  auto type_with_string_struct = static_cast<const fidl::coded::StructType*>(type_with_string);
  ASSERT_EQ(40, type_with_string_struct->size);
  ASSERT_EQ(0xFFFFFFFF, type_with_string_struct->max_out_of_line);
  ASSERT_TRUE(type_with_string_struct->contains_union);

  END_TEST;
}

bool CodedTypesOfStructsWithPaddings() {
  BEGIN_TEST;

  TestLibrary library(R"FIDL(
library example;

struct BoolAndInt32 {
  bool foo;
  // 3 bytes of padding here.
  int32 bar;
};

struct Complex {
  int32 i32;
  bool b1;
  // 3 bytes of padding here.
  int64 i64;
  int16 i16;
  // 6 bytes of padding here.
};

)FIDL");
  ASSERT_TRUE(library.Compile());
  fidl::CodedTypesGenerator gen(library.library());
  gen.CompileCodedTypes(fidl::WireFormat::kOld);

  ASSERT_EQ(4, gen.coded_types().size());

  auto type0 = gen.coded_types().at(0).get();
  EXPECT_STR_EQ("int32", type0->coded_name.c_str());
  EXPECT_FALSE(type0->coding_needed);
  auto type1 = gen.coded_types().at(1).get();
  EXPECT_STR_EQ("bool", type1->coded_name.c_str());
  EXPECT_FALSE(type1->coding_needed);
  auto type2 = gen.coded_types().at(2).get();
  EXPECT_STR_EQ("int64", type2->coded_name.c_str());
  EXPECT_FALSE(type2->coding_needed);
  auto type3 = gen.coded_types().at(3).get();
  EXPECT_STR_EQ("int16", type3->coded_name.c_str());
  EXPECT_FALSE(type3->coding_needed);

  auto name_bool_and_int32 = fidl::flat::Name(library.library(), "BoolAndInt32");
  auto type_bool_and_int32 = gen.CodedTypeFor(&name_bool_and_int32);
  ASSERT_NONNULL(type_bool_and_int32);
  ASSERT_STR_EQ("example_BoolAndInt32", type_bool_and_int32->coded_name.c_str());
  auto type_bool_and_int32_struct =
      static_cast<const fidl::coded::StructType*>(type_bool_and_int32);
  EXPECT_EQ(type_bool_and_int32_struct->fields.size(), 2);
  EXPECT_EQ(type_bool_and_int32_struct->fields[0].type, nullptr);
  EXPECT_EQ(type_bool_and_int32_struct->fields[0].offset, 0);
  EXPECT_EQ(type_bool_and_int32_struct->fields[0].padding, 3);
  EXPECT_EQ(type_bool_and_int32_struct->fields[1].type, nullptr);
  EXPECT_EQ(type_bool_and_int32_struct->fields[1].offset, 4);
  EXPECT_EQ(type_bool_and_int32_struct->fields[1].padding, 0);

  auto name_complex = fidl::flat::Name(library.library(), "Complex");
  auto type_complex = gen.CodedTypeFor(&name_complex);
  ASSERT_NONNULL(type_complex);
  ASSERT_STR_EQ("example_Complex", type_complex->coded_name.c_str());
  auto type_complex_struct = static_cast<const fidl::coded::StructType*>(type_complex);
  EXPECT_EQ(type_complex_struct->fields.size(), 4);
  EXPECT_EQ(type_complex_struct->fields[0].type, nullptr);
  EXPECT_EQ(type_complex_struct->fields[0].offset, 0);
  EXPECT_EQ(type_complex_struct->fields[0].padding, 0);
  EXPECT_EQ(type_complex_struct->fields[1].type, nullptr);
  EXPECT_EQ(type_complex_struct->fields[1].offset, 4);
  EXPECT_EQ(type_complex_struct->fields[1].padding, 3);
  EXPECT_EQ(type_complex_struct->fields[2].type, nullptr);
  EXPECT_EQ(type_complex_struct->fields[2].offset, 8);
  EXPECT_EQ(type_complex_struct->fields[2].padding, 0);
  EXPECT_EQ(type_complex_struct->fields[3].type, nullptr);
  EXPECT_EQ(type_complex_struct->fields[3].offset, 16);
  EXPECT_EQ(type_complex_struct->fields[3].padding, 6);

  END_TEST;
}

// The code between |CodedTypesOfXUnions| and |CodedTypesOfNullableXUnions| is now very similar
// because the compiler emits both the non-nullable and nullable xunion types regardless of whether
// it is used in the library in which it was defined.
bool CodedTypesOfNullableXUnions() {
  BEGIN_TEST;

  TestLibrary library(R"FIDL(
library example;

xunion MyXUnion {
  1: bool foo;
  2: int32 bar;
};

struct Wrapper1 {
  MyXUnion? xu;
};

// This ensures that MyXUnion? doesn't show up twice in the coded types.
struct Wrapper2 {
  MyXUnion? xu;
};

)FIDL");
  ASSERT_TRUE(library.Compile());
  fidl::CodedTypesGenerator gen(library.library());
  gen.CompileCodedTypes(fidl::WireFormat::kOld);

  // 3 == size of {bool, int32, MyXUnion?}, which is all of the types used in
  // the example.
  ASSERT_EQ(3, gen.coded_types().size());

  auto type0 = gen.coded_types().at(0).get();
  ASSERT_STR_EQ("example_MyXUnionNullableRef", type0->coded_name.c_str());
  ASSERT_TRUE(type0->coding_needed);
  ASSERT_EQ(fidl::coded::Type::Kind::kXUnion, type0->kind);
  auto nullable_xunion = static_cast<const fidl::coded::XUnionType*>(type0);
  ASSERT_EQ(fidl::types::Nullability::kNullable, nullable_xunion->nullability);

  auto type1 = gen.coded_types().at(1).get();
  ASSERT_STR_EQ("bool", type1->coded_name.c_str());
  ASSERT_TRUE(type1->coding_needed);
  ASSERT_EQ(fidl::coded::Type::Kind::kPrimitive, type1->kind);
  auto type2_primitive = static_cast<const fidl::coded::PrimitiveType*>(type1);
  ASSERT_EQ(fidl::types::PrimitiveSubtype::kBool, type2_primitive->subtype);

  auto type2 = gen.coded_types().at(2).get();
  ASSERT_STR_EQ("int32", type2->coded_name.c_str());
  ASSERT_TRUE(type2->coding_needed);
  ASSERT_EQ(fidl::coded::Type::Kind::kPrimitive, type2->kind);
  auto type1_primitive = static_cast<const fidl::coded::PrimitiveType*>(type2);
  ASSERT_EQ(fidl::types::PrimitiveSubtype::kInt32, type1_primitive->subtype);

  END_TEST;
}

bool CodedTypesOfTables() {
  BEGIN_TEST;

  TestLibrary library(R"FIDL(
library example;

table MyTable {
  1: bool foo;
  2: int32 bar;
  3: array<bool>:42 baz;
};
)FIDL");
  ASSERT_TRUE(library.Compile());
  fidl::CodedTypesGenerator gen(library.library());
  gen.CompileCodedTypes(fidl::WireFormat::kOld);

  ASSERT_EQ(4, gen.coded_types().size());

  // This bool is used in the coding table of the MyTable table.
  auto type0 = gen.coded_types().at(0).get();
  ASSERT_STR_EQ("bool", type0->coded_name.c_str());
  ASSERT_TRUE(type0->coding_needed);
  ASSERT_EQ(fidl::coded::Type::Kind::kPrimitive, type0->kind);
  auto type0_primitive = static_cast<const fidl::coded::PrimitiveType*>(type0);
  ASSERT_EQ(fidl::types::PrimitiveSubtype::kBool, type0_primitive->subtype);

  auto type1 = gen.coded_types().at(1).get();
  ASSERT_STR_EQ("int32", type1->coded_name.c_str());
  ASSERT_TRUE(type1->coding_needed);
  ASSERT_EQ(fidl::coded::Type::Kind::kPrimitive, type1->kind);
  auto type1_primitive = static_cast<const fidl::coded::PrimitiveType*>(type1);
  ASSERT_EQ(fidl::types::PrimitiveSubtype::kInt32, type1_primitive->subtype);

  // This bool is part of array<bool>; it will not map to any coding table.
  auto type2 = gen.coded_types().at(2).get();
  ASSERT_STR_EQ("bool", type2->coded_name.c_str());
  ASSERT_FALSE(type2->coding_needed);
  ASSERT_EQ(fidl::coded::Type::Kind::kPrimitive, type2->kind);
  auto type2_primitive = static_cast<const fidl::coded::PrimitiveType*>(type0);
  ASSERT_EQ(fidl::types::PrimitiveSubtype::kBool, type2_primitive->subtype);

  auto type3 = gen.coded_types().at(3).get();
  ASSERT_STR_EQ("Array42_4bool", type3->coded_name.c_str());
  ASSERT_TRUE(type3->coding_needed);
  ASSERT_EQ(fidl::coded::Type::Kind::kArray, type3->kind);
  auto type3_array = static_cast<const fidl::coded::ArrayType*>(type3);
  ASSERT_EQ(42, type3_array->size);
  ASSERT_EQ(fidl::coded::Type::Kind::kPrimitive, type3_array->element_type->kind);
  auto type3_array_element_type =
      static_cast<const fidl::coded::PrimitiveType*>(type3_array->element_type);
  ASSERT_EQ(fidl::types::PrimitiveSubtype::kBool, type3_array_element_type->subtype);

  auto name_table = fidl::flat::Name(library.library(), "MyTable");
  auto type_table = gen.CodedTypeFor(&name_table);
  ASSERT_NONNULL(type_table);
  ASSERT_STR_EQ("example_MyTable", type_table->coded_name.c_str());
  ASSERT_TRUE(type_table->coding_needed);
  ASSERT_EQ(fidl::coded::Type::Kind::kTable, type_table->kind);
  auto type_table_table = static_cast<const fidl::coded::TableType*>(type_table);
  ASSERT_EQ(3, type_table_table->fields.size());
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
  ASSERT_STR_EQ("example/MyTable", type_table_table->qname.c_str());

  END_TEST;
}

bool CodedTypesOfBits() {
  BEGIN_TEST;

  TestLibrary library(R"FIDL(
library example;

bits MyBits : uint8 {
    HELLO = 0x1;
    WORLD = 0x10;
};

)FIDL");
  ASSERT_TRUE(library.Compile());
  fidl::CodedTypesGenerator gen(library.library());
  gen.CompileCodedTypes(fidl::WireFormat::kOld);

  ASSERT_EQ(0, gen.coded_types().size());
  auto name_bits = fidl::flat::Name(library.library(), "MyBits");
  auto type_bits = gen.CodedTypeFor(&name_bits);
  ASSERT_NONNULL(type_bits);
  ASSERT_STR_EQ("example_MyBits", type_bits->coded_name.c_str());
  ASSERT_TRUE(type_bits->coding_needed);
  ASSERT_EQ(fidl::coded::Type::Kind::kBits, type_bits->kind);
  auto type_bits_bits = static_cast<const fidl::coded::BitsType*>(type_bits);
  ASSERT_EQ(fidl::types::PrimitiveSubtype::kUint8, type_bits_bits->subtype);
  ASSERT_EQ(0x1u | 0x10u, type_bits_bits->mask);

  END_TEST;
}

bool CodedTypesOfEnum() {
  BEGIN_TEST;

  TestLibrary library(R"FIDL(
library example;

enum MyEnum : uint16 {
    HELLO = 0x1;
    WORLD = 0x10;
};

)FIDL");
  ASSERT_TRUE(library.Compile());
  fidl::CodedTypesGenerator gen(library.library());
  gen.CompileCodedTypes(fidl::WireFormat::kOld);

  ASSERT_EQ(0, gen.coded_types().size());

  ASSERT_EQ(0, gen.coded_types().size());
  auto name_enum = fidl::flat::Name(library.library(), "MyEnum");
  auto type_enum = gen.CodedTypeFor(&name_enum);
  ASSERT_NONNULL(type_enum);
  ASSERT_STR_EQ("example_MyEnum", type_enum->coded_name.c_str());
  ASSERT_TRUE(type_enum->coding_needed);
  ASSERT_EQ(fidl::coded::Type::Kind::kEnum, type_enum->kind);
  auto type_enum_enum = static_cast<const fidl::coded::EnumType*>(type_enum);
  ASSERT_EQ(fidl::types::PrimitiveSubtype::kUint16, type_enum_enum->subtype);

  ASSERT_EQ(2, type_enum_enum->members.size());
  ASSERT_EQ(0x1, type_enum_enum->members[0]);
  ASSERT_EQ(0x10, type_enum_enum->members[1]);

  END_TEST;
}

bool CodedTypesOfUnionsWithReverseOrdinals() {
  BEGIN_TEST;

  TestLibrary library(R"FIDL(
library example;

struct First {};
struct Second {};

union MyUnion {
  3: Second second;
  2: reserved;
  1: First first;
};
)FIDL");
  ASSERT_TRUE(library.Compile());
  fidl::CodedTypesGenerator gen(library.library());
  gen.CompileCodedTypes(fidl::WireFormat::kOld);

  auto name = fidl::flat::Name(library.library(), "MyUnion");
  auto type = gen.CodedTypeFor(&name);
  ASSERT_NONNULL(type);
  ASSERT_STR_EQ("example_MyUnion", type->coded_name.c_str());
  ASSERT_TRUE(type->coding_needed);
  ASSERT_EQ(fidl::coded::Type::Kind::kUnion, type->kind);

  auto coded_union = static_cast<const fidl::coded::UnionType*>(type);
  ASSERT_EQ(2, coded_union->members.size());

  auto union_field0 = coded_union->members.at(0);
  ASSERT_EQ(1, union_field0.xunion_ordinal);
  ASSERT_NONNULL(union_field0.type);
  auto union_field0_struct = static_cast<const fidl::coded::StructType*>(union_field0.type);
  ASSERT_STR_EQ("example/First", union_field0_struct->qname.c_str());

  auto union_field1 = coded_union->members.at(1);
  ASSERT_EQ(3, union_field1.xunion_ordinal);
  ASSERT_NONNULL(union_field1.type);
  auto union_field1_struct = static_cast<const fidl::coded::StructType*>(union_field1.type);
  ASSERT_STR_EQ("example/Second", union_field1_struct->qname.c_str());

  END_TEST;
}

bool field_num_in_struct() {
  BEGIN_TEST;

  auto wire_formats = std::vector<fidl::WireFormat>{
      fidl::WireFormat::kOld,
      fidl::WireFormat::kV1NoEe,
  };
  for (auto wire_format : wire_formats) {
    TestLibrary library(R"FIDL(
library example;

struct MyStruct {
    uint64        f0;
    vector<uint8> f1; // coded field 0
    uint64        f2;
    vector<uint8> f3; // coded field 1
    vector<uint8> f4; // coded field 2
    uint64        f5;
    vector<uint8> f6; // coded field 3
    vector<uint8> f7; // coded field 4
    vector<uint8> f8; // coded field 5
    uint64        f9;
};

  )FIDL");
    ASSERT_TRUE(library.Compile());
    fidl::CodedTypesGenerator gen(library.library());
    gen.CompileCodedTypes(wire_format);

    ASSERT_NE(0, gen.coded_types().size());

    auto name_struct = fidl::flat::Name(library.library(), "MyStruct");
    auto coded_type = gen.CodedTypeFor(&name_struct);
    ASSERT_NONNULL(coded_type);
    ASSERT_EQ(fidl::coded::Type::Kind::kStruct, coded_type->kind);
    auto coded_struct_type = static_cast<const fidl::coded::StructType*>(coded_type);

    ASSERT_EQ(10, coded_struct_type->fields.size());

    EXPECT_EQ(1, coded_struct_type->fields[1].field_num);
    EXPECT_EQ(3, coded_struct_type->fields[3].field_num);
    EXPECT_EQ(4, coded_struct_type->fields[4].field_num);
    EXPECT_EQ(6, coded_struct_type->fields[6].field_num);
    EXPECT_EQ(7, coded_struct_type->fields[7].field_num);
    EXPECT_EQ(8, coded_struct_type->fields[8].field_num);
  }

  END_TEST;
}

bool field_num_in_message() {
  BEGIN_TEST;

  auto wire_formats = std::vector<fidl::WireFormat>{
      fidl::WireFormat::kOld,
      fidl::WireFormat::kV1NoEe,
  };
  for (auto wire_format : wire_formats) {
    TestLibrary library(R"FIDL(
library example;

protocol MyProtocol {
    MyMethod(
      uint64        f0,
      vector<uint8> f1, // coded field 0
      uint64        f2,
      vector<uint8> f3, // coded field 1
      vector<uint8> f4, // coded field 2
      uint64        f5,
      vector<uint8> f6, // coded field 3
      vector<uint8> f7, // coded field 4
      vector<uint8> f8, // coded field 5
      uint64        f9
    ) -> (
      uint64        f0,
      vector<uint8> f1, // coded field 0
      vector<uint8> f2, // coded field 1
      vector<uint8> f3, // coded field 2
      uint64        f4,
      vector<uint8> f5, // coded field 3
      vector<uint8> f6, // coded field 4
      uint64        f7,
      vector<uint8> f8, // coded field 5
      uint64        f9
    );
};

  )FIDL");
    ASSERT_TRUE(library.Compile());
    fidl::CodedTypesGenerator gen(library.library());
    gen.CompileCodedTypes(wire_format);

    ASSERT_NE(0, gen.coded_types().size());

    auto name_protocol = fidl::flat::Name(library.library(), "MyProtocol");
    auto coded_type = gen.CodedTypeFor(&name_protocol);
    ASSERT_NONNULL(coded_type);
    ASSERT_EQ(fidl::coded::Type::Kind::kProtocol, coded_type->kind);
    auto coded_protocol_type = static_cast<const fidl::coded::ProtocolType*>(coded_type);

    ASSERT_EQ(2, coded_protocol_type->messages_after_compile.size());

    const auto& request_message_type = coded_protocol_type->messages_after_compile[0];
    ASSERT_NONNULL(request_message_type);
    ASSERT_EQ(10, request_message_type->fields.size());

    EXPECT_EQ(1, request_message_type->fields[1].field_num);
    EXPECT_EQ(3, request_message_type->fields[3].field_num);
    EXPECT_EQ(4, request_message_type->fields[4].field_num);
    EXPECT_EQ(6, request_message_type->fields[6].field_num);
    EXPECT_EQ(7, request_message_type->fields[7].field_num);
    EXPECT_EQ(8, request_message_type->fields[8].field_num);

    const auto& response_message_type = coded_protocol_type->messages_after_compile[1];
    ASSERT_NONNULL(response_message_type);
    ASSERT_EQ(10, response_message_type->fields.size());

    EXPECT_EQ(1, response_message_type->fields[1].field_num);
    EXPECT_EQ(2, response_message_type->fields[2].field_num);
    EXPECT_EQ(3, response_message_type->fields[3].field_num);
    EXPECT_EQ(5, response_message_type->fields[5].field_num);
    EXPECT_EQ(6, response_message_type->fields[6].field_num);
    EXPECT_EQ(8, response_message_type->fields[8].field_num);
  }

  END_TEST;
}

bool check_duplicate_coded_type_names(const fidl::CodedTypesGenerator& gen) {
  BEGIN_HELPER;
  const auto types = gen.AllCodedTypes();
  for (auto const& type : types) {
    auto count = std::count_if(types.begin(), types.end(),
                               [&](auto& t) { return t->coded_name == type->coded_name; });
    ASSERT_EQ(count, 1, "Duplicate coded type name.");
  }

  END_HELPER;
}

bool duplicate_coded_types_two_unions() {
  BEGIN_TEST;
  TestLibrary library(R"FIDL(
library example;

union U1 {
  1: array<string>:2 hs;
};

union U2 {
  1: array<array<string>:2>:2 hss;
};
  )FIDL");
  ASSERT_TRUE(library.Compile());
  fidl::CodedTypesGenerator gen(library.library());
  gen.CompileCodedTypes(fidl::WireFormat::kV1NoEe);
  ASSERT_TRUE(check_duplicate_coded_type_names(gen));
  END_TEST;
}

bool duplicate_coded_types_union_array_array() {
  BEGIN_TEST;
  TestLibrary library(R"FIDL(
library example;

union Union {
    1: array<string>:2 hs;
    2: array<array<string>:2>:2 hss;
};
  )FIDL");
  ASSERT_TRUE(library.Compile());
  fidl::CodedTypesGenerator gen(library.library());
  gen.CompileCodedTypes(fidl::WireFormat::kV1NoEe);
  ASSERT_TRUE(check_duplicate_coded_type_names(gen));
  END_TEST;
}

bool duplicate_coded_types_union_vector_array() {
  BEGIN_TEST;
  TestLibrary library(R"FIDL(
library example;

union Union {
    1: array<string>:2 hs;
    2: vector<array<string>:2>:2 hss;
};
  )FIDL");
  ASSERT_TRUE(library.Compile());
  fidl::CodedTypesGenerator gen(library.library());
  gen.CompileCodedTypes(fidl::WireFormat::kV1NoEe);
  ASSERT_TRUE(check_duplicate_coded_type_names(gen));
  END_TEST;
}

bool duplicate_coded_types_table_array_array() {
  BEGIN_TEST;
  TestLibrary library(R"FIDL(
library example;

table Table {
    1: array<string>:2 hs;
    2: array<array<string>:2>:2 hss;
};
  )FIDL");
  ASSERT_TRUE(library.Compile());
  fidl::CodedTypesGenerator gen(library.library());
  gen.CompileCodedTypes(fidl::WireFormat::kV1NoEe);
  ASSERT_TRUE(check_duplicate_coded_type_names(gen));
  END_TEST;
}

}  // namespace

BEGIN_TEST_CASE(coded_types_generator_tests)

RUN_TEST(CodedTypesOfArrays);
RUN_TEST(CodedTypesOfVectors);
RUN_TEST(CodedTypesOfProtocol);
RUN_TEST(CodedTypesOfRequestOfProtocol);
RUN_TEST(CodedTypesOfXUnions);
RUN_TEST(CodedTypesOfNullablePointers);
RUN_TEST(CodedTypesOfStructsWithPaddings);
RUN_TEST(CodedTypesOfNullableXUnions);
RUN_TEST(CodedTypesOfTables);
RUN_TEST(CodedTypesOfBits);
RUN_TEST(CodedTypesOfEnum);
RUN_TEST(CodedTypesOfUnionsWithReverseOrdinals);
RUN_TEST(field_num_in_struct);
RUN_TEST(field_num_in_message);
RUN_TEST(UnboundedOutOfLineContainsUnion);
RUN_TEST(duplicate_coded_types_two_unions);
RUN_TEST(duplicate_coded_types_union_array_array);
RUN_TEST(duplicate_coded_types_union_vector_array);
RUN_TEST(duplicate_coded_types_table_array_array);

END_TEST_CASE(coded_types_generator_tests)
