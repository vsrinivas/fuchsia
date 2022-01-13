// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl/llcpp/internal/transport_channel.h>
#include <lib/fidl/llcpp/message.h>
#include <zircon/assert.h>
#include <zircon/syscalls.h>

#include <array>
#include <memory>
#include <string>

#include <gtest/gtest.h>

#include "src/lib/fidl/llcpp/tests/types_test_utils.h"

TEST(IncomingMessage, ConstructNonOkMessage) {
  constexpr auto kError = "test error";
  auto message =
      fidl::IncomingMessage::Create(fidl::Result::TransportError(ZX_ERR_ACCESS_DENIED, kError));
  EXPECT_FALSE(message.ok());
  EXPECT_EQ(ZX_ERR_ACCESS_DENIED, message.status());
}

TEST(IncomingMessage, ConstructNonOkMessageRequiresNonOkStatus) {
#if ZX_DEBUG_ASSERT_IMPLEMENTED
  ASSERT_DEATH({ auto message = fidl::IncomingMessage::Create(fidl::Result::DecodeError(ZX_OK)); },
               "!= ZX_OK");
#else
  GTEST_SKIP() << "Debug assertions are disabled";
#endif
}

// This fixture checks that handles have been closed at the end of the test case.
class IncomingMessageWithHandlesTest : public ::testing::Test {
 protected:
  void SetUp() override {
    fidl_init_txn_header(reinterpret_cast<fidl_message_header_t*>(bytes_), /* txid */ 1,
                         /* ordinal */ 1);

    ASSERT_EQ(ZX_OK, zx_event_create(0, &event1_));
    checker_.AddEvent(event1_);
    ASSERT_EQ(ZX_OK, zx_event_create(0, &event2_));
    checker_.AddEvent(event2_);

    handles_[0] = event1_;
    handles_[1] = event2_;
    handle_metadata_[0] = fidl_channel_handle_metadata_t{
        .obj_type = ZX_OBJ_TYPE_EVENT,
        .rights = ZX_RIGHTS_BASIC,
    };
    handle_metadata_[1] = fidl_channel_handle_metadata_t{
        .obj_type = ZX_OBJ_TYPE_EVENT,
        .rights = ZX_RIGHTS_BASIC,
    };
  }

  void TearDown() override { checker_.CheckEvents(); }

  llcpp_types_test_utils::HandleChecker checker_;
  uint8_t bytes_[sizeof(fidl_message_header_t)] = {};
  zx_handle_t event1_;
  zx_handle_t event2_;
  zx_handle_t handles_[2];
  fidl_channel_handle_metadata_t handle_metadata_[2];
};

TEST_F(IncomingMessageWithHandlesTest, AdoptHandlesFromC) {
  fidl_incoming_msg_t c_msg = {
      .bytes = bytes_,
      .handles = handles_,
      .num_bytes = static_cast<uint32_t>(std::size(bytes_)),
      .num_handles = static_cast<uint32_t>(std::size(handles_)),
  };
  auto incoming = fidl::IncomingMessage::FromEncodedCMessage(&c_msg);
  EXPECT_EQ(ZX_OK, incoming.status());
}

TEST_F(IncomingMessageWithHandlesTest, AdoptHandlesWithRegularConstructor) {
  auto incoming =
      fidl::IncomingMessage::Create(bytes_, static_cast<uint32_t>(std::size(bytes_)), handles_,
                                    handle_metadata_, static_cast<uint32_t>(std::size(handles_)));
  EXPECT_EQ(ZX_OK, incoming.status());
}

TEST_F(IncomingMessageWithHandlesTest, ReleaseHandles) {
  fidl_incoming_msg c_msg = {};

  {
    auto incoming =
        fidl::IncomingMessage::Create(bytes_, static_cast<uint32_t>(std::size(bytes_)), handles_,
                                      handle_metadata_, static_cast<uint32_t>(std::size(handles_)));
    ASSERT_EQ(ZX_OK, incoming.status());
    c_msg = std::move(incoming).ReleaseToEncodedCMessage();
    // At this point, |incoming| will not close the handles.
  }

  for (zx_handle_t event : handles_) {
    zx_info_handle_count_t info = {};
    zx_status_t status =
        zx_object_get_info(event, ZX_INFO_HANDLE_COUNT, &info, sizeof(info), nullptr, nullptr);
    ASSERT_EQ(ZX_OK, status);
    EXPECT_GT(info.handle_count, 1U);
  }

  // Adopt the handles again to close them.
  auto incoming = fidl::IncomingMessage::FromEncodedCMessage(&c_msg);
}

TEST_F(IncomingMessageWithHandlesTest, MoveConstructorHandleOwnership) {
  auto incoming =
      fidl::IncomingMessage::Create(bytes_, static_cast<uint32_t>(std::size(bytes_)), handles_,
                                    handle_metadata_, static_cast<uint32_t>(std::size(handles_)));
  fidl::IncomingMessage another{std::move(incoming)};
  EXPECT_EQ(incoming.handle_actual(), 0u);
  EXPECT_GT(another.handle_actual(), 0u);
  EXPECT_EQ(ZX_OK, another.status());
}

TEST(IncomingMessage, ValidateTransactionalMessageHeader) {
  uint8_t bytes[sizeof(fidl_message_header_t)] = {};
  auto* hdr = reinterpret_cast<fidl_message_header_t*>(bytes);
  fidl_init_txn_header(hdr, /* txid */ 1, /* ordinal */ 1);
  // Unsupported wire-format magic number.
  hdr->magic_number = 42;

  {
    auto incoming = fidl::IncomingMessage::Create<fidl::internal::ChannelTransport>(
        bytes, static_cast<uint32_t>(std::size(bytes)), nullptr, nullptr, 0);
    EXPECT_EQ(ZX_ERR_PROTOCOL_NOT_SUPPORTED, incoming.status());
    EXPECT_FALSE(incoming.ok());
  }

  {
    auto incoming = fidl::IncomingMessage::Create<fidl::internal::ChannelTransport>(
        bytes, static_cast<uint32_t>(std::size(bytes)), nullptr, nullptr, 0,
        fidl::IncomingMessage::kSkipMessageHeaderValidation);
    EXPECT_EQ(ZX_OK, incoming.status());
    EXPECT_TRUE(incoming.ok());
  }
}

class IncomingMessageChannelReadEtcTest : public ::testing::Test {
 protected:
  void SetUp() override {
    byte_buffer_ = std::make_unique<std::array<uint8_t, ZX_CHANNEL_MAX_MSG_BYTES>>();
    handle_buffer_ = std::make_unique<std::array<zx_handle_t, ZX_CHANNEL_MAX_MSG_HANDLES>>();
    handle_metadata_buffer_ =
        std::make_unique<std::array<fidl_channel_handle_metadata_t, ZX_CHANNEL_MAX_MSG_HANDLES>>();
  }

  fidl::BufferSpan byte_buffer_view() {
    return fidl::BufferSpan(byte_buffer_->data(), ZX_CHANNEL_MAX_MSG_HANDLES);
  }

  zx_handle_t* handle_data() { return handle_buffer_->data(); }
  fidl_channel_handle_metadata_t* handle_metadata_data() { return handle_metadata_buffer_->data(); }
  uint32_t handle_buffer_size() { return static_cast<uint32_t>(handle_buffer_->size()); }

 private:
  std::unique_ptr<std::array<uint8_t, ZX_CHANNEL_MAX_MSG_BYTES>> byte_buffer_;
  std::unique_ptr<std::array<zx_handle_t, ZX_CHANNEL_MAX_MSG_HANDLES>> handle_buffer_;
  std::unique_ptr<std::array<fidl_channel_handle_metadata_t, ZX_CHANNEL_MAX_MSG_HANDLES>>
      handle_metadata_buffer_;
};

TEST_F(IncomingMessageChannelReadEtcTest, ReadFromChannel) {
  zx::channel source, sink;
  ASSERT_EQ(ZX_OK, zx::channel::create(0, &source, &sink));

  uint8_t bytes[sizeof(fidl_message_header_t)] = {};
  auto* hdr = reinterpret_cast<fidl_message_header_t*>(bytes);
  fidl_init_txn_header(hdr, /* txid */ 1, /* ordinal */ 1);
  sink.write(0, bytes, std::size(bytes), nullptr, 0);

  auto incoming = fidl::MessageRead(source, byte_buffer_view(), handle_data(),
                                    handle_metadata_data(), handle_buffer_size());
  EXPECT_EQ(ZX_OK, incoming.status());
  EXPECT_EQ(incoming.byte_actual(), sizeof(fidl_message_header_t));
  EXPECT_EQ(0, memcmp(incoming.bytes(), bytes, incoming.byte_actual()));
  EXPECT_EQ(0u, incoming.handle_actual());

  auto incoming2 = fidl::MessageRead(source, byte_buffer_view(), handle_data(),
                                     handle_metadata_data(), handle_buffer_size());
  EXPECT_EQ(ZX_ERR_SHOULD_WAIT, incoming2.status());
  EXPECT_EQ(fidl::Reason::kTransportError, incoming2.reason());
  EXPECT_EQ(
      "FIDL operation failed due to underlying transport I/O error, "
      "status: ZX_ERR_SHOULD_WAIT (-22)",
      incoming2.FormatDescription());
}

TEST_F(IncomingMessageChannelReadEtcTest, ReadFromClosedChannel) {
  zx::channel source, sink;
  ASSERT_EQ(ZX_OK, zx::channel::create(0, &source, &sink));

  sink.reset();
  auto incoming = fidl::MessageRead(source, byte_buffer_view(), handle_data(),
                                    handle_metadata_data(), handle_buffer_size());
  EXPECT_EQ(ZX_ERR_PEER_CLOSED, incoming.status());
  EXPECT_EQ(fidl::Reason::kPeerClosed, incoming.reason());
}

TEST_F(IncomingMessageChannelReadEtcTest, ReadFromChannelInvalidMessage) {
  zx::channel source, sink;
  ASSERT_EQ(ZX_OK, zx::channel::create(0, &source, &sink));

  uint8_t bytes[sizeof(fidl_message_header_t)] = {};
  auto* hdr = reinterpret_cast<fidl_message_header_t*>(bytes);
  // An epitaph must have zero txid, so the following is invalid.
  fidl_init_txn_header(hdr, /* txid */ 42, /* ordinal */ kFidlOrdinalEpitaph);
  sink.write(0, bytes, std::size(bytes), nullptr, 0);

  auto incoming = fidl::MessageRead(source, byte_buffer_view(), handle_data(),
                                    handle_metadata_data(), handle_buffer_size());
  EXPECT_EQ(ZX_ERR_INVALID_ARGS, incoming.status());
  EXPECT_EQ(fidl::Reason::kUnexpectedMessage, incoming.reason());
  EXPECT_EQ(
      "FIDL operation failed due to unexpected message, "
      "status: ZX_ERR_INVALID_ARGS (-10), detail: invalid header",
      incoming.FormatDescription());
}
