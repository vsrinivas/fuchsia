// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl/coding.h>
#include <lib/fidl/internal.h>
#include <stddef.h>

#include <unittest/unittest.h>

#include "extra_messages.h"
#include "fidl_coded_types.h"

#define EXPECT_NAME_EQ(expected, type, capacity)                                             \
  do {                                                                                       \
    char buffer[capacity];                                                                   \
    size_t count = fidl_format_type_name((type), (buffer), (capacity));                      \
    EXPECT_EQ(strlen(expected), count);                                                      \
    EXPECT_BYTES_EQ(reinterpret_cast<const uint8_t*>(expected),                              \
                    reinterpret_cast<const uint8_t*>(buffer), strlen(expected), (expected)); \
  } while (0)

namespace fidl {
namespace {

bool no_output() {
  BEGIN_TEST;

  char buffer[1024];
  const fidl_type_t type = {.type_tag = kFidlTypeHandle,
                            {.coded_handle = {.handle_subtype = ZX_OBJ_TYPE_NONE,
                                              .nullable = kFidlNullability_Nonnullable}}};

  EXPECT_EQ(0u, fidl_format_type_name(nullptr, buffer, sizeof(buffer)));
  EXPECT_EQ(0u, fidl_format_type_name(&type, nullptr, sizeof(buffer)));
  EXPECT_EQ(0u, fidl_format_type_name(&type, buffer, 0u));

  END_TEST;
}

bool truncated_output() {
  BEGIN_TEST;

  EXPECT_NAME_EQ("han", &nonnullable_handle, 3);

  END_TEST;
}

bool handle_types() {
  BEGIN_TEST;

  EXPECT_NAME_EQ("handle", &nonnullable_handle, 1024);
  EXPECT_NAME_EQ("handle?", &nullable_handle, 1024);
  EXPECT_NAME_EQ("handle<channel>?", &nullable_channel_handle, 1024);
  EXPECT_NAME_EQ("handle<vmo>?", &nullable_vmo_handle, 1024);
  EXPECT_NAME_EQ("handle<channel>", &nonnullable_channel_handle, 1024);
  EXPECT_NAME_EQ("handle<vmo>", &nonnullable_vmo_handle, 1024);

  END_TEST;
}

bool array_types() {
  BEGIN_TEST;

  EXPECT_NAME_EQ("array<handle>:2", &array_of_two_nonnullable_handles, 1024);
  EXPECT_NAME_EQ("array<handle>:4", &array_of_four_nonnullable_handles, 1024);
  EXPECT_NAME_EQ("array<handle?>:5", &array_of_five_nullable_handles, 1024);
  EXPECT_NAME_EQ("array<array<handle>:4>:3", &array_of_three_arrays_of_four_nonnullable_handles,
                 1024);
  EXPECT_NAME_EQ("array<array<handle>:2>:2", &array_of_two_arrays_of_two_nonnullable_handles, 1024);

  END_TEST;
}

bool string_types() {
  BEGIN_TEST;

  EXPECT_NAME_EQ("string", &unbounded_nonnullable_string, 1024);
  EXPECT_NAME_EQ("string?", &unbounded_nullable_string, 1024);
  EXPECT_NAME_EQ("string:32", &bounded_32_nonnullable_string, 1024);
  EXPECT_NAME_EQ("string:32?", &bounded_32_nullable_string, 1024);
  EXPECT_NAME_EQ("string:4", &bounded_4_nonnullable_string, 1024);
  EXPECT_NAME_EQ("string:4?", &bounded_4_nullable_string, 1024);

  END_TEST;
}

bool vector_types() {
  BEGIN_TEST;

  EXPECT_NAME_EQ("vector<handle>", &unbounded_nonnullable_vector_of_handles, 1024);
  EXPECT_NAME_EQ("vector<handle>?", &unbounded_nullable_vector_of_handles, 1024);
  EXPECT_NAME_EQ("vector<handle>:32", &bounded_32_nonnullable_vector_of_handles, 1024);
  EXPECT_NAME_EQ("vector<handle>:32?", &bounded_32_nullable_vector_of_handles, 1024);
  EXPECT_NAME_EQ("vector<handle>:2", &bounded_2_nonnullable_vector_of_handles, 1024);
  EXPECT_NAME_EQ("vector<handle>:2?", &bounded_2_nullable_vector_of_handles, 1024);

  EXPECT_NAME_EQ("vector<primitive>", &unbounded_nonnullable_vector_of_uint32, 1024);
  EXPECT_NAME_EQ("vector<primitive>?", &unbounded_nullable_vector_of_uint32, 1024);
  EXPECT_NAME_EQ("vector<primitive>:32", &bounded_32_nonnullable_vector_of_uint32, 1024);
  EXPECT_NAME_EQ("vector<primitive>:32?", &bounded_32_nullable_vector_of_uint32, 1024);
  EXPECT_NAME_EQ("vector<primitive>:2", &bounded_2_nonnullable_vector_of_uint32, 1024);
  EXPECT_NAME_EQ("vector<primitive>:2?", &bounded_2_nullable_vector_of_uint32, 1024);

  END_TEST;
}

bool enum_types() {
  BEGIN_TEST;

  EXPECT_NAME_EQ("fidl.test.coding/Int32Enum", &fidl_test_coding_Int32EnumTable, 1024);

  END_TEST;
}

bool bits_types() {
  BEGIN_TEST;

  EXPECT_NAME_EQ("fidl.test.coding/Int32Bits", &fidl_test_coding_Int32BitsTable, 1024);

  END_TEST;
}

bool union_types() {
  BEGIN_TEST;

  EXPECT_NAME_EQ("nonnullable_handle_union", &nonnullable_handle_union_type, 1024);
  EXPECT_NAME_EQ("fidl.test.coding/SampleUnion", &fidl_test_coding_SampleUnionTable, 1024);

  END_TEST;
}

bool union_ptr_types() {
  BEGIN_TEST;

  EXPECT_NAME_EQ("nonnullable_handle_union?", &nonnullable_handle_union_ptr, 1024);

  END_TEST;
}

bool struct_types() {
  BEGIN_TEST;

  EXPECT_NAME_EQ("struct_level_0", &struct_level_0_struct, 1024);
  EXPECT_NAME_EQ("fidl.test.coding/StructWithManyHandles",
                 &fidl_test_coding_StructWithManyHandlesTable, 1024);

  END_TEST;
}

bool struct_ptr_types() {
  BEGIN_TEST;

  EXPECT_NAME_EQ("struct_ptr_level_0?", &struct_ptr_level_0_struct_pointer, 1024);

  END_TEST;
}

bool xunion_types() {
  BEGIN_TEST;

  EXPECT_NAME_EQ("fidl.test.coding/SampleXUnion", &fidl_test_coding_SampleXUnionTable, 1024);

  END_TEST;
}

bool table_types() {
  BEGIN_TEST;

  EXPECT_NAME_EQ("fidl.test.coding/SimpleTable", &fidl_test_coding_SimpleTableTable, 1024);

  END_TEST;
}

BEGIN_TEST_CASE(formatting)
RUN_TEST(no_output)
RUN_TEST(truncated_output)
RUN_TEST(handle_types)
RUN_TEST(array_types)
RUN_TEST(string_types)
RUN_TEST(vector_types)
RUN_TEST(enum_types)
RUN_TEST(bits_types)
RUN_TEST(union_types)
RUN_TEST(union_ptr_types)
RUN_TEST(struct_types)
RUN_TEST(struct_ptr_types)
RUN_TEST(xunion_types)
RUN_TEST(table_types)
END_TEST_CASE(formatting)

}  // namespace
}  // namespace fidl
