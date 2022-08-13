// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl/cpp/wire/internal/transport_channel.h>
#include <lib/fidl/cpp/wire/message.h>
#include <lib/zx/event.h>

#include <cstring>
#include <iterator>

#include <gtest/gtest.h>

// Test that |OutgoingToIncomingMessage| concatenates iovec elements correctly.
TEST(OutgoingToIncomingMessage, IovecMessage) {
  uint8_t bytes1[8] = {
      0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
  };
  uint8_t bytes2[8] = {
      0xff, 0x00, 0xff, 0x00, 0xff, 0x00, 0xff, 0x00,
  };
  zx_channel_iovec_t iovecs[2] = {
      {.buffer = bytes1, .capacity = std::size(bytes1), .reserved = 0},
      {.buffer = bytes2, .capacity = std::size(bytes2), .reserved = 0},
  };
  fidl_outgoing_msg_t c_msg = {
      .type = FIDL_OUTGOING_MSG_TYPE_IOVEC,
      .iovec =
          {
              .iovecs = iovecs,
              .num_iovecs = std::size(iovecs),
          },
  };
  auto msg = fidl::OutgoingMessage::FromEncodedCValue(&c_msg);
  auto result = fidl::OutgoingToIncomingMessage(msg);
  ASSERT_EQ(ZX_OK, result.status());
  ASSERT_EQ(std::size(bytes1) + std::size(bytes2), result.incoming_message().byte_actual());
  EXPECT_EQ(0, memcmp(result.incoming_message().bytes(), bytes1, std::size(bytes1)));
  EXPECT_EQ(
      0, memcmp(&result.incoming_message().bytes()[std::size(bytes1)], bytes2, std::size(bytes2)));
  ASSERT_EQ(0u, result.incoming_message().handle_actual());
}

#ifdef __Fuchsia__
// Test that |OutgoingToIncomingMessage| converts handle metadata.
TEST(OutgoingToIncomingMessage, Handles) {
  uint8_t bytes[8] = {
      0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
  };
  zx::event ev;
  ASSERT_EQ(ZX_OK, zx::event::create(0, &ev));
  zx_handle_t handle = ev.get();
  fidl_channel_handle_metadata_t handle_metadata = {
      .obj_type = ZX_OBJ_TYPE_EVENT,
      .rights = ZX_DEFAULT_EVENT_RIGHTS,
  };
  zx_channel_iovec_t iovecs[1] = {
      {.buffer = bytes, .capacity = std::size(bytes), .reserved = 0},
  };
  fidl_outgoing_msg_t c_msg = {
      .type = FIDL_OUTGOING_MSG_TYPE_IOVEC,
      .iovec =
          {
              .iovecs = iovecs,
              .num_iovecs = std::size(iovecs),
              .handles = &handle,
              .handle_metadata = reinterpret_cast<fidl_handle_metadata_t*>(&handle_metadata),
              .num_handles = 1,
          },
  };
  auto msg = fidl::OutgoingMessage::FromEncodedCValue(&c_msg);
  auto result = fidl::OutgoingToIncomingMessage(msg);
  ASSERT_EQ(ZX_OK, result.status());
  fidl::IncomingMessage& output = result.incoming_message();
  EXPECT_EQ(output.byte_actual(), std::size(bytes));
  EXPECT_EQ(0, memcmp(output.bytes(), bytes, output.byte_actual()));
  EXPECT_EQ(output.handle_actual(), 1u);
  EXPECT_EQ(output.handles()[0], ev.get());
  fidl_channel_handle_metadata_t* out_handle_metadata =
      output.handle_metadata<fidl::internal::ChannelTransport>();
  EXPECT_EQ(out_handle_metadata[0].obj_type, handle_metadata.obj_type);
  EXPECT_EQ(out_handle_metadata[0].rights, handle_metadata.rights);
}

// Test that |OutgoingToIncomingMessage| rejects handles with wrong type.
TEST(OutgoingToIncomingMessage, HandlesWrongType) {
  uint8_t bytes[16];
  memset(bytes, 0, std::size(bytes));
  zx::event ev;
  ASSERT_EQ(ZX_OK, zx::event::create(0, &ev));
  zx_handle_t handle = ev.get();
  fidl_channel_handle_metadata_t handle_metadata = {
      .obj_type = ZX_OBJ_TYPE_CHANNEL,
      .rights = ZX_RIGHT_SAME_RIGHTS,
  };
  zx_channel_iovec_t iovecs[] = {
      {.buffer = bytes, .capacity = std::size(bytes), .reserved = 0},
  };
  fidl_outgoing_msg_t c_msg = {
      .type = FIDL_OUTGOING_MSG_TYPE_IOVEC,
      .iovec =
          {
              .iovecs = iovecs,
              .num_iovecs = std::size(iovecs),
              .handles = &handle,
              .handle_metadata = reinterpret_cast<fidl_handle_metadata_t*>(&handle_metadata),
              .num_handles = 1,
          },
  };
  auto msg = fidl::OutgoingMessage::FromEncodedCValue(&c_msg);
  auto result = fidl::OutgoingToIncomingMessage(msg);
  ASSERT_EQ(ZX_ERR_INVALID_ARGS, result.status());
  ASSERT_EQ(fidl::Reason::kEncodeError, result.error().reason());
}

// Test that |OutgoingToIncomingMessage| rejects handles with too few rights.
TEST(OutgoingToIncomingMessage, HandlesWrongRights) {
  uint8_t bytes[16];
  memset(bytes, 0, std::size(bytes));
  zx::event ev;
  ASSERT_EQ(ZX_OK, zx::event::create(0, &ev));
  zx_handle_t handle = ev.get();
  fidl_channel_handle_metadata_t handle_metadata = {
      .obj_type = ZX_OBJ_TYPE_EVENT,
      .rights = ZX_RIGHT_DESTROY,
  };
  zx_channel_iovec_t iovecs[1] = {
      {.buffer = bytes, .capacity = std::size(bytes), .reserved = 0},
  };
  fidl_outgoing_msg_t c_msg = {
      .type = FIDL_OUTGOING_MSG_TYPE_IOVEC,
      .iovec =
          {
              .iovecs = iovecs,
              .num_iovecs = std::size(iovecs),
              .handles = &handle,
              .handle_metadata = reinterpret_cast<fidl_handle_metadata_t*>(&handle_metadata),
              .num_handles = 1,
          },
  };
  auto msg = fidl::OutgoingMessage::FromEncodedCValue(&c_msg);
  auto result = fidl::OutgoingToIncomingMessage(msg);
  ASSERT_EQ(ZX_ERR_INVALID_ARGS, result.status());
  ASSERT_EQ(fidl::Reason::kEncodeError, result.error().reason());
}
#endif
