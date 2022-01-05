// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl/llcpp/internal/transport_channel.h>
#include <lib/fidl/llcpp/message.h>
#include <lib/zx/event.h>

#include <cstring>
#include <iterator>

#include <gtest/gtest.h>

TEST(OutgoingToIncomingMessage, IovecMessage) {
  fidl_message_header_t header = {.magic_number = kFidlWireFormatMagicNumberInitial};
  uint8_t bytes[sizeof(header)];
  memcpy(bytes, &header, sizeof(header));
  zx_channel_iovec_t iovecs[1] = {
      {.buffer = bytes, .capacity = std::size(bytes), .reserved = 0},
  };
  fidl_outgoing_msg_t c_msg = {
      .type = FIDL_OUTGOING_MSG_TYPE_IOVEC,
      .iovec =
          {
              .iovecs = iovecs,
              .num_iovecs = std::size(iovecs),
          },
  };
  auto msg = fidl::OutgoingMessage::FromEncodedCMessage(&c_msg);
  auto result = fidl::OutgoingToIncomingMessage(msg);
  ASSERT_EQ(ZX_OK, result.status());
  ASSERT_EQ(std::size(bytes), result.incoming_message().byte_actual());
  EXPECT_EQ(
      0, memcmp(result.incoming_message().bytes(), bytes, result.incoming_message().byte_actual()));
  ASSERT_EQ(0u, result.incoming_message().handle_actual());
}

#ifdef __Fuchsia__
TEST(OutgoingToIncomingMessage, Handles) {
  fidl_message_header_t header = {.magic_number = kFidlWireFormatMagicNumberInitial};
  uint8_t bytes[sizeof(header)];
  memcpy(bytes, &header, sizeof(header));
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
  auto msg = fidl::OutgoingMessage::FromEncodedCMessage(&c_msg);
  auto result = fidl::OutgoingToIncomingMessage(msg);
  ASSERT_EQ(ZX_OK, result.status());
  fidl::IncomingMessage& output = result.incoming_message();
  EXPECT_EQ(output.byte_actual(), std::size(bytes));
  EXPECT_EQ(0, memcmp(output.bytes(), bytes, output.byte_actual()));
  EXPECT_EQ(output.handle_actual(), std::size(iovecs));
  EXPECT_EQ(output.handles()[0], ev.get());
  fidl_channel_handle_metadata_t* out_handle_metadata =
      output.handle_metadata<fidl::internal::ChannelTransport>();
  EXPECT_EQ(out_handle_metadata[0].obj_type, handle_metadata.obj_type);
  EXPECT_EQ(out_handle_metadata[0].rights, handle_metadata.rights);
}

TEST(OutgoingToIncomingMessage, HandlesWrongType) {
  uint8_t bytes[16];
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
  auto msg = fidl::OutgoingMessage::FromEncodedCMessage(&c_msg);
  auto result = fidl::OutgoingToIncomingMessage(msg);
  ASSERT_EQ(ZX_ERR_INVALID_ARGS, result.status());
}

TEST(OutgoingToIncomingMessage, HandlesWrongRights) {
  uint8_t bytes[16];
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
  auto msg = fidl::OutgoingMessage::FromEncodedCMessage(&c_msg);
  auto result = fidl::OutgoingToIncomingMessage(msg);
  ASSERT_EQ(ZX_ERR_INVALID_ARGS, result.status());
}
#endif
