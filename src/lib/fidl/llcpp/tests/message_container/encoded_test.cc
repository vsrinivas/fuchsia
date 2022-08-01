// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fidl.llcpp.linearized.test/cpp/wire.h>
#include <lib/fidl/cpp/wire/message.h>

#include <gtest/gtest.h>

namespace fidl_linearized = ::fidl_llcpp_linearized_test;

// Successful encode must include the inline and out of line objects in the buffer.
constexpr size_t kSizeJustRight = FIDL_ALIGN(sizeof(fidl_linearized::wire::FullyLinearizedStruct) +
                                             sizeof(fidl_linearized::wire::InnerStruct));
// ZX_ERR_INVALID_ARGS failures happen when the buffer size is less than the size of all objects.
constexpr size_t kSizeTooSmall = sizeof(fidl_linearized::wire::FullyLinearizedStruct);

TEST(Encoded, CallerAllocateEncoded) {
  fidl_linearized::wire::InnerStruct inner = {.x = 1};
  fidl_linearized::wire::FullyLinearizedStruct input{
      .ptr = fidl::ObjectView<fidl_linearized::wire::InnerStruct>::FromExternal(&inner)};
  uint8_t bytes[kSizeJustRight];
  fidl::unstable::UnownedEncodedMessage<fidl_linearized::wire::FullyLinearizedStruct> encoded(
      fidl::internal::WireFormatVersion::kV2, bytes, std::size(bytes), &input);
  EXPECT_TRUE(encoded.ok());

  auto message_bytes = encoded.GetOutgoingMessage().CopyBytes();
  auto encoded_obj =
      reinterpret_cast<const fidl_linearized::wire::FullyLinearizedStruct*>(message_bytes.data());
  ASSERT_NE(nullptr, encoded_obj);
  EXPECT_NE(encoded_obj, &input);
  EXPECT_EQ(*reinterpret_cast<const uintptr_t*>(&encoded_obj->ptr), FIDL_ALLOC_PRESENT);
  EXPECT_EQ(reinterpret_cast<const fidl_linearized::wire::InnerStruct*>(encoded_obj + 1)->x,
            input.ptr->x);
}

TEST(Encoded, BufferTooSmall) {
  fidl_linearized::wire::InnerStruct inner = {.x = 1};
  fidl_linearized::wire::FullyLinearizedStruct input{
      .ptr = fidl::ObjectView<fidl_linearized::wire::InnerStruct>::FromExternal(&inner)};
  uint8_t bytes[kSizeTooSmall];
  fidl::unstable::UnownedEncodedMessage<fidl_linearized::wire::FullyLinearizedStruct> encoded(
      fidl::internal::WireFormatVersion::kV2, bytes, std::size(bytes), &input);
  EXPECT_EQ(ZX_ERR_BUFFER_TOO_SMALL, encoded.status());
}

TEST(Encoded, EarlyCatchBufferTooSmall) {
  fidl_linearized::wire::InnerStruct inner = {.x = 1};
  fidl_linearized::wire::FullyLinearizedStruct input{
      .ptr = fidl::ObjectView<fidl_linearized::wire::InnerStruct>::FromExternal(&inner)};
  // Allocate a buffer that follows FIDL alignment.
  FIDL_ALIGNDECL uint8_t bytes[kSizeTooSmall];
  constexpr size_t kEarlyCatchSizeTooSmall = 0;
  fidl::unstable::UnownedEncodedMessage<fidl_linearized::wire::FullyLinearizedStruct> encoded(
      fidl::internal::WireFormatVersion::kV2, bytes, kEarlyCatchSizeTooSmall, &input);
  // ZX_ERR_BUFFER_TOO_SMALL failures only happen when the buffer size is less than the inline size.
  EXPECT_EQ(ZX_ERR_BUFFER_TOO_SMALL, encoded.status());
}

TEST(Iovec, EncodeDoesntMutateVectorObject) {
  std::vector<uint32_t> arr = {1u, 2u, 3u};
  fidl_linearized::wire::Uint32VectorStruct obj{
      .vec = fidl::VectorView<uint32_t>::FromExternal(arr),
  };

  constexpr size_t obj_size = sizeof(fidl_linearized::wire::Uint32VectorStruct);
  // The third uint32_t is not stored in the iovec in the algorithm currently used.
  size_t iovec_body_size = sizeof(uint32_t) * 2;

  auto make_snapshot = [&](fidl_linearized::wire::Uint32VectorStruct* obj) {
    std::vector<uint8_t> snapshot;
    snapshot.resize(obj_size);
    memcpy(snapshot.data(), obj, obj_size);
    snapshot.resize(obj_size + iovec_body_size);
    memcpy(snapshot.data() + obj_size, obj->vec.data(), iovec_body_size);
    return snapshot;
  };
  auto initial_snapshot = make_snapshot(&obj);

  auto buffer = std::make_unique<uint8_t[]>(ZX_CHANNEL_MAX_MSG_BYTES);
  fidl::unstable::UnownedEncodedMessage<fidl_linearized::wire::Uint32VectorStruct> encoded(
      fidl::internal::ChannelTransport::kNumIovecs, buffer.get(), ZX_CHANNEL_MAX_MSG_BYTES, &obj);
  ASSERT_TRUE(encoded.ok());
  ASSERT_EQ(encoded.GetOutgoingMessage().iovec_actual(), 3u);
  EXPECT_EQ(encoded.GetOutgoingMessage().handle_actual(), 0u);
  EXPECT_EQ(encoded.GetOutgoingMessage().iovecs()[0].capacity, obj_size);
  EXPECT_EQ(encoded.GetOutgoingMessage().iovecs()[0].reserved, 0u);
  EXPECT_EQ(encoded.GetOutgoingMessage().iovecs()[1].buffer, arr.data());
  EXPECT_EQ(encoded.GetOutgoingMessage().iovecs()[1].capacity, iovec_body_size);
  EXPECT_EQ(encoded.GetOutgoingMessage().iovecs()[1].reserved, 0u);
  EXPECT_EQ(encoded.GetOutgoingMessage().iovecs()[2].buffer,
            static_cast<const uint8_t*>(encoded.GetOutgoingMessage().iovecs()[0].buffer) +
                sizeof(fidl_vector_t));
  EXPECT_EQ(encoded.GetOutgoingMessage().iovecs()[2].capacity, 8u);
  EXPECT_EQ(encoded.GetOutgoingMessage().iovecs()[2].reserved, 0u);

  auto final_snapshot = make_snapshot(&obj);
  EXPECT_EQ(initial_snapshot.size(), final_snapshot.size());
  ASSERT_EQ(0, memcmp(initial_snapshot.data(), final_snapshot.data(), initial_snapshot.size()));
}

TEST(Iovec, ExceedVectorBufferCount) {
  std::vector<uint32_t> arr = {1u, 2u, 3u};
  fidl_linearized::wire::Uint32VectorStruct obj{
      .vec = fidl::VectorView<uint32_t>::FromExternal(arr),
  };

  auto buffer = std::make_unique<uint8_t[]>(ZX_CHANNEL_MAX_MSG_BYTES);
  // 3 iovecs are needed to directly point at the vector body.
  // When 1 or 2 are present, the encoder should linearize into just the first
  // iovec.
  fidl::unstable::UnownedEncodedMessage<fidl_linearized::wire::Uint32VectorStruct> encoded(
      2u, buffer.get(), ZX_CHANNEL_MAX_MSG_BYTES, &obj);
  ASSERT_TRUE(encoded.ok());
  ASSERT_EQ(encoded.GetOutgoingMessage().iovec_actual(), 1u);
  EXPECT_EQ(encoded.GetOutgoingMessage().handle_actual(), 0u);
  EXPECT_EQ(encoded.GetOutgoingMessage().iovecs()[0].buffer, buffer.get());
  EXPECT_EQ(encoded.GetOutgoingMessage().iovecs()[0].capacity,
            sizeof(obj) + arr.size() * sizeof(uint32_t) + 4);
  EXPECT_EQ(encoded.GetOutgoingMessage().iovecs()[0].reserved, 0u);
  EXPECT_EQ(0, memcmp(encoded.GetOutgoingMessage().iovecs()[0].buffer, &obj, 8));
  EXPECT_EQ(0, memcmp(obj.vec.data(), arr.data(), arr.size() * sizeof(uint32_t)));
}

TEST(Iovec, MatchNeededVectorBufferCount) {
  std::vector<uint32_t> arr = {1u, 2u, 3u};
  fidl_linearized::wire::Uint32VectorStruct obj{
      .vec = fidl::VectorView<uint32_t>::FromExternal(arr),
  };

  auto buffer = std::make_unique<uint8_t[]>(ZX_CHANNEL_MAX_MSG_BYTES);
  // With 3 iovecs, the second iovec will directly point at the vector body.
  fidl::unstable::UnownedEncodedMessage<fidl_linearized::wire::Uint32VectorStruct> encoded(
      2u, buffer.get(), ZX_CHANNEL_MAX_MSG_BYTES, &obj);
  ASSERT_TRUE(encoded.ok());
  ASSERT_EQ(encoded.GetOutgoingMessage().iovec_actual(), 1u);
  EXPECT_EQ(encoded.GetOutgoingMessage().handle_actual(), 0u);
  EXPECT_EQ(encoded.GetOutgoingMessage().iovecs()[0].buffer, buffer.get());
  EXPECT_EQ(encoded.GetOutgoingMessage().iovecs()[0].capacity,
            sizeof(obj) + arr.size() * sizeof(uint32_t) + 4);
  EXPECT_EQ(encoded.GetOutgoingMessage().iovecs()[0].reserved, 0u);
  EXPECT_EQ(0, memcmp(encoded.GetOutgoingMessage().iovecs()[0].buffer, &obj, 8));
  EXPECT_EQ(0, memcmp(obj.vec.data(), arr.data(), arr.size() * sizeof(uint32_t)));
}
