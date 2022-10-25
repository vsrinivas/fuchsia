// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/test.types/cpp/natural_types.h>

#include <gtest/gtest.h>

namespace {

test_types::HandleStruct MakeHandleStruct() {
  zx::event event;
  zx_status_t status = zx::event::create(0, &event);
  ZX_ASSERT(status == ZX_OK);
  return test_types::HandleStruct{std::move(event)};
}

TEST(Table, DefaultConstruction) {
  test_types::SampleTable table;
  EXPECT_TRUE(table.IsEmpty());
  EXPECT_FALSE(table.x().has_value());
  EXPECT_FALSE(table.y().has_value());
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

TEST(Table, PresenceAccessors) {
  test_types::SampleTable sample_table{{.x = 0, .y = 1, .b = false}};
  EXPECT_TRUE(sample_table.x());
  EXPECT_TRUE(sample_table.x().has_value());
  EXPECT_TRUE(sample_table.y());
  EXPECT_TRUE(sample_table.y().has_value());
  EXPECT_FALSE(sample_table.vector_of_struct());
  EXPECT_FALSE(sample_table.vector_of_struct().has_value());
  EXPECT_TRUE(sample_table.b());
  EXPECT_TRUE(sample_table.b().has_value());
}

TEST(Table, ValueAccessors) {
  std::vector<test_types::CopyableStruct> vec{{3}, {4}};
  test_types::SampleTable sample_table{{.x = 0, .y = 1, .vector_of_struct = vec, .b = false}};
  EXPECT_EQ(0, sample_table.x());
  EXPECT_EQ(0, sample_table.x().value());
  EXPECT_EQ(1, sample_table.y());
  EXPECT_EQ(1, sample_table.y().value());
  EXPECT_EQ(vec, sample_table.vector_of_struct());
  EXPECT_EQ(vec, sample_table.vector_of_struct().value());
  EXPECT_EQ(false, sample_table.b());
  EXPECT_EQ(false, sample_table.b().value());
}

TEST(Table, SetAndClearUsingSetters) {
  test_types::SampleTable sample_table{};
  EXPECT_TRUE(sample_table.IsEmpty());
  sample_table.x(42);
  sample_table.y(0);
  EXPECT_FALSE(sample_table.IsEmpty());
  EXPECT_EQ(sample_table.x(), 42);
  EXPECT_EQ(sample_table.x().value(), 42);
  EXPECT_TRUE(sample_table.x());
  EXPECT_TRUE(sample_table.x().has_value());
  EXPECT_EQ(sample_table.y(), 0);
  EXPECT_EQ(sample_table.y().value(), 0);
  EXPECT_TRUE(sample_table.y());
  EXPECT_TRUE(sample_table.y().has_value());
  sample_table.y(std::nullopt);
  EXPECT_FALSE(sample_table.IsEmpty());
  EXPECT_FALSE(sample_table.y());
  EXPECT_FALSE(sample_table.y().has_value());
  EXPECT_TRUE(sample_table.x());
  sample_table.x({});
  EXPECT_TRUE(sample_table.IsEmpty());
  EXPECT_FALSE(sample_table.x());
  EXPECT_FALSE(sample_table.x().has_value());

  // Test chaining.
  sample_table.x(10).y(20);
  EXPECT_EQ(sample_table.x(), 10);
  EXPECT_EQ(sample_table.y(), 20);

  auto table = test_types::SampleTable{}.x(5).y(10);
  EXPECT_EQ(table.x(), 5);
  EXPECT_EQ(table.y(), 10);
}

TEST(Table, SetAndClearUsingMutableReferenceGetters) {
  test_types::SampleTable sample_table{};
  EXPECT_TRUE(sample_table.IsEmpty());
  sample_table.x() = 42;
  sample_table.y() = 0;
  EXPECT_FALSE(sample_table.IsEmpty());
  EXPECT_EQ(sample_table.x(), 42);
  EXPECT_EQ(sample_table.x().value(), 42);
  EXPECT_TRUE(sample_table.x());
  EXPECT_TRUE(sample_table.x().has_value());
  EXPECT_EQ(sample_table.y(), 0);
  EXPECT_EQ(sample_table.y().value(), 0);
  EXPECT_TRUE(sample_table.y());
  EXPECT_TRUE(sample_table.y().has_value());
  sample_table.y() = std::nullopt;
  EXPECT_FALSE(sample_table.IsEmpty());
  EXPECT_FALSE(sample_table.y());
  EXPECT_FALSE(sample_table.y().has_value());
  EXPECT_TRUE(sample_table.x());
  sample_table.x().reset();
  EXPECT_TRUE(sample_table.IsEmpty());
  EXPECT_FALSE(sample_table.x());
  EXPECT_FALSE(sample_table.x().has_value());
}

TEST(Table, AccessorsAfterMove) {
  test_types::SampleTable table{
      {.x = 1, .y = 2, .vector_of_struct = std::vector<test_types::CopyableStruct>{{3}, {4}}}};
  const test_types::SampleTable& const_table = table;

  auto& mutable_x = table.x();
  auto& mutable_vec = table.vector_of_struct();
  const auto& const_x = const_table.x();
  const auto& const_vec = const_table.vector_of_struct();

  test_types::SampleTable moved = std::move(table);
  moved.x() = 42;

  ASSERT_EQ(mutable_x, 1);
  ASSERT_EQ(const_x, 1);
  ASSERT_EQ(mutable_vec->size(), 0UL);
  ASSERT_EQ(const_vec->size(), 0UL);
}

TEST(Table, Copy) {
  test_types::SampleTable original{
      {.x = 1, .y = 2, .vector_of_struct = std::vector<test_types::CopyableStruct>{{3}, {4}}}};
  test_types::SampleTable copy = original;
  EXPECT_EQ(copy, original);
  original.vector_of_struct()->push_back({5});
  EXPECT_NE(copy, original);
}

TEST(Table, Move) {
  test_types::SampleTable original{
      {.x = 1, .y = 2, .vector_of_struct = std::vector<test_types::CopyableStruct>{{3}, {4}}}};
  test_types::SampleTable moved = std::move(original);
  EXPECT_EQ(original.x(), 1);
  EXPECT_EQ(original.y(), 2);
  EXPECT_EQ(original.vector_of_struct(), (std::vector<test_types::CopyableStruct>{}));
  EXPECT_EQ(moved.x(), 1);
  EXPECT_EQ(moved.y(), 2);
  EXPECT_EQ(moved.vector_of_struct(), (std::vector<test_types::CopyableStruct>{{3}, {4}}));

  test_types::TestHandleTable original_resource{{.hs = MakeHandleStruct()}};
  zx_handle_t handle = original_resource.hs()->h().get();
  test_types::TestHandleTable moved_resource = std::move(original_resource);
  EXPECT_EQ(handle, moved_resource.hs()->h().get());
  EXPECT_EQ(ZX_HANDLE_INVALID, original_resource.hs()->h().get());
}

TEST(Table, Traits) {
  static_assert(fidl::IsFidlType<test_types::SampleTable>::value);
  static_assert(fidl::IsTable<test_types::SampleTable>::value);
  static_assert(!fidl::IsTable<int>::value);
  static_assert(!fidl::IsTable<test_types::FlexibleBits>::value);
  static_assert(fidl::TypeTraits<test_types::SampleTable>::kPrimarySize == sizeof(fidl_table_t));
  static_assert(fidl::TypeTraits<test_types::SampleTable>::kMaxOutOfLine ==
                std::numeric_limits<uint32_t>::max());
  static_assert(fidl::TypeTraits<test_types::SampleTable>::kHasEnvelope);
}

}  // namespace
