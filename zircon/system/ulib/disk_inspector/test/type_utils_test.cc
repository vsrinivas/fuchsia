// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "disk_inspector/type_utils.h"

#include <disk_inspector/disk_struct.h>
#include <disk_inspector/supported_types.h>
#include <zxtest/zxtest.h>

namespace disk_inspector {
namespace {

TEST(TypeUtilsTest, GetFieldTypeGivesCorrectType) {
  void* void_type;
  EXPECT_EQ(FieldType::kNotSupported, GetFieldType<decltype(void_type)>());

  uint8_t uint8;
  EXPECT_EQ(FieldType::kUint8, GetFieldType<decltype(uint8)>());
  uint8_t uint8_array[5];
  EXPECT_EQ(
      FieldType::kUint8,
      GetFieldType<typename std::remove_pointer<std::decay<decltype(uint8_array)>::type>::type>());

  uint16_t uint16;
  EXPECT_EQ(FieldType::kUint16, GetFieldType<decltype(uint16)>());
  uint16_t uint16_array[5];
  EXPECT_EQ(
      FieldType::kUint16,
      GetFieldType<typename std::remove_pointer<std::decay<decltype(uint16_array)>::type>::type>());

  uint32_t uint32;
  EXPECT_EQ(FieldType::kUint32, GetFieldType<decltype(uint32)>());
  uint32_t uint32_array[5];
  EXPECT_EQ(
      FieldType::kUint32,
      GetFieldType<typename std::remove_pointer<std::decay<decltype(uint32_array)>::type>::type>());

  uint64_t uint64;
  EXPECT_EQ(FieldType::kUint64, GetFieldType<decltype(uint64)>());
  uint64_t uint64_array[5];
  EXPECT_EQ(
      FieldType::kUint64,
      GetFieldType<typename std::remove_pointer<std::decay<decltype(uint64_array)>::type>::type>());
}

TEST(TypeUtilsTest, AddFieldMacroCompiles) {
  struct TestStruct {
    uint64_t test_field;
  };
  TestStruct test_struct = {
      .test_field = 64,
  };
  std::unique_ptr<DiskStruct> disk_struct = DiskStruct::Create("test_struct", sizeof(test_struct));
}

TEST(TypeUtilsTest, AddArrayFieldMacroCompiles) {
  constexpr uint64_t count = 5;
  struct TestStruct {
    uint64_t test_field[count];
  };
  TestStruct test_struct;
  std::unique_ptr<DiskStruct> disk_struct = DiskStruct::Create("test_struct", sizeof(test_struct));
  ADD_ARRAY_FIELD(disk_struct, TestStruct, test_field, count);
}

TEST(TypeUtilsTest, AddStructFieldMacroCompiles) {
  struct TestStructChild {
    uint64_t test_field;
  };
  struct TestStruct {
    TestStructChild child;
  };
  TestStruct test_struct;
  std::unique_ptr<DiskStruct> disk_struct = DiskStruct::Create("test_struct", sizeof(test_struct));
  TestStructChild test_struct_child;
  std::unique_ptr<DiskStruct> disk_struct_child =
      DiskStruct::Create("test_struct_child", sizeof(test_struct_child));
  ADD_STRUCT_FIELD(disk_struct, TestStruct, child, std::move(disk_struct_child));
}

TEST(TypeUtilsTest, AddStructArrayFieldMacroCompiles) {
  constexpr uint64_t count = 5;
  struct TestStructChild {
    uint64_t test_field;
  };
  struct TestStruct {
    TestStructChild child[count];
  };
  TestStruct test_struct;
  std::unique_ptr<DiskStruct> disk_struct = DiskStruct::Create("test_struct", sizeof(test_struct));
  TestStructChild test_struct_child;
  std::unique_ptr<DiskStruct> disk_struct_child =
      DiskStruct::Create("test_struct_child", sizeof(test_struct_child));
  ADD_STRUCT_ARRAY_FIELD(disk_struct, TestStruct, child, count, std::move(disk_struct_child));
}

}  // namespace
}  // namespace disk_inspector
