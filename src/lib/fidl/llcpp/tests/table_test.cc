// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl/llcpp/heap_allocator.h>

#include <fidl/llcpp/types/test/llcpp/fidl.h>
#include <gtest/gtest.h>
#include <src/lib/fidl/llcpp/tests/types_test_utils.h>

namespace {

fidl::HeapAllocator allocator;

}  // namespace

TEST(Table, UnownedBuilderBuildTablePrimitive) {
  namespace test = llcpp::fidl::llcpp::types::test;
  fidl::aligned<uint8_t> x = 3;
  fidl::aligned<uint8_t> y = 100;
  auto builder =
      test::SampleTable::UnownedBuilder().set_x(fidl::unowned_ptr(&x)).set_y(fidl::unowned_ptr(&y));
  const auto& table = builder.build();

  ASSERT_TRUE(table.has_x());
  ASSERT_TRUE(table.has_y());
  ASSERT_FALSE(table.has_vector_of_struct());
  ASSERT_EQ(table.x(), x);
  ASSERT_EQ(table.y(), y);
}

TEST(Table, BuilderBuildTablePrimitive) {
  namespace test = llcpp::fidl::llcpp::types::test;
  fidl::aligned<uint8_t> x = 3;
  fidl::aligned<uint8_t> y = 100;
  test::SampleTable::Frame frame;
  auto builder = test::SampleTable::Builder(fidl::unowned_ptr(&frame))
                     .set_x(fidl::unowned_ptr(&x))
                     .set_y(fidl::unowned_ptr(&y));
  const auto& table = builder.build();

  ASSERT_TRUE(table.has_x());
  ASSERT_TRUE(table.has_y());
  ASSERT_FALSE(table.has_vector_of_struct());
  ASSERT_EQ(table.x(), x);
  ASSERT_EQ(table.y(), y);
}

TEST(Table, UnownedBuilderBuildTableVectorOfStruct) {
  namespace test = llcpp::fidl::llcpp::types::test;
  std::vector<test::CopyableStruct> structs = {
      {.x = 30},
      {.x = 42},
  };
  fidl::VectorView<test::CopyableStruct> vector_view = fidl::unowned_vec(structs);
  auto builder =
      test::SampleTable::UnownedBuilder().set_vector_of_struct(fidl::unowned_ptr(&vector_view));
  const auto& table = builder.build();

  ASSERT_FALSE(table.has_x());
  ASSERT_FALSE(table.has_y());
  ASSERT_TRUE(table.has_vector_of_struct());
  ASSERT_EQ(table.vector_of_struct().count(), structs.size());
  ASSERT_EQ(table.vector_of_struct()[0].x, structs[0].x);
  ASSERT_EQ(table.vector_of_struct()[1].x, structs[1].x);
}

TEST(Table, BuilderBuildTableVectorOfStruct) {
  namespace test = llcpp::fidl::llcpp::types::test;
  std::vector<test::CopyableStruct> structs = {
      {.x = 30},
      {.x = 42},
  };
  fidl::VectorView<test::CopyableStruct> vector_view = fidl::unowned_vec(structs);
  test::SampleTable::Frame frame;
  auto builder = test::SampleTable::Builder(fidl::unowned_ptr(&frame))
                     .set_vector_of_struct(fidl::unowned_ptr(&vector_view));
  const auto& table = builder.build();

  ASSERT_FALSE(table.has_x());
  ASSERT_FALSE(table.has_y());
  ASSERT_TRUE(table.has_vector_of_struct());
  ASSERT_EQ(table.vector_of_struct().count(), structs.size());
  ASSERT_EQ(table.vector_of_struct()[0].x, structs[0].x);
  ASSERT_EQ(table.vector_of_struct()[1].x, structs[1].x);
}

TEST(Table, UnownedBuilderBuildEmptyTable) {
  namespace test = llcpp::fidl::llcpp::types::test;
  auto builder = test::SampleEmptyTable::UnownedBuilder();
  const auto& table = builder.build();
  ASSERT_TRUE(table.IsEmpty());
}

TEST(Table, BuilderBuildEmptyTable) {
  namespace test = llcpp::fidl::llcpp::types::test;
  fidl::aligned<test::SampleEmptyTable::Frame> frame;
  auto builder = test::SampleEmptyTable::Builder(fidl::unowned_ptr(&frame));
  const auto& table = builder.build();
  ASSERT_TRUE(table.IsEmpty());
}

TEST(Table, BuilderGetters) {
  namespace test = llcpp::fidl::llcpp::types::test;
  fidl::aligned<test::SampleTable::Frame> frame;
  fidl::aligned<uint8_t> x = 3;
  fidl::aligned<uint8_t> x2 = 4;
  auto builder = test::SampleTable::Builder(fidl::unowned_ptr(&frame));
  EXPECT_FALSE(builder.has_x());
  builder.set_x(fidl::unowned_ptr(&x));
  static_assert(std::is_same<uint8_t&, decltype(builder.x())>::value);
  EXPECT_TRUE(builder.has_x());
  EXPECT_EQ(3, builder.x());
  const test::SampleTable::Builder& const_builder_ref = builder;
  static_assert(std::is_same<const uint8_t&, decltype(const_builder_ref.x())>::value);
  EXPECT_TRUE(const_builder_ref.has_x());
  EXPECT_EQ(3, const_builder_ref.x());
  builder.set_x(fidl::unowned_ptr(&x2));
  EXPECT_TRUE(builder.has_x());
  EXPECT_EQ(4, builder.x());
}

TEST(Table, UnownedBuilderGetters) {
  namespace test = llcpp::fidl::llcpp::types::test;
  fidl::aligned<uint8_t> x = 3;
  fidl::aligned<uint8_t> x2 = 4;
  auto builder = test::SampleTable::UnownedBuilder();
  EXPECT_FALSE(builder.has_x());
  builder.set_x(fidl::unowned_ptr(&x));
  static_assert(std::is_same<uint8_t&, decltype(builder.x())>::value);
  EXPECT_TRUE(builder.has_x());
  EXPECT_EQ(3, builder.x());
  const test::SampleTable::UnownedBuilder& const_builder_ref = builder;
  static_assert(std::is_same<const uint8_t&, decltype(const_builder_ref.x())>::value);
  EXPECT_TRUE(const_builder_ref.has_x());
  EXPECT_EQ(3, const_builder_ref.x());
  builder.set_x(fidl::unowned_ptr(&x2));
  EXPECT_TRUE(builder.has_x());
  EXPECT_EQ(4, builder.x());
}

TEST(Table, BuilderGetBuilder) {
  namespace test = llcpp::fidl::llcpp::types::test;
  auto builder =
      test::TableWithSubTables::Builder(allocator.make<test::TableWithSubTables::Frame>());
  EXPECT_FALSE(builder.has_t());
  builder.set_t(allocator.make<test::SampleTable>(
      test::SampleTable::Builder(allocator.make<test::SampleTable::Frame>()).build()));
  EXPECT_TRUE(builder.has_t());
  EXPECT_FALSE(builder.t().has_x());
  builder.get_builder_t().set_x(allocator.make<uint8_t>(12));
  EXPECT_TRUE(builder.t().has_x());
  EXPECT_EQ(12, builder.t().x());
  EXPECT_FALSE(builder.has_vt());
  builder.set_vt(allocator.make<fidl::VectorView<test::SampleTable>>(
      allocator.make<test::SampleTable[]>(6), 6));
  EXPECT_TRUE(builder.has_vt());
  for (uint32_t i = 0; i < 2; ++i) {
    EXPECT_FALSE(builder.vt()[0].has_x());
    switch (i) {
      case 0:
        // Assign as table with full-size Frame.
        builder.vt()[0] =
            test::SampleTable::Builder(allocator.make<test::SampleTable::Frame>()).build();
        break;
      case 1:
        // Assign as builder with full-size Frame.
        builder.get_builders_vt()[0] =
            test::SampleTable::Builder(allocator.make<test::SampleTable::Frame>());
        break;
    }
    EXPECT_FALSE(builder.vt()[0].has_x());
    builder.get_builders_vt()[0].set_x(allocator.make<uint8_t>(13 + i));
    EXPECT_TRUE(builder.vt()[0].has_x());
    EXPECT_EQ(13 + i, builder.vt()[0].x());
    builder.get_builders_vt()[0].set_x(nullptr);
    EXPECT_FALSE(builder.vt()[0].has_x());
  }
  EXPECT_FALSE(builder.has_at());
  builder.set_at(allocator.make<fidl::Array<test::SampleTable, 3>>());
  EXPECT_TRUE(builder.has_at());
  for (uint32_t i = 0; i < 2; ++i) {
    EXPECT_FALSE(builder.at()[0].has_x());
    switch (i) {
      case 0:
        builder.at()[0] =
            test::SampleTable::Builder(allocator.make<test::SampleTable::Frame>()).build();
        break;
      case 1:
        builder.get_builders_at()[0] =
            test::SampleTable::Builder(allocator.make<test::SampleTable::Frame>());
        break;
    }
    EXPECT_FALSE(builder.at()[0].has_x());
    builder.get_builders_at()[0].set_x(allocator.make<uint8_t>(15 + i));
    EXPECT_TRUE(builder.at()[0].has_x());
    EXPECT_EQ(15 + i, builder.at()[0].x());
    builder.get_builders_at()[0].set_x(nullptr);
    EXPECT_FALSE(builder.at()[0].has_x());
  }
}

TEST(Table, UnownedBuilderGetBuilder) {
  namespace test = llcpp::fidl::llcpp::types::test;
  test::TableWithSubTables::UnownedBuilder builder;
  EXPECT_FALSE(builder.has_t());
  builder.set_t(allocator.make<test::SampleTable>(
      test::SampleTable::Builder(allocator.make<test::SampleTable::Frame>()).build()));
  EXPECT_TRUE(builder.has_t());
  EXPECT_FALSE(builder.t().has_x());
  builder.get_builder_t().set_x(allocator.make<uint8_t>(12));
  EXPECT_TRUE(builder.t().has_x());
  EXPECT_EQ(12, builder.t().x());
  EXPECT_FALSE(builder.has_vt());
  builder.set_vt(allocator.make<fidl::VectorView<test::SampleTable>>(
      allocator.make<test::SampleTable[]>(6), 6));
  EXPECT_TRUE(builder.has_vt());
  for (uint32_t i = 0; i < 2; ++i) {
    EXPECT_FALSE(builder.vt()[0].has_x());
    switch (i) {
      case 0:
        // Assign as table with full-size Frame.
        builder.vt()[0] =
            test::SampleTable::Builder(allocator.make<test::SampleTable::Frame>()).build();
        break;
      case 1:
        // Assign as builder with full-size Frame.
        builder.get_builders_vt()[0] =
            test::SampleTable::Builder(allocator.make<test::SampleTable::Frame>());
        break;
    }
    EXPECT_FALSE(builder.vt()[0].has_x());
    builder.get_builders_vt()[0].set_x(allocator.make<uint8_t>(13 + i));
    EXPECT_TRUE(builder.vt()[0].has_x());
    EXPECT_EQ(13 + i, builder.vt()[0].x());
    builder.get_builders_vt()[0].set_x(nullptr);
    EXPECT_FALSE(builder.vt()[0].has_x());
  }
  EXPECT_FALSE(builder.has_at());
  builder.set_at(allocator.make<fidl::Array<test::SampleTable, 3>>());
  EXPECT_TRUE(builder.has_at());
  for (uint32_t i = 0; i < 2; ++i) {
    EXPECT_FALSE(builder.at()[0].has_x());
    switch (i) {
      case 0:
        builder.at()[0] =
            test::SampleTable::Builder(allocator.make<test::SampleTable::Frame>()).build();
        break;
      case 1:
        builder.get_builders_at()[0] =
            test::SampleTable::Builder(allocator.make<test::SampleTable::Frame>());
        break;
    }
    EXPECT_FALSE(builder.at()[0].has_x());
    builder.get_builders_at()[0].set_x(allocator.make<uint8_t>(15 + i));
    EXPECT_TRUE(builder.at()[0].has_x());
    EXPECT_EQ(15 + i, builder.at()[0].x());
    builder.get_builders_at()[0].set_x(nullptr);
    EXPECT_FALSE(builder.at()[0].has_x());
  }
}

TEST(Table, UnknownHandlesResource) {
  namespace test = llcpp::fidl::llcpp::types::test;

  auto bytes = std::vector<uint8_t>{
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01,  // txn header
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // max ordinal of 2
      0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,  // vector present
      0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // envelope 1 (8 bytes, 0 handles)
      0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,  //
      0x08, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00,  // unknown envelope (8 bytes, 3 handles)
      0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,  //
      0xab, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // envelope 1 data
      0xde, 0xad, 0xbe, 0xef, 0x00, 0x00, 0x00, 0x00,  // unknown data
  };

  zx_handle_t h1, h2, h3;
  ASSERT_EQ(ZX_OK, zx_event_create(0, &h1));
  ASSERT_EQ(ZX_OK, zx_event_create(0, &h2));
  ASSERT_EQ(ZX_OK, zx_event_create(0, &h3));
  std::vector<zx_handle_t> handles = {h1, h2, h3};

  auto check = [](const test::TestResourceTable& table) {
    EXPECT_TRUE(table.has_x());
    EXPECT_EQ(table.x(), 0xab);
  };
  llcpp_types_test_utils::CannotProxyUnknownEnvelope<test::MsgWrapper::TestResourceTableResponse>(
      bytes, handles, std::move(check));
}

TEST(Table, UnknownHandlesNonResource) {
  namespace test = llcpp::fidl::llcpp::types::test;

  auto bytes = std::vector<uint8_t>{
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01,  // txn header
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // max ordinal of 2
      0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,  // vector present
      0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // envelope 1 (8 bytes, 0 handles)
      0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,  //
      0x08, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00,  // unknown envelope (8 bytes, 3 handles)
      0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,  //
      0xab, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // envelope 1 data
      0xde, 0xad, 0xbe, 0xef, 0x00, 0x00, 0x00, 0x00,  // unknown data
  };

  zx_handle_t h1, h2, h3;
  ASSERT_EQ(ZX_OK, zx_event_create(0, &h1));
  ASSERT_EQ(ZX_OK, zx_event_create(0, &h2));
  ASSERT_EQ(ZX_OK, zx_event_create(0, &h3));
  std::vector<zx_handle_t> handles = {h1, h2, h3};

  auto check = [](const test::TestTable& table) {
    EXPECT_TRUE(table.has_x());
    EXPECT_EQ(table.x(), 0xab);
  };
  llcpp_types_test_utils::CannotProxyUnknownEnvelope<test::MsgWrapper::TestTableResponse>(
      bytes, handles, std::move(check));
}
