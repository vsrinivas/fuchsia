// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl/llcpp/message_storage.h>
#include <lib/zx/channel.h>

#include <memory>
#include <optional>
#include <utility>

#include <fidl/llcpp/types/test/llcpp/fidl.h>
#include <gtest/gtest.h>

using ::fidl_llcpp_types_test::TypesTest;
using ::fidl_llcpp_types_test::wire::VectorStruct;
using NonNullableChannelRequest = fidl::WireRequest<TypesTest::NonNullableChannel>;

namespace {

// Because the EncodedMessage/DecodedMessage classes close handles using the corresponding
// Zircon system call instead of calling a destructor, we indirectly test for handle closure
// via the ZX_ERR_PEER_CLOSED error message.

void HelperExpectPeerValid(zx::channel& channel) {
  const char* foo = "A";
  EXPECT_EQ(channel.write(0, foo, 1, nullptr, 0), ZX_OK);
}

void HelperExpectPeerInvalid(zx::channel& channel) {
  const char* foo = "A";
  EXPECT_EQ(channel.write(0, foo, 1, nullptr, 0), ZX_ERR_PEER_CLOSED);
}

TEST(LlcppTypesTests, EncodedMessageTest) {
  NonNullableChannelRequest msg(0);

  // Capture the extra handle here; it will not be cleaned by encoded_message
  zx::channel channel_1;

  EXPECT_EQ(zx::channel::create(0, &msg.channel, &channel_1), ZX_OK);

  {
    fidl::OwnedEncodedMessage<NonNullableChannelRequest> encoded(&msg);

    HelperExpectPeerValid(channel_1);
  }

  HelperExpectPeerInvalid(channel_1);
}

// Start with a message, then encode, decode and encode again.
TEST(LlcppTypesTests, RoundTripTest) {
  NonNullableChannelRequest msg(10);

  // Capture the extra handle here; it will not be cleaned by encoded_message
  zx::channel channel_1;

  EXPECT_EQ(zx::channel::create(0, &msg.channel, &channel_1), ZX_OK);

  zx_handle_t unsafe_handle_backup(msg.channel.get());

  // We need to define our own storage because it is used after encoded is deleted.
  FIDL_ALIGNDECL uint8_t storage[sizeof(NonNullableChannelRequest)];

  auto* encoded =
      new fidl::UnownedEncodedMessage<NonNullableChannelRequest>(storage, sizeof(storage), &msg);
  EXPECT_EQ(encoded->status(), ZX_OK);
  auto encoded_bytes = encoded->GetOutgoingMessage().CopyBytes();
  EXPECT_EQ(encoded_bytes.size(), sizeof(NonNullableChannelRequest));

  uint8_t golden_encoded[] = {0x0a, 0x00, 0x00, 0x00,   // txid
                              0x00, 0x00, 0x00, 0x01,   // flags and version
                              0x4c, 0xf1, 0x17, 0xe9,   // low bytes of ordinal
                              0xa3, 0x24, 0xcb, 0x2d,   // high bytes of ordinal
                              0xff, 0xff, 0xff, 0xff,   // handle present
                              0x00, 0x00, 0x00, 0x00};  // Padding

  // Byte-accurate comparison
  EXPECT_EQ(memcmp(golden_encoded, encoded_bytes.data(), encoded_bytes.size()), 0);

  HelperExpectPeerValid(channel_1);

  // Decode
  auto converted = fidl::OutgoingToIncomingMessage(encoded->GetOutgoingMessage());
  auto& incoming = converted.incoming_message();
  ASSERT_EQ(ZX_OK, incoming.status());
  auto decoded = fidl::DecodedMessage<NonNullableChannelRequest>(std::move(incoming));
  EXPECT_TRUE(decoded.ok());
  EXPECT_EQ(decoded.PrimaryObject()->_hdr.txid, 10u);
  EXPECT_EQ(decoded.PrimaryObject()->_hdr.ordinal, 0x2DCB24A3E917F14Clu);
  EXPECT_EQ(decoded.PrimaryObject()->channel.get(), unsafe_handle_backup);
  // encoded_message should be consumed
  EXPECT_EQ(encoded->GetOutgoingMessage().handle_actual(), 0u);
  delete encoded;
  // At this point, |encoded| is destroyed but not |decoded|, it should not accidentally close the
  // channel.
  HelperExpectPeerValid(channel_1);

  // Encode
  {
    fidl::OwnedEncodedMessage<NonNullableChannelRequest> encoded2(decoded.PrimaryObject());
    EXPECT_TRUE(encoded2.ok());

    // Byte-level comparison
    auto encoded2_bytes = encoded2.GetOutgoingMessage().CopyBytes();
    EXPECT_EQ(encoded2_bytes.size(), sizeof(NonNullableChannelRequest));
    EXPECT_EQ(memcmp(golden_encoded, encoded2_bytes.data(), encoded2_bytes.size()), 0);
    EXPECT_EQ(encoded2.GetOutgoingMessage().handle_actual(), 1u);
    EXPECT_EQ(encoded2.GetOutgoingMessage().handles()[0].handle, unsafe_handle_backup);

    HelperExpectPeerValid(channel_1);
  }
  // Encoded message was destroyed, bringing down the handle with it
  HelperExpectPeerInvalid(channel_1);
}

TEST(LlcppTypesTests, ArrayLayoutTest) {
  static_assert(sizeof(fidl::Array<uint8_t, 3>) == sizeof(uint8_t[3]));
  static_assert(sizeof(fidl::Array<fidl::Array<uint8_t, 7>, 3>) == sizeof(uint8_t[3][7]));

  constexpr fidl::Array<uint8_t, 3> a = {1, 2, 3};
  constexpr uint8_t b[3] = {1, 2, 3};
  EXPECT_EQ((&a[2] - &a[0]), (&b[2] - &b[0]));
}

TEST(LlcppTypesTests, StringView) {
  fidl::Arena allocator;

  fidl::StringView view;
  EXPECT_TRUE(view.empty());
  EXPECT_TRUE(view.is_null());

  view.Set(allocator, "123");

  EXPECT_FALSE(view.empty());
  EXPECT_EQ(view.size(), 3u);
  EXPECT_EQ(std::string(view.data(), 3), "123");

  EXPECT_EQ(view.at(1), '2');
}

TEST(LlcppTypesTests, VectorView) {
  fidl::Arena allocator;

  fidl::VectorView<int> view;
  EXPECT_TRUE(view.empty());
  EXPECT_TRUE(view.data() == nullptr);

  view.Allocate(allocator, 3);
  const int data[] = {1, 2, 3};
  view[0] = data[0];
  view[1] = data[1];
  view[2] = data[2];

  EXPECT_EQ(view.count(), 3u);
  EXPECT_EQ(std::vector<int>(std::begin(view), std::end(view)),
            std::vector<int>(std::begin(data), std::end(data)));

  EXPECT_EQ(view.at(1), 2);
}

TEST(LlcppTypesTests, InlineMessageBuffer) {
  fidl::internal::InlineMessageBuffer<32> buffer;
  ASSERT_EQ(32u, buffer.size());
  ASSERT_EQ(reinterpret_cast<uint8_t*>(&buffer), buffer.data());
  ASSERT_EQ(buffer.data(), buffer.view().data);
  ASSERT_EQ(32u, buffer.view().capacity);

  const fidl::internal::InlineMessageBuffer<32> const_buffer;
  ASSERT_EQ(reinterpret_cast<const uint8_t*>(&const_buffer), const_buffer.data());
}

TEST(LlcppTypesTests, BoxedMessageBuffer) {
  fidl::internal::BoxedMessageBuffer<32> buffer;
  ASSERT_EQ(32u, buffer.size());
  ASSERT_NE(reinterpret_cast<uint8_t*>(&buffer), buffer.data());
  ASSERT_EQ(buffer.data(), buffer.view().data);
  ASSERT_EQ(32u, buffer.view().capacity);

  const fidl::internal::BoxedMessageBuffer<32> const_buffer;
  ASSERT_NE(reinterpret_cast<const uint8_t*>(&const_buffer), const_buffer.data());
}

TEST(LlcppTypesTests, ResponseStorageAllocationStrategyTest) {
  // The stack allocation limit of 512 bytes is defined in
  // tools/fidl/lib/fidlgen_cpp/protocol.go

  static_assert(sizeof(fidl::WireRequest<TypesTest::RequestOf512Bytes>) == 512);
  // Buffers for messages no bigger than 512 bytes are embedded, for this request,
  // OwnedEncodedMessage size is bigger than 512 bytes.
  static_assert(sizeof(fidl::OwnedEncodedMessage<fidl::WireRequest<TypesTest::RequestOf512Bytes>>) >
                512);

  static_assert(sizeof(fidl::WireRequest<TypesTest::RequestOf513Bytes>) == 520);
  // Buffers for messages bigger than 512 bytes are store on the heap, for this request,
  // OwnedEncodedMessage size is smaller than 512 bytes.
  static_assert(sizeof(fidl::OwnedEncodedMessage<fidl::WireRequest<TypesTest::RequestOf513Bytes>>) <
                512);
}

// Ensure the encoded message with default number of iovecs can be decoded and accessed without
// triggering ASAN errors, even after the initial encoded object goes out of scope.
// A vector is used in this test because the encoder will typically use a dedicated iovec to
// point directly into its body. This behavior could change, however, but is verified in the test.
TEST(LlcppTypesTests, OwnedEncodedMessageOwns) {
  constexpr uint32_t vector_view_count = 100;
  std::unique_ptr<fidl::OwnedEncodedMessage<VectorStruct>> encoded;

  {
    fidl::Arena<vector_view_count * sizeof(uint32_t)> allocator;
    VectorStruct vector_struct = {
        .v = fidl::VectorView<uint32_t>(allocator, vector_view_count),
    };
    for (uint32_t i = 0; i < vector_view_count; i++) {
      vector_struct.v[i] = i;
    }

    encoded = std::make_unique<fidl::OwnedEncodedMessage<VectorStruct>>(&vector_struct);
    ASSERT_TRUE(encoded->ok());

    auto encoded_with_iovecs = std::make_unique<fidl::OwnedEncodedMessage<VectorStruct>>(
        fidl::internal::AllowUnownedInputRef{}, &vector_struct);
    ASSERT_TRUE(encoded_with_iovecs->ok());
    ASSERT_GT(encoded_with_iovecs->GetOutgoingMessage().iovec_actual(), 1u);
  }

  fidl::OutgoingToIncomingMessage converted(encoded->GetOutgoingMessage());
  ASSERT_TRUE(converted.ok());
  fidl::DecodedMessage<VectorStruct> decoded(std::move(converted.incoming_message()));

  ASSERT_EQ(vector_view_count, decoded.PrimaryObject()->v.count());
  for (uint32_t i = 0; i < vector_view_count; i++) {
    EXPECT_EQ(i, decoded.PrimaryObject()->v[i]);
  }
}

}  // namespace
