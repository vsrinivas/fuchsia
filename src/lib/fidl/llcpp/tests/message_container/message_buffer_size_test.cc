// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fidl.llcpp.buffersize.test/cpp/wire.h>
#include <lib/fidl/llcpp/sync_call.h>

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

TEST(MessageBufferSize, ResponseStorageAllocationStrategy) {
  // The stack allocation limit of 512 bytes is defined in
  // tools/fidl/lib/fidlgen_cpp/protocol.go

  static_assert(sizeof(fidl::WireRequest<Protocol::RequestOf512Bytes>) == 512);
  // Buffers for messages no bigger than 512 bytes are embedded, for this request,
  // OwnedEncodedMessage size is bigger than 512 bytes.
  static_assert(sizeof(fidl::OwnedEncodedMessage<fidl::WireRequest<Protocol::RequestOf512Bytes>>) >
                512);

  static_assert(sizeof(fidl::WireRequest<Protocol::RequestOf513Bytes>) == 520);
  // Buffers for messages bigger than 512 bytes are store on the heap, for this request,
  // OwnedEncodedMessage size is smaller than 512 bytes.
  static_assert(sizeof(fidl::OwnedEncodedMessage<fidl::WireRequest<Protocol::RequestOf513Bytes>>) <
                512);
}

TEST(MessageBufferSize, BufferSizeConstexprFunctions) {
  static_assert(fidl::SyncClientMethodBufferSizeInChannel<Protocol::RequestOf512Bytes>() == 512);
  // 513 bytes becomes 520 bytes after alignment.
  static_assert(fidl::SyncClientMethodBufferSizeInChannel<Protocol::RequestOf513Bytes>() == 520);
  static_assert(fidl::SyncClientMethodBufferSizeInChannel<
                    Protocol::RequestOf512BytesAndResponseOf256Bytes>() == 512 + 256);
  static_assert(fidl::AsyncClientMethodBufferSizeInChannel<
                    Protocol::RequestOf512BytesAndResponseOf256Bytes>() == 512);
  static_assert(
      fidl::ServerReplyBufferSizeInChannel<Protocol::RequestOf512BytesAndResponseOf256Bytes>() ==
      256);
  static_assert(fidl::ServerReplyBufferSizeInChannel<Protocol::EventOf256Bytes>() == 256);
}
