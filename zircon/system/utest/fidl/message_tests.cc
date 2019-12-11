// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl/cpp/builder.h>
#include <lib/fidl/cpp/message.h>
#include <lib/fidl/cpp/message_builder.h>
#include <lib/fidl/llcpp/string_view.h>
#include <lib/zx/channel.h>
#include <lib/zx/event.h>
#include <unittest/unittest.h>

#include "fidl_coded_types.h"

namespace {

bool message_test() {
  BEGIN_TEST;

  uint8_t byte_buffer[ZX_CHANNEL_MAX_MSG_BYTES];
  zx_handle_t handle_buffer[ZX_CHANNEL_MAX_MSG_HANDLES];

  fidl::Builder builder(byte_buffer, ZX_CHANNEL_MAX_MSG_BYTES);

  fidl_message_header_t* header = builder.New<fidl_message_header_t>();
  header->txid = 5u;
  header->ordinal = 42u;

  fidl::StringView* view = builder.New<fidl::StringView>();

  char* data = builder.NewArray<char>(4);
  view->set_data(data);
  view->set_size(4);

  data[0] = 'a';
  data[1] = 'b';
  data[2] = 'c';

  fidl::Message message(builder.Finalize(),
                        fidl::HandlePart(handle_buffer, ZX_CHANNEL_MAX_MSG_HANDLES));

  EXPECT_EQ(message.txid(), 5u);
  EXPECT_EQ(message.ordinal(), 42u);

  fidl::BytePart payload = message.payload();
  EXPECT_EQ(reinterpret_cast<fidl::StringView*>(payload.data()), view);

  zx::channel h1, h2;
  EXPECT_EQ(zx::channel::create(0, &h1, &h2), ZX_OK);

  EXPECT_EQ(ZX_OK, message.Write(h1.get(), 0u));

  memset(byte_buffer, 0, ZX_CHANNEL_MAX_MSG_BYTES);

  EXPECT_EQ(message.txid(), 0u);
  EXPECT_EQ(message.ordinal(), 0u);

  EXPECT_EQ(ZX_OK, message.Read(h2.get(), 0u));

  EXPECT_EQ(message.txid(), 5u);
  EXPECT_EQ(message.ordinal(), 42u);

  END_TEST;
}

bool message_builder_test() {
  BEGIN_TEST;

  zx::event e;
  EXPECT_EQ(zx::event::create(0, &e), ZX_OK);
  EXPECT_NE(e.get(), ZX_HANDLE_INVALID);

  fidl::MessageBuilder builder(&nonnullable_handle_message_type);
  builder.header()->txid = 5u;
  builder.header()->ordinal = 42u;

  zx_handle_t* handle_ptr = builder.New<zx_handle_t>();
  zx_handle_t handle_value = e.release();
  *handle_ptr = handle_value;

  fidl::Message message;
  const char* error_msg;
  EXPECT_EQ(builder.Encode(&message, &error_msg), ZX_OK);

  EXPECT_EQ(message.txid(), 5u);
  EXPECT_EQ(message.ordinal(), 42u);
  EXPECT_EQ(message.handles().actual(), 1u);
  EXPECT_EQ(message.handles().size(), 1u);
  EXPECT_EQ(message.handles().data()[0], handle_value);

  END_TEST;
}

bool message_part_is_stl_container_test() {
  BEGIN_TEST;

  EXPECT_EQ(sizeof(uint8_t), sizeof(fidl::BytePart::value_type));
  EXPECT_EQ(sizeof(zx_handle_t), sizeof(fidl::HandlePart::value_type));

  EXPECT_EQ(sizeof(const uint8_t*), sizeof(fidl::BytePart::const_iterator));
  EXPECT_EQ(sizeof(const zx_handle_t*), sizeof(fidl::HandlePart::const_iterator));

  END_TEST;
}

bool message_part_size_test() {
  BEGIN_TEST;

  fidl::Message message;

  EXPECT_EQ(message.bytes().size(), 0u);

  uint8_t dummy_msg[42];
  fidl::MessagePart msg(dummy_msg, 42, 10);

  EXPECT_EQ(msg.size(), 10u);

  fidl::MessagePart new_msg = std::move(msg);

  EXPECT_EQ(new_msg.size(), 10u);
  EXPECT_EQ(msg.size(), 0u);

  END_TEST;
}

bool message_part_wrap_array_test() {
  BEGIN_TEST;

  uint8_t dummy[42];

  auto full = fidl::MessagePart<uint8_t>::WrapFull(dummy);
  EXPECT_EQ(full.data(), dummy);
  EXPECT_EQ(full.actual(), 42);
  EXPECT_EQ(full.capacity(), 42);

  auto empty = fidl::MessagePart<uint8_t>::WrapEmpty(dummy);
  EXPECT_EQ(empty.data(), dummy);
  EXPECT_EQ(empty.actual(), 0);
  EXPECT_EQ(empty.capacity(), 42);

  END_TEST;
}

extern "C" {
  // Defined in generated/transformer_tables.test.h.
  extern const fidl_type_t example_Sandwich1Table;
  extern const fidl_type_t example_SimpleTableArrayStructTable;

  // Defined in transformer_tests.c.
  extern const uint8_t simpletablearraystruct_v1_and_old[0x50];
  extern const uint8_t sandwich1_case1_v1[0x30];
  extern const uint8_t sandwich1_case1_old[0x10];
}

bool transform_with_callback_noop() {
  BEGIN_TEST;

  const auto& src_bytes = simpletablearraystruct_v1_and_old;
  uint32_t src_num_bytes = sizeof(simpletablearraystruct_v1_and_old);
  uint32_t num_called = 0;
  auto callback = [&](const uint8_t* dst_bytes, uint32_t dst_num_bytes) -> zx_status_t {
    num_called++;
    // since this struct does not contain any unions, dst_bytes should be
    // the same as the input
    if (dst_bytes == src_bytes) {
      return ZX_OK;
    }
    // return a special value to distinguish from transformer errors
    return 1;
  };

  ASSERT_EQ(ZX_OK, fidl::FidlTransformWithCallback(FIDL_TRANSFORMATION_OLD_TO_V1,
                                                   &example_SimpleTableArrayStructTable, src_bytes,
                                                   src_num_bytes, nullptr, callback));
  ASSERT_EQ(num_called, 1);
  END_TEST;
}

bool transform_with_callback() {
  BEGIN_TEST;

  uint32_t num_called = 0;
  auto callback = [&](const uint8_t* dst_bytes, uint32_t dst_num_bytes) -> zx_status_t {
    num_called++;
    if (memcmp(dst_bytes, sandwich1_case1_v1, dst_num_bytes) == 0) {
      return ZX_OK;
    }
    return 1;
  };

  ASSERT_EQ(ZX_OK, fidl::FidlTransformWithCallback(FIDL_TRANSFORMATION_OLD_TO_V1,
                                                   &example_Sandwich1Table, sandwich1_case1_old,
                                                   sizeof(sandwich1_case1_old), nullptr, callback));
  ASSERT_EQ(num_called, 1);
  END_TEST;
}

}  // namespace

BEGIN_TEST_CASE(message_tests)
RUN_NAMED_TEST("Message test", message_test)
RUN_NAMED_TEST("MessageBuilder test", message_builder_test)
RUN_NAMED_TEST("MessagePart friendly with STL test", message_part_is_stl_container_test)
RUN_NAMED_TEST("MessagePart size test", message_part_size_test)
RUN_NAMED_TEST("MessagePart wrap array test", message_part_wrap_array_test)
RUN_TEST(transform_with_callback_noop)
RUN_TEST(transform_with_callback)
END_TEST_CASE(message_tests)
