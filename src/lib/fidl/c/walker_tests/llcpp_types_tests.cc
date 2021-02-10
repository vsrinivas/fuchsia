// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl/internal.h>
#include <lib/fidl/llcpp/array.h>
#include <lib/fidl/llcpp/coding.h>
#include <lib/fidl/llcpp/message.h>
#include <lib/fidl/llcpp/sync_call.h>
#include <lib/zx/channel.h>
#include <zircon/fidl.h>

#include <memory>
#include <optional>
#include <utility>

#include <fidl/test/coding/fuchsia/llcpp/fidl.h>
#include <zxtest/zxtest.h>

using ::llcpp::fidl::test::coding::fuchsia::TypesTest;
using NonNullableChannelRequest = TypesTest::NonNullableChannelRequest;

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

TEST(LlcppTypesTests, EncodedByteMessageTest) {
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

TEST(LlcppTypesTests, EncodedIovecMessageTest) {
  NonNullableChannelRequest msg(0);

  // Capture the extra handle here; it will not be cleaned by encoded_message
  zx::channel channel_1;

  EXPECT_EQ(zx::channel::create(0, &msg.channel, &channel_1), ZX_OK);

  {
    fidl::internal::EncodedMessageTypes<NonNullableChannelRequest>::OwnedIovec encoded(&msg);

    HelperExpectPeerValid(channel_1);
  }

  HelperExpectPeerInvalid(channel_1);
}

TEST(LlcppTypesTests, DecodedMessageFromBytesTest) {
  NonNullableChannelRequest msg(0);

  // Capture the extra handle here; it will not be cleaned by encoded.
  zx::channel channel_1;

  EXPECT_EQ(zx::channel::create(0, &msg.channel, &channel_1), ZX_OK);

  fidl::OwnedEncodedMessage<NonNullableChannelRequest> encoded(&msg);

  {
    fidl::OutgoingToIncomingMessage converted(encoded.GetOutgoingMessage());
    auto decoded = fidl::DecodedMessage<NonNullableChannelRequest>(converted.incoming_message());

    HelperExpectPeerValid(channel_1);
  }

  HelperExpectPeerInvalid(channel_1);
}

TEST(LlcppTypesTests, DecodedMessageFromIovecTest) {
  NonNullableChannelRequest msg(0);

  // Capture the extra handle here; it will not be cleaned by encoded.
  zx::channel channel_1;

  EXPECT_EQ(zx::channel::create(0, &msg.channel, &channel_1), ZX_OK);

  fidl::internal::EncodedMessageTypes<NonNullableChannelRequest>::OwnedIovec encoded(&msg);

  {
    fidl::OutgoingToIncomingMessage converted(encoded.GetOutgoingMessage());
    auto decoded = fidl::DecodedMessage<NonNullableChannelRequest>(converted.incoming_message());

    HelperExpectPeerValid(channel_1);
  }

  HelperExpectPeerInvalid(channel_1);
}

template <typename OwnedEncodedType, typename UnownedState, typename BuildUnowned,
          typename AssertBytesEqual>
void RunRountripTest(BuildUnowned build_unowned, AssertBytesEqual assert_bytes_equal) {
  NonNullableChannelRequest msg(10);

  // Capture the extra handle here; it will not be cleaned by encoded_message
  zx::channel channel_1;

  EXPECT_EQ(zx::channel::create(0, &msg.channel, &channel_1), ZX_OK);

  zx_handle_t unsafe_handle_backup(msg.channel.get());

  // We need to define our own storage because it is used after encoded is deleted.
  UnownedState unowned_state;

  auto encoded = build_unowned(&unowned_state, &msg);

  uint8_t golden_encoded[] = {0x0a, 0x00, 0x00, 0x00,   // txid
                              0x00, 0x00, 0x00, 0x01,   // flags and version
                              0xa1, 0xd4, 0x9b, 0x76,   // low bytes of ordinal
                              0x82, 0x41, 0x13, 0x06,   // high bytes of ordinal
                              0xff, 0xff, 0xff, 0xff,   // handle present
                              0x00, 0x00, 0x00, 0x00};  // Padding

  assert_bytes_equal(encoded->GetOutgoingMessage(), golden_encoded,
                     sizeof(NonNullableChannelRequest));

  HelperExpectPeerValid(channel_1);

  // Decode
  fidl::OutgoingToIncomingMessage converted(encoded->GetOutgoingMessage());
  ASSERT_TRUE(converted.ok());
  auto decoded = fidl::DecodedMessage<NonNullableChannelRequest>(converted.incoming_message());
  ASSERT_TRUE(decoded.ok());
  EXPECT_NULL(decoded.error(), "%s", decoded.error());
  EXPECT_EQ(decoded.PrimaryObject()->_hdr.txid, 10);
  EXPECT_EQ(decoded.PrimaryObject()->_hdr.ordinal, 0x6134182769bd4a1lu);
  EXPECT_EQ(decoded.PrimaryObject()->channel.get(), unsafe_handle_backup);
  // encoded_message should be consumed
  EXPECT_EQ(encoded->GetOutgoingMessage().handle_actual(), 0);
  delete encoded;
  // At this point, encoded is destroyed but not decoded, it should not accidentally close the
  // channel.
  HelperExpectPeerValid(channel_1);

  // Encode
  {
    OwnedEncodedType encoded2(decoded.PrimaryObject());
    EXPECT_TRUE(encoded2.ok());
    EXPECT_NULL(encoded2.error(), "%s", encoded2.error());

    assert_bytes_equal(encoded2.GetOutgoingMessage(), golden_encoded,
                       sizeof(NonNullableChannelRequest));

    EXPECT_EQ(encoded2.GetOutgoingMessage().handle_actual(), 1);
    EXPECT_EQ(encoded2.GetOutgoingMessage().handles()[0].handle, unsafe_handle_backup);

    HelperExpectPeerValid(channel_1);
  }
  // Encoded message was destroyed, bringing down the handle with it
  HelperExpectPeerInvalid(channel_1);
}

// Start with a message, then encode to a byte buffer, decode and encode again.
TEST(LlcppTypesTests, ByteRoundTripTest) {
  struct UnownedState {
    FIDL_ALIGNDECL uint8_t bytes[sizeof(NonNullableChannelRequest)];
  };
  auto build_unowned = [](UnownedState* unowned_state, NonNullableChannelRequest* msg) {
    return new fidl::UnownedEncodedMessage<NonNullableChannelRequest>(
        unowned_state->bytes, sizeof(unowned_state->bytes), msg);
  };
  auto assert_bytes_equal = [](fidl::OutgoingByteMessage& outgoing_msg, uint8_t* expected_bytes,
                               uint32_t expected_size) {
    EXPECT_EQ(outgoing_msg.byte_actual(), expected_size);
    EXPECT_EQ(memcmp(expected_bytes, outgoing_msg.bytes(), expected_size), 0);
  };
  RunRountripTest<fidl::OwnedEncodedMessage<NonNullableChannelRequest>, UnownedState>(
      build_unowned, assert_bytes_equal);
}
// Start with a message, then encode to an iovec array, decode and encode again.
TEST(LlcppTypesTests, IovecRoundTripTest) {
  constexpr uint32_t kNumIovecs = 16;
  struct UnownedState {
    zx_channel_iovec_t iovecs[kNumIovecs];
    fidl_iovec_substitution_t substitutions[kNumIovecs];
  };
  auto build_unowned = [](UnownedState* unowned_state, NonNullableChannelRequest* msg) {
    return new fidl::internal::EncodedMessageTypes<NonNullableChannelRequest>::UnownedIovec(
        unowned_state->iovecs, sizeof(unowned_state->iovecs), unowned_state->substitutions,
        sizeof(unowned_state->substitutions), msg);
  };
  auto assert_bytes_equal = [](fidl::OutgoingIovecMessage& outgoing_msg, uint8_t* expected_bytes,
                               uint32_t expected_size) {
    FIDL_ALIGNDECL uint8_t storage_bytes[sizeof(NonNullableChannelRequest)];
    uint32_t offset = 0;
    for (uint32_t i = 0; i < outgoing_msg.iovec_actual(); i++) {
      zx_channel_iovec_t iovec = outgoing_msg.iovecs()[i];
      memcpy(&storage_bytes[offset], iovec.buffer, iovec.capacity);
      offset += iovec.capacity;
    }
    EXPECT_EQ(sizeof(storage_bytes), expected_size);
    EXPECT_EQ(memcmp(expected_bytes, storage_bytes, expected_size), 0);
  };
  RunRountripTest<fidl::internal::EncodedMessageTypes<NonNullableChannelRequest>::OwnedIovec,
                  UnownedState>(build_unowned, assert_bytes_equal);
}

TEST(LlcppTypesTests, ArrayLayoutTest) {
  static_assert(sizeof(fidl::Array<uint8_t, 3>) == sizeof(uint8_t[3]));
  static_assert(sizeof(fidl::Array<fidl::Array<uint8_t, 7>, 3>) == sizeof(uint8_t[3][7]));

  constexpr fidl::Array<uint8_t, 3> a = {1, 2, 3};
  constexpr uint8_t b[3] = {1, 2, 3};
  EXPECT_EQ((&a[2] - &a[0]), (&b[2] - &b[0]));
}

TEST(LlcppTypesTests, UninitializedBufferStackAllocationAlignmentTest) {
  fidl::internal::AlignedBuffer<1> array_of_1;
  ASSERT_EQ(sizeof(array_of_1), 8);
  ASSERT_TRUE(reinterpret_cast<uintptr_t>(&array_of_1) % 8 == 0);

  fidl::internal::AlignedBuffer<5> array_of_5;
  ASSERT_EQ(sizeof(array_of_5), 8);
  ASSERT_TRUE(reinterpret_cast<uintptr_t>(&array_of_5) % 8 == 0);

  fidl::internal::AlignedBuffer<25> array_of_25;
  ASSERT_EQ(sizeof(array_of_25), 32);
  ASSERT_TRUE(reinterpret_cast<uintptr_t>(&array_of_25) % 8 == 0);

  fidl::internal::AlignedBuffer<100> array_of_100;
  ASSERT_EQ(sizeof(array_of_100), 104);
  ASSERT_TRUE(reinterpret_cast<uintptr_t>(&array_of_100) % 8 == 0);
}

TEST(LlcppTypesTests, UninitializedBufferHeapAllocationAlignmentTest) {
  std::unique_ptr array_of_1 = std::make_unique<fidl::internal::AlignedBuffer<1>>();
  ASSERT_TRUE(reinterpret_cast<uintptr_t>(array_of_1.get()) % 8 == 0);

  std::unique_ptr array_of_5 = std::make_unique<fidl::internal::AlignedBuffer<5>>();
  ASSERT_TRUE(reinterpret_cast<uintptr_t>(array_of_5.get()) % 8 == 0);

  std::unique_ptr array_of_25 = std::make_unique<fidl::internal::AlignedBuffer<25>>();
  ASSERT_TRUE(reinterpret_cast<uintptr_t>(array_of_25.get()) % 8 == 0);

  std::unique_ptr array_of_100 = std::make_unique<fidl::internal::AlignedBuffer<100>>();
  ASSERT_TRUE(reinterpret_cast<uintptr_t>(array_of_100.get()) % 8 == 0);
}

TEST(LlcppTypesTests, ResponseStorageAllocationStrategyTest) {
  // The stack allocation limit of 512 bytes is defined in
  // zircon/system/ulib/fidl/include/lib/fidl/llcpp/sync_call.h

  static_assert(sizeof(TypesTest::RequestOf512BytesRequest) == 512);
  // Buffers for messages no bigger than 512 bytes are embedded, for this request,
  // OwnedEncodedMessage size is bigger than 512 bytes.
  static_assert(sizeof(fidl::OwnedEncodedMessage<TypesTest::RequestOf512BytesRequest>) > 512);

  static_assert(sizeof(TypesTest::RequestOf513BytesRequest) == 520);
  // Buffers for messages bigger than 512 bytes are store on the heap, for this request,
  // OwnedEncodedMessage size is smaller than 512 bytes.
  static_assert(sizeof(fidl::OwnedEncodedMessage<TypesTest::RequestOf513BytesRequest>) < 512);
}

}  // namespace
