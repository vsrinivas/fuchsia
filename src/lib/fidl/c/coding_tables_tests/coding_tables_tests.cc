// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl/internal.h>

#include <fidl/test/example/codingtables/llcpp/fidl.h>
#include <zxtest/zxtest.h>

TEST(SomeStruct, CodingTable) {
  const fidl_type& type = llcpp::fidl::test::example::codingtables::
      fidl_test_example_codingtables_CodingSomeStructRequestTable;
  ASSERT_EQ(kFidlTypeStruct, type.type_tag());
  const FidlCodedStruct& some_struct_table = type.coded_struct();
  ASSERT_STR_EQ("fidl.test.example.codingtables/CodingSomeStructRequest", some_struct_table.name);
  // Every field (including primitives without padding) has a coding table generated for it.
  ASSERT_EQ(2, some_struct_table.element_count);
  ASSERT_EQ(kFidlStructElementType_Field, some_struct_table.elements[0].header.element_type);
  EXPECT_EQ(&fidl_internal_kBoolTable, some_struct_table.elements[0].field.field_type);
  ASSERT_EQ(kFidlIsResource_NotResource, some_struct_table.elements[0].header.is_resource);
  EXPECT_EQ(16, some_struct_table.elements[0].field.offset);
  ASSERT_EQ(kFidlStructElementType_Padding32, some_struct_table.elements[1].header.element_type);
  ASSERT_EQ(kFidlIsResource_NotResource, some_struct_table.elements[1].header.is_resource);
  EXPECT_EQ(16, some_struct_table.elements[1].padding.offset);
  EXPECT_EQ(0xffffff00, some_struct_table.elements[1].padding.mask_32);
}

TEST(StructWithSomeFieldsRemoved, CodingTable) {
  const fidl_type& type = llcpp::fidl::test::example::codingtables::
      fidl_test_example_codingtables_CodingStructWithSomeFieldsRemovedFromCodingTablesRequestTable;
  ASSERT_EQ(kFidlTypeStruct, type.type_tag());
  const FidlCodedStruct& coded_struct = type.coded_struct();
  ASSERT_STR_EQ(
      "fidl.test.example.codingtables/CodingStructWithSomeFieldsRemovedFromCodingTablesRequest",
      coded_struct.name);

  ASSERT_EQ(6, coded_struct.element_count);

  ASSERT_EQ(kFidlStructElementType_Padding64, coded_struct.elements[0].header.element_type);
  ASSERT_EQ(kFidlIsResource_NotResource, coded_struct.elements[0].header.is_resource);
  EXPECT_EQ(16, coded_struct.elements[0].padding.offset);
  EXPECT_EQ(0xffffffffffffff00ull, coded_struct.elements[0].padding.mask_64);

  ASSERT_EQ(kFidlStructElementType_Padding64, coded_struct.elements[1].header.element_type);
  ASSERT_EQ(kFidlIsResource_NotResource, coded_struct.elements[1].header.is_resource);
  EXPECT_EQ(32, coded_struct.elements[1].padding.offset);
  EXPECT_EQ(0xffffffffff000000ull, coded_struct.elements[1].padding.mask_64);

  ASSERT_EQ(kFidlStructElementType_Padding16, coded_struct.elements[2].header.element_type);
  ASSERT_EQ(kFidlIsResource_NotResource, coded_struct.elements[2].header.is_resource);
  EXPECT_EQ(48, coded_struct.elements[2].padding.offset);
  EXPECT_EQ(0xff00, coded_struct.elements[2].padding.mask_16);

  ASSERT_EQ(kFidlStructElementType_Field, coded_struct.elements[3].header.element_type);
  ASSERT_EQ(kFidlIsResource_NotResource, coded_struct.elements[3].header.is_resource);
  EXPECT_EQ(&fidl_internal_kBoolTable,
            coded_struct.elements[3].field.field_type->coded_array().element);
  EXPECT_EQ(54, coded_struct.elements[3].field.offset);

  ASSERT_EQ(kFidlStructElementType_Padding16, coded_struct.elements[4].header.element_type);
  ASSERT_EQ(kFidlIsResource_NotResource, coded_struct.elements[4].header.is_resource);
  EXPECT_EQ(54, coded_struct.elements[4].padding.offset);
  EXPECT_EQ(0xff00, coded_struct.elements[4].padding.mask_16);
}

TEST(MyXUnion, CodingTableWhenNullable) {
  const fidl_type& type = llcpp::fidl::test::example::codingtables::
      fidl_test_example_codingtables_CodingMyXUnionRequestTable;
  ASSERT_EQ(kFidlTypeStruct, type.type_tag());
  const FidlCodedStruct& request_struct = type.coded_struct();
  ASSERT_EQ(1, request_struct.element_count);
  ASSERT_STR_EQ("fidl.test.example.codingtables/CodingMyXUnionRequest", request_struct.name);
  ASSERT_EQ(kFidlStructElementType_Field, request_struct.elements[0].header.element_type);
  ASSERT_EQ(kFidlIsResource_NotResource, request_struct.elements[0].header.is_resource);
  const FidlStructField& my_xunion_field = request_struct.elements[0].field;
  ASSERT_EQ(16, my_xunion_field.offset);

  const fidl_type& my_xunion_type = *my_xunion_field.field_type;
  ASSERT_EQ(kFidlTypeXUnion, my_xunion_type.type_tag());
  const FidlCodedXUnion& my_xunion_table = my_xunion_type.coded_xunion();

  // Please keep these assertions in the same order as FidlCodedXUnion's member variables.

  ASSERT_EQ(2, my_xunion_table.field_count);

  ASSERT_EQ(&fidl_internal_kBoolTable, my_xunion_table.fields[0].type);
  ASSERT_EQ(&fidl_internal_kInt32Table, my_xunion_table.fields[1].type);

  ASSERT_EQ(kFidlNullability_Nullable, my_xunion_table.nullable);

  ASSERT_STR_EQ("fidl.test.example.codingtables/MyXUnion", my_xunion_table.name);

  ASSERT_EQ(kFidlStrictness_Flexible, my_xunion_type.coded_xunion().strictness);
}

TEST(MyStrictXUnion, CodingTableWhenNullable) {
  const fidl_type& type = llcpp::fidl::test::example::codingtables::
      fidl_test_example_codingtables_CodingMyStrictXUnionRequestTable;
  ASSERT_EQ(kFidlTypeStruct, type.type_tag());
  const FidlCodedStruct& request_struct = type.coded_struct();
  ASSERT_EQ(1, request_struct.element_count);

  ASSERT_STR_EQ("fidl.test.example.codingtables/CodingMyStrictXUnionRequest", request_struct.name);
  ASSERT_EQ(kFidlStructElementType_Field, request_struct.elements[0].header.element_type);
  ASSERT_EQ(kFidlIsResource_NotResource, request_struct.elements[0].header.is_resource);
  const FidlStructField& my_strict_xunion_field = request_struct.elements[0].field;
  ASSERT_EQ(16, my_strict_xunion_field.offset);

  const fidl_type& my_strict_xunion_type = *my_strict_xunion_field.field_type;
  ASSERT_EQ(kFidlTypeXUnion, my_strict_xunion_type.type_tag());
  const FidlCodedXUnion& my_strict_xunion_table = my_strict_xunion_type.coded_xunion();

  // Please keep these assertions in the same order as FidlCodedXUnion's member variables.

  ASSERT_EQ(2, my_strict_xunion_table.field_count);

  ASSERT_EQ(&fidl_internal_kBoolTable, my_strict_xunion_table.fields[0].type);
  ASSERT_EQ(&fidl_internal_kInt32Table, my_strict_xunion_table.fields[1].type);

  ASSERT_EQ(kFidlNullability_Nullable, my_strict_xunion_table.nullable);

  ASSERT_STR_EQ("fidl.test.example.codingtables/MyStrictXUnion", my_strict_xunion_table.name);

  ASSERT_EQ(kFidlStrictness_Strict, my_strict_xunion_type.coded_xunion().strictness);
}

TEST(MyTable, CodingTable) {
  const fidl_type& type = llcpp::fidl::test::example::codingtables::
      fidl_test_example_codingtables_CodingVectorOfMyTableRequestTable;
  ASSERT_EQ(kFidlTypeStruct, type.type_tag());
  const FidlCodedStruct& request_struct = type.coded_struct();
  ASSERT_EQ(1, request_struct.element_count);
  ASSERT_STR_EQ("fidl.test.example.codingtables/CodingVectorOfMyTableRequest", request_struct.name);
  ASSERT_EQ(kFidlStructElementType_Field, request_struct.elements[0].header.element_type);
  ASSERT_EQ(kFidlIsResource_NotResource, request_struct.elements[0].header.is_resource);
  const FidlStructField& vector_of_my_table_field = request_struct.elements[0].field;
  ASSERT_EQ(16, vector_of_my_table_field.offset);
  const fidl_type& vector_of_my_table_type = *vector_of_my_table_field.field_type;
  ASSERT_EQ(kFidlTypeVector, vector_of_my_table_type.type_tag());
  const FidlCodedVector& table_vector = vector_of_my_table_type.coded_vector();

  const fidl_type& table_type = *table_vector.element;
  ASSERT_EQ(kFidlTypeTable, table_type.type_tag());
  const FidlCodedTable& coded_table = table_type.coded_table();
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
  ASSERT_EQ(kFidlTypeArray, field_2.type->type_tag());

  const FidlTableField& field_3 = coded_table.fields[3];
  ASSERT_EQ(5, field_3.ordinal);
  ASSERT_EQ(kFidlTypeVector, field_3.type->type_tag());
}

TEST(MyXUnion, CodingTableWhenNonnullable) {
  const fidl_type& type = llcpp::fidl::test::example::codingtables::
      fidl_test_example_codingtables_CodingVectorOfMyXUnionRequestTable;
  ASSERT_EQ(kFidlTypeStruct, type.type_tag());
  const FidlCodedStruct& request_struct = type.coded_struct();
  ASSERT_EQ(1, request_struct.element_count);
  ASSERT_STR_EQ("fidl.test.example.codingtables/CodingVectorOfMyXUnionRequest",
                request_struct.name);
  ASSERT_EQ(kFidlStructElementType_Field, request_struct.elements[0].header.element_type);
  ASSERT_EQ(kFidlIsResource_NotResource, request_struct.elements[0].header.is_resource);
  const FidlStructField& vector_of_my_xunion_field = request_struct.elements[0].field;
  ASSERT_EQ(16, vector_of_my_xunion_field.offset);
  const fidl_type& vector_of_my_xunion_type = *vector_of_my_xunion_field.field_type;
  ASSERT_EQ(kFidlTypeVector, vector_of_my_xunion_type.type_tag());
  const FidlCodedVector& xunion_vector = vector_of_my_xunion_type.coded_vector();

  const fidl_type& xunion_type = *xunion_vector.element;
  ASSERT_EQ(kFidlTypeXUnion, xunion_type.type_tag());
  const FidlCodedXUnion& coded_xunion = xunion_type.coded_xunion();

  ASSERT_EQ(kFidlNullability_Nonnullable, coded_xunion.nullable);

  ASSERT_EQ(kFidlStrictness_Flexible, coded_xunion.strictness);
}

TEST(MyStrictXUnion, CodingTableWhenNonnullable) {
  const fidl_type& type = llcpp::fidl::test::example::codingtables::
      fidl_test_example_codingtables_CodingVectorOfMyStrictXUnionRequestTable;
  ASSERT_EQ(kFidlTypeStruct, type.type_tag());
  const FidlCodedStruct& request_struct = type.coded_struct();
  ASSERT_EQ(1, request_struct.element_count);
  ASSERT_STR_EQ("fidl.test.example.codingtables/CodingVectorOfMyStrictXUnionRequest",
                request_struct.name);
  ASSERT_EQ(kFidlStructElementType_Field, request_struct.elements[0].header.element_type);
  ASSERT_EQ(kFidlIsResource_NotResource, request_struct.elements[0].header.is_resource);
  const FidlStructField& vector_of_my_xunion_field = request_struct.elements[0].field;
  ASSERT_EQ(16, vector_of_my_xunion_field.offset);
  const fidl_type& vector_of_my_xunion_type = *vector_of_my_xunion_field.field_type;
  ASSERT_EQ(kFidlTypeVector, vector_of_my_xunion_type.type_tag());
  const FidlCodedVector& xunion_vector = vector_of_my_xunion_type.coded_vector();

  const fidl_type& xunion_type = *xunion_vector.element;
  ASSERT_EQ(kFidlTypeXUnion, xunion_type.type_tag());
  const FidlCodedXUnion& coded_xunion = xunion_type.coded_xunion();

  ASSERT_EQ(kFidlNullability_Nonnullable, coded_xunion.nullable);

  ASSERT_EQ(kFidlStrictness_Strict, coded_xunion.strictness);
}

TEST(MyBits, CodingTable) {
  const fidl_type& type = llcpp::fidl::test::example::codingtables::
      fidl_test_example_codingtables_CodingMyBitsRequestTable;
  ASSERT_EQ(kFidlTypeStruct, type.type_tag());
  const FidlCodedStruct& request_struct = type.coded_struct();
  ASSERT_EQ(2, request_struct.element_count);
  ASSERT_STR_EQ("fidl.test.example.codingtables/CodingMyBitsRequest", request_struct.name);
  ASSERT_EQ(kFidlStructElementType_Field, request_struct.elements[0].header.element_type);
  ASSERT_EQ(kFidlIsResource_NotResource, request_struct.elements[0].header.is_resource);
  const FidlStructField& my_bits_field = request_struct.elements[0].field;
  ASSERT_EQ(16, my_bits_field.offset);
  const fidl_type& my_bits_type = *my_bits_field.field_type;
  ASSERT_EQ(kFidlTypeBits, my_bits_type.type_tag());
  const FidlCodedBits& my_bits_table = my_bits_type.coded_bits();
  ASSERT_EQ(kFidlCodedPrimitiveSubtype_Uint8, my_bits_table.underlying_type);
  ASSERT_EQ(0x1u | 0x10u, my_bits_table.mask);

  ASSERT_EQ(kFidlStructElementType_Padding64, request_struct.elements[1].header.element_type);
  ASSERT_EQ(16, request_struct.elements[1].padding.offset);
  ASSERT_EQ(0xffffffffffffff00, request_struct.elements[1].padding.mask_64);
}

TEST(MyEnum, CodingTable) {
  const fidl_type& type = llcpp::fidl::test::example::codingtables::
      fidl_test_example_codingtables_CodingMyEnumRequestTable;
  ASSERT_EQ(kFidlTypeStruct, type.type_tag());
  const FidlCodedStruct& request_struct = type.coded_struct();
  ASSERT_EQ(2, request_struct.element_count);
  ASSERT_STR_EQ("fidl.test.example.codingtables/CodingMyEnumRequest", request_struct.name);
  ASSERT_EQ(kFidlStructElementType_Field, request_struct.elements[0].header.element_type);
  ASSERT_EQ(kFidlIsResource_NotResource, request_struct.elements[0].header.is_resource);
  const FidlStructField& my_enum_field = request_struct.elements[0].field;
  ASSERT_EQ(16, my_enum_field.offset);
  const fidl_type& my_enum_type = *my_enum_field.field_type;
  ASSERT_EQ(kFidlTypeEnum, my_enum_type.type_tag());
  const FidlCodedEnum& my_enum_table = my_enum_type.coded_enum();
  ASSERT_EQ(kFidlCodedPrimitiveSubtype_Uint32, my_enum_table.underlying_type);

  ASSERT_EQ(kFidlStructElementType_Padding32, request_struct.elements[1].header.element_type);
  ASSERT_EQ(20, request_struct.elements[1].padding.offset);
  ASSERT_EQ(0xffffffff, request_struct.elements[1].padding.mask_32);
}

// This ensures that the number collision tests compile. (See FIDL-448).
// These tests ensure that the name mangling rules used in the generator avoid certain types
// of collisions that appeared in earlier versions (e.g. number of elements would merge with
// other content).
TEST(NumberCollision, CodingTable) {
  const fidl_type& type = llcpp::fidl::test::example::codingtables::
      fidl_test_example_codingtables_CodingNumberCollisionRequestTable;
  ASSERT_EQ(kFidlTypeStruct, type.type_tag());
  const FidlCodedStruct& number_collision_table = type.coded_struct();
  ASSERT_STR_EQ("fidl.test.example.codingtables/CodingNumberCollisionRequest",
                number_collision_table.name);
  ASSERT_EQ(5, number_collision_table.element_count);
}

TEST(ForeignXUnions, CodingTable) {
  const fidl_type& req_type = llcpp::fidl::test::example::codingtables::
      fidl_test_example_codingtables_CodingForeignXUnionsRequestTable;
  ASSERT_EQ(kFidlTypeStruct, req_type.type_tag());
  const FidlCodedStruct& request_struct = req_type.coded_struct();
  ASSERT_EQ(1, request_struct.element_count);
  ASSERT_STR_EQ("fidl.test.example.codingtables/CodingForeignXUnionsRequest", request_struct.name);
  ASSERT_EQ(kFidlStructElementType_Field, request_struct.elements[0].header.element_type);
  ASSERT_EQ(kFidlIsResource_NotResource, request_struct.elements[0].header.is_resource);
  const FidlStructField& tx_field = request_struct.elements[0].field;
  const fidl_type& tx_type = *tx_field.field_type;
  ASSERT_EQ(kFidlTypeXUnion, tx_type.type_tag());
  const FidlCodedXUnion& tx_table = tx_type.coded_xunion();
  ASSERT_STR_EQ("fidl.test.example.codingtablesdeps/MyXUnionA", tx_table.name);
  ASSERT_EQ(kFidlNullability_Nonnullable, tx_table.nullable);
  ASSERT_EQ(2, tx_table.field_count);

  const fidl_type& resp_type = llcpp::fidl::test::example::codingtables::
      fidl_test_example_codingtables_CodingForeignXUnionsResponseTable;
  ASSERT_EQ(kFidlTypeStruct, resp_type.type_tag());
  const FidlCodedStruct& response_struct = resp_type.coded_struct();
  ASSERT_EQ(1, response_struct.element_count);
  ASSERT_STR_EQ("fidl.test.example.codingtables/CodingForeignXUnionsResponse",
                response_struct.name);
  ASSERT_EQ(kFidlStructElementType_Field, response_struct.elements[0].header.element_type);
  ASSERT_EQ(kFidlIsResource_NotResource, request_struct.elements[0].header.is_resource);
  const FidlStructField& rx_field = response_struct.elements[0].field;
  const fidl_type& rx_type = *rx_field.field_type;
  ASSERT_EQ(kFidlTypeXUnion, rx_type.type_tag());
  const FidlCodedXUnion& rx_table = rx_type.coded_xunion();
  ASSERT_STR_EQ("fidl.test.example.codingtablesdeps/MyXUnionA", rx_table.name);
  ASSERT_EQ(kFidlNullability_Nullable, rx_table.nullable);
  ASSERT_EQ(2, rx_table.field_count);
}
