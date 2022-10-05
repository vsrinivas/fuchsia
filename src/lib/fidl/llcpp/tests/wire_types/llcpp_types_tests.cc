// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/test.types/cpp/wire.h>
#include <lib/fidl/cpp/wire/message_storage.h>
#include <lib/zx/channel.h>

#include <memory>
#include <optional>
#include <utility>

#include <gtest/gtest.h>

using ::test_types::TypesTest;
using ::test_types::wire::VectorStruct;
using NonNullableChannelRequest = fidl::WireRequest<TypesTest::NonNullableChannel>;
using NonNullableChannelTransactionalRequest =
    fidl::internal::TransactionalRequest<TypesTest::NonNullableChannel>;

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
  NonNullableChannelRequest msg{};

  // Capture the extra handle here; it will not be cleaned by encoded_message
  zx::channel channel_1;

  EXPECT_EQ(zx::channel::create(0, &msg.channel, &channel_1), ZX_OK);

  {
    fidl::unstable::OwnedEncodedMessage<NonNullableChannelRequest> encoded(&msg);

    HelperExpectPeerValid(channel_1);
  }

  HelperExpectPeerInvalid(channel_1);
}

// Start with a message, then encode, decode and encode again.
TEST(LlcppTypesTests, RoundTripTest) {
  NonNullableChannelTransactionalRequest msg{};

  // Capture the extra handle here; it will not be cleaned by encoded_message
  zx::channel channel_1;

  EXPECT_EQ(zx::channel::create(0, &msg.body.channel, &channel_1), ZX_OK);

  zx_handle_t unsafe_handle_backup(msg.body.channel.get());

  // We need to define our own storage because it is used after encoded is deleted.
  FIDL_ALIGNDECL uint8_t storage[sizeof(NonNullableChannelTransactionalRequest)];

  auto* encoded = new fidl::unstable::UnownedEncodedMessage<NonNullableChannelTransactionalRequest>(
      storage, sizeof(storage), &msg);
  EXPECT_EQ(encoded->status(), ZX_OK);
  encoded->GetOutgoingMessage().set_txid(10);
  fidl::OutgoingMessage& outgoing = encoded->GetOutgoingMessage();
  auto encoded_bytes = outgoing.CopyBytes();
  EXPECT_EQ(encoded_bytes.size(), sizeof(NonNullableChannelTransactionalRequest));

  uint8_t golden_encoded[] = {0x0a, 0x00, 0x00, 0x00,   // txid
                              0x02, 0x00, 0x00, 0x01,   // flags and version
                              0xa2, 0x37, 0xe0, 0x88,   // low bytes of ordinal
                              0xbd, 0x2e, 0x98, 0x67,   // high bytes of ordinal
                              0xff, 0xff, 0xff, 0xff,   // handle present
                              0x00, 0x00, 0x00, 0x00};  // Padding

  // Byte-accurate comparison
  EXPECT_EQ(memcmp(golden_encoded, encoded_bytes.data(), encoded_bytes.size()), 0);

  HelperExpectPeerValid(channel_1);

  // Decode
  auto incoming = fidl::IncomingHeaderAndMessage::Create(
      encoded_bytes.data(), encoded_bytes.size(), outgoing.handles(),
      outgoing.handle_metadata<fidl::internal::ChannelTransport>(), outgoing.handle_actual());
  ASSERT_EQ(ZX_OK, incoming.status());
  outgoing.ReleaseHandles();
  const fidl_message_header_t header = *incoming.header();
  EXPECT_EQ(header.txid, 10u);
  EXPECT_EQ(header.ordinal, 0x67982ebd88e037a2lu);
  fit::result decoded =
      fidl::internal::InplaceDecodeTransactionalMessage<NonNullableChannelRequest>(
          std::move(incoming));
  ASSERT_TRUE(decoded.is_ok());
  EXPECT_EQ(decoded->channel.get(), unsafe_handle_backup);
  // encoded_message should be consumed
  EXPECT_EQ(encoded->GetOutgoingMessage().handle_actual(), 0u);
  delete encoded;
  // At this point, |encoded| is destroyed but not |decoded|, it should not accidentally close the
  // channel.
  HelperExpectPeerValid(channel_1);

  // Encode
  {
    NonNullableChannelTransactionalRequest request;
    request.header = header;
    request.body = std::move(decoded.value().value());
    fidl::unstable::OwnedEncodedMessage<NonNullableChannelTransactionalRequest> encoded2(&request);
    EXPECT_TRUE(encoded2.ok());

    // Byte-level comparison
    auto encoded2_bytes = encoded2.GetOutgoingMessage().CopyBytes();
    EXPECT_EQ(encoded2_bytes.size(), sizeof(NonNullableChannelTransactionalRequest));
    EXPECT_EQ(memcmp(golden_encoded, encoded2_bytes.data(), encoded2_bytes.size()), 0);
    EXPECT_EQ(encoded2.GetOutgoingMessage().handle_actual(), 1u);
    EXPECT_EQ(encoded2.GetOutgoingMessage().handles()[0], unsafe_handle_backup);

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

TEST(LlcppTypesTests, ArrayEquality) {
  constexpr fidl::Array<uint8_t, 3> foo{1, 2, 3};
  constexpr fidl::Array<uint8_t, 3> bar{1, 2, 3};
  EXPECT_EQ(foo, bar);

  constexpr fidl::Array<uint8_t, 3> baz{0, 2, 3};
  EXPECT_NE(foo, baz);
  EXPECT_NE(bar, baz);
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

// Ensure the encoded message with default number of iovecs can be decoded and accessed without
// triggering ASAN errors, even after the initial encoded object goes out of scope.
// A vector is used in this test because the encoder will typically use a dedicated iovec to
// point directly into its body. This behavior could change, however, but is verified in the test.
TEST(LlcppTypesTests, OwnedEncodedMessageOwns) {
  constexpr uint32_t vector_view_count = 100;
  std::unique_ptr<fidl::unstable::OwnedEncodedMessage<VectorStruct>> encoded;

  {
    fidl::Arena<vector_view_count * sizeof(uint32_t)> allocator;
    VectorStruct vector_struct = {
        .v = fidl::VectorView<uint32_t>(allocator, vector_view_count),
    };
    for (uint32_t i = 0; i < vector_view_count; i++) {
      vector_struct.v[i] = i;
    }

    encoded = std::make_unique<fidl::unstable::OwnedEncodedMessage<VectorStruct>>(
        fidl::internal::WireFormatVersion::kV2, &vector_struct);
    ASSERT_TRUE(encoded->ok());

    auto encoded_with_iovecs = std::make_unique<fidl::unstable::OwnedEncodedMessage<VectorStruct>>(
        fidl::internal::AllowUnownedInputRef{}, fidl::internal::WireFormatVersion::kV2,
        &vector_struct);
    ASSERT_TRUE(encoded_with_iovecs->ok());
  }

  fidl::OutgoingToIncomingMessage converted(encoded->GetOutgoingMessage());
  ASSERT_TRUE(converted.ok());
  fit::result decoded = fidl::InplaceDecode<VectorStruct>(
      std::move(converted.incoming_message()),
      fidl::internal::WireFormatMetadataForVersion(fidl::internal::WireFormatVersion::kV2));
  ASSERT_TRUE(decoded.is_ok());
  ASSERT_EQ(vector_view_count, decoded->v.count());
  for (uint32_t i = 0; i < vector_view_count; i++) {
    EXPECT_EQ(i, decoded->v[i]);
  }
}

}  // namespace
