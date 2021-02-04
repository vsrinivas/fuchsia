// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl/llcpp/message.h>
#include <lib/zx/event.h>

#include <gtest/gtest.h>

TEST(OutgoingToIncomingMessage, ByteMessage) {
  uint8_t bytes[3] = {1, 2, 3};
  fidl::OutgoingByteMessage msg(bytes, sizeof(bytes), sizeof(bytes), nullptr, 0, 0);
  auto result = fidl::OutgoingToIncomingMessage(msg);
  ASSERT_EQ(ZX_OK, result.status());
  ASSERT_EQ(bytes, result.incoming_message()->bytes);
  ASSERT_EQ(sizeof(bytes), result.incoming_message()->num_bytes);
  ASSERT_EQ(0u, result.incoming_message()->num_handles);
}

TEST(OutgoingToIncomingMessage, IovecMessage) {
  uint8_t backing_buf[3] = {1, 2, 3};  // buffer backing the iovecs
  zx_channel_iovec_t iovecs[2] = {
      {
          .buffer = &backing_buf[0],
          .capacity = 2,
          .reserved = 0,
      },
      {
          .buffer = &backing_buf[2],
          .capacity = 1,
          .reserved = 0,
      },
  };
  fidl::OutgoingIovecMessage msg({
      .iovecs = iovecs,
      .iovecs_actual = 2,
      .iovecs_capacity = 2,
      .substitutions = nullptr,
      .substitutions_actual = 0,
      .substitutions_capacity = 0,
      .handles = nullptr,
      .handle_actual = 0,
      .handle_capacity = 0,
  });
  auto result = fidl::OutgoingToIncomingMessage(msg);
  ASSERT_EQ(ZX_OK, result.status());
  fidl_incoming_msg_t* output = result.incoming_message();
  ASSERT_EQ(3u, output->num_bytes);
  EXPECT_EQ(0, memcmp(backing_buf, output->bytes, output->num_bytes));
  EXPECT_EQ(0u, output->num_handles);
}

TEST(OutgoingToIncomingMessage, TooManyHandles) {
  constexpr uint32_t num_handles = ZX_CHANNEL_MAX_MSG_HANDLES + 1;
  fidl::OutgoingByteMessage msg(nullptr, 0, 0, nullptr, num_handles, num_handles);
  auto result = fidl::OutgoingToIncomingMessage(msg);
  ASSERT_EQ(ZX_ERR_OUT_OF_RANGE, result.status());
}

#ifdef __Fuchsia__
TEST(OutgoingToIncomingMessage, Handles) {
  uint8_t bytes[16];
  zx::event ev;
  ASSERT_EQ(ZX_OK, zx::event::create(0, &ev));
  zx_handle_disposition_t hd[1] = {zx_handle_disposition_t{
      .operation = ZX_HANDLE_OP_MOVE,
      .handle = ev.get(),
      .type = ZX_OBJ_TYPE_NONE,
      .rights = ZX_RIGHT_SAME_RIGHTS,
      .result = ZX_OK,
  }};
  fidl::OutgoingByteMessage msg(bytes, 16, 16, hd, 1, 1);
  auto result = fidl::OutgoingToIncomingMessage(msg);
  ASSERT_EQ(ZX_OK, result.status());
  fidl_incoming_msg_t* output = result.incoming_message();
  EXPECT_EQ(output->bytes, bytes);
  EXPECT_EQ(output->num_bytes, 16u);
  EXPECT_EQ(output->num_handles, 1u);
  EXPECT_EQ(output->handles[0].handle, ev.get());
  EXPECT_EQ(output->handles[0].type, ZX_OBJ_TYPE_EVENT);
  EXPECT_EQ(output->handles[0].rights, ZX_DEFAULT_EVENT_RIGHTS);
}
#endif
