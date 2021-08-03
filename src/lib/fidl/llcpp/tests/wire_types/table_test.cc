// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/llcpp/types/test/llcpp/fidl.h>
#include <gtest/gtest.h>
#include <src/lib/fidl/llcpp/tests/types_test_utils.h>

TEST(Table, TablePrimitive) {
  namespace test = fidl_llcpp_types_test;
  fidl::Arena allocator;
  test::wire::SampleTable table(allocator);
  table.set_x(allocator, 3).set_y(allocator, 100);

  ASSERT_TRUE(table.has_x());
  ASSERT_TRUE(table.has_y());
  ASSERT_FALSE(table.has_vector_of_struct());
  ASSERT_EQ(table.x(), 3);
  ASSERT_EQ(table.y(), 100);
}

TEST(Table, TableVectorOfStruct) {
  namespace test = fidl_llcpp_types_test;
  fidl::Arena allocator;
  fidl::VectorView<test::wire::CopyableStruct> structs(allocator, 2);
  structs[0].x = 30;
  structs[1].x = 42;

  test::wire::SampleTable table(allocator);
  table.set_vector_of_struct(allocator, std::move(structs));

  ASSERT_FALSE(table.has_x());
  ASSERT_FALSE(table.has_y());
  ASSERT_TRUE(table.has_vector_of_struct());
  ASSERT_EQ(table.vector_of_struct().count(), 2UL);
  ASSERT_EQ(table.vector_of_struct()[0].x, 30);
  ASSERT_EQ(table.vector_of_struct()[1].x, 42);
}

TEST(Table, EmptyTable) {
  namespace test = fidl_llcpp_types_test;
  fidl::Arena allocator;
  test::wire::SampleEmptyTable table(allocator);
  ASSERT_TRUE(table.IsEmpty());
}

TEST(Table, NotEmptyTable) {
  namespace test = fidl_llcpp_types_test;
  fidl::Arena allocator;
  test::wire::SampleTable table(allocator);
  ASSERT_TRUE(table.IsEmpty());
  table.set_x(allocator, 3).set_y(allocator, 100);
  ASSERT_FALSE(table.IsEmpty());
}

TEST(Table, Getters) {
  namespace test = fidl_llcpp_types_test;
  fidl::Arena allocator;
  test::wire::SampleTable table(allocator);
  EXPECT_FALSE(table.has_x());
  table.set_x(allocator, 3);
  static_assert(std::is_same<uint8_t&, decltype(table.x())>::value);
  EXPECT_TRUE(table.has_x());
  EXPECT_EQ(3, table.x());
  table.set_x(allocator, 4);
  EXPECT_TRUE(table.has_x());
  EXPECT_EQ(4, table.x());
}

TEST(Table, SubTables) {
  namespace test = fidl_llcpp_types_test;
  fidl::Arena allocator;
  test::wire::TableWithSubTables table(allocator);

  // Test setting a field which is a table.
  EXPECT_FALSE(table.has_t());
  table.set_t(allocator, allocator);
  EXPECT_TRUE(table.has_t());

  EXPECT_FALSE(table.t().has_x());
  table.t().set_x(allocator, 12);
  EXPECT_TRUE(table.t().has_x());
  EXPECT_EQ(12, table.t().x());

  // Test setting a field which is a vector of tables.
  EXPECT_FALSE(table.has_vt());
  table.set_vt(allocator, allocator, 6);
  EXPECT_TRUE(table.has_vt());
  // |Allocate| must be called on a default-constructed table before using it.
  table.vt()[0].Allocate(allocator);

  EXPECT_FALSE(table.vt()[0].has_x());
  table.vt()[0].set_x(allocator, 13);
  EXPECT_TRUE(table.vt()[0].has_x());
  EXPECT_EQ(13, table.vt()[0].x());
  table.vt()[0].set_x(nullptr);
  EXPECT_FALSE(table.vt()[0].has_x());

  // Test setting a field which is an array of tables.
  EXPECT_FALSE(table.has_at());
  table.set_at(allocator);
  EXPECT_TRUE(table.has_at());

  // |Allocate| must be called on a default-constructed table before using it.
  table.at()[0].Allocate(allocator);
  EXPECT_FALSE(table.at()[0].has_x());
  table.at()[0].set_x(allocator, 15);
  EXPECT_TRUE(table.at()[0].has_x());
  EXPECT_EQ(15, table.at()[0].x());
  table.at()[0].set_x(nullptr);
  EXPECT_FALSE(table.at()[0].has_x());
}

TEST(Table, UnknownHandlesResource) {
  namespace test = fidl_llcpp_types_test;

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

  auto check = [](const test::wire::TestResourceTable& table) {
    EXPECT_TRUE(table.has_x());
    EXPECT_EQ(table.x(), 0xab);
  };
  llcpp_types_test_utils::CannotProxyUnknownEnvelope<
      fidl::WireResponse<test::MsgWrapper::TestResourceTable>>(bytes, handles, std::move(check));
}

TEST(Table, UnknownHandlesNonResource) {
  namespace test = fidl_llcpp_types_test;

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

  auto check = [](const test::wire::TestTable& table) {
    EXPECT_TRUE(table.has_x());
    EXPECT_EQ(table.x(), 0xab);
  };
  llcpp_types_test_utils::CannotProxyUnknownEnvelope<
      fidl::WireResponse<test::MsgWrapper::TestTable>>(bytes, handles, std::move(check));
}
