// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl/internal.h>

#include <fidl/test/example/codingtables/c/fidl.h>
#include <zxtest/zxtest.h>

TEST(SomeStruct, CodingTable) {
  const fidl_type& type = fidl_test_example_codingtables_CodingSomeStructRequestTable;
  ASSERT_EQ(kFidlTypeStruct, type.type_tag);
  const FidlCodedStruct& request_struct = type.coded_struct;
  ASSERT_EQ(1, request_struct.field_count);
  ASSERT_STR_EQ("fidl.test.example.codingtables/CodingSomeStructRequest", request_struct.name);
  const FidlStructField& some_struct_field = request_struct.fields[0];
  // Transaction message header is 16 bytes.
  ASSERT_EQ(16, some_struct_field.offset);

  const fidl_type& some_struct_type = *some_struct_field.type;
  ASSERT_EQ(kFidlTypeStruct, some_struct_type.type_tag);
  const FidlCodedStruct& some_struct_table = some_struct_type.coded_struct;
  ASSERT_STR_EQ("fidl.test.example.codingtables/SomeStruct", some_struct_table.name);
  // Every field (including primitives without padding) has a coding table generated for it.
  ASSERT_EQ(2, some_struct_table.field_count);
  ASSERT_EQ(nullptr, some_struct_table.fields[0].type);
  ASSERT_EQ(nullptr, some_struct_table.fields[1].type);
  // When |type| is nullptr, |offset| stores the starting offset of the padding.
  ASSERT_EQ(1, some_struct_table.fields[0].offset);
  ASSERT_EQ(3, some_struct_table.fields[0].padding);
  ASSERT_EQ(8, some_struct_table.fields[1].offset);
  ASSERT_EQ(0, some_struct_table.fields[1].padding);
}

TEST(MyXUnion, CodingTableWhenNullable) {
  const fidl_type& type = fidl_test_example_codingtables_CodingMyXUnionRequestTable;
  ASSERT_EQ(kFidlTypeStruct, type.type_tag);
  const FidlCodedStruct& request_struct = type.coded_struct;
  ASSERT_EQ(1, request_struct.field_count);
  ASSERT_STR_EQ("fidl.test.example.codingtables/CodingMyXUnionRequest", request_struct.name);
  const FidlStructField& my_xunion_field = request_struct.fields[0];
  ASSERT_EQ(16, my_xunion_field.offset);

  const fidl_type& my_xunion_type = *my_xunion_field.type;
  ASSERT_EQ(kFidlTypeXUnion, my_xunion_type.type_tag);
  const FidlCodedXUnion& my_xunion_table = my_xunion_type.coded_xunion;

  // Please keep these assertions in the same order as FidlCodedXUnion's member variables.

  ASSERT_EQ(2, my_xunion_table.field_count);

  ASSERT_EQ(&fidl_internal_kBoolTable, my_xunion_table.fields[0].type);
  ASSERT_EQ(&fidl_internal_kInt32Table, my_xunion_table.fields[1].type);

  ASSERT_EQ(kFidlNullability_Nullable, my_xunion_table.nullable);

  ASSERT_STR_EQ("fidl.test.example.codingtables/MyXUnion", my_xunion_table.name);

  ASSERT_EQ(kFidlStrictness_Flexible, my_xunion_type.coded_xunion.strictness);
}

TEST(MyStrictXUnion, CodingTableWhenNullable) {
  const fidl_type& type = fidl_test_example_codingtables_CodingMyStrictXUnionRequestTable;
  ASSERT_EQ(kFidlTypeStruct, type.type_tag);
  const FidlCodedStruct& request_struct = type.coded_struct;
  ASSERT_EQ(1, request_struct.field_count);

  ASSERT_STR_EQ("fidl.test.example.codingtables/CodingMyStrictXUnionRequest", request_struct.name);
  const FidlStructField& my_strict_xunion_field = request_struct.fields[0];
  ASSERT_EQ(16, my_strict_xunion_field.offset);

  const fidl_type& my_strict_xunion_type = *my_strict_xunion_field.type;
  ASSERT_EQ(kFidlTypeXUnion, my_strict_xunion_type.type_tag);
  const FidlCodedXUnion& my_strict_xunion_table = my_strict_xunion_type.coded_xunion;

  // Please keep these assertions in the same order as FidlCodedXUnion's member variables.

  ASSERT_EQ(2, my_strict_xunion_table.field_count);

  ASSERT_EQ(&fidl_internal_kBoolTable, my_strict_xunion_table.fields[0].type);
  ASSERT_EQ(&fidl_internal_kInt32Table, my_strict_xunion_table.fields[1].type);

  ASSERT_EQ(kFidlNullability_Nullable, my_strict_xunion_table.nullable);

  ASSERT_STR_EQ("fidl.test.example.codingtables/MyStrictXUnion", my_strict_xunion_table.name);

  ASSERT_EQ(kFidlStrictness_Strict, my_strict_xunion_type.coded_xunion.strictness);
}

TEST(MyTable, CodingTable) {
  const fidl_type& type = fidl_test_example_codingtables_CodingVectorOfMyTableRequestTable;
  ASSERT_EQ(kFidlTypeStruct, type.type_tag);
  const FidlCodedStruct& request_struct = type.coded_struct;
  ASSERT_EQ(1, request_struct.field_count);
  ASSERT_STR_EQ("fidl.test.example.codingtables/CodingVectorOfMyTableRequest", request_struct.name);
  const FidlStructField& vector_of_my_table_field = request_struct.fields[0];
  ASSERT_EQ(16, vector_of_my_table_field.offset);
  const fidl_type& vector_of_my_table_type = *vector_of_my_table_field.type;
  ASSERT_EQ(kFidlTypeVector, vector_of_my_table_type.type_tag);
  const FidlCodedVector& table_vector = vector_of_my_table_type.coded_vector;

  const fidl_type& table_type = *table_vector.element;
  ASSERT_EQ(kFidlTypeTable, table_type.type_tag);
  const FidlCodedTable& coded_table = table_type.coded_table;
  ASSERT_EQ(4, coded_table.field_count);

  // The ordering in the coding table is |foo|, |bar|, |baz|, and finally
  // |qux|, i.e. following ordinal order.
  const FidlTableField& field_0 = coded_table.fields[0];
  ASSERT_EQ(1, field_0.ordinal);
  ASSERT_EQ(&fidl_internal_kBoolTable, field_0.type);

  const FidlTableField& field_1 = coded_table.fields[1];
  ASSERT_EQ(2, field_1.ordinal);
  ASSERT_EQ(&fidl_internal_kInt32Table, field_1.type);

  const FidlTableField& field_2 = coded_table.fields[2];
  ASSERT_EQ(4, field_2.ordinal);
  ASSERT_EQ(kFidlTypeArray, field_2.type->type_tag);

  const FidlTableField& field_3 = coded_table.fields[3];
  ASSERT_EQ(5, field_3.ordinal);
  ASSERT_EQ(kFidlTypeVector, field_3.type->type_tag);
}

TEST(MyXUnion, CodingTableWhenNonnullable) {
  const fidl_type& type = fidl_test_example_codingtables_CodingVectorOfMyXUnionRequestTable;
  ASSERT_EQ(kFidlTypeStruct, type.type_tag);
  const FidlCodedStruct& request_struct = type.coded_struct;
  ASSERT_EQ(1, request_struct.field_count);
  ASSERT_STR_EQ("fidl.test.example.codingtables/CodingVectorOfMyXUnionRequest",
                request_struct.name);
  const FidlStructField& vector_of_my_xunion_field = request_struct.fields[0];
  ASSERT_EQ(16, vector_of_my_xunion_field.offset);
  const fidl_type& vector_of_my_xunion_type = *vector_of_my_xunion_field.type;
  ASSERT_EQ(kFidlTypeVector, vector_of_my_xunion_type.type_tag);
  const FidlCodedVector& xunion_vector = vector_of_my_xunion_type.coded_vector;

  const fidl_type& xunion_type = *xunion_vector.element;
  ASSERT_EQ(kFidlTypeXUnion, xunion_type.type_tag);
  const FidlCodedXUnion& coded_xunion = xunion_type.coded_xunion;

  ASSERT_EQ(kFidlNullability_Nonnullable, coded_xunion.nullable);

  ASSERT_EQ(kFidlStrictness_Flexible, coded_xunion.strictness);
}

TEST(MyStrictXUnion, CodingTableWhenNonnullable) {
  const fidl_type& type = fidl_test_example_codingtables_CodingVectorOfMyStrictXUnionRequestTable;
  ASSERT_EQ(kFidlTypeStruct, type.type_tag);
  const FidlCodedStruct& request_struct = type.coded_struct;
  ASSERT_EQ(1, request_struct.field_count);
  ASSERT_STR_EQ("fidl.test.example.codingtables/CodingVectorOfMyStrictXUnionRequest",
                request_struct.name);
  const FidlStructField& vector_of_my_xunion_field = request_struct.fields[0];
  ASSERT_EQ(16, vector_of_my_xunion_field.offset);
  const fidl_type& vector_of_my_xunion_type = *vector_of_my_xunion_field.type;
  ASSERT_EQ(kFidlTypeVector, vector_of_my_xunion_type.type_tag);
  const FidlCodedVector& xunion_vector = vector_of_my_xunion_type.coded_vector;

  const fidl_type& xunion_type = *xunion_vector.element;
  ASSERT_EQ(kFidlTypeXUnion, xunion_type.type_tag);
  const FidlCodedXUnion& coded_xunion = xunion_type.coded_xunion;

  ASSERT_EQ(kFidlNullability_Nonnullable, coded_xunion.nullable);

  ASSERT_EQ(kFidlStrictness_Strict, coded_xunion.strictness);
}

TEST(MyBits, CodingTable) {
  const fidl_type& type = fidl_test_example_codingtables_CodingMyBitsRequestTable;
  ASSERT_EQ(kFidlTypeStruct, type.type_tag);
  const FidlCodedStruct& request_struct = type.coded_struct;
  ASSERT_EQ(1, request_struct.field_count);
  ASSERT_STR_EQ("fidl.test.example.codingtables/CodingMyBitsRequest", request_struct.name);
  const FidlStructField& my_bits_field = request_struct.fields[0];
  ASSERT_EQ(16, my_bits_field.offset);
  const fidl_type& my_bits_type = *my_bits_field.type;
  ASSERT_EQ(kFidlTypeBits, my_bits_type.type_tag);
  const FidlCodedBits& my_bits_table = my_bits_type.coded_bits;
  ASSERT_EQ(kFidlCodedPrimitive_Uint8, my_bits_table.underlying_type);
  ASSERT_EQ(0x1u | 0x10u, my_bits_table.mask);
}

TEST(MyEnum, CodingTable) {
  const fidl_type& type = fidl_test_example_codingtables_CodingMyEnumRequestTable;
  ASSERT_EQ(kFidlTypeStruct, type.type_tag);
  const FidlCodedStruct& request_struct = type.coded_struct;
  ASSERT_EQ(1, request_struct.field_count);
  ASSERT_STR_EQ("fidl.test.example.codingtables/CodingMyEnumRequest", request_struct.name);
  const FidlStructField& my_enum_field = request_struct.fields[0];
  ASSERT_EQ(16, my_enum_field.offset);
  const fidl_type& my_enum_type = *my_enum_field.type;
  ASSERT_EQ(kFidlTypeEnum, my_enum_type.type_tag);
  const FidlCodedEnum& my_enum_table = my_enum_type.coded_enum;
  ASSERT_EQ(kFidlCodedPrimitive_Uint32, my_enum_table.underlying_type);
}

// This ensures that the number collision tests compile. (See FIDL-448).
// These tests ensure that the name mangling rules used in the generator avoid certain types
// of collisions that appeared in earlier versions (e.g. number of elements would merge with
// other content).
TEST(NumberCollision, CodingTable) {
  const fidl_type& type = fidl_test_example_codingtables_CodingNumberCollisionRequestTable;
  ASSERT_EQ(kFidlTypeStruct, type.type_tag);
  const FidlCodedStruct& request_struct = type.coded_struct;
  ASSERT_EQ(1, request_struct.field_count);
  ASSERT_STR_EQ("fidl.test.example.codingtables/CodingNumberCollisionRequest", request_struct.name);
  const FidlStructField& number_collision_field = request_struct.fields[0];
  // Transaction message header is 16 bytes.
  ASSERT_EQ(16, number_collision_field.offset);

  const fidl_type& number_collision_type = *number_collision_field.type;
  ASSERT_EQ(kFidlTypeStruct, number_collision_type.type_tag);
  const FidlCodedStruct& number_collision_table = number_collision_type.coded_struct;
  ASSERT_STR_EQ("fidl.test.example.codingtables/NumberCollision", number_collision_table.name);
  ASSERT_EQ(6, number_collision_table.field_count);
}

TEST(ForeignXUnions, CodingTable) {
  const fidl_type& req_type = fidl_test_example_codingtables_CodingForeignXUnionsRequestTable;
  ASSERT_EQ(kFidlTypeStruct, req_type.type_tag);
  const FidlCodedStruct& request_struct = req_type.coded_struct;
  ASSERT_EQ(1, request_struct.field_count);
  ASSERT_STR_EQ("fidl.test.example.codingtables/CodingForeignXUnionsRequest", request_struct.name);
  const FidlStructField& tx_field = request_struct.fields[0];
  const fidl_type& tx_type = *tx_field.type;
  ASSERT_EQ(kFidlTypeXUnion, tx_type.type_tag);
  const FidlCodedXUnion& tx_table = tx_type.coded_xunion;
  ASSERT_STR_EQ("fidl.test.example.codingtablesdeps/MyXUnionA", tx_table.name);
  ASSERT_EQ(kFidlNullability_Nonnullable, tx_table.nullable);
  ASSERT_EQ(2, tx_table.field_count);

  const fidl_type& resp_type = fidl_test_example_codingtables_CodingForeignXUnionsResponseTable;
  ASSERT_EQ(kFidlTypeStruct, resp_type.type_tag);
  const FidlCodedStruct& response_struct = resp_type.coded_struct;
  ASSERT_EQ(1, response_struct.field_count);
  ASSERT_STR_EQ("fidl.test.example.codingtables/CodingForeignXUnionsResponse",
                response_struct.name);
  const FidlStructField& rx_field = response_struct.fields[0];
  const fidl_type& rx_type = *rx_field.type;
  ASSERT_EQ(kFidlTypeXUnion, rx_type.type_tag);
  const FidlCodedXUnion& rx_table = rx_type.coded_xunion;
  ASSERT_STR_EQ("fidl.test.example.codingtablesdeps/MyXUnionA", rx_table.name);
  ASSERT_EQ(kFidlNullability_Nullable, rx_table.nullable);
  ASSERT_EQ(2, rx_table.field_count);
}

TEST(AltTypes, CodingTable) {
  // There are extern declarations here, since those declarations are not exposed by the C coding
  // table headers, which expose the coding tables for method requests & responses only.
  extern const fidl_type_t fidl_test_example_codingtables_MyUnionContainerTable;
  extern const fidl_type_t v1_fidl_test_example_codingtables_MyUnionContainerTable;
  extern const fidl_type_t fidl_test_example_codingtables_MyUnionTable;
  extern const fidl_type_t v1_fidl_test_example_codingtables_MyUnionTable;
  extern const fidl_type_t fidl_test_example_codingtables_MyXUnionTable;
  extern const fidl_type_t v1_fidl_test_example_codingtables_MyXUnionTable;

  const FidlCodedStruct& old_struct =
      fidl_test_example_codingtables_MyUnionContainerTable.coded_struct;
  ASSERT_STR_EQ("fidl.test.example.codingtables/MyUnionContainer", old_struct.name);
  ASSERT_EQ(5, old_struct.field_count);
  ASSERT_EQ(0, old_struct.fields[0].offset);
  ASSERT_EQ(8, old_struct.fields[1].offset);
  ASSERT_EQ(48, old_struct.fields[2].offset);
  ASSERT_EQ(88, old_struct.fields[3].offset);
  ASSERT_EQ(104, old_struct.fields[4].offset);

  ASSERT_EQ(&v1_fidl_test_example_codingtables_MyUnionContainerTable, old_struct.alt_type);
  const FidlCodedStruct& v1_struct = old_struct.alt_type->coded_struct;
  ASSERT_STR_EQ("fidl.test.example.codingtables/MyUnionContainer", v1_struct.name);
  ASSERT_EQ(&fidl_test_example_codingtables_MyUnionContainerTable, v1_struct.alt_type);
  ASSERT_EQ(5, v1_struct.field_count);
  ASSERT_EQ(0, v1_struct.fields[0].offset);
  ASSERT_EQ(24, v1_struct.fields[1].offset);
  ASSERT_EQ(144, v1_struct.fields[2].offset);
  ASSERT_EQ(264, v1_struct.fields[3].offset);
  ASSERT_EQ(280, v1_struct.fields[4].offset);

  ASSERT_EQ(&fidl_test_example_codingtables_MyUnionTable, old_struct.fields[0].type);
  const FidlCodedUnion& old_union = old_struct.fields[0].type->coded_union;
  ASSERT_STR_EQ("fidl.test.example.codingtables/MyUnion", old_union.name);
  ASSERT_EQ(4, old_union.data_offset);
  ASSERT_EQ(8, old_union.size);

  ASSERT_EQ(&v1_fidl_test_example_codingtables_MyUnionTable, old_union.alt_type);
  const FidlCodedXUnion& v1_union = old_union.alt_type->coded_xunion;
  ASSERT_STR_EQ("fidl.test.example.codingtables/MyUnion", v1_union.name);
  ASSERT_EQ(&fidl_test_example_codingtables_MyUnionTable, v1_union.alt_type);

  const FidlCodedArray& old_array = old_struct.fields[1].type->coded_array;
  ASSERT_EQ(&fidl_test_example_codingtables_MyUnionTable, old_array.element);
  ASSERT_EQ(40, old_array.array_size);
  ASSERT_EQ(8, old_array.element_size);

  const FidlCodedArray& v1_array = old_array.alt_type->coded_array;
  ASSERT_EQ(&v1_fidl_test_example_codingtables_MyUnionTable, v1_array.element);
  ASSERT_EQ(&old_array, &v1_array.alt_type->coded_array);
  ASSERT_EQ(120, v1_array.array_size);
  ASSERT_EQ(24, v1_array.element_size);

  const FidlCodedArray& old_optional_array = old_struct.fields[2].type->coded_array;
  ASSERT_EQ(kFidlTypeUnionPointer, old_optional_array.element->type_tag);
  ASSERT_EQ(&fidl_test_example_codingtables_MyUnionTable.coded_union,
            old_optional_array.element->coded_union_pointer.union_type);
  ASSERT_EQ(40, old_optional_array.array_size);
  ASSERT_EQ(8, old_optional_array.element_size);

  const FidlCodedArray& v1_optional_array = old_optional_array.alt_type->coded_array;
  ASSERT_EQ(kFidlTypeXUnion, v1_optional_array.element->type_tag);
  ASSERT_EQ(&old_optional_array, &v1_optional_array.alt_type->coded_array);
  ASSERT_EQ(120, v1_optional_array.array_size);
  ASSERT_EQ(24, v1_optional_array.element_size);

  const FidlCodedVector& old_vector = old_struct.fields[3].type->coded_vector;
  ASSERT_EQ(&fidl_test_example_codingtables_MyUnionTable, old_vector.element);
  ASSERT_EQ(7, old_vector.max_count);
  ASSERT_EQ(8, old_vector.element_size);

  const FidlCodedVector& v1_vector = old_vector.alt_type->coded_vector;
  ASSERT_EQ(&old_vector, &v1_vector.alt_type->coded_vector);
  ASSERT_EQ(&v1_fidl_test_example_codingtables_MyUnionTable, v1_vector.element);
  ASSERT_EQ(7, v1_vector.max_count);
  ASSERT_EQ(24, v1_vector.element_size);

  ASSERT_EQ(&fidl_test_example_codingtables_MyXUnionTable, old_struct.fields[4].type);
  const FidlCodedXUnion& old_xunion = old_struct.fields[4].type->coded_xunion;
  ASSERT_STR_EQ("fidl.test.example.codingtables/MyXUnion", old_xunion.name);
  ASSERT_EQ(&v1_fidl_test_example_codingtables_MyXUnionTable, old_xunion.alt_type);

  ASSERT_EQ(&v1_fidl_test_example_codingtables_MyXUnionTable, v1_struct.fields[4].type);
  const FidlCodedXUnion& v1_xunion = v1_struct.fields[4].type->coded_xunion;
  ASSERT_STR_EQ("fidl.test.example.codingtables/MyXUnion", v1_xunion.name);
  ASSERT_EQ(&fidl_test_example_codingtables_MyXUnionTable, v1_xunion.alt_type);
}
