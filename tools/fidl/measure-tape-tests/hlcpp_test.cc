// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <measuretape/cpp/fidl.h>
#include <measure_tape/hlcpp/measure_tape_for_toplevelunion.h>

#include <gtest/gtest.h>

namespace measure_tape {
namespace measuretape {

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
  struct_with_string.string = "hello, world!"; // 13 chars
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
  struct_with_opt_string.opt_string = "bonjour, le monde!"; // 18 chars
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
  table.set_string("hallo, welt!"); // 12 chars
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
  std::array<std::string, 3> array_of_three_strings = {
    "Hola, Mundo!", // 12 bytes
    "Привет мир!",  // 20 bytes
    "你好，世界!",    // 16 bytes
  };
  EXPECT_EQ(12u, array_of_three_strings[0].length());
  EXPECT_EQ(20u, array_of_three_strings[1].length());
  EXPECT_EQ(16u, array_of_three_strings[2].length());

  ::measuretape::TopLevelUnion value;
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

TEST(MeasureTape, ArrayOfThreeStructWithHandles) {
  ::measuretape::TopLevelUnion value;
  std::array<::measuretape::StructWithHandle, 3> array_of_three_structs_with_handles = {};
  value.set_array_of_three_structs_with_handles(std::move(array_of_three_structs_with_handles));

  auto size = Measure(value);
  EXPECT_EQ(size.num_bytes, 24 + (3 * 12 + 4));
  EXPECT_EQ(size.num_handles, 3);
}

}  // namespace measuretape
}  // namespace measure_tape
