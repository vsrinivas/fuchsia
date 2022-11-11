// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fidl.llcpp.buffersize.test/cpp/wire.h>
#include <lib/fidl/cpp/wire/sync_call.h>

#include <zxtest/zxtest.h>

using Protocol = ::fidl_llcpp_buffersize_test::Protocol;

TEST(MessageBufferSize, InlineMessageBuffer) {
  fidl::internal::InlineMessageBuffer<32> buffer;
  ASSERT_EQ(32u, buffer.size());
  ASSERT_EQ(reinterpret_cast<uint8_t*>(&buffer), buffer.data());
  ASSERT_EQ(buffer.data(), buffer.view().data);
  ASSERT_EQ(32u, buffer.view().capacity);

  const fidl::internal::InlineMessageBuffer<32> const_buffer;
  ASSERT_EQ(reinterpret_cast<const uint8_t*>(&const_buffer), const_buffer.data());
}

TEST(MessageBufferSize, BoxedMessageBuffer) {
  fidl::internal::BoxedMessageBuffer<32> buffer;
  ASSERT_EQ(32u, buffer.size());
  ASSERT_NE(reinterpret_cast<uint8_t*>(&buffer), buffer.data());
  ASSERT_EQ(buffer.data(), buffer.view().data);
  ASSERT_EQ(32u, buffer.view().capacity);

  const fidl::internal::BoxedMessageBuffer<32> const_buffer;
  ASSERT_NE(reinterpret_cast<const uint8_t*>(&const_buffer), const_buffer.data());
}

// Ensure both large and small encoded buffers are within expected sizes.
TEST(MessageBufferSize, ResponseStorageAllocationStrategy) {
  static_assert(sizeof(fidl::internal::OutgoingMessageBuffer<
                       fidl_llcpp_buffersize_test::wire::Array256Elements>) == 256);
  static_assert(sizeof(fidl::internal::OutgoingMessageBuffer<
                       fidl_llcpp_buffersize_test::wire::Array4096Elements>) == 8);

  // The stored message is expected to be smaller than the array size, since the array is heap
  // allocated (though the actual size of the object is not specified).
  static_assert(sizeof(fidl::unstable::OwnedEncodedMessage<
                       fidl_llcpp_buffersize_test::wire::Array4096Elements>) < 4096);
}

TEST(MessageBufferSize, MaxSizeInChannel) {
  static_assert(fidl::MaxSizeInChannel<fidl::WireRequest<Protocol::RequestWith496ByteArray>,
                                       fidl::MessageDirection::kSending>() == 496);
  static_assert(fidl::MaxSizeInChannel<fidl::WireRequest<Protocol::RequestWith496ByteArray>,
                                       fidl::MessageDirection::kReceiving>() == 496);
  static_assert(fidl::MaxSizeInChannel<
                    fidl::internal::TransactionalRequest<Protocol::RequestWith496ByteArray>,
                    fidl::MessageDirection::kSending>() == 512);
  static_assert(fidl::MaxSizeInChannel<
                    fidl::internal::TransactionalRequest<Protocol::RequestWith496ByteArray>,
                    fidl::MessageDirection::kReceiving>() == 512);

  static_assert(fidl::MaxSizeInChannel<fidl::WireRequest<Protocol::SmallRequestWithFlexibleType>,
                                       fidl::MessageDirection::kSending>() < 512);
  static_assert(fidl::MaxSizeInChannel<fidl::WireRequest<Protocol::SmallRequestWithFlexibleType>,
                                       fidl::MessageDirection::kReceiving>() ==
                ZX_CHANNEL_MAX_MSG_BYTES);
  static_assert(fidl::MaxSizeInChannel<
                    fidl::internal::TransactionalRequest<Protocol::SmallRequestWithFlexibleType>,
                    fidl::MessageDirection::kSending>() < 512);
  static_assert(fidl::MaxSizeInChannel<
                    fidl::internal::TransactionalRequest<Protocol::SmallRequestWithFlexibleType>,
                    fidl::MessageDirection::kReceiving>() == ZX_CHANNEL_MAX_MSG_BYTES);

  static_assert(fidl::MaxSizeInChannel<
                    fidl::internal::TransactionalRequest<Protocol::SmallResponseWithFlexibleType>,
                    fidl::MessageDirection::kSending>() < 512);
  static_assert(fidl::MaxSizeInChannel<
                    fidl::internal::TransactionalRequest<Protocol::SmallResponseWithFlexibleType>,
                    fidl::MessageDirection::kReceiving>() < 512);

  static_assert(fidl::MaxSizeInChannel<fidl::WireResponse<Protocol::SmallResponseWithFlexibleType>,
                                       fidl::MessageDirection::kSending>() < 512);
  static_assert(fidl::MaxSizeInChannel<fidl::WireResponse<Protocol::SmallResponseWithFlexibleType>,
                                       fidl::MessageDirection::kReceiving>() ==
                ZX_CHANNEL_MAX_MSG_BYTES);
  static_assert(fidl::MaxSizeInChannel<
                    fidl::internal::TransactionalResponse<Protocol::SmallResponseWithFlexibleType>,
                    fidl::MessageDirection::kSending>() < 512);
  static_assert(fidl::MaxSizeInChannel<
                    fidl::internal::TransactionalResponse<Protocol::SmallResponseWithFlexibleType>,
                    fidl::MessageDirection::kReceiving>() == ZX_CHANNEL_MAX_MSG_BYTES);

  static_assert(fidl::MaxSizeInChannel<fidl::WireResponse<Protocol::SmallResponseWithFlexibleType>,
                                       fidl::MessageDirection::kSending>() < 512);
  static_assert(fidl::MaxSizeInChannel<fidl::WireResponse<Protocol::SmallResponseWithFlexibleType>,
                                       fidl::MessageDirection::kReceiving>() ==
                ZX_CHANNEL_MAX_MSG_BYTES);
  static_assert(fidl::MaxSizeInChannel<
                    fidl::internal::TransactionalResponse<Protocol::SmallResponseWithFlexibleType>,
                    fidl::MessageDirection::kSending>() < 512);
  static_assert(fidl::MaxSizeInChannel<
                    fidl::internal::TransactionalResponse<Protocol::SmallResponseWithFlexibleType>,
                    fidl::MessageDirection::kReceiving>() == ZX_CHANNEL_MAX_MSG_BYTES);

  static_assert(fidl::MaxSizeInChannel<fidl::WireEvent<Protocol::EventOf256Bytes>,
                                       fidl::MessageDirection::kSending>() == 240);
  static_assert(fidl::MaxSizeInChannel<fidl::WireEvent<Protocol::EventOf256Bytes>,
                                       fidl::MessageDirection::kReceiving>() == 240);
  static_assert(
      fidl::MaxSizeInChannel<fidl::internal::TransactionalEvent<Protocol::EventOf256Bytes>,
                             fidl::MessageDirection::kSending>() == 256);
  static_assert(
      fidl::MaxSizeInChannel<fidl::internal::TransactionalEvent<Protocol::EventOf256Bytes>,
                             fidl::MessageDirection::kReceiving>() == 256);
}

TEST(MessageBufferSize, BufferSizeConstexprFunctions) {
  static_assert(fidl::SyncClientMethodBufferSizeInChannel<Protocol::RequestWith496ByteArray>() ==
                512);
  // 513 bytes becomes 520 bytes after alignment.
  static_assert(fidl::SyncClientMethodBufferSizeInChannel<Protocol::RequestWith497ByteArray>() ==
                520);
  static_assert(fidl::SyncClientMethodBufferSizeInChannel<
                    Protocol::RequestWith496ByteArrayAndResponseOf256Bytes>() == 512 + 256);
  static_assert(fidl::AsyncClientMethodBufferSizeInChannel<
                    Protocol::RequestWith496ByteArrayAndResponseOf256Bytes>() == 512);
  static_assert(fidl::ServerReplyBufferSizeInChannel<
                    Protocol::RequestWith496ByteArrayAndResponseOf256Bytes>() == 256);
  static_assert(fidl::EventReplyBufferSizeInChannel<Protocol::EventOf256Bytes>() == 256);

  // Note: the computed value may need to be adjusted when changing the
  // in-memory wire format.
  static_assert(
      fidl::SyncClientMethodBufferSizeInChannel<Protocol::SmallRequestWithFlexibleType>() ==
      sizeof(fidl_message_header_t) + sizeof(fidl_xunion_v2_t) + sizeof(int64_t));

  static_assert(
      fidl::SyncClientMethodBufferSizeInChannel<Protocol::SmallResponseWithFlexibleType>() ==
      sizeof(fidl_message_header_t) + ZX_CHANNEL_MAX_MSG_BYTES);
  static_assert(
      fidl::AsyncClientMethodBufferSizeInChannel<Protocol::SmallResponseWithFlexibleType>() ==
      sizeof(fidl_message_header_t));
  // A server is sending the flexible response, hence we do not have to
  // over-allocate for unknown fields.
  static_assert(fidl::ServerReplyBufferSizeInChannel<Protocol::SmallResponseWithFlexibleType>() <
                512);
}
