// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/test.types/cpp/wire.h>
#include <lib/fidl/llcpp/wire_messaging_declarations.h>

#include <gtest/gtest.h>
#include <src/lib/fidl/llcpp/tests/types_test_utils.h>

TEST(Table, TablePrimitive) {
  namespace test = test_types;
  fidl::Arena allocator;
  test::wire::SampleTable table(allocator);
  table.set_x(3).set_y(100);

  ASSERT_TRUE(table.has_x());
  ASSERT_TRUE(table.has_y());
  ASSERT_FALSE(table.has_vector_of_struct());
  ASSERT_EQ(table.x(), 3);
  ASSERT_EQ(table.y(), 100);
  EXPECT_FALSE(table.HasUnknownData());
}

TEST(Table, InlineSetClear) {
  namespace test = test_types;
  fidl::Arena allocator;
  test::wire::SampleTable table(allocator);
  table.set_x(3u);

  ASSERT_TRUE(table.has_x());
  ASSERT_EQ(table.x(), 3u);
  EXPECT_FALSE(table.HasUnknownData());

  table.clear_x();
  ASSERT_FALSE(table.has_x());
  EXPECT_FALSE(table.HasUnknownData());
}

TEST(Table, OutOfLineSetClear) {
  namespace test = test_types;
  fidl::Arena allocator;
  test::wire::Uint64Table table(allocator);
  table.set_x(allocator, 3u);

  ASSERT_TRUE(table.has_x());
  ASSERT_EQ(table.x(), 3u);
  EXPECT_FALSE(table.HasUnknownData());

  table.clear_x();
  ASSERT_FALSE(table.has_x());
  EXPECT_FALSE(table.HasUnknownData());
}

TEST(Table, TableVectorOfStruct) {
  namespace test = test_types;
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
  fidl::Arena allocator;
  test::wire::SampleEmptyTable table(allocator);
  ASSERT_TRUE(table.IsEmpty());
  EXPECT_FALSE(table.HasUnknownData());
}

TEST(Table, NotEmptyTable) {
  namespace test = test_types;
  fidl::Arena allocator;
  test::wire::SampleTable table(allocator);
  ASSERT_TRUE(table.IsEmpty());
  table.set_x(3).set_y(100);
  ASSERT_FALSE(table.IsEmpty());
  EXPECT_FALSE(table.HasUnknownData());
}

TEST(Table, ManualFrame) {
  namespace test = test_types;
  fidl::WireTableFrame<test::wire::SampleTable> frame;
  test::wire::SampleTable table(
      fidl::ObjectView<fidl::WireTableFrame<test::wire::SampleTable>>::FromExternal(&frame));
  table.set_x(42);
  table.set_y(100);
  EXPECT_EQ(table.x(), 42);
  EXPECT_EQ(table.y(), 100);
  EXPECT_FALSE(table.HasUnknownData());
}

#if ZX_DEBUG_ASSERT_IMPLEMENTED
TEST(Table, CrashWhenSettingFieldWithoutFrameOrArena) {
  namespace test = test_types;
  test::wire::SampleTable table;
  ASSERT_DEATH({ table.set_x(42); }, "");
}
#endif

TEST(Table, Getters) {
  namespace test = test_types;
  fidl::Arena allocator;
  test::wire::SampleTable table(allocator);
  EXPECT_FALSE(table.has_x());
  table.set_x(3);
  static_assert(std::is_same<uint8_t&, decltype(table.x())>::value);
  EXPECT_TRUE(table.has_x());
  EXPECT_EQ(3, table.x());
  table.set_x(4);
  EXPECT_TRUE(table.has_x());
  EXPECT_EQ(4, table.x());
}

TEST(Table, SubTables) {
  namespace test = test_types;
  fidl::Arena allocator;
  test::wire::TableWithSubTables table(allocator);

  // Test setting a field which is a table.
  EXPECT_FALSE(table.has_t());
  table.set_t(allocator, allocator);
  EXPECT_TRUE(table.has_t());

  EXPECT_FALSE(table.t().has_x());
  table.t().set_x(12);
  EXPECT_TRUE(table.t().has_x());
  EXPECT_EQ(12, table.t().x());

  // Test setting a field which is a vector of tables.
  EXPECT_FALSE(table.has_vt());
  table.set_vt(allocator, allocator, 6);
  EXPECT_TRUE(table.has_vt());
  // |Allocate| must be called on a default-constructed table before using it.
  table.vt()[0].Allocate(allocator);

  EXPECT_FALSE(table.vt()[0].has_x());
  table.vt()[0].set_x(13);
  EXPECT_TRUE(table.vt()[0].has_x());
  EXPECT_EQ(13, table.vt()[0].x());
  table.vt()[0].clear_x();
  EXPECT_FALSE(table.vt()[0].has_x());

  // Test setting a field which is an array of tables.
  EXPECT_FALSE(table.has_at());
  table.set_at(allocator);
  EXPECT_TRUE(table.has_at());

  // |Allocate| must be called on a default-constructed table before using it.
  table.at()[0].Allocate(allocator);
  EXPECT_FALSE(table.at()[0].has_x());
  table.at()[0].set_x(15);
  EXPECT_TRUE(table.at()[0].has_x());
  EXPECT_EQ(15, table.at()[0].x());
  table.at()[0].clear_x();
  EXPECT_FALSE(table.at()[0].has_x());
}

TEST(Table, SettingUnsettingHandles) {
  namespace test = test_types;
  fidl::Arena allocator;
  test::wire::TestHandleTable table(allocator);

  auto event_ref_count = [](const zx::event& event) {
    zx_info_handle_count_t handle_count;
    ZX_ASSERT(ZX_OK == event.get_info(ZX_INFO_HANDLE_COUNT, &handle_count, sizeof(handle_count),
                                      nullptr, nullptr));
    return handle_count.handle_count;
  };

  zx::event event1;
  ASSERT_EQ(ZX_OK, zx::event::create(0, &event1));
  zx::event event1_dup;
  event1.duplicate(ZX_RIGHT_SAME_RIGHTS, &event1_dup);
  table.set_hs(test::wire::HandleStruct{
      .h = std::move(event1),
  });
  ASSERT_EQ(2u, event_ref_count(event1_dup));

  zx::event event2;
  ASSERT_EQ(ZX_OK, zx::event::create(0, &event2));
  zx::event event2_dup;
  event2.duplicate(ZX_RIGHT_SAME_RIGHTS, &event2_dup);
  table.set_hs(test::wire::HandleStruct{
      .h = std::move(event2),
  });
  ASSERT_EQ(1u, event_ref_count(event1_dup));
  ASSERT_EQ(2u, event_ref_count(event2_dup));

  table.clear_hs();
  ASSERT_EQ(1u, event_ref_count(event2_dup));
}

TEST(Table, UnknownHandlesResource) {
  namespace test = test_types;

  auto bytes = std::vector<uint8_t>{
      0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x01,  // txn header
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // max ordinal of 2
      0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,  // vector present
      0xab, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00,  // inline envelope 1 (0 handles)
      0xde, 0xad, 0xbe, 0xef, 0x03, 0x00, 0x01, 0x00,  // unknown inline envelope (3 handles)
  };

  zx_handle_t h1, h2, h3;
  ASSERT_EQ(ZX_OK, zx_event_create(0, &h1));
  ASSERT_EQ(ZX_OK, zx_event_create(0, &h2));
  ASSERT_EQ(ZX_OK, zx_event_create(0, &h3));
  std::vector<zx_handle_t> handles = {h1, h2, h3};

  auto check = [](const test::wire::TestResourceTable& table) {
    EXPECT_TRUE(table.HasUnknownData());
    ASSERT_TRUE(table.has_x());
    EXPECT_EQ(table.x(), 0xab);
  };
  llcpp_types_test_utils::CannotProxyUnknownEnvelope<
      fidl::internal::TransactionalResponse<test::MsgWrapper::TestResourceTable>>(bytes, handles,
                                                                                  std::move(check));
}

TEST(Table, UnknownHandlesNonResource) {
  namespace test = test_types;

  auto bytes = std::vector<uint8_t>{
      0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x01,  // txn header
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // max ordinal of 2
      0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,  // vector present
      0xab, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00,  // inline envelope 1 (0 handles)
      0xde, 0xad, 0xbe, 0xef, 0x03, 0x00, 0x01, 0x00,  // unknown inline envelope (3 handles)
  };

  zx_handle_t h1, h2, h3;
  ASSERT_EQ(ZX_OK, zx_event_create(0, &h1));
  ASSERT_EQ(ZX_OK, zx_event_create(0, &h2));
  ASSERT_EQ(ZX_OK, zx_event_create(0, &h3));
  std::vector<zx_handle_t> handles = {h1, h2, h3};

  auto check = [](const test::wire::TestTable& table) {
    EXPECT_TRUE(table.HasUnknownData());
    ASSERT_TRUE(table.has_x());
    EXPECT_EQ(table.x(), 0xab);
  };
  llcpp_types_test_utils::CannotProxyUnknownEnvelope<
      fidl::internal::TransactionalResponse<test::MsgWrapper::TestTable>>(bytes, handles,
                                                                          std::move(check));
}

TEST(Table, UnknownDataAtReservedOrdinal) {
  namespace test = test_types;

  auto bytes = std::vector<uint8_t>{
      0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // max ordinal of 2
      0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,  // vector present
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // absent envelope 1
      0xde, 0xad, 0xbe, 0xef, 0x00, 0x00, 0x01, 0x00,  // unknown inline envelope
  };
  std::vector<zx_handle_t> handles = {};

  auto check = [](const test::wire::TableMaxOrdinal3WithReserved2& table) {
    EXPECT_TRUE(table.HasUnknownData());
    EXPECT_FALSE(table.IsEmpty());
  };
  llcpp_types_test_utils::CannotProxyUnknownEnvelope<test::wire::TableMaxOrdinal3WithReserved2>(
      bytes, handles, std::move(check));
}

TEST(Table, UnknownDataAboveMaxOrdinal) {
  namespace test = test_types;

  auto bytes = std::vector<uint8_t>{
      0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // max ordinal of 4
      0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,  // vector present
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // absent envelope 1
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // absent envelope 2
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // absent envelope 3
      0xde, 0xad, 0xbe, 0xef, 0x00, 0x00, 0x01, 0x00,  // unknown inline envelope
  };
  std::vector<zx_handle_t> handles = {};

  auto check = [](const test::wire::TableMaxOrdinal3WithReserved2& table) {
    EXPECT_TRUE(table.HasUnknownData());
    EXPECT_FALSE(table.IsEmpty());
  };
  llcpp_types_test_utils::CannotProxyUnknownEnvelope<test::wire::TableMaxOrdinal3WithReserved2>(
      bytes, handles, std::move(check));
}
