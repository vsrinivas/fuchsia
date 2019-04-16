// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/test/example/codingtables/c/fidl.h>
#include <lib/fidl/internal.h>
#include <zxtest/zxtest.h>

TEST(SomeStruct, CodingTable) {
    const fidl_type& type = fidl_test_example_codingtables_CodingSomeStructRequestTable;
    ASSERT_EQ(fidl::FidlTypeTag::kFidlTypeStruct, type.type_tag);
    const fidl::FidlCodedStruct& request_struct = type.coded_struct;
    ASSERT_EQ(1, request_struct.field_count);
    ASSERT_STR_EQ("fidl.test.example.codingtables/CodingSomeStructRequest", request_struct.name);
    const fidl::FidlStructField& some_struct_field = request_struct.fields[0];
    // Transaction message header is 16 bytes.
    ASSERT_EQ(16, some_struct_field.offset);

    const fidl_type& some_struct_type = *some_struct_field.type;
    ASSERT_EQ(fidl::FidlTypeTag::kFidlTypeStruct, some_struct_type.type_tag);
    const fidl::FidlCodedStruct& some_struct_table = some_struct_type.coded_struct;
    ASSERT_STR_EQ("fidl.test.example.codingtables/SomeStruct", some_struct_table.name);
    // The struct only had primitives; they will not appear in its coding table.
    ASSERT_EQ(0, some_struct_table.field_count);
}

TEST(MyXUnion, CodingTableWhenNullable) {
    const fidl_type& type = fidl_test_example_codingtables_CodingMyXUnionRequestTable;
    ASSERT_EQ(fidl::FidlTypeTag::kFidlTypeStruct, type.type_tag);
    const fidl::FidlCodedStruct& request_struct = type.coded_struct;
    ASSERT_EQ(1, request_struct.field_count);
    ASSERT_STR_EQ("fidl.test.example.codingtables/CodingMyXUnionRequest", request_struct.name);
    const fidl::FidlStructField& my_xunion_field = request_struct.fields[0];
    ASSERT_EQ(16, my_xunion_field.offset);

    const fidl_type& my_xunion_type = *my_xunion_field.type;
    ASSERT_EQ(fidl::FidlTypeTag::kFidlTypeXUnion, my_xunion_type.type_tag);
    const fidl::FidlCodedXUnion& my_xunion_table = my_xunion_type.coded_xunion;
    ASSERT_STR_EQ("fidl.test.example.codingtables/MyXUnion", my_xunion_table.name);
    ASSERT_EQ(2, my_xunion_table.field_count);

    // The |MyXUnion? x| parameter was defined to be nullable.
    ASSERT_EQ(fidl::kNullable, my_xunion_table.nullable);

    // The ordering in the coding table is |bar| followed by |foo|, due to sorting.
    const fidl::FidlXUnionField& field_0 = my_xunion_table.fields[0];
    ASSERT_EQ(&fidl::internal::kInt32Table, field_0.type);

    const fidl::FidlXUnionField& field_1 = my_xunion_table.fields[1];
    ASSERT_EQ(&fidl::internal::kBoolTable, field_1.type);
}

TEST(MyTable, CodingTable) {
    const fidl_type& type = fidl_test_example_codingtables_CodingVectorOfMyTableRequestTable;
    ASSERT_EQ(fidl::FidlTypeTag::kFidlTypeStruct, type.type_tag);
    const fidl::FidlCodedStruct& request_struct = type.coded_struct;
    ASSERT_EQ(1, request_struct.field_count);
    ASSERT_STR_EQ("fidl.test.example.codingtables/CodingVectorOfMyTableRequest",
                  request_struct.name);
    const fidl::FidlStructField& vector_of_my_table_field = request_struct.fields[0];
    ASSERT_EQ(16, vector_of_my_table_field.offset);
    const fidl_type& vector_of_my_table_type = *vector_of_my_table_field.type;
    ASSERT_EQ(fidl::FidlTypeTag::kFidlTypeVector, vector_of_my_table_type.type_tag);
    const fidl::FidlCodedVector& table_vector = vector_of_my_table_type.coded_vector;

    const fidl_type& table_type = *table_vector.element;
    ASSERT_EQ(fidl::FidlTypeTag::kFidlTypeTable, table_type.type_tag);
    const fidl::FidlCodedTable& coded_table = table_type.coded_table;
    ASSERT_EQ(4, coded_table.field_count);

    // The ordering in the coding table is |foo|, |bar|, |baz|, and finally
    // |qux|, i.e. following ordinal order.
    const fidl::FidlTableField& field_0 = coded_table.fields[0];
    ASSERT_EQ(1, field_0.ordinal);
    ASSERT_EQ(&fidl::internal::kBoolTable, field_0.type);

    const fidl::FidlTableField& field_1 = coded_table.fields[1];
    ASSERT_EQ(2, field_1.ordinal);
    ASSERT_EQ(&fidl::internal::kInt32Table, field_1.type);

    const fidl::FidlTableField& field_2 = coded_table.fields[2];
    ASSERT_EQ(4, field_2.ordinal);
    ASSERT_EQ(fidl::FidlTypeTag::kFidlTypeArray, field_2.type->type_tag);

    const fidl::FidlTableField& field_3 = coded_table.fields[3];
    ASSERT_EQ(5, field_3.ordinal);
    ASSERT_EQ(fidl::FidlTypeTag::kFidlTypeVector, field_3.type->type_tag);
}

TEST(MyXUnion, CodingTableWhenNonnullable) {
    const fidl_type& type = fidl_test_example_codingtables_CodingVectorOfMyXUnionRequestTable;
    ASSERT_EQ(fidl::FidlTypeTag::kFidlTypeStruct, type.type_tag);
    const fidl::FidlCodedStruct& request_struct = type.coded_struct;
    ASSERT_EQ(1, request_struct.field_count);
    ASSERT_STR_EQ("fidl.test.example.codingtables/CodingVectorOfMyXUnionRequest",
                  request_struct.name);
    const fidl::FidlStructField& vector_of_my_xunion_field = request_struct.fields[0];
    ASSERT_EQ(16, vector_of_my_xunion_field.offset);
    const fidl_type& vector_of_my_xunion_type = *vector_of_my_xunion_field.type;
    ASSERT_EQ(fidl::FidlTypeTag::kFidlTypeVector, vector_of_my_xunion_type.type_tag);
    const fidl::FidlCodedVector& xunion_vector = vector_of_my_xunion_type.coded_vector;

    const fidl_type& xunion_type = *xunion_vector.element;
    ASSERT_EQ(fidl::FidlTypeTag::kFidlTypeXUnion, xunion_type.type_tag);
    const fidl::FidlCodedXUnion& coded_xunion = xunion_type.coded_xunion;

    // The xunion in vector<MyXUnion> is not nullable.
    ASSERT_EQ(fidl::kNonnullable, coded_xunion.nullable);
}
