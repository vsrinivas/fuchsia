// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/test/handles/cpp/fidl.h>
#include <lib/fidl/cpp/builder.h>
#include <lib/fidl/cpp/message.h>
#include <lib/fidl/cpp/message_builder.h>
#include <lib/zx/channel.h>
#include <lib/zx/event.h>

#include <zxtest/zxtest.h>

namespace {

TEST(Message, BasicTests) {
  uint8_t byte_buffer[ZX_CHANNEL_MAX_MSG_BYTES];
  zx_handle_info_t handle_info_buffer[ZX_CHANNEL_MAX_MSG_HANDLES];
  zx_handle_disposition_t handle_disposition_buffer[ZX_CHANNEL_MAX_MSG_HANDLES];

  fidl::Builder builder(byte_buffer, ZX_CHANNEL_MAX_MSG_BYTES);

  fidl_message_header_t* header = builder.New<fidl_message_header_t>();
  header->txid = 5u;
  header->ordinal = 42u;

  fidl_string_t* view = builder.New<fidl_string_t>();

  char* data = builder.NewArray<char>(4);
  view->data = data;
  view->size = 4;

  data[0] = 'a';
  data[1] = 'b';
  data[2] = 'c';

  fidl::HLCPPOutgoingMessage outgoing_message(
      builder.Finalize(),
      fidl::HandleDispositionPart(handle_disposition_buffer, ZX_CHANNEL_MAX_MSG_HANDLES));

  EXPECT_EQ(outgoing_message.txid(), 5u);
  EXPECT_EQ(outgoing_message.ordinal(), 42u);

  fidl::BytePart payload = fidl::BytePart(outgoing_message.body_view().bytes(), 0);
  EXPECT_EQ(reinterpret_cast<fidl_string_t*>(payload.data()), view);

  zx::channel h1, h2;
  EXPECT_EQ(zx::channel::create(0, &h1, &h2), ZX_OK);

  EXPECT_EQ(ZX_OK, outgoing_message.Write(h1.get(), 0u));

  memset(byte_buffer, 0, ZX_CHANNEL_MAX_MSG_BYTES);

  EXPECT_EQ(outgoing_message.txid(), 0u);
  EXPECT_EQ(outgoing_message.ordinal(), 0u);

  fidl::HLCPPIncomingMessage incoming_message(
      fidl::BytePart(byte_buffer, ZX_CHANNEL_MAX_MSG_BYTES),
      fidl::HandleInfoPart(handle_info_buffer, ZX_CHANNEL_MAX_MSG_HANDLES));
  EXPECT_EQ(ZX_OK, incoming_message.Read(h2.get(), 0u));

  EXPECT_EQ(incoming_message.txid(), 5u);
  EXPECT_EQ(incoming_message.ordinal(), 42u);
}

TEST(Message, ReadErrorCodes) {
  // Create a Message buffer.
  constexpr size_t kBufferSize = 100;
  uint8_t byte_buffer[kBufferSize];
  fidl::HLCPPIncomingMessage message(fidl::BytePart::WrapEmpty(byte_buffer),
                                     fidl::HandleInfoPart());

  // Create a channel.
  zx::channel client, server;
  EXPECT_OK(zx::channel::create(0, &client, &server));

  // Read from an empty channel.
  EXPECT_EQ(message.Read(client.get(), /*flags=*/0), ZX_ERR_SHOULD_WAIT);

  // Read with invalid flags.
  EXPECT_EQ(message.Read(client.get(), /*flags=*/~0), ZX_ERR_NOT_SUPPORTED);

  // Read a message smaller than the FIDL header size.
  {
    uint8_t write_buffer[1] = {0};
    EXPECT_OK(
        server.write(/*flags=*/0, &write_buffer, sizeof(write_buffer), /*handles=*/nullptr, 0));
    EXPECT_EQ(message.Read(client.get(), /*flags=*/0), ZX_ERR_INVALID_ARGS);
  }

  // Read a message larger than our receive buffer.
  {
    uint8_t write_buffer[kBufferSize + 1];
    memset(write_buffer, 0xff, sizeof(write_buffer));
    EXPECT_OK(
        server.write(/*flags=*/0, &write_buffer, sizeof(write_buffer), /*handles=*/nullptr, 0));
    EXPECT_EQ(message.Read(client.get(), /*flags=*/ZX_CHANNEL_READ_MAY_DISCARD),
              ZX_ERR_BUFFER_TOO_SMALL);
  }

  // Read from closed channel.
  server.reset();
  EXPECT_EQ(message.Read(client.get(), /*flags=*/0), ZX_ERR_PEER_CLOSED);
}

TEST(MessageBuilder, BasicTests) {
  zx::event e;
  EXPECT_EQ(zx::event::create(0, &e), ZX_OK);
  EXPECT_NE(e.get(), ZX_HANDLE_INVALID);

  // Note: |MessageBuilder| takes a coding table which is strictly speaking
  // internal API.
  fidl::MessageBuilder builder(
      &fidl::test::handles::_internal::fidl_test_handles_FooBarRequestTable);
  builder.header()->txid = 5u;
  builder.header()->ordinal = 42u;

  zx_handle_t* handle_ptr = builder.New<zx_handle_t>();
  zx_handle_t handle_value = e.release();
  *handle_ptr = handle_value;

  fidl::HLCPPOutgoingMessage message;
  const char* error_msg;
  EXPECT_EQ(builder.Encode(&message, &error_msg), ZX_OK);

  EXPECT_EQ(message.txid(), 5u);
  EXPECT_EQ(message.ordinal(), 42u);
  EXPECT_EQ(message.handles().actual(), 1u);
  EXPECT_EQ(message.handles().size(), 1u);
  EXPECT_EQ(message.handles().data()[0].handle, handle_value);
}

TEST(MessagePart, IsStlContainerTest) {
  EXPECT_EQ(sizeof(uint8_t), sizeof(fidl::BytePart::value_type));
  EXPECT_EQ(sizeof(zx_handle_t), sizeof(fidl::HandlePart::value_type));

  EXPECT_EQ(sizeof(const uint8_t*), sizeof(fidl::BytePart::const_iterator));
  EXPECT_EQ(sizeof(const zx_handle_t*), sizeof(fidl::HandlePart::const_iterator));
}

TEST(MessagePart, Size) {
  fidl::HLCPPOutgoingMessage message;

  EXPECT_EQ(message.bytes().size(), 0u);

  uint8_t dummy_msg[42];
  fidl::MessagePart msg(dummy_msg, 42, 10);

  EXPECT_EQ(msg.size(), 10u);

  fidl::MessagePart new_msg = std::move(msg);

  EXPECT_EQ(new_msg.size(), 10u);
  EXPECT_EQ(msg.size(), 0u);
}

TEST(MessagePart, WrapArray) {
  uint8_t dummy[42];

  auto full = fidl::MessagePart<uint8_t>::WrapFull(dummy);
  EXPECT_EQ(full.data(), dummy);
  EXPECT_EQ(full.actual(), 42);
  EXPECT_EQ(full.capacity(), 42);

  auto empty = fidl::MessagePart<uint8_t>::WrapEmpty(dummy);
  EXPECT_EQ(empty.data(), dummy);
  EXPECT_EQ(empty.actual(), 0);
  EXPECT_EQ(empty.capacity(), 42);
}

}  // namespace
