// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/test.types/cpp/natural_types.h>

#include <gtest/gtest.h>

test_types::HandleStruct MakeHandleStruct() {
  zx::event event;
  zx_status_t status = zx::event::create(0, &event);
  ZX_ASSERT(status == ZX_OK);
  return test_types::HandleStruct{std::move(event)};
}

TEST(Table, AggregateInitializationCopyable) {
  test_types::SampleTable table{{.x = 3, .y = 100}};

  ASSERT_TRUE(table.x().has_value());
  ASSERT_TRUE(table.y().has_value());
  EXPECT_FALSE(table.vector_of_struct().has_value());
  EXPECT_EQ(table.x(), 3);
  EXPECT_EQ(table.y(), 100);

  // Values should be copied when passed into constructors.
  std::vector<test_types::CopyableStruct> struct_vec{{1}, {2}, {3}};
  EXPECT_EQ(struct_vec.size(), 3UL);
  test_types::SampleTable vec_table{{.vector_of_struct = struct_vec}};
  ASSERT_TRUE(vec_table.vector_of_struct().has_value());
  EXPECT_EQ(struct_vec.size(), 3UL);
  EXPECT_EQ(vec_table.vector_of_struct()->size(), 3UL);
  // Modifying this vector shouldn't modify the vector in the table.
  struct_vec.emplace_back(test_types::CopyableStruct{4});
  EXPECT_EQ(struct_vec.size(), 4UL);
  EXPECT_EQ(vec_table.vector_of_struct()->size(), 3UL);
}

TEST(Table, AggregateInitializationMoveOnly) {
  test_types::HandleStruct handle_struct = MakeHandleStruct();
  ASSERT_TRUE(handle_struct.h().is_valid());
  zx_handle_t handle = handle_struct.h().get();

  test_types::TestHandleTable table{{.hs = std::move(handle_struct)}};
  EXPECT_FALSE(handle_struct.h().is_valid());
  ASSERT_TRUE(table.hs().has_value());
  EXPECT_TRUE(table.hs()->h().is_valid());
  EXPECT_EQ(table.hs()->h().get(), handle);
}

TEST(Table, Equality) {
  EXPECT_EQ(test_types::SampleEmptyTable{}, test_types::SampleEmptyTable{});
  EXPECT_EQ(test_types::SampleTable{}, test_types::SampleTable{});

  test_types::SampleTable table{
      {.x = 1, .y = 2, .vector_of_struct = std::vector<test_types::CopyableStruct>{{3}, {4}}}};
  test_types::SampleTable same{
      {.x = 1, .y = 2, .vector_of_struct = std::vector<test_types::CopyableStruct>{{3}, {4}}}};
  test_types::SampleTable different1{
      {.x = 1, .y = 1, .vector_of_struct = std::vector<test_types::CopyableStruct>{{3}, {4}}}};
  test_types::SampleTable different2{
      {.x = 1, .y = 2, .vector_of_struct = std::vector<test_types::CopyableStruct>{{3}, {6}}}};
  test_types::SampleTable different3{
      {.x = 1, .y = 2, .vector_of_struct = std::vector<test_types::CopyableStruct>{{3}, {4}, {5}}}};

  EXPECT_EQ(table, same);
  EXPECT_NE(table, different1);
  EXPECT_NE(table, different2);
  EXPECT_NE(table, different3);
}
