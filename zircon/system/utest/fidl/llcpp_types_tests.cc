// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <utility>
#include <optional>

#include <lib/fidl/internal.h>
#include <lib/fidl/llcpp/array.h>
#include <lib/fidl/llcpp/coding.h>
#include <lib/fidl/llcpp/sync_call.h>
#include <lib/zx/channel.h>
#include <unittest/unittest.h>
#include <zircon/fidl.h>

namespace {

// Manually define the coding table for FIDL messages used in these tests.
// These will match the llcpp codegen output.

extern const fidl_type_t NonnullableChannelMessageType;

// A message with a single non-nullable channel.
struct NonnullableChannelMessage {
  alignas(FIDL_ALIGNMENT) fidl_message_header_t header;
  zx::channel channel;

  [[maybe_unused]] static constexpr uint32_t MaxNumHandles = 1;

  static constexpr uint32_t PrimarySize =
      FIDL_ALIGN(sizeof(fidl_message_header_t)) + FIDL_ALIGN(sizeof(zx::channel));

  [[maybe_unused]] static constexpr uint32_t MaxOutOfLine = 0;

  static constexpr bool HasPointer = false;

  static constexpr const fidl_type_t* Type = &NonnullableChannelMessageType;

  static bool MakeDecodedMessageHelper(
      fidl::BytePart buffer, fidl::DecodedMessage<NonnullableChannelMessage>* out_decoded_message,
      zx::channel* out_channel);
};

const fidl_type_t NonnullableChannelType = {
    .type_tag = kFidlTypeHandle,
    {.coded_handle = {.handle_subtype = ZX_OBJ_TYPE_CHANNEL,
                      .nullable = kFidlNullability_Nonnullable}}};
const FidlStructField NonnullableChannelMessageFields[] = {
    FidlStructField(&NonnullableChannelType, offsetof(NonnullableChannelMessage, channel), 4),
};
const fidl_type_t NonnullableChannelMessageType = {
    .type_tag = kFidlTypeStruct,
    {.coded_struct = {.fields = NonnullableChannelMessageFields,
                      .field_count = 1,
                      .size = sizeof(NonnullableChannelMessage),
                      .max_out_of_line = UINT32_MAX,
                      .contains_union = true,
                      .name = "NonnullableChannelMessage",
                      .alt_type = nullptr}}};

extern const fidl_type_t InlinePODStructType;

// A message with a uint64_t.
struct InlinePODStruct {
  uint64_t payload;

  [[maybe_unused]] static constexpr uint32_t MaxNumHandles = 0;

  static constexpr uint32_t PrimarySize = FIDL_ALIGN(sizeof(payload));

  [[maybe_unused]] static constexpr uint32_t MaxOutOfLine = 0;

  static constexpr bool HasPointer = false;

  static constexpr const fidl_type_t* Type = &InlinePODStructType;

  static bool MakeDecodedMessageHelper(fidl::BytePart buffer, uint64_t payload,
                                       fidl::DecodedMessage<InlinePODStruct>* out_decoded_message);
};

// Full-width primitives do not need coding tables.
const FidlStructField InlinePODStructStructFields[] = {};
const fidl_type_t InlinePODStructType = {.type_tag = kFidlTypeStruct,
                                         {.coded_struct = {.fields = InlinePODStructStructFields,
                                                           .field_count = 0,
                                                           .size = sizeof(InlinePODStruct),
                                                           .max_out_of_line = UINT32_MAX,
                                                           .contains_union = true,
                                                           .name = "InlinePODStruct",
                                                           .alt_type = nullptr}}};

extern const fidl_type_t OutOfLineMessageType;

// A message with an optional struct.
struct OutOfLineMessage {
  alignas(FIDL_ALIGNMENT) fidl_message_header_t header;
  InlinePODStruct* optional;

  [[maybe_unused]] static constexpr uint32_t MaxNumHandles = 0;

  static constexpr uint32_t PrimarySize =
      FIDL_ALIGN(sizeof(fidl_message_header_t)) + FIDL_ALIGN(sizeof(InlinePODStruct*));

  [[maybe_unused]] static constexpr uint32_t MaxOutOfLine = 8;

  static constexpr bool HasPointer = true;

  static constexpr const fidl_type_t* Type = &OutOfLineMessageType;

  static bool MakeDecodedMessageHelper(fidl::BytePart buffer,
                                       std::optional<uint64_t> optional_field,
                                       fidl::DecodedMessage<OutOfLineMessage>* out_decoded_message);
};

const fidl_type_t OptionalPointerType = {
    .type_tag = kFidlTypeStructPointer,
    {.coded_struct_pointer = {.struct_type = &InlinePODStructType.coded_struct}}};
const FidlStructField OutOfLineMessageTypeFields[] = {
    FidlStructField(&OptionalPointerType, offsetof(OutOfLineMessage, optional), 0),
};
const fidl_type_t OutOfLineMessageType = {.type_tag = kFidlTypeStruct,
                                          {.coded_struct = {.fields = OutOfLineMessageTypeFields,
                                                            .field_count = 1,
                                                            .size = sizeof(OutOfLineMessage),
                                                            .max_out_of_line = UINT32_MAX,
                                                            .contains_union = true,
                                                            .name = "OutOfLineMessage",
                                                            .alt_type = nullptr}}};

extern const fidl_type_t LargeStructType;

// A message with a large array, such that it need to be heap-allocated.
struct LargeStruct {
  // 4096 * 8 = 32 KB
  fidl::Array<uint64_t, 4096> payload;

  [[maybe_unused]] static constexpr uint32_t MaxNumHandles = 0;

  static constexpr uint32_t PrimarySize = FIDL_ALIGN(sizeof(payload));

  [[maybe_unused]] static constexpr uint32_t MaxOutOfLine = 0;

  [[maybe_unused]] static constexpr bool HasPointer = false;

  static constexpr const fidl_type_t* Type = &LargeStructType;

  static bool MakeDecodedMessageHelper(fidl::BytePart buffer, uint64_t fill,
                                       fidl::DecodedMessage<LargeStruct>* out_decoded_message);
};

// Full-width primitives do not need coding tables.
const FidlStructField LargeStructStructFields[] = {};
const fidl_type_t LargeStructType = {.type_tag = kFidlTypeStruct,
                                     {.coded_struct = {.fields = LargeStructStructFields,
                                                       .field_count = 0,
                                                       .size = sizeof(LargeStruct),
                                                       .max_out_of_line = UINT32_MAX,
                                                       .contains_union = true,
                                                       .name = "LargeStruct",
                                                       .alt_type = nullptr}}};

// These two structs are used to test the stack/heap allocation selection in
// fidl::internal::ResponseStorage

struct StructOf512Bytes {
  fidl::Array<uint8_t, 512> payload;

  [[maybe_unused]] static constexpr uint32_t MaxNumHandles = 0;
  static constexpr uint32_t PrimarySize = FIDL_ALIGN(sizeof(payload));
  [[maybe_unused]] static constexpr uint32_t MaxOutOfLine = 0;
  [[maybe_unused]] static constexpr bool HasPointer = false;
  [[maybe_unused]] static constexpr const fidl_type_t* Type = nullptr;
};

struct StructOf513Bytes {
  fidl::Array<uint8_t, 513> payload;

  [[maybe_unused]] static constexpr uint32_t MaxNumHandles = 0;
  static constexpr uint32_t PrimarySize = FIDL_ALIGN(sizeof(payload));
  [[maybe_unused]] static constexpr uint32_t MaxOutOfLine = 0;
  [[maybe_unused]] static constexpr bool HasPointer = false;
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

bool HelperExpectPeerValid(zx::channel& channel) {
  BEGIN_HELPER;

  const char* foo = "A";
  EXPECT_EQ(channel.write(0, foo, 1, nullptr, 0), ZX_OK);

  END_HELPER;
}

bool HelperExpectPeerInvalid(zx::channel& channel) {
  BEGIN_HELPER;

  const char* foo = "A";
  EXPECT_EQ(channel.write(0, foo, 1, nullptr, 0), ZX_ERR_PEER_CLOSED);

  END_HELPER;
}

bool EncodedMessageTest() {
  BEGIN_TEST;

  // Manually construct an encoded message
  alignas(NonnullableChannelMessage) uint8_t buf[sizeof(NonnullableChannelMessage)] = {};
  auto msg = reinterpret_cast<NonnullableChannelMessage*>(&buf[0]);
  msg->channel.reset(FIDL_HANDLE_PRESENT);

  // Capture the extra handle here; it will not be cleaned by encoded_message
  zx::channel channel_1 = {};

  {
    fidl::EncodedMessage<NonnullableChannelMessage> encoded_message;
    encoded_message.Initialize(
        [&buf, &channel_1](fidl::BytePart* out_msg_bytes, fidl::HandlePart* msg_handles) {
          *out_msg_bytes = fidl::BytePart(buf, sizeof(buf), sizeof(buf));
          zx_handle_t* handle = msg_handles->data();

          // Unsafely open a channel, which should be closed automatically by encoded_message
          {
            zx::channel out0, out1;
            EXPECT_EQ(zx::channel::create(0, &out0, &out1), ZX_OK);
            *handle = out0.release();
            channel_1 = std::move(out1);
          }

          msg_handles->set_actual(1);
        });

    EXPECT_TRUE(HelperExpectPeerValid(channel_1));
  }

  EXPECT_TRUE(HelperExpectPeerInvalid(channel_1));

  END_TEST;
}

bool DecodedMessageTest() {
  BEGIN_TEST;

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

    EXPECT_TRUE(HelperExpectPeerValid(channel_1));
  }

  EXPECT_TRUE(HelperExpectPeerInvalid(channel_1));

  END_TEST;
}

// Start with an encoded message, then decode and back.
bool RoundTripTest() {
  BEGIN_TEST;

  alignas(NonnullableChannelMessage) uint8_t buf[sizeof(NonnullableChannelMessage)] = {};
  auto msg = reinterpret_cast<NonnullableChannelMessage*>(&buf[0]);
  msg->header.txid = 10;
  msg->header.ordinal = (42lu << 32);
  msg->channel.reset(FIDL_HANDLE_PRESENT);

  // Capture the extra handle here; it will not be cleaned by encoded_message
  zx::channel channel_1 = {};

  fidl::EncodedMessage<NonnullableChannelMessage>* encoded_message =
      new fidl::EncodedMessage<NonnullableChannelMessage>();
  zx_handle_t unsafe_handle_backup;

  encoded_message->Initialize([&buf, &channel_1, &unsafe_handle_backup](
                                  fidl::BytePart* out_msg_bytes, fidl::HandlePart* msg_handles) {
    *out_msg_bytes = fidl::BytePart(buf, sizeof(buf), sizeof(buf));
    zx_handle_t* handle = msg_handles->data();

    // Unsafely open a channel, which should be closed automatically by encoded_message
    {
      zx::channel out0, out1;
      EXPECT_EQ(zx::channel::create(0, &out0, &out1), ZX_OK);
      *handle = out0.release();
      unsafe_handle_backup = *handle;
      channel_1 = std::move(out1);
    }

    msg_handles->set_actual(1);
  });

  uint8_t golden_encoded[] = {10,  0,   0,   0,    // txid
                              0,   0,   0,   0,    // reserved
                              0,   0,   0,   0,    // low bytes of ordinal (was flags)
                              42,  0,   0,   0,    // high bytes of ordinal
                              255, 255, 255, 255,  // handle present
                              0,   0,   0,   0};

  // Byte-accurate comparison
  EXPECT_EQ(memcmp(golden_encoded, buf, sizeof(buf)), 0);

  EXPECT_TRUE(HelperExpectPeerValid(channel_1));

  // Decode
  auto decode_result = fidl::Decode(std::move(*encoded_message));
  auto& decoded_message = decode_result.message;
  EXPECT_EQ(decode_result.status, ZX_OK);
  EXPECT_NULL(decode_result.error, decode_result.error);
  EXPECT_EQ(decoded_message.message()->header.txid, 10);
  EXPECT_EQ(decoded_message.message()->header.ordinal, (42lu << 32));
  EXPECT_EQ(decoded_message.message()->channel.get(), unsafe_handle_backup);
  // encoded_message should be consumed
  EXPECT_EQ(encoded_message->handles().actual(), 0);
  EXPECT_EQ(encoded_message->bytes().actual(), 0);
  // If we destroy encoded_message, it should not accidentally close the channel
  delete encoded_message;
  EXPECT_TRUE(HelperExpectPeerValid(channel_1));

  // Encode
  {
    auto encode_result = fidl::Encode(std::move(decoded_message));
    auto& encoded_message = encode_result.message;
    EXPECT_EQ(encode_result.status, ZX_OK);
    EXPECT_NULL(encode_result.error, encode_result.error);
    // decoded_message should be consumed
    EXPECT_EQ(decoded_message.message(), nullptr);

    // Byte-level comparison
    EXPECT_EQ(encoded_message.bytes().actual(), sizeof(buf));
    EXPECT_EQ(encoded_message.handles().actual(), 1);
    EXPECT_EQ(encoded_message.handles().data()[0], unsafe_handle_backup);
    EXPECT_EQ(memcmp(golden_encoded, encoded_message.bytes().data(), sizeof(buf)), 0);

    EXPECT_TRUE(HelperExpectPeerValid(channel_1));
  }
  // Encoded message was destroyed, bringing down the handle with it
  EXPECT_TRUE(HelperExpectPeerInvalid(channel_1));

  END_TEST;
}

bool ArrayLayoutTest() {
  BEGIN_TEST;

  static_assert(sizeof(fidl::Array<uint8_t, 3>) == sizeof(uint8_t[3]));
  static_assert(sizeof(fidl::Array<fidl::Array<uint8_t, 7>, 3>) == sizeof(uint8_t[3][7]));

  constexpr fidl::Array<uint8_t, 3> a = {1, 2, 3};
  constexpr uint8_t b[3] = {1, 2, 3};
  EXPECT_EQ((&a[2] - &a[0]), (&b[2] - &b[0]));

  END_TEST;
}

bool UninitializedBufferStackAllocationAlignmentTest() {
  BEGIN_TEST;

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

  END_TEST;
}

bool UninitializedBufferHeapAllocationAlignmentTest() {
  BEGIN_TEST;

  std::unique_ptr array_of_1 = std::make_unique<fidl::internal::AlignedBuffer<1>>();
  ASSERT_TRUE(reinterpret_cast<uintptr_t>(array_of_1.get()) % 8 == 0);

  std::unique_ptr array_of_5 = std::make_unique<fidl::internal::AlignedBuffer<5>>();
  ASSERT_TRUE(reinterpret_cast<uintptr_t>(array_of_5.get()) % 8 == 0);

  std::unique_ptr array_of_25 = std::make_unique<fidl::internal::AlignedBuffer<25>>();
  ASSERT_TRUE(reinterpret_cast<uintptr_t>(array_of_25.get()) % 8 == 0);

  std::unique_ptr array_of_100 = std::make_unique<fidl::internal::AlignedBuffer<100>>();
  ASSERT_TRUE(reinterpret_cast<uintptr_t>(array_of_100.get()) % 8 == 0);

  END_TEST;
}

template <typename TestMessage>
class MySyncCall;

// Helper to populate a OwnedSyncCallBase<NonnullableChannelMessage> with a decoded message,
// as if receiving a FIDL reply.
bool NonnullableChannelMessage::MakeDecodedMessageHelper(
    fidl::BytePart buffer, fidl::DecodedMessage<NonnullableChannelMessage>* out_decoded_message,
    zx::channel* out_channel) {
  BEGIN_HELPER;

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

  EXPECT_TRUE(HelperExpectPeerValid(*out_channel));

  END_HELPER;
}

// Manually define the OwnedSyncCallBase type for NonnullableChannelMessage for the
// OwningSyncCallWithHandlesTest below.
// This is similar to the llcpp codegen output.
template <>
class MySyncCall<NonnullableChannelMessage>
    : private fidl::internal::OwnedSyncCallBase<NonnullableChannelMessage> {
  using Super = fidl::internal::OwnedSyncCallBase<NonnullableChannelMessage>;

 public:
  explicit MySyncCall(zx::channel* out_channel) {
    fidl::DecodedMessage<NonnullableChannelMessage> decoded_message;
    EXPECT_TRUE(NonnullableChannelMessage::MakeDecodedMessageHelper(Super::response_buffer(),
                                                                    &decoded_message, out_channel));
    Super::SetResult(fidl::DecodeResult(ZX_OK, nullptr, std::move(decoded_message)));
  }
  ~MySyncCall() = default;
  MySyncCall(MySyncCall&& other) = default;
  MySyncCall& operator=(MySyncCall&& other) = default;
  using Super::error;
  using Super::status;
  using Super::Unwrap;
};

// Helper to populate a OwnedSyncCallBase<InlinePODStruct> with a decoded message,
// as if receiving a FIDL reply.
bool InlinePODStruct::MakeDecodedMessageHelper(
    fidl::BytePart buffer, uint64_t payload,
    fidl::DecodedMessage<InlinePODStruct>* out_decoded_message) {
  BEGIN_HELPER;

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

  END_HELPER;
}

// Manually define the OwnedSyncCallBase type for InlinePODStruct for the
// OwningSyncCallWithPODTest below.
// This is similar to the llcpp codegen output.
template <>
class MySyncCall<InlinePODStruct> : private fidl::internal::OwnedSyncCallBase<InlinePODStruct> {
  using Super = fidl::internal::OwnedSyncCallBase<InlinePODStruct>;

 public:
  explicit MySyncCall(uint64_t payload) {
    fidl::DecodedMessage<InlinePODStruct> decoded_message;
    EXPECT_TRUE(InlinePODStruct::MakeDecodedMessageHelper(Super::response_buffer(), payload,
                                                          &decoded_message));
    Super::SetResult(fidl::DecodeResult(ZX_OK, nullptr, std::move(decoded_message)));
  }

  // Constructs a failed OwnedSyncCallBase.
  MySyncCall(zx_status_t status, const char* error) {
    Super::SetFailure(fidl::EncodeResult<InlinePODStruct>(status, error));
  }

  ~MySyncCall() = default;
  MySyncCall(MySyncCall&& other) = default;
  MySyncCall& operator=(MySyncCall&& other) = default;
  using Super::error;
  using Super::status;
  using Super::Unwrap;
};

// Helper to populate a OwnedSyncCallBase<OutOfLineMessage> with a decoded message,
// as if receiving a FIDL reply.
bool OutOfLineMessage::MakeDecodedMessageHelper(
    fidl::BytePart buffer, std::optional<uint64_t> optional_field,
    fidl::DecodedMessage<OutOfLineMessage>* out_decoded_message) {
  BEGIN_HELPER;

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

  END_HELPER;
}

// Manually define the OwnedSyncCallBase type for OutOfLineMessage for the
// OwningSyncCallWithOutOfLineTest below.
// This is similar to the llcpp codegen output.
template <>
class MySyncCall<OutOfLineMessage> : private fidl::internal::OwnedSyncCallBase<OutOfLineMessage> {
  using Super = fidl::internal::OwnedSyncCallBase<OutOfLineMessage>;

 public:
  explicit MySyncCall(std::optional<uint64_t> optional_field) {
    fidl::DecodedMessage<OutOfLineMessage> decoded_message;
    EXPECT_TRUE(OutOfLineMessage::MakeDecodedMessageHelper(Super::response_buffer(), optional_field,
                                                           &decoded_message));
    Super::SetResult(fidl::DecodeResult(ZX_OK, nullptr, std::move(decoded_message)));
  }
  ~MySyncCall() = default;
  MySyncCall(MySyncCall&& other) = default;
  MySyncCall& operator=(MySyncCall&& other) = default;
  using Super::error;
  using Super::status;
  using Super::Unwrap;
};

// Helper to populate a OwnedSyncCallBase<LargeStruct> with a decoded message,
// as if receiving a FIDL reply.
bool LargeStruct::MakeDecodedMessageHelper(fidl::BytePart buffer, uint64_t fill,
                                           fidl::DecodedMessage<LargeStruct>* out_decoded_message) {
  BEGIN_HELPER;

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

  END_HELPER;
}

// Manually define the OwnedSyncCallBase type for LargeStruct, for the OwningSyncCallHeapTest below.
// This is similar to the llcpp codegen output.
template <>
class MySyncCall<LargeStruct> : private fidl::internal::OwnedSyncCallBase<LargeStruct> {
  using Super = fidl::internal::OwnedSyncCallBase<LargeStruct>;

 public:
  explicit MySyncCall(uint64_t fill) {
    fidl::DecodedMessage<LargeStruct> decoded_message;
    EXPECT_TRUE(
        LargeStruct::MakeDecodedMessageHelper(Super::response_buffer(), fill, &decoded_message));
    Super::SetResult(fidl::DecodeResult(ZX_OK, nullptr, std::move(decoded_message)));
  }

  ~MySyncCall() = default;
  MySyncCall(MySyncCall&& other) = default;
  MySyncCall& operator=(MySyncCall&& other) = default;
  using Super::error;
  using Super::status;
  using Super::Unwrap;
};

// Test that in the case of stack-allocated response, handles are transferred correctly
// when the message is moved.
bool OwningSyncCallWithHandlesTest() {
  BEGIN_TEST;

  zx::channel peer_1;
  zx::channel peer_2;

  {
    MySyncCall<NonnullableChannelMessage> sync_call_1(&peer_1);
    ASSERT_TRUE(HelperExpectPeerValid(peer_1));
    ASSERT_EQ(sync_call_1.status(), ZX_OK);
    ASSERT_EQ(sync_call_1.error(), nullptr);

    MySyncCall<NonnullableChannelMessage> sync_call_2(&peer_2);
    ASSERT_TRUE(HelperExpectPeerValid(peer_2));
    ASSERT_EQ(sync_call_2.status(), ZX_OK);
    ASSERT_EQ(sync_call_2.error(), nullptr);

    // Moving |sync_call_2| into |sync_call_1| will destroy the message originally in |sync_call_1|.
    sync_call_1 = std::move(sync_call_2);
    ASSERT_TRUE(HelperExpectPeerInvalid(peer_1));
    ASSERT_TRUE(HelperExpectPeerValid(peer_2));
  }

  ASSERT_TRUE(HelperExpectPeerInvalid(peer_1));
  ASSERT_TRUE(HelperExpectPeerInvalid(peer_2));

  END_TEST;
}

// Test that in the case of stack-allocated response, pointers to out-of-line are correctly
// updated when the message is moved.
bool OwningSyncCallWithOutOfLineTest() {
  BEGIN_TEST;

  MySyncCall<OutOfLineMessage> sync_call_1(std::nullopt);
  ASSERT_EQ(sync_call_1.status(), ZX_OK);
  ASSERT_EQ(sync_call_1.error(), nullptr);
  ASSERT_NULL(sync_call_1.Unwrap()->optional);

  MySyncCall<OutOfLineMessage> sync_call_2(0xABCDABCD);
  ASSERT_EQ(sync_call_2.status(), ZX_OK);
  ASSERT_EQ(sync_call_2.error(), nullptr);
  ASSERT_NONNULL(sync_call_2.Unwrap()->optional);
  ASSERT_EQ(sync_call_2.Unwrap()->optional->payload, 0xABCDABCD);

  sync_call_1 = std::move(sync_call_2);
  ASSERT_NONNULL(sync_call_1.Unwrap());
  ASSERT_NULL(sync_call_2.Unwrap());
  ASSERT_EQ(sync_call_1.Unwrap()->optional->payload, 0xABCDABCD);

  auto pointer_to_optional = reinterpret_cast<InlinePODStruct*>(
      reinterpret_cast<uint8_t*>(sync_call_1.Unwrap()) + FIDL_ALIGN(OutOfLineMessage::PrimarySize));
  ASSERT_EQ(sync_call_1.Unwrap()->optional, pointer_to_optional);

  END_TEST;
}

// Test that std::move on a stack-allocated POD response works correctly.
// Internally, it should be implemented as a memcpy.
bool OwningSyncCallWithPODTest() {
  BEGIN_TEST;

  MySyncCall<InlinePODStruct> sync_call_1(0x12345678);
  ASSERT_EQ(sync_call_1.status(), ZX_OK);
  ASSERT_EQ(sync_call_1.error(), nullptr);
  ASSERT_EQ(sync_call_1.Unwrap()->payload, 0x12345678);

  MySyncCall<InlinePODStruct> sync_call_2(0xABABABAB);
  ASSERT_EQ(sync_call_2.status(), ZX_OK);
  ASSERT_EQ(sync_call_2.error(), nullptr);
  ASSERT_EQ(sync_call_2.Unwrap()->payload, 0xABABABAB);

  ASSERT_NE(&sync_call_1.Unwrap()->payload, &sync_call_2.Unwrap()->payload);

  sync_call_1 = std::move(sync_call_2);
  ASSERT_NONNULL(sync_call_1.Unwrap());
  ASSERT_NULL(sync_call_2.Unwrap());
  ASSERT_EQ(sync_call_1.Unwrap()->payload, 0xABABABAB);

  END_TEST;
}

// Test that in the case of heap-allocated response, moving the message moves the pointer
// to response buffer.
bool OwningSyncCallHeapTest() {
  BEGIN_TEST;

  MySyncCall<LargeStruct> sync_call_1(0x12345678);
  ASSERT_EQ(sync_call_1.status(), ZX_OK);
  ASSERT_EQ(sync_call_1.error(), nullptr);
  ASSERT_EQ(sync_call_1.Unwrap()->payload[0], 0x12345678);

  uint64_t* array_address = &sync_call_1.Unwrap()->payload[0];
  MySyncCall<LargeStruct> sync_call_2(std::move(sync_call_1));
  ASSERT_NE(&sync_call_1, &sync_call_2);
  ASSERT_EQ(array_address, &sync_call_2.Unwrap()->payload[0]);
  ASSERT_EQ(sync_call_2.Unwrap()->payload[0], 0x12345678);

  END_TEST;
}

// Test the |OwnedSyncCallBase| holds failure during encode/decode etc correctly.
bool OwningSyncCallFailureTest() {
  BEGIN_TEST;

  MySyncCall<InlinePODStruct> failed_call(ZX_ERR_INVALID_ARGS, "err");
  ASSERT_EQ(failed_call.status(), ZX_ERR_INVALID_ARGS);
  ASSERT_STR_EQ(failed_call.error(), "err");

  END_TEST;
}

bool UnownedSyncCallTest() {
  BEGIN_TEST;

  // Manually define the unowned OwnedSyncCallBase type for InlinePODStruct.
  // This is similar to the llcpp codegen output.
  class MyUnownedSyncCall : private fidl::internal::UnownedSyncCallBase<InlinePODStruct> {
    using Super = fidl::internal::UnownedSyncCallBase<InlinePODStruct>;

   public:
    explicit MyUnownedSyncCall(fidl::BytePart buffer, uint64_t payload) {
      fidl::DecodedMessage<InlinePODStruct> decoded_message;
      EXPECT_TRUE(
          InlinePODStruct::MakeDecodedMessageHelper(std::move(buffer), payload, &decoded_message));
      Super::SetResult(fidl::DecodeResult(ZX_OK, nullptr, std::move(decoded_message)));
    }

    ~MyUnownedSyncCall() = default;
    MyUnownedSyncCall(MyUnownedSyncCall&& other) = default;
    MyUnownedSyncCall& operator=(MyUnownedSyncCall&& other) = default;
    using Super::error;
    using Super::status;
    using Super::Unwrap;
  };

  // When using caller-allocated buffer, it has to be FIDL-aligned.
  FIDL_ALIGNDECL uint8_t response_buffer[64];
  MyUnownedSyncCall call_1(fidl::BytePart::WrapEmpty(response_buffer), 0xABCDABCD);
  ASSERT_EQ(call_1.status(), ZX_OK);
  ASSERT_EQ(call_1.error(), nullptr);
  ASSERT_EQ(call_1.Unwrap()->payload, 0xABCDABCD);
  ASSERT_EQ(reinterpret_cast<uint8_t*>(call_1.Unwrap()), &response_buffer[0]);

  MyUnownedSyncCall call_2(std::move(call_1));
  ASSERT_EQ(call_2.status(), ZX_OK);
  ASSERT_EQ(call_2.error(), nullptr);
  ASSERT_EQ(call_2.Unwrap()->payload, 0xABCDABCD);
  ASSERT_EQ(reinterpret_cast<uint8_t*>(call_2.Unwrap()), &response_buffer[0]);

  END_TEST;
}

bool ResponseStorageAllocationStrategyTest() {
  BEGIN_TEST;

  // The stack allocation limit of 512 bytes is defined in
  // zircon/system/ulib/fidl/include/lib/fidl/llcpp/sync_call.h
  ASSERT_EQ(sizeof(fidl::internal::ResponseStorage<StructOf512Bytes>), 512);

  // Since the buffer is on heap, |ResponseStorage| becomes a pointer.
  ASSERT_EQ(sizeof(fidl::internal::ResponseStorage<StructOf513Bytes>), sizeof(std::uintptr_t));

  END_TEST;
}

}  // namespace

BEGIN_TEST_CASE(llcpp_types_tests)
RUN_NAMED_TEST("EncodedMessage test", EncodedMessageTest)
RUN_NAMED_TEST("DecodedMessage test", DecodedMessageTest)
RUN_NAMED_TEST("Round trip test", RoundTripTest)
RUN_NAMED_TEST("Array layout test", ArrayLayoutTest)
RUN_NAMED_TEST("fidl::internal::AlignedBuffer alignment on stack",
               UninitializedBufferStackAllocationAlignmentTest)
RUN_NAMED_TEST("fidl::internal::AlignedBuffer alignment on heap",
               UninitializedBufferHeapAllocationAlignmentTest)
RUN_NAMED_TEST("OwnedSyncCallBase std::move [inlined response, message has handles]",
               OwningSyncCallWithHandlesTest)
RUN_NAMED_TEST("OwnedSyncCallBase std::move [inlined response, message has out-of-line]",
               OwningSyncCallWithOutOfLineTest)
RUN_NAMED_TEST("OwnedSyncCallBase std::move [inlined response, message is POD]",
               OwningSyncCallWithPODTest)
RUN_NAMED_TEST("OwnedSyncCallBase std::move [response on heap]", OwningSyncCallHeapTest)
RUN_NAMED_TEST("OwnedSyncCallBase when call failed", OwningSyncCallFailureTest)
RUN_NAMED_TEST("Unowned OwnedSyncCallBase std::move", UnownedSyncCallTest)
RUN_NAMED_TEST("ResponseStorage allocation strategy", ResponseStorageAllocationStrategyTest)
END_TEST_CASE(llcpp_types_tests)
