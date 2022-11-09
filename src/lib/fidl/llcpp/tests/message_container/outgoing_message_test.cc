// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fidl.llcpp.empty.test/cpp/wire_messaging.h>
#include <fidl/fidl.llcpp.linearized.test/cpp/wire_types.h>
#include <fidl/fidl.test.misc/cpp/wire_messaging.h>
#include <lib/fidl/cpp/wire/message.h>
#include <lib/fidl/cpp/wire/wire_messaging_declarations.h>
#include <zircon/errors.h>

#include <iterator>

#include <gtest/gtest.h>

#include "message_checkers.h"

using fidl_testing::MessageChecker;

TEST(OutgoingMessage, CreateWithInternalIovecConstructorArgs) {
  zx_channel_iovec_t iovecs[1];
  zx_handle_t handles[2];
  fidl_channel_handle_metadata_t handle_metadata[2];
  uint8_t backing_buffer[1];
  fidl::OutgoingMessage msg = fidl::OutgoingMessage::Create_InternalMayBreak(
      fidl::OutgoingMessage::InternalIovecConstructorArgs{
          .transport_vtable = &fidl::internal::ChannelTransport::VTable,
          .iovecs = iovecs,
          .iovec_capacity = std::size(iovecs),
          .handles = handles,
          .handle_metadata = reinterpret_cast<fidl_handle_metadata_t*>(handle_metadata),
          .handle_capacity = std::size(handles),
          .backing_buffer = backing_buffer,
          .backing_buffer_capacity = std::size(backing_buffer),
      });
  // Capacities are stored but not exposed. Actual sizes are zero initialized.
  EXPECT_EQ(0u, msg.iovec_actual());
  EXPECT_EQ(iovecs, msg.iovecs());
  EXPECT_EQ(0u, msg.handle_actual());
  EXPECT_EQ(handles, msg.handles());
  EXPECT_EQ(FIDL_TRANSPORT_TYPE_CHANNEL, msg.transport_type());
  EXPECT_EQ(handle_metadata, msg.handle_metadata<fidl::internal::ChannelTransport>());
}

TEST(OutgoingMessage, CreateWithInternalByteBackedConstructorArgs) {
  uint8_t bytes[3] = {1, 2, 3};
  fidl_handle_t handles[2];
  fidl_channel_handle_metadata_t handle_metadata[2];
  fidl::OutgoingMessage msg = fidl::OutgoingMessage::Create_InternalMayBreak(
      fidl::OutgoingMessage::InternalByteBackedConstructorArgs{
          .transport_vtable = &fidl::internal::ChannelTransport::VTable,
          .bytes = bytes,
          .num_bytes = std::size(bytes),
          .handles = handles,
          .handle_metadata = reinterpret_cast<fidl_handle_metadata_t*>(handle_metadata),
          .num_handles = std::size(handles),
      });
  // Capacities are stored but not exposed. Actual sizes are zero initialized.
  EXPECT_EQ(FIDL_TRANSPORT_TYPE_CHANNEL, msg.transport_type());
  EXPECT_EQ(1u, msg.iovec_actual());
  EXPECT_NE(nullptr, msg.iovecs());
  EXPECT_EQ(2u, msg.handle_actual());
  EXPECT_EQ(handles, msg.handles());
  EXPECT_EQ(handle_metadata, msg.handle_metadata<fidl::internal::ChannelTransport>());

  auto copied_bytes = msg.CopyBytes();
  EXPECT_EQ(3u, copied_bytes.size());
  EXPECT_EQ(1u, copied_bytes.data()[0]);
  EXPECT_EQ(2u, copied_bytes.data()[1]);
  EXPECT_EQ(3u, copied_bytes.data()[2]);
}

TEST(OutgoingMessage, ConstructFromCIovecMessage) {
  zx_channel_iovec_t iovec = {
      .buffer = nullptr,
      .capacity = 0,
      .reserved = 0,
  };
  zx_handle_t handle = ZX_HANDLE_INVALID;
  fidl_channel_handle_metadata_t handle_metadata = {
      .obj_type = ZX_OBJ_TYPE_CHANNEL,
      .rights = ZX_RIGHT_SAME_RIGHTS,
  };
  fidl_outgoing_msg_t c_msg = {
      .type = FIDL_OUTGOING_MSG_TYPE_IOVEC,
      .iovec =
          {
              .iovecs = &iovec,
              .num_iovecs = 1,
              .handles = &handle,
              .handle_metadata = reinterpret_cast<fidl_handle_metadata_t*>(&handle_metadata),
              .num_handles = 1,
          },
  };
  auto msg = fidl::OutgoingMessage::FromEncodedCMessage(&c_msg);
  ASSERT_EQ(FIDL_OUTGOING_MSG_TYPE_IOVEC, MessageChecker::GetCMessage(msg)->type);
  ASSERT_EQ(&iovec, msg.iovecs());
  ASSERT_EQ(1u, msg.iovec_actual());
  ASSERT_EQ(&handle, msg.handles());
  ASSERT_EQ(FIDL_TRANSPORT_TYPE_CHANNEL, msg.transport_type());
  EXPECT_EQ(&handle_metadata, msg.handle_metadata<fidl::internal::ChannelTransport>());
  ASSERT_EQ(1u, msg.handle_actual());
}

TEST(OutgoingMessage, ConstructFromCByteMessage) {
  uint8_t bytes[] = {1, 2, 3, 4};
  zx_handle_t handle = ZX_HANDLE_INVALID;
  fidl_channel_handle_metadata_t handle_metadata = {
      .obj_type = ZX_OBJ_TYPE_CHANNEL,
      .rights = ZX_RIGHT_SAME_RIGHTS,
  };
  fidl_outgoing_msg_t c_msg = {
      .type = FIDL_OUTGOING_MSG_TYPE_BYTE,
      .byte =
          {
              .bytes = bytes,
              .handles = &handle,
              .handle_metadata = reinterpret_cast<fidl_handle_metadata_t*>(&handle_metadata),
              .num_bytes = std::size(bytes),
              .num_handles = 1,
          },
  };
  auto msg = fidl::OutgoingMessage::FromEncodedCMessage(&c_msg);
  ASSERT_EQ(FIDL_OUTGOING_MSG_TYPE_IOVEC, MessageChecker::GetCMessage(msg)->type);

  ASSERT_NE(nullptr, msg.iovecs());
  ASSERT_EQ(1u, msg.iovec_actual());
  const auto& msg_iovec0 = msg.iovecs()[0];
  ASSERT_EQ(bytes, msg_iovec0.buffer);
  ASSERT_EQ(std::size(bytes), msg_iovec0.capacity);
  ASSERT_EQ(0u, msg_iovec0.reserved);

  ASSERT_EQ(&handle, msg.handles());
  ASSERT_EQ(1u, msg.handle_actual());
}

TEST(OutgoingMessage, OutgoingMessageBytesMatch) {
  uint8_t bytes_a1[]{1};
  uint8_t bytes_a2[]{2, 3, 4};
  zx_channel_iovec_t iovecs_a[] = {
      {
          .buffer = bytes_a1,
          .capacity = std::size(bytes_a1),
          .reserved = 0,
      },
      {
          .buffer = bytes_a2,
          .capacity = std::size(bytes_a2),
          .reserved = 0,
      },
  };
  fidl_outgoing_msg_t c_msg_a = {
      .type = FIDL_OUTGOING_MSG_TYPE_IOVEC,
      .iovec =
          {
              .iovecs = iovecs_a,
              .num_iovecs = std::size(iovecs_a),
          },
  };
  auto msg_a = fidl::OutgoingMessage::FromEncodedCMessage(&c_msg_a);

  uint8_t bytes_b1[]{1, 2};
  uint8_t bytes_b2[]{3};
  uint8_t bytes_b3[]{4};
  zx_channel_iovec_t iovecs_b[] = {
      {
          .buffer = bytes_b1,
          .capacity = std::size(bytes_b1),
          .reserved = 0,
      },
      {
          .buffer = bytes_b2,
          .capacity = std::size(bytes_b2),
          .reserved = 0,
      },
      {
          .buffer = bytes_b3,
          .capacity = std::size(bytes_b3),
          .reserved = 0,
      },
  };
  fidl_outgoing_msg_t c_msg_b = {
      .type = FIDL_OUTGOING_MSG_TYPE_IOVEC,
      .iovec =
          {
              .iovecs = iovecs_b,
              .num_iovecs = std::size(iovecs_b),
          },
  };
  auto msg_b = fidl::OutgoingMessage::FromEncodedCMessage(&c_msg_b);

  EXPECT_TRUE(msg_a.BytesMatch(msg_b));
  EXPECT_TRUE(msg_b.BytesMatch(msg_a));
}

TEST(OutgoingMessage, OutgoingMessageBytesMatchIgnoreHandles) {
  uint8_t bytes[]{1, 2, 3, 4};
  zx_channel_iovec_t iovecs[] = {
      {
          .buffer = bytes,
          .capacity = std::size(bytes),
          .reserved = 0,
      },
  };
  fidl_outgoing_msg_t c_msg_without_handles = {
      .type = FIDL_OUTGOING_MSG_TYPE_IOVEC,
      .iovec =
          {
              .iovecs = iovecs,
              .num_iovecs = std::size(iovecs),
          },
  };
  auto msg_without_handles = fidl::OutgoingMessage::FromEncodedCMessage(&c_msg_without_handles);

  // Bytes should match even if one has handles and the other doesn't.
  zx_handle_t handle;
  fidl_channel_handle_metadata_t handle_metadata;
  ASSERT_EQ(ZX_OK, zx_event_create(0, &handle));
  fidl_outgoing_msg_t c_msg_with_handles = {
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
  auto msg_with_handles = fidl::OutgoingMessage::FromEncodedCMessage(&c_msg_with_handles);

  EXPECT_TRUE(msg_without_handles.BytesMatch(msg_with_handles));
  EXPECT_TRUE(msg_with_handles.BytesMatch(msg_without_handles));
}

TEST(OutgoingMessage, OutgoingMessageBytesMismatchByteLength) {
  uint8_t bytes[]{1, 2, 3};

  // 2 bytes.
  zx_channel_iovec_t iovecs_a[] = {
      {
          .buffer = bytes,
          .capacity = 2,
          .reserved = 0,
      },
  };
  fidl_outgoing_msg_t c_msg_a = {
      .type = FIDL_OUTGOING_MSG_TYPE_IOVEC,
      .iovec =
          {
              .iovecs = iovecs_a,
              .num_iovecs = std::size(iovecs_a),
          },
  };
  auto msg_a = fidl::OutgoingMessage::FromEncodedCMessage(&c_msg_a);

  // 3 bytes.
  zx_channel_iovec_t iovecs_b[] = {
      {
          .buffer = bytes,
          .capacity = 3,
          .reserved = 0,
      },
  };
  fidl_outgoing_msg_t c_msg_b = {
      .type = FIDL_OUTGOING_MSG_TYPE_IOVEC,
      .iovec =
          {
              .iovecs = iovecs_b,
              .num_iovecs = std::size(iovecs_b),
          },
  };
  auto msg_b = fidl::OutgoingMessage::FromEncodedCMessage(&c_msg_b);

  EXPECT_FALSE(msg_a.BytesMatch(msg_b));
  EXPECT_FALSE(msg_b.BytesMatch(msg_a));
}

TEST(OutgoingMessage, OutgoingMessageBytesMismatchIovecLength) {
  uint8_t bytes1[]{1, 2};
  uint8_t bytes2[]{3};

  // 1 iovec.
  zx_channel_iovec_t iovecs_a[] = {
      {
          .buffer = bytes1,
          .capacity = sizeof(bytes1),
          .reserved = 0,
      },
  };
  fidl_outgoing_msg_t c_msg_a = {
      .type = FIDL_OUTGOING_MSG_TYPE_IOVEC,
      .iovec =
          {
              .iovecs = iovecs_a,
              .num_iovecs = std::size(iovecs_a),
          },
  };
  auto msg_a = fidl::OutgoingMessage::FromEncodedCMessage(&c_msg_a);

  // 2 iovecs.
  zx_channel_iovec_t iovecs_b[] = {
      {
          .buffer = bytes1,
          .capacity = std::size(bytes1),
          .reserved = 0,
      },
      {
          .buffer = bytes2,
          .capacity = std::size(bytes2),
          .reserved = 0,
      },
  };
  fidl_outgoing_msg_t c_msg_b = {
      .type = FIDL_OUTGOING_MSG_TYPE_IOVEC,
      .iovec =
          {
              .iovecs = iovecs_b,
              .num_iovecs = std::size(iovecs_b),
          },
  };
  auto msg_b = fidl::OutgoingMessage::FromEncodedCMessage(&c_msg_b);

  EXPECT_FALSE(msg_a.BytesMatch(msg_b));
  EXPECT_FALSE(msg_b.BytesMatch(msg_a));
}

TEST(OutgoingMessage, OutgoingMessageBytesMismatch) {
  uint8_t bytes_a1[]{1};
  uint8_t bytes_a2[]{2, 3, 4};
  zx_channel_iovec_t iovecs_a[] = {
      {
          .buffer = bytes_a1,
          .capacity = std::size(bytes_a1),
          .reserved = 0,
      },
      {
          .buffer = bytes_a2,
          .capacity = std::size(bytes_a2),
          .reserved = 0,
      },
  };
  fidl_outgoing_msg_t c_msg_a = {
      .type = FIDL_OUTGOING_MSG_TYPE_IOVEC,
      .iovec =
          {
              .iovecs = iovecs_a,
              .num_iovecs = std::size(iovecs_a),
          },
  };
  auto msg_a = fidl::OutgoingMessage::FromEncodedCMessage(&c_msg_a);

  uint8_t bytes_b1[]{1, 2};
  uint8_t bytes_b2[]{5};
  uint8_t bytes_b3[]{4};
  zx_channel_iovec_t iovecs_b[] = {
      {
          .buffer = bytes_b1,
          .capacity = std::size(bytes_b1),
          .reserved = 0,
      },
      {
          .buffer = bytes_b2,
          .capacity = std::size(bytes_b2),
          .reserved = 0,
      },
      {
          .buffer = bytes_b3,
          .capacity = std::size(bytes_b3),
          .reserved = 0,
      },
  };
  fidl_outgoing_msg_t c_msg_b = {
      .type = FIDL_OUTGOING_MSG_TYPE_IOVEC,
      .iovec =
          {
              .iovecs = iovecs_b,
              .num_iovecs = std::size(iovecs_b),
          },
  };
  auto msg_b = fidl::OutgoingMessage::FromEncodedCMessage(&c_msg_b);

  EXPECT_FALSE(msg_a.BytesMatch(msg_b));
  EXPECT_FALSE(msg_b.BytesMatch(msg_a));
}

TEST(OutgoingMessage, OutgoingMessageCopiedBytes) {
  uint8_t bytes1[]{1, 2};
  uint8_t bytes2[]{3};
  uint8_t bytes3[]{4};
  zx_channel_iovec_t iovecs[] = {
      {
          .buffer = bytes1,
          .capacity = std::size(bytes1),
          .reserved = 0,
      },
      {
          .buffer = bytes2,
          .capacity = std::size(bytes2),
          .reserved = 0,
      },
      {
          .buffer = bytes3,
          .capacity = std::size(bytes3),
          .reserved = 0,
      },
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

  uint8_t expected_bytes[] = {1, 2, 3, 4};
  EXPECT_EQ(4u, msg.CountBytes());
  fidl::OutgoingMessage::CopiedBytes msg_bytes = msg.CopyBytes();
  EXPECT_EQ(std::size(expected_bytes), msg_bytes.size());
  EXPECT_EQ(0, memcmp(expected_bytes, msg_bytes.data(), std::size(expected_bytes)));
}

TEST(OutgoingMessage, SettingTxIdRequiresTransactionalMessageNegative) {
  fidl_llcpp_linearized_test::wire::NoOpLinearizedStruct value{.x = 42};
  fidl::unstable::OwnedEncodedMessage<fidl_llcpp_linearized_test::wire::NoOpLinearizedStruct>
      encoded(fidl::internal::WireFormatVersion::kV2, &value);
  ASSERT_EQ(ZX_OK, encoded.status());
  ASSERT_DEATH({ encoded.GetOutgoingMessage().set_txid(1); }, "transactional");
}

TEST(OutgoingMessage, SettingTxIdRequiresTransactionalMessagePositive) {
  using Request = fidl::internal::TransactionalRequest<fidl_test_misc::Echo::EchoString>;
  Request request{fidl::StringView("")};
  fidl::unstable::OwnedEncodedMessage<Request> encoded(fidl::internal::WireFormatVersion::kV2,
                                                       &request);
  ASSERT_EQ(ZX_OK, encoded.status());
  encoded.GetOutgoingMessage().set_txid(1);
}

TEST(OutgoingMessage, GoodEncodeNoBody) {
  zx_channel_iovec_t iovecs[1];
  //   zx_handle_t handles[1];
  //   fidl_channel_handle_metadata_t handle_metadata[1];
  uint8_t backing_buffer[16];
  fidl::OutgoingMessage msg = fidl::OutgoingMessage::Create_InternalMayBreak(
      fidl::OutgoingMessage::InternalIovecConstructorArgs{
          .transport_vtable = &fidl::internal::ChannelTransport::VTable,
          .iovecs = iovecs,
          .iovec_capacity = std::size(iovecs),
          .handles = nullptr,
          .handle_metadata = nullptr,
          .handle_capacity = 0u,
          .backing_buffer = backing_buffer,
          .backing_buffer_capacity = std::size(backing_buffer),
      });

  using Request = fidl::internal::TransactionalRequest<fidl_llcpp_empty_test::OnlyEmpty::Empty>;
  Request request;
  fidl::InitTxnHeader(
      &request.header, 1,
      ::fidl::internal::WireOrdinal<::fidl_llcpp_empty_test::OnlyEmpty::Empty>::value,
      fidl::MessageDynamicFlags::kStrictMethod);

  msg.Encode<Request>(fidl::internal::WireFormatVersion::kV2, &request);
  ASSERT_EQ(ZX_OK, msg.status());
}
