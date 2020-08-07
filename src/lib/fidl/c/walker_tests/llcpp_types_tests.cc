// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl/internal.h>
#include <lib/fidl/llcpp/array.h>
#include <lib/fidl/llcpp/coding.h>
#include <lib/fidl/llcpp/sync_call.h>
#include <lib/zx/channel.h>
#include <zircon/fidl.h>

#include <memory>
#include <optional>
#include <utility>

#include <zxtest/zxtest.h>

namespace {

// Manually define the coding table for FIDL messages used in these tests.
// These will match the llcpp codegen output.

extern const FidlCodedStruct NonnullableChannelMessageType;

// A message with a single non-nullable channel.
struct NonnullableChannelMessage {
  alignas(FIDL_ALIGNMENT) fidl_message_header_t header;
  zx::channel channel;

  [[maybe_unused]] static constexpr uint32_t MaxNumHandles = 1;

  static constexpr uint32_t PrimarySize =
      FIDL_ALIGN(sizeof(fidl_message_header_t)) + FIDL_ALIGN(sizeof(zx::channel));

  [[maybe_unused]] static constexpr uint32_t MaxOutOfLine = 0;

  [[maybe_unused]] static constexpr bool HasPointer = false;

  static constexpr bool IsResource = true;

  static constexpr const fidl_type_t* Type = &NonnullableChannelMessageType;

  static void MakeDecodedMessageHelper(
      fidl::BytePart buffer, fidl::DecodedMessage<NonnullableChannelMessage>* out_decoded_message,
      zx::channel* out_channel);
};

const FidlCodedHandle NonnullableChannelType = {
    .tag = kFidlTypeHandle,
    .nullable = kFidlNullability_Nonnullable,
    .handle_subtype = ZX_OBJ_TYPE_CHANNEL,
    .handle_rights = 0,
};
const FidlStructElement NonnullableChannelMessageFields[] = {
    FidlStructElement::Field(&NonnullableChannelType, offsetof(NonnullableChannelMessage, channel),
                             kFidlIsResource_Resource),
    FidlStructElement::Padding32(offsetof(NonnullableChannelMessage, channel) + 4, 0xffffffff),
};
const FidlCodedStruct NonnullableChannelMessageType = {
    .tag = kFidlTypeStruct,
    .element_count = 2,
    .size = sizeof(NonnullableChannelMessage),
    .elements = NonnullableChannelMessageFields,
    .name = "NonnullableChannelMessage",
};

extern const FidlCodedStruct InlinePODStructType;

// A message with a uint64_t.
struct InlinePODStruct {
  uint64_t payload;

  [[maybe_unused]] static constexpr uint32_t MaxNumHandles = 0;

  static constexpr uint32_t PrimarySize = FIDL_ALIGN(sizeof(payload));

  [[maybe_unused]] static constexpr uint32_t MaxOutOfLine = 0;

  [[maybe_unused]] static constexpr bool HasPointer = false;

  static constexpr bool IsResource = false;

  static constexpr const fidl_type_t* Type = &InlinePODStructType;

  static void MakeDecodedMessageHelper(fidl::BytePart buffer, uint64_t payload,
                                       fidl::DecodedMessage<InlinePODStruct>* out_decoded_message);
};

// Full-width primitives do not need coding tables.
const FidlStructElement InlinePODStructStructFields[] = {};
const FidlCodedStruct InlinePODStructType = {
    .tag = kFidlTypeStruct,
    .element_count = 0,
    .size = sizeof(InlinePODStruct),
    .elements = InlinePODStructStructFields,
    .name = "InlinePODStruct",
};

extern const FidlCodedStruct OutOfLineMessageType;

// A message with an optional struct.
struct OutOfLineMessage {
  alignas(FIDL_ALIGNMENT) fidl_message_header_t header;
  InlinePODStruct* optional;

  [[maybe_unused]] static constexpr uint32_t MaxNumHandles = 0;

  static constexpr uint32_t PrimarySize =
      FIDL_ALIGN(sizeof(fidl_message_header_t)) + FIDL_ALIGN(sizeof(InlinePODStruct*));

  [[maybe_unused]] static constexpr uint32_t MaxOutOfLine = 8;

  [[maybe_unused]] static constexpr bool HasPointer = true;

  static constexpr bool IsResource = false;

  static constexpr const fidl_type_t* Type = &OutOfLineMessageType;

  static void MakeDecodedMessageHelper(fidl::BytePart buffer,
                                       std::optional<uint64_t> optional_field,
                                       fidl::DecodedMessage<OutOfLineMessage>* out_decoded_message);
};

const FidlCodedStructPointer OptionalPointerType = {
    .tag = kFidlTypeStructPointer,
    .struct_type = &InlinePODStructType.coded_struct(),
};
const FidlStructElement OutOfLineMessageTypeFields[] = {
    FidlStructElement::Field(&OptionalPointerType, offsetof(OutOfLineMessage, optional),
                             kFidlIsResource_NotResource),
};
const FidlCodedStruct OutOfLineMessageType = {
    .tag = kFidlTypeStruct,
    .element_count = 1,
    .size = sizeof(OutOfLineMessage),
    .elements = OutOfLineMessageTypeFields,
    .name = "OutOfLineMessage",
};

extern const FidlCodedStruct LargeStructType;

// A message with a large array, such that it need to be heap-allocated.
struct LargeStruct {
  // 4096 * 8 = 32 KB
  fidl::Array<uint64_t, 4096> payload;

  [[maybe_unused]] static constexpr uint32_t MaxNumHandles = 0;

  static constexpr uint32_t PrimarySize = FIDL_ALIGN(sizeof(payload));

  [[maybe_unused]] static constexpr uint32_t MaxOutOfLine = 0;

  [[maybe_unused]] static constexpr bool HasPointer = false;

  [[maybe_unused]] static constexpr bool IsResource = false;

  static constexpr const fidl_type_t* Type = &LargeStructType;

  static void MakeDecodedMessageHelper(fidl::BytePart buffer, uint64_t fill,
                                       fidl::DecodedMessage<LargeStruct>* out_decoded_message);
};

// Full-width primitives do not need coding tables.
const FidlStructElement LargeStructStructFields[] = {};
const FidlCodedStruct LargeStructType = {
    .tag = kFidlTypeStruct,
    .element_count = 0,
    .size = sizeof(LargeStruct),
    .elements = LargeStructStructFields,
    .name = "LargeStruct",
};

// These two structs are used to test the stack/heap allocation selection in
// fidl::internal::ResponseStorage

struct StructOf512Bytes {
  fidl::Array<uint8_t, 512> payload;

  [[maybe_unused]] static constexpr uint32_t MaxNumHandles = 0;
  static constexpr uint32_t PrimarySize = FIDL_ALIGN(sizeof(payload));
  [[maybe_unused]] static constexpr uint32_t MaxOutOfLine = 0;
  [[maybe_unused]] static constexpr bool HasPointer = false;
  [[maybe_unused]] static constexpr bool IsResource = false;
  [[maybe_unused]] static constexpr const fidl_type_t* Type = nullptr;
};

struct StructOf513Bytes {
  fidl::Array<uint8_t, 513> payload;

  [[maybe_unused]] static constexpr uint32_t MaxNumHandles = 0;
  static constexpr uint32_t PrimarySize = FIDL_ALIGN(sizeof(payload));
  [[maybe_unused]] static constexpr uint32_t MaxOutOfLine = 0;
  [[maybe_unused]] static constexpr bool HasPointer = false;
  [[maybe_unused]] static constexpr bool IsResource = false;
  [[maybe_unused]] static constexpr const fidl_type_t* Type = nullptr;
};

}  // namespace

namespace fidl {

// Manually specialize the templates.
// These will match the llcpp codegen output.

template <>
struct IsFidlType<NonnullableChannelMessage> : public std::true_type {};
template <>
struct IsFidlMessage<NonnullableChannelMessage> : public std::true_type {};

template <>
struct IsFidlType<InlinePODStruct> : public std::true_type {};

template <>
struct IsFidlType<OutOfLineMessage> : public std::true_type {};
template <>
struct IsFidlMessage<OutOfLineMessage> : public std::true_type {};

template <>
struct IsFidlType<LargeStruct> : public std::true_type {};

template <>
struct IsFidlType<StructOf512Bytes> : public std::true_type {};

template <>
struct IsFidlType<StructOf513Bytes> : public std::true_type {};

}  // namespace fidl

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
  // Manually construct an encoded message
  alignas(NonnullableChannelMessage) uint8_t buf[sizeof(NonnullableChannelMessage)] = {};
  auto msg = reinterpret_cast<NonnullableChannelMessage*>(&buf[0]);
  msg->channel.reset(FIDL_HANDLE_PRESENT);

  // Capture the extra handle here; it will not be cleaned by encoded_message
  zx::channel channel_1 = {};

  {
    fidl::EncodedMessage<NonnullableChannelMessage> encoded_message;
    encoded_message.bytes() = fidl::BytePart(buf, sizeof(buf), sizeof(buf));
    zx_handle_t* handle = encoded_message.handles().data();

    // Unsafely open a channel, which should be closed automatically by encoded_message
    {
      zx::channel out0, out1;
      EXPECT_EQ(zx::channel::create(0, &out0, &out1), ZX_OK);
      *handle = out0.release();
      channel_1 = std::move(out1);
    }

    encoded_message.handles().set_actual(1);

    HelperExpectPeerValid(channel_1);
  }

  HelperExpectPeerInvalid(channel_1);
}

TEST(LlcppTypesTests, DecodedMessageTest) {
  // Manually construct a decoded message
  alignas(NonnullableChannelMessage) uint8_t buf[sizeof(NonnullableChannelMessage)] = {};
  auto msg = reinterpret_cast<NonnullableChannelMessage*>(&buf[0]);

  // Capture the extra handle here; it will not be cleaned by decoded_message
  zx::channel channel_1 = {};

  {
    // Unsafely open a channel, which should be closed automatically by decoded_message
    {
      zx::channel out0, out1;
      EXPECT_EQ(zx::channel::create(0, &out0, &out1), ZX_OK);
      msg->channel = std::move(out0);
      channel_1 = std::move(out1);
    }

    fidl::DecodedMessage<NonnullableChannelMessage> decoded_message(
        fidl::BytePart(buf, sizeof(buf), sizeof(buf)));

    HelperExpectPeerValid(channel_1);
  }

  HelperExpectPeerInvalid(channel_1);
}

// Start with an encoded message, then decode and back.
TEST(LlcppTypesTests, RoundTripTest) {
  alignas(NonnullableChannelMessage) uint8_t buf[sizeof(NonnullableChannelMessage)] = {};
  auto msg = reinterpret_cast<NonnullableChannelMessage*>(&buf[0]);
  msg->header.txid = 10;
  msg->header.ordinal = (42lu << 32);
  msg->channel.reset(FIDL_HANDLE_PRESENT);

  // Capture the extra handle here; it will not be cleaned by encoded_message
  zx::channel channel_1 = {};

  fidl::EncodedMessage<NonnullableChannelMessage>* encoded_message =
      new fidl::EncodedMessage<NonnullableChannelMessage>();

  encoded_message->bytes() = fidl::BytePart(buf, sizeof(buf), sizeof(buf));
  zx_handle_t* handle = encoded_message->handles().data();

  zx_handle_t unsafe_handle_backup;
  // Unsafely open a channel, which should be closed automatically by encoded_message
  {
    zx::channel out0, out1;
    EXPECT_EQ(zx::channel::create(0, &out0, &out1), ZX_OK);
    *handle = out0.release();
    unsafe_handle_backup = *handle;
    channel_1 = std::move(out1);
  }

  encoded_message->handles().set_actual(1);

  uint8_t golden_encoded[] = {10,  0,   0,   0,    // txid
                              0,   0,   0,   0,    // reserved
                              0,   0,   0,   0,    // low bytes of ordinal (was flags)
                              42,  0,   0,   0,    // high bytes of ordinal
                              255, 255, 255, 255,  // handle present
                              0,   0,   0,   0};

  // Byte-accurate comparison
  EXPECT_EQ(memcmp(golden_encoded, buf, sizeof(buf)), 0);

  HelperExpectPeerValid(channel_1);

  // Decode
  auto decode_result = fidl::Decode(std::move(*encoded_message));
  auto& decoded_message = decode_result.message;
  EXPECT_EQ(decode_result.status, ZX_OK);
  EXPECT_NULL(decode_result.error, "%s", decode_result.error);
  EXPECT_EQ(decoded_message.message()->header.txid, 10);
  EXPECT_EQ(decoded_message.message()->header.ordinal, (42lu << 32));
  EXPECT_EQ(decoded_message.message()->channel.get(), unsafe_handle_backup);
  // encoded_message should be consumed
  EXPECT_EQ(encoded_message->handles().actual(), 0);
  EXPECT_EQ(encoded_message->bytes().actual(), 0);
  // If we destroy encoded_message, it should not accidentally close the channel
  delete encoded_message;
  HelperExpectPeerValid(channel_1);

  // Encode
  {
    auto encode_result = fidl::Encode(std::move(decoded_message));
    auto& encoded_message = encode_result.message;
    EXPECT_EQ(encode_result.status, ZX_OK);
    EXPECT_NULL(encode_result.error, "%s", encode_result.error);
    // decoded_message should be consumed
    EXPECT_EQ(decoded_message.message(), nullptr);

    // Byte-level comparison
    EXPECT_EQ(encoded_message.bytes().actual(), sizeof(buf));
    EXPECT_EQ(encoded_message.handles().actual(), 1);
    EXPECT_EQ(encoded_message.handles().data()[0], unsafe_handle_backup);
    EXPECT_EQ(memcmp(golden_encoded, encoded_message.bytes().data(), sizeof(buf)), 0);

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

template <typename TestMessage>
class MySyncCall;

// Helper to populate a OwnedSyncCallBase<NonnullableChannelMessage> with a decoded message,
// as if receiving a FIDL reply.
void NonnullableChannelMessage::MakeDecodedMessageHelper(
    fidl::BytePart buffer, fidl::DecodedMessage<NonnullableChannelMessage>* out_decoded_message,
    zx::channel* out_channel) {
  auto msg = reinterpret_cast<NonnullableChannelMessage*>(buffer.data());
  memset(buffer.data(), 0, buffer.capacity());

  {
    zx::channel out0, out1;
    EXPECT_EQ(zx::channel::create(0, &out0, &out1), ZX_OK);
    msg->channel = std::move(out0);
    // Capture the extra handle here; it will not be cleaned by decoded_message
    *out_channel = std::move(out1);

    fidl::BytePart full_buffer(std::move(buffer));
    full_buffer.set_actual(sizeof(NonnullableChannelMessage));
    fidl::DecodedMessage<NonnullableChannelMessage> decoded_message(std::move(full_buffer));
    *out_decoded_message = std::move(decoded_message);
  }

  HelperExpectPeerValid(*out_channel);
}

// Helper to populate a OwnedSyncCallBase<InlinePODStruct> with a decoded message,
// as if receiving a FIDL reply.
void InlinePODStruct::MakeDecodedMessageHelper(
    fidl::BytePart buffer, uint64_t payload,
    fidl::DecodedMessage<InlinePODStruct>* out_decoded_message) {
  auto msg = reinterpret_cast<InlinePODStruct*>(buffer.data());
  memset(buffer.data(), 0, buffer.capacity());
  msg->payload = payload;

  {
    fidl::BytePart full_buffer(std::move(buffer));
    full_buffer.set_actual(sizeof(InlinePODStruct));
    fidl::DecodedMessage<InlinePODStruct> decoded_message(std::move(full_buffer));
    *out_decoded_message = std::move(decoded_message);
  }

  EXPECT_EQ(out_decoded_message->message()->payload, payload);
}

// Helper to populate a OwnedSyncCallBase<OutOfLineMessage> with a decoded message,
// as if receiving a FIDL reply.
void OutOfLineMessage::MakeDecodedMessageHelper(
    fidl::BytePart buffer, std::optional<uint64_t> optional_field,
    fidl::DecodedMessage<OutOfLineMessage>* out_decoded_message) {
  auto msg = reinterpret_cast<OutOfLineMessage*>(buffer.data());
  memset(buffer.data(), 0, buffer.capacity());

  if (optional_field) {
    ASSERT_EQ(buffer.capacity(), FIDL_ALIGN(OutOfLineMessage::PrimarySize) +
                                     FIDL_ALIGN(OutOfLineMessage::MaxOutOfLine));
    auto out_of_line = reinterpret_cast<InlinePODStruct*>(
        reinterpret_cast<uint8_t*>(msg) + FIDL_ALIGN(OutOfLineMessage::PrimarySize));
    out_of_line->payload = optional_field.value();
    msg->optional = out_of_line;
  } else {
    ASSERT_GE(buffer.capacity(), FIDL_ALIGN(OutOfLineMessage::PrimarySize));
    msg->optional = nullptr;
  }

  {
    fidl::BytePart full_buffer(std::move(buffer));
    full_buffer.set_actual(sizeof(OutOfLineMessage));
    fidl::DecodedMessage<OutOfLineMessage> decoded_message(std::move(full_buffer));
    *out_decoded_message = std::move(decoded_message);
  }
}

// Helper to populate a OwnedSyncCallBase<LargeStruct> with a decoded message,
// as if receiving a FIDL reply.
void LargeStruct::MakeDecodedMessageHelper(fidl::BytePart buffer, uint64_t fill,
                                           fidl::DecodedMessage<LargeStruct>* out_decoded_message) {
  auto msg = reinterpret_cast<LargeStruct*>(buffer.data());
  memset(buffer.data(), 0, buffer.capacity());
  for (auto& x : msg->payload) {
    x = fill;
  }

  {
    fidl::BytePart full_buffer(std::move(buffer));
    full_buffer.set_actual(sizeof(LargeStruct));
    fidl::DecodedMessage<LargeStruct> decoded_message(std::move(full_buffer));
    *out_decoded_message = std::move(decoded_message);
  }

  for (const auto& x : out_decoded_message->message()->payload) {
    EXPECT_EQ(x, fill);
  }
}

TEST(LlcppTypesTests, ResponseStorageAllocationStrategyTest) {
  // The stack allocation limit of 512 bytes is defined in
  // zircon/system/ulib/fidl/include/lib/fidl/llcpp/sync_call.h
  ASSERT_EQ(sizeof(fidl::internal::ResponseStorage<StructOf512Bytes>), 512);

  // Since the buffer is on heap, |ResponseStorage| becomes a pointer.
  ASSERT_EQ(sizeof(fidl::internal::ResponseStorage<StructOf513Bytes>), sizeof(std::uintptr_t));
}

}  // namespace
