// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl/cpp/wire/internal/transport.h>
#include <lib/fidl/cpp/wire/message.h>
#include <lib/fidl/cpp/wire/traits.h>
#include <lib/fidl/cpp/wire/wire_coding_traits.h>

#include <zxtest/zxtest.h>

namespace {

struct TestHandleMetadata {
  uint32_t metadata;
};

constexpr uint32_t kTestHandleValue = 123;
constexpr uint32_t kTestMetadataValue = 456;

zx_status_t encode_process_handle(fidl::internal::HandleAttributes attr, uint32_t metadata_index,
                                  void* out_metadata_array, const char** out_error) {
  static_cast<TestHandleMetadata*>(out_metadata_array)[metadata_index] = {.metadata =
                                                                              kTestMetadataValue};
  return ZX_OK;
}
zx_status_t decode_process_handle(fidl_handle_t* handle, fidl::internal::HandleAttributes attr,
                                  uint32_t metadata_index, const void* metadata_array,
                                  const char** error) {
  ZX_ASSERT(static_cast<const TestHandleMetadata*>(metadata_array)[metadata_index].metadata ==
            kTestMetadataValue);
  return ZX_OK;
}

void close_handle(fidl_handle_t h) { ZX_ASSERT(h == kTestHandleValue); }

void(close_handle_many)(const fidl_handle_t* handles, size_t num_handles) {
  ZX_ASSERT_MSG(num_handles == 1, "expected 1 handle, got %d", static_cast<uint32_t>(num_handles));
  close_handle(*handles);
}

}  // namespace

struct TestTransport {
  using HandleMetadata = TestHandleMetadata;
  static constexpr uint32_t kNumIovecs = 1;
  static constexpr const fidl::internal::CodingConfig EncodingConfiguration = {
      .handle_metadata_stride = sizeof(TestHandleMetadata),
      .encode_process_handle = encode_process_handle,
      .decode_process_handle = decode_process_handle,
      .close = close_handle,
      .close_many = close_handle_many,
  };
  static constexpr const fidl::internal::TransportVTable VTable = {
      .type = FIDL_TRANSPORT_TYPE_TEST,
      .encoding_configuration = &TestTransport::EncodingConfiguration,
  };
};

template <>
struct fidl::internal::AssociatedTransportImpl<TestHandleMetadata> {
  using type = TestTransport;
};

struct Input {
  fidl_handle_t h;
};

template <>
struct fidl::TypeTraits<Input> {
  static constexpr uint32_t kMaxNumHandles = 1;
  static constexpr uint32_t kMaxDepth = 0;
  static constexpr uint32_t kPrimarySize = 4;
  static constexpr uint32_t kMaxOutOfLine = 0;
  static constexpr uint32_t kPrimarySizeV1 = 4;
  static constexpr uint32_t kMaxOutOfLineV1 = 0;
  static constexpr bool kHasEnvelope = false;
  static constexpr bool kHasPointer = false;
};

template <bool IsRecursive>
struct fidl::internal::WireCodingTraits<Input, fidl::internal::WireCodingConstraintEmpty,
                                        IsRecursive> {
  static constexpr size_t inline_size = 4;

  static void Encode(fidl::internal::WireEncoder* encoder, Input* value, WirePosition position,
                     RecursionDepth<IsRecursive> recursion_depth) {
    encoder->EncodeHandle(value->h, {}, position, false);
  }
  static void Decode(fidl::internal::WireDecoder* decoder, WirePosition position,
                     RecursionDepth<IsRecursive> recursion_depth) {
    decoder->DecodeHandle(position, {}, false);
  }
};

template <>
struct fidl::IsFidlType<Input> : public std::true_type {};

template <>
struct fidl::IsFidlObject<Input> : public std::true_type {};

TEST(Coding, EncodedDecode) {
  Input input{.h = kTestHandleValue};
  fidl::unstable::OwnedEncodedMessage<Input, TestTransport> encoded(
      fidl::internal::WireFormatVersion::kV2, &input);
  ASSERT_OK(encoded.status());
  auto& msg = encoded.GetOutgoingMessage();

  ASSERT_EQ(1, msg.handle_actual());
  ASSERT_EQ(kTestMetadataValue, msg.handle_metadata<TestTransport>()[0].metadata);

  auto copied_bytes = encoded.GetOutgoingMessage().CopyBytes();
  fidl::EncodedMessage message = fidl::EncodedMessage::Create<TestTransport>(
      copied_bytes, msg.handles(), msg.handle_metadata<TestTransport>(), msg.handle_actual());
  fit::result decoded = fidl::InplaceDecode<Input>(
      std::move(message),
      fidl::internal::WireFormatMetadataForVersion(fidl::internal::WireFormatVersion::kV2));
  ASSERT_TRUE(decoded.is_ok());
  ASSERT_EQ(kTestHandleValue, decoded->h);
}
