// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/test.types/cpp/wire.h>
#include <lib/fidl/cpp/wire/wire_messaging_declarations.h>

#include <gtest/gtest.h>

#include "src/lib/fidl/llcpp/tests/arena_checker.h"
#include "src/lib/fidl/llcpp/tests/types_test_utils.h"

TEST(Table, TablePrimitive) {
  namespace test = test_types;
  fidl::Arena arena;
  auto table = test::wire::SampleTable::Builder(arena).x(3).y(100).Build();

  ASSERT_TRUE(table.has_x());
  ASSERT_TRUE(table.has_y());
  ASSERT_FALSE(table.has_vector_of_struct());
  ASSERT_EQ(table.x(), 3);
  ASSERT_EQ(table.y(), 100);
  EXPECT_FALSE(table.HasUnknownData());
}

TEST(Table, InlineSet) {
  namespace test = test_types;
  fidl::Arena arena;
  auto table = test::wire::SampleTable::Builder(arena).x(3u).Build();

  ASSERT_TRUE(table.has_x());
  ASSERT_EQ(table.x(), 3u);
  EXPECT_FALSE(table.HasUnknownData());
}

TEST(Table, InlineSetEmptyBrace) {
  namespace test = test_types;
  fidl::Arena arena;
  auto table = test::wire::SampleTable::Builder(arena).x({}).Build();

  ASSERT_TRUE(table.has_x());
  ASSERT_EQ(table.x(), 0u);
  EXPECT_FALSE(table.HasUnknownData());
}

TEST(Table, OutOfLineSet) {
  namespace test = test_types;
  fidl::Arena arena;
  auto table = test::wire::Uint64Table::Builder(arena).x(3u).Build();

  ASSERT_TRUE(table.has_x());
  ASSERT_EQ(table.x(), 3u);
  EXPECT_FALSE(table.HasUnknownData());
}

TEST(Table, OutOfLineSetWithEmptyBrace) {
  namespace test = test_types;
  fidl::Arena arena;
  auto table = test::wire::Uint64Table::Builder(arena).x({}).Build();

  ASSERT_TRUE(table.has_x());
  ASSERT_EQ(table.x(), 0u);
  EXPECT_FALSE(table.HasUnknownData());
}

TEST(Table, OutOfLineSetUsingSeparatelyAllocatedObjectView) {
  namespace test = test_types;
  fidl::Arena arena;
  fidl::Arena arena2;

  fidl::ObjectView<uint64_t> x{arena, 100u};
  auto table = test::wire::Uint64Table::Builder(arena2).x(x).Build();

  ASSERT_TRUE(table.has_x());
  ASSERT_EQ(table.x(), 100u);
  EXPECT_FALSE(table.HasUnknownData());

  EXPECT_TRUE(fidl_testing::ArenaChecker::IsPointerInArena(&table.x(), arena));
  EXPECT_FALSE(fidl_testing::ArenaChecker::IsPointerInArena(&table.x(), arena2));
}

TEST(Table, Builder) {
  namespace test = test_types;
  fidl::Arena arena;
  auto builder = test::wire::Uint64Table::Builder(arena).x(3u);
  auto table = builder.Build();
  ASSERT_EQ(table.x(), 3u);
  EXPECT_FALSE(table.HasUnknownData());

  builder = test::wire::Uint64Table::Builder(arena);
  table = builder.x(3u).Build();
  ASSERT_EQ(table.x(), 3u);
  EXPECT_FALSE(table.HasUnknownData());
}

TEST(Table, BuilderArena) {
  namespace test = test_types;

  // A buffer to store string contents.
  const size_t kSize = 1024;
  char buffer[kSize];
  strlcpy(buffer, "hello", kSize);

  // Build a table containing that string. The contents should be copied to the arena.
  fidl::Arena arena;
  auto table = test::wire::SampleTable::Builder(arena).s(buffer).Build();

  // Overwrite the buffer.
  strncpy(buffer, "world", kSize - 1);

  // Make sure the table contains what was passed into the builder, not what's now in the buffer.
  ASSERT_EQ("hello", table.s().get());
}

TEST(Table, TableVectorOfStruct) {
  namespace test = test_types;
  fidl::Arena arena;
  fidl::VectorView<test::wire::CopyableStruct> structs(arena, 2);
  structs[0].x = 30;
  structs[1].x = 42;

  auto table = test::wire::SampleTable::Builder(arena).vector_of_struct(structs).Build();

  ASSERT_FALSE(table.has_x());
  ASSERT_FALSE(table.has_y());
  ASSERT_TRUE(table.has_vector_of_struct());
  ASSERT_EQ(table.vector_of_struct().count(), 2UL);
  ASSERT_EQ(table.vector_of_struct()[0].x, 30);
  ASSERT_EQ(table.vector_of_struct()[1].x, 42);
  EXPECT_FALSE(table.HasUnknownData());
}

TEST(Table, EmptyTableWithoutFrame) {
  namespace test = test_types;
  test::wire::SampleEmptyTable table;
  ASSERT_TRUE(table.IsEmpty());
  EXPECT_FALSE(table.HasUnknownData());
}

TEST(Table, EmptyTableWithFrame) {
  namespace test = test_types;
  fidl::Arena arena;
  auto table = test::wire::SampleEmptyTable::Builder(arena).Build();
  ASSERT_TRUE(table.IsEmpty());
  EXPECT_FALSE(table.HasUnknownData());
}

TEST(Table, NotEmptyTable) {
  namespace test = test_types;
  fidl::Arena arena;
  auto table = test::wire::SampleTable::Builder(arena).x(3).y(100).Build();
  ASSERT_FALSE(table.IsEmpty());
  EXPECT_FALSE(table.HasUnknownData());
}

TEST(Table, ManualFrame) {
  namespace test = test_types;
  fidl::WireTableFrame<test::wire::SampleTable> frame;
  auto table =
      test::wire::SampleTable::ExternalBuilder(
          fidl::ObjectView<fidl::WireTableFrame<test::wire::SampleTable>>::FromExternal(&frame))
          .x(42)
          .y(100)
          .Build();
  EXPECT_EQ(table.x(), 42);
  EXPECT_EQ(table.y(), 100);
  EXPECT_FALSE(table.HasUnknownData());
}

TEST(Table, Getters) {
  namespace test = test_types;
  fidl::Arena arena;
  auto table = test::wire::SampleTable ::Builder(arena).x(3).Build();
  static_assert(std::is_same<uint8_t&, decltype(table.x())>::value);
  EXPECT_TRUE(table.has_x());
  EXPECT_EQ(3, table.x());
}

TEST(Table, BuilderGetClear) {
  namespace test = test_types;
  fidl::Arena arena;
  auto builder = test::wire::SampleTable::Builder(arena).x(3);
  static_assert(std::is_same<uint8_t&, decltype(builder.x())>::value);
  EXPECT_TRUE(builder.has_x());
  EXPECT_EQ(3, builder.x());
  builder.x(4);
  EXPECT_TRUE(builder.has_x());
  EXPECT_EQ(4, builder.x());

  builder.clear_x();
  EXPECT_FALSE(builder.has_x());
  EXPECT_DEATH({ (void)builder.x(); }, "");

  auto table = builder.Build();
  EXPECT_FALSE(table.has_x());
  EXPECT_DEATH({ (void)table.x(); }, "");
}

TEST(Table, SubTables) {
  namespace test = test_types;
  fidl::Arena arena;

  // Test setting a field which is a table.
  auto table = test::wire::TableWithSubTables::Builder(arena)
                   .t(test::wire::SampleTable::Builder(arena).x(12).Build())
                   .Build();
  EXPECT_TRUE(table.has_t());
  EXPECT_TRUE(table.t().has_x());
  EXPECT_EQ(12, table.t().x());

  // Test setting a field which is a vector of tables.
  EXPECT_FALSE(table.has_vt());
  table = test::wire::TableWithSubTables::Builder(arena).vt({}).Build();
  table.vt().Allocate(arena, 1);
  table.vt()[0] = test::wire::SampleTable::Builder(arena).x(13).Build();
  EXPECT_TRUE(table.has_vt());
  EXPECT_TRUE(table.vt()[0].has_x());
  EXPECT_EQ(13, table.vt()[0].x());

  // Test setting a field which is an array of tables
  table = test::wire::TableWithSubTables::Builder(arena).at({}).Build();
  table.at()[0] = test::wire::SampleTable::Builder(arena).x(15).Build();
  EXPECT_TRUE(table.has_at());
  EXPECT_EQ(15, table.at()[0].x());
}
