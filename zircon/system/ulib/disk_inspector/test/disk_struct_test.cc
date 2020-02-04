// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "disk_inspector/disk_struct.h"

#include <disk_inspector/type_utils.h>
#include <zxtest/zxtest.h>

namespace disk_inspector {
namespace {

constexpr uint64_t element_count = 3;

std::string child_struct_name = "child_struct";
struct ChildTestStruct {
  uint8_t child_uint8;
  uint64_t child_uint64;
};

std::string struct_name = "struct";
struct TestStruct {
  uint8_t uint8;
  uint16_t uint16;
  uint32_t uint32;
  uint64_t uint64;
  ChildTestStruct child_test_struct;
  uint8_t uint8_array[element_count];
  uint16_t uint16_array[element_count];
  uint32_t uint32_array[element_count];
  uint64_t uint64_array[element_count];
  ChildTestStruct child_test_struct_array[element_count];
};

std::unique_ptr<DiskStruct> BuildChildTestStruct() {
  std::unique_ptr<DiskStruct> object =
      DiskStruct::Create(child_struct_name, sizeof(ChildTestStruct));
  ADD_FIELD(object, ChildTestStruct, child_uint8);
  ADD_FIELD(object, ChildTestStruct, child_uint64);
  return object;
}

std::unique_ptr<DiskStruct> BuildTestStruct() {
  std::unique_ptr<DiskStruct> object = DiskStruct::Create(struct_name, sizeof(TestStruct));
  ADD_FIELD(object, TestStruct, uint8);
  ADD_FIELD(object, TestStruct, uint16);
  ADD_FIELD(object, TestStruct, uint32);
  ADD_FIELD(object, TestStruct, uint64);
  ADD_STRUCT_FIELD(object, TestStruct, child_test_struct, BuildChildTestStruct());
  ADD_ARRAY_FIELD(object, TestStruct, uint8_array, element_count);
  ADD_ARRAY_FIELD(object, TestStruct, uint16_array, element_count);
  ADD_ARRAY_FIELD(object, TestStruct, uint32_array, element_count);
  ADD_ARRAY_FIELD(object, TestStruct, uint64_array, element_count);
  ADD_STRUCT_ARRAY_FIELD(object, TestStruct, child_test_struct_array, element_count,
                         BuildChildTestStruct());
  return object;
}

TEST(DiskStructTest, GetSize) {
  uint64_t test_size = 42;
  std::unique_ptr<DiskStruct> disk_struct = DiskStruct::Create("disk_struct", test_size);
  EXPECT_EQ(disk_struct->GetSize(), test_size);
}

TEST(DiskStructTest, WriteUint8Field) {
  TestStruct test_struct;
  uint8_t result = 42;
  std::unique_ptr<DiskStruct> disk_struct = BuildTestStruct();
  ASSERT_OK(disk_struct->WriteField(&test_struct, {"uint8"}, {0}, std::to_string(result)));
  EXPECT_EQ(test_struct.uint8, result);
}

TEST(DiskStructTest, WriteUint16Field) {
  TestStruct test_struct;
  uint16_t result = 42;
  std::unique_ptr<DiskStruct> disk_struct = BuildTestStruct();
  ASSERT_OK(disk_struct->WriteField(&test_struct, {"uint16"}, {0}, std::to_string(result)));
  EXPECT_EQ(test_struct.uint16, result);
}

TEST(DiskStructTest, WriteUint32Field) {
  TestStruct test_struct;
  uint32_t result = 42;
  std::unique_ptr<DiskStruct> disk_struct = BuildTestStruct();
  ASSERT_OK(disk_struct->WriteField(&test_struct, {"uint32"}, {0}, std::to_string(result)));
  EXPECT_EQ(test_struct.uint32, result);
}

TEST(DiskStructTest, WriteUint64Field) {
  TestStruct test_struct;
  uint64_t result = 42;
  std::unique_ptr<DiskStruct> disk_struct = BuildTestStruct();
  ASSERT_OK(disk_struct->WriteField(&test_struct, {"uint64"}, {0}, std::to_string(result)));
  EXPECT_EQ(test_struct.uint64, result);
}

TEST(DiskStructTest, WriteFieldOfStructField) {
  TestStruct test_struct;
  uint8_t result = 42;
  std::unique_ptr<DiskStruct> disk_struct = BuildTestStruct();
  ASSERT_OK(disk_struct->WriteField(&test_struct, {"child_test_struct", "child_uint8"}, {0, 0},
                                    std::to_string(result)));
  EXPECT_EQ(test_struct.child_test_struct.child_uint8, result);
}

TEST(DiskStructTest, WriteUint8ArrayFieldElement) {
  TestStruct test_struct;
  uint8_t result = 42;
  uint64_t index = element_count - 1;
  std::unique_ptr<DiskStruct> disk_struct = BuildTestStruct();
  ASSERT_OK(
      disk_struct->WriteField(&test_struct, {"uint8_array"}, {index}, std::to_string(result)));
  EXPECT_EQ(test_struct.uint8_array[index], result);
}

TEST(DiskStructTest, WriteUint16ArrayFieldElement) {
  TestStruct test_struct;
  uint16_t result = 42;
  uint64_t index = element_count - 1;
  std::unique_ptr<DiskStruct> disk_struct = BuildTestStruct();
  ASSERT_OK(
      disk_struct->WriteField(&test_struct, {"uint16_array"}, {index}, std::to_string(result)));
  EXPECT_EQ(test_struct.uint16_array[index], result);
}

TEST(DiskStructTest, WriteUint32ArrayFieldElement) {
  TestStruct test_struct;
  uint32_t result = 42;
  uint64_t index = element_count - 1;
  std::unique_ptr<DiskStruct> disk_struct = BuildTestStruct();
  ASSERT_OK(
      disk_struct->WriteField(&test_struct, {"uint32_array"}, {index}, std::to_string(result)));
  EXPECT_EQ(test_struct.uint32_array[index], result);
}

TEST(DiskStructTest, WriteUint64ArrayFieldElement) {
  TestStruct test_struct;
  uint64_t result = 42;
  uint64_t index = element_count - 1;
  std::unique_ptr<DiskStruct> disk_struct = BuildTestStruct();
  ASSERT_OK(
      disk_struct->WriteField(&test_struct, {"uint64_array"}, {index}, std::to_string(result)));
  EXPECT_EQ(test_struct.uint64_array[index], result);
}

TEST(DiskStructTest, WriteFieldOfStructArrayFieldElement) {
  TestStruct test_struct;
  uint8_t result = 42;
  uint64_t index = element_count - 1;
  std::unique_ptr<DiskStruct> disk_struct = BuildTestStruct();
  ASSERT_OK(disk_struct->WriteField(&test_struct, {"child_test_struct_array", "child_uint8"},
                                    {index, 0}, std::to_string(result)));
  EXPECT_EQ(test_struct.child_test_struct_array[index].child_uint8, result);
}

}  // namespace
}  // namespace disk_inspector
