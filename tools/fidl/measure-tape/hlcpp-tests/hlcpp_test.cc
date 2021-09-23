// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>
#include <measure_tape/hlcpp/measure_tape_for_toplevelunion.h>
#include <measuretape/cpp/fidl.h>

namespace measure_tape {
namespace measuretape {

const char kHelloWorldEn[] = "hello, world!";
const char kHelloWorldFr[] = "bonjour, le monde!";
const char kHelloWorldDe[] = "hallo, welt!";
const char kHelloWorldEs[] = "Hola, Mundo!";
const char kHelloWorldRu[] = "Привет мир!";
const char kHelloWorldZh[] = "你好，世界!";

static_assert(sizeof(kHelloWorldEn) == 13 + 1);
static_assert(sizeof(kHelloWorldFr) == 18 + 1);
static_assert(sizeof(kHelloWorldDe) == 12 + 1);
static_assert(sizeof(kHelloWorldEs) == 12 + 1);
static_assert(sizeof(kHelloWorldRu) == 20 + 1);
static_assert(sizeof(kHelloWorldZh) == 16 + 1);

TEST(MeasureTape, Primitive) {
  ::measuretape::TopLevelUnion value;
  value.set_primitive(5);

  auto size = Measure(value);
  EXPECT_EQ(size.num_bytes, 24 + 8);
  EXPECT_EQ(size.num_handles, 0);
}

TEST(MeasureTape, Handle) {
  ::measuretape::TopLevelUnion value;
  zx::handle h;
  value.set_handle(std::move(h));

  auto size = Measure(value);
  EXPECT_EQ(size.num_bytes, 24 + 8);
  EXPECT_EQ(size.num_handles, 1);
}

TEST(MeasureTape, StructWithString) {
  ::measuretape::TopLevelUnion value;
  ::measuretape::StructWithString struct_with_string;
  struct_with_string.string = kHelloWorldEn;  // 13 chars
  value.set_struct_with_string(std::move(struct_with_string));

  auto size = Measure(value);
  EXPECT_EQ(size.num_bytes, 24 + 16 + 16);
  EXPECT_EQ(size.num_handles, 0);
}

TEST(MeasureTape, StructWithOptString_NoString) {
  ::measuretape::TopLevelUnion value;
  ::measuretape::StructWithOptString struct_with_opt_string;
  value.set_struct_with_opt_string(std::move(struct_with_opt_string));

  auto size = Measure(value);
  EXPECT_EQ(size.num_bytes, 24 + 16);
  EXPECT_EQ(size.num_handles, 0);
}

TEST(MeasureTape, StructWithOptString_HasString) {
  ::measuretape::TopLevelUnion value;
  ::measuretape::StructWithOptString struct_with_opt_string;
  struct_with_opt_string.opt_string = kHelloWorldFr;  // 18 chars
  value.set_struct_with_opt_string(std::move(struct_with_opt_string));

  auto size = Measure(value);
  EXPECT_EQ(size.num_bytes, 24 + 16 + 24);
  EXPECT_EQ(size.num_handles, 0);
}

TEST(MeasureTape, Table_Empty) {
  ::measuretape::TopLevelUnion value;
  ::measuretape::Table table;
  value.set_table(std::move(table));

  auto size = Measure(value);
  EXPECT_EQ(size.num_bytes, 24 + 16);
  EXPECT_EQ(size.num_handles, 0);
}

TEST(MeasureTape, Table_OnlyMaxOrdinalIsSet) {
  ::measuretape::TopLevelUnion value;
  ::measuretape::Table table;
  table.set_primitive(42);
  value.set_table(std::move(table));

  auto size = Measure(value);
  EXPECT_EQ(size.num_bytes, 24 + 16 + (5 * 16) + 8);
  EXPECT_EQ(size.num_handles, 0);
}

TEST(MeasureTape, Table_StringIsSet) {
  ::measuretape::TopLevelUnion value;
  ::measuretape::Table table;
  table.set_string(kHelloWorldDe);  // 12 chars
  value.set_table(std::move(table));

  auto size = Measure(value);
  EXPECT_EQ(size.num_bytes, 24 + 16 + (3 * 16) + 16 + 16);
  EXPECT_EQ(size.num_handles, 0);
}

TEST(MeasureTape, ArrayOfTwelveBytes) {
  ::measuretape::TopLevelUnion value;
  std::array<uint8_t, 12> array_of_twelve_bytes = {};
  value.set_array_of_twelve_bytes(std::move(array_of_twelve_bytes));

  auto size = Measure(value);
  EXPECT_EQ(size.num_bytes, 24 + 16);
  EXPECT_EQ(size.num_handles, 0);
}

TEST(MeasureTape, ArrayOfThreeStrings) {
  ::measuretape::TopLevelUnion value;
  std::array<std::string, 3> array_of_three_strings = {
      kHelloWorldEs,  // 12 bytes
      kHelloWorldRu,  // 20 bytes
      kHelloWorldZh,  // 16 bytes
  };
  value.set_array_of_three_strings(std::move(array_of_three_strings));

  auto size = Measure(value);
  EXPECT_EQ(size.num_bytes, 24 + (3 * 16) + 16 + 24 + 16);
  EXPECT_EQ(size.num_handles, 0);
}

TEST(MeasureTape, ArrayOfThreeHandles) {
  ::measuretape::TopLevelUnion value;
  std::array<zx::handle, 3> array_of_three_handles = {};
  value.set_array_of_three_handles(std::move(array_of_three_handles));

  auto size = Measure(value);
  EXPECT_EQ(size.num_bytes, 24 + 16);
  EXPECT_EQ(size.num_handles, 3);
}

TEST(MeasureTape, ArrayOfTwoTables_BothEmpty) {
  ::measuretape::TopLevelUnion value;
  std::array<::measuretape::Table, 2> array_of_two_tables = {};
  value.set_array_of_two_tables(std::move(array_of_two_tables));

  auto size = Measure(value);
  EXPECT_EQ(size.num_bytes, 24 + (2 * 16));
  EXPECT_EQ(size.num_handles, 0);
}

TEST(MeasureTape, ArrayOfTwoTables_Mixed) {
  ::measuretape::TopLevelUnion value;
  ::measuretape::Table t1;
  t1.set_primitive(27);
  ::measuretape::Table t2;
  zx::handle handle;
  t2.set_handle(std::move(handle));
  std::array<::measuretape::Table, 2> array_of_two_tables = {
      std::move(t1),
      std::move(t2),
  };
  value.set_array_of_two_tables(std::move(array_of_two_tables));

  auto size = Measure(value);
  EXPECT_EQ(size.num_bytes, 24 + (2 * 16) + (5 * 16) + 8 + (4 * 16) + 8);
  EXPECT_EQ(size.num_handles, 1);
}

TEST(MeasureTape, ArrayOfTwoUnions) {
  ::measuretape::TopLevelUnion value;
  ::measuretape::Union u1;
  u1.set_primitive(654321);
  ::measuretape::Union u2;
  u2.set_primitive(123456);
  std::array<::measuretape::Union, 2> array_of_two_unions = {
      std::move(u1),
      std::move(u2),
  };
  value.set_array_of_two_unions(std::move(array_of_two_unions));

  auto size = Measure(value);
  EXPECT_EQ(size.num_bytes, 24 + (2 * 24) + 8 + 8);
  EXPECT_EQ(size.num_handles, 0);
}

TEST(MeasureTape, StructWithTwoArrays) {
  ::measuretape::TopLevelUnion value;
  ::measuretape::StructWithTwoArrays struct_with_two_arrays;
  value.set_struct_with_two_arrays(std::move(struct_with_two_arrays));

  auto size = Measure(value);
  EXPECT_EQ(size.num_bytes, 24 + 64);
  EXPECT_EQ(size.num_handles, 0);
}

TEST(MeasureTape, ArrayOfThreeStructsWithOneHandle) {
  ::measuretape::TopLevelUnion value;
  std::array<::measuretape::StructWithOneHandle, 3> array_of_three_structs_with_one_handle = {};
  value.set_array_of_three_structs_with_one_handle(
      std::move(array_of_three_structs_with_one_handle));

  auto size = Measure(value);
  EXPECT_EQ(size.num_bytes, 24 + (3 * 12 + 4));
  EXPECT_EQ(size.num_handles, 3);
}

TEST(MeasureTape, ArrayOfThreeStructsWithTwoHandles) {
  ::measuretape::TopLevelUnion value;
  std::array<::measuretape::StructWithTwoHandles, 3> array_of_three_structs_with_two_handles = {};
  value.set_array_of_three_structs_with_two_handles(
      std::move(array_of_three_structs_with_two_handles));

  auto size = Measure(value);
  EXPECT_EQ(size.num_bytes, 24 + (3 * 12 + 4));
  EXPECT_EQ(size.num_handles, 6);
}

TEST(MeasureTape, VectorOfBytes_ThreeBytes) {
  ::measuretape::TopLevelUnion value;
  std::vector<uint8_t> vector_of_bytes = {1, 2, 3};
  value.set_vector_of_bytes(std::move(vector_of_bytes));

  auto size = Measure(value);
  EXPECT_EQ(size.num_bytes, 24 + 16 + 8);
  EXPECT_EQ(size.num_handles, 0);
}

TEST(MeasureTape, VectorOfBytes_NineBytes) {
  ::measuretape::TopLevelUnion value;
  std::vector<uint8_t> vector_of_bytes = {1, 2, 3, 4, 5, 6, 7, 8, 9};
  value.set_vector_of_bytes(std::move(vector_of_bytes));

  auto size = Measure(value);
  EXPECT_EQ(size.num_bytes, 24 + 16 + 16);
  EXPECT_EQ(size.num_handles, 0);
}

TEST(MeasureTape, VectorOfStrings) {
  ::measuretape::TopLevelUnion value;
  std::vector<std::string> vector_of_strings = {
      kHelloWorldEs,  // 12 bytes
      kHelloWorldRu,  // 20 bytes
      kHelloWorldZh,  // 16 bytes
  };
  value.set_vector_of_strings(std::move(vector_of_strings));

  auto size = Measure(value);
  EXPECT_EQ(size.num_bytes, 24 + 16 + (3 * 16) + 16 + 24 + 16);
  EXPECT_EQ(size.num_handles, 0);
}

TEST(MeasureTape, VectorOfHandles_Empty) {
  ::measuretape::TopLevelUnion value;
  std::vector<zx::handle> vector_of_handles;
  value.set_vector_of_handles(std::move(vector_of_handles));

  auto size = Measure(value);
  EXPECT_EQ(size.num_bytes, 24 + 16);
  EXPECT_EQ(size.num_handles, 0);
}

TEST(MeasureTape, VectorOfHandles_ThreeHandles) {
  // three handles, i.e. 16 bytes payload with alignment
  ::measuretape::TopLevelUnion value;
  std::vector<zx::handle> vector_of_handles;
  zx::handle handle1;
  vector_of_handles.push_back(std::move(handle1));
  zx::handle handle2;
  vector_of_handles.push_back(std::move(handle2));
  zx::handle handle3;
  vector_of_handles.push_back(std::move(handle3));
  value.set_vector_of_handles(std::move(vector_of_handles));

  auto size = Measure(value);
  EXPECT_EQ(size.num_bytes, 24 + 16 + 16);
  EXPECT_EQ(size.num_handles, 3);
}

TEST(MeasureTape, VectorOfTables_TwoEmptyTables) {
  ::measuretape::TopLevelUnion value;
  std::vector<::measuretape::Table> vector_of_tables;
  ::measuretape::Table t1;
  vector_of_tables.push_back(std::move(t1));
  ::measuretape::Table t2;
  vector_of_tables.push_back(std::move(t2));
  value.set_vector_of_tables(std::move(vector_of_tables));

  auto size = Measure(value);
  EXPECT_EQ(size.num_bytes, 24 + 16 + (2 * 16));
  EXPECT_EQ(size.num_handles, 0);
}

TEST(MeasureTape, VectorOfTables_Mixed) {
  ::measuretape::TopLevelUnion value;
  std::vector<::measuretape::Table> vector_of_tables;
  ::measuretape::Table t1;
  t1.set_primitive(27);
  vector_of_tables.push_back(std::move(t1));
  ::measuretape::Table t2;
  zx::handle handle;
  t2.set_handle(std::move(handle));
  vector_of_tables.push_back(std::move(t2));
  value.set_vector_of_tables(std::move(vector_of_tables));

  auto size = Measure(value);
  EXPECT_EQ(size.num_bytes, 24 + 16 + (2 * 16) + (5 * 16) + 8 + (4 * 16) + 8);
  EXPECT_EQ(size.num_handles, 1);
}

TEST(MeasureTape, VectorOfUnions) {
  ::measuretape::TopLevelUnion value;
  std::vector<::measuretape::Union> vector_of_unions;
  ::measuretape::Union u1;
  u1.set_primitive(654321);
  vector_of_unions.push_back(std::move(u1));
  ::measuretape::Union u2;
  u2.set_primitive(123456);
  vector_of_unions.push_back(std::move(u2));
  value.set_vector_of_unions(std::move(vector_of_unions));

  auto size = Measure(value);
  EXPECT_EQ(size.num_bytes, 24 + 16 + (2 * 24) + 8 + 8);
  EXPECT_EQ(size.num_handles, 0);
}

TEST(MeasureTape, StructWithTwoVectors_BothNull) {
  ::measuretape::TopLevelUnion value;
  ::measuretape::StructWithTwoVectors struct_with_two_vectors;
  EXPECT_FALSE(struct_with_two_vectors.vector_of_bytes.has_value());
  EXPECT_FALSE(struct_with_two_vectors.vector_of_strings.has_value());
  value.set_struct_with_two_vectors(std::move(struct_with_two_vectors));

  auto size = Measure(value);
  EXPECT_EQ(size.num_bytes, 24 + 32);
  EXPECT_EQ(size.num_handles, 0);
}

TEST(MeasureTape, StructWithTwoVectors_ThreeBytesInFirstTwoStringsInSecond) {
  ::measuretape::TopLevelUnion value;
  ::measuretape::StructWithTwoVectors struct_with_two_vectors;
  std::vector<uint8_t> vector_of_bytes = {1, 2, 3};
  fidl::VectorPtr<uint8_t> ptr_vector_of_bytes(std::move(vector_of_bytes));
  struct_with_two_vectors.vector_of_bytes = std::move(ptr_vector_of_bytes);
  std::vector<std::string> vector_of_strings = {
      kHelloWorldRu,  // 20 bytes
      kHelloWorldDe,  // 12 bytes
  };
  fidl::VectorPtr<std::string> ptr_vector_of_strings(std::move(vector_of_strings));
  struct_with_two_vectors.vector_of_strings = std::move(ptr_vector_of_strings);
  value.set_struct_with_two_vectors(std::move(struct_with_two_vectors));

  auto size = Measure(value);
  EXPECT_EQ(size.num_bytes, 24 + 32 + 8 + (2 * 16) + 24 + 16);
  EXPECT_EQ(size.num_handles, 0);
}

TEST(MeasureTape, VectorOfStructsWithOneHandle) {
  ::measuretape::TopLevelUnion value;
  std::vector<::measuretape::StructWithOneHandle> vector_of_structs_with_one_handle;
  ::measuretape::StructWithOneHandle struct_with_handle1;
  vector_of_structs_with_one_handle.push_back(std::move(struct_with_handle1));
  ::measuretape::StructWithOneHandle struct_with_handle2;
  vector_of_structs_with_one_handle.push_back(std::move(struct_with_handle2));
  ::measuretape::StructWithOneHandle struct_with_handle3;
  vector_of_structs_with_one_handle.push_back(std::move(struct_with_handle3));
  value.set_vector_of_structs_with_one_handle(std::move(vector_of_structs_with_one_handle));

  auto size = Measure(value);
  EXPECT_EQ(size.num_bytes, 24 + 16 + (3 * 12 + 4));
  EXPECT_EQ(size.num_handles, 3);
}

TEST(MeasureTape, VectorOfStructsWithTwoHandles) {
  ::measuretape::TopLevelUnion value;
  std::vector<::measuretape::StructWithTwoHandles> vector_of_structs_with_two_handles;
  ::measuretape::StructWithTwoHandles struct_with_two_handles1;
  vector_of_structs_with_two_handles.push_back(std::move(struct_with_two_handles1));
  ::measuretape::StructWithTwoHandles struct_with_two_handles2;
  vector_of_structs_with_two_handles.push_back(std::move(struct_with_two_handles1));
  ::measuretape::StructWithTwoHandles struct_with_two_handles3;
  vector_of_structs_with_two_handles.push_back(std::move(struct_with_two_handles1));
  value.set_vector_of_structs_with_two_handles(std::move(vector_of_structs_with_two_handles));

  auto size = Measure(value);
  EXPECT_EQ(size.num_bytes, 24 + 16 + (3 * 12 + 4));
  EXPECT_EQ(size.num_handles, 6);
}

TEST(MeasureTape, AnotherTopLevelThing) {
  ::measuretape::AnotherTopLevelThing value;
  auto size = Measure(value);
  EXPECT_EQ(size.num_bytes, 8);
  EXPECT_EQ(size.num_handles, 0);
}

}  // namespace measuretape
}  // namespace measure_tape
