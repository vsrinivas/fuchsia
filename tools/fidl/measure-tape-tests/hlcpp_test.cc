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

}  // namespace measuretape
}  // namespace measure_tape
