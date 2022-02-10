// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl/llcpp/internal/transport.h>
#include <lib/fidl/llcpp/message.h>
#include <lib/fidl/llcpp/traits.h>

#include <zxtest/zxtest.h>

struct TestHandleMetadata {
  uint32_t metadata;
};

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

struct TestTransport {
  using HandleMetadata = TestHandleMetadata;
  static constexpr const fidl::internal::CodingConfig EncodingConfiguration = {
      .max_iovecs_write = 256,
      .encode_process_handle = encode_process_handle,
      .decode_process_handle = decode_process_handle,
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

static const struct FidlCodedHandle CodingTableHandle = {.tag = kFidlTypeHandle,
                                                         .nullable = kFidlNullability_Nonnullable};
static const struct FidlStructElement CodingTableFields[] = {
    FidlStructElement{
        .field =
            FidlStructField{
                .header = FidlStructElementHeader{.element_type = kFidlStructElementType_Field,
                                                  .is_resource = kFidlIsResource_Resource},
                .offset_v1 = 0u,
                .offset_v2 = 0u,
                .field_type = reinterpret_cast<const fidl_type_t*>(&CodingTableHandle)}},
};
const struct FidlCodedStruct CodingTableStruct = {
    .tag = kFidlTypeStruct,
    .contains_envelope = kFidlContainsEnvelope_DoesNotContainEnvelope,
    .element_count = 1u,
    .size_v1 = 4u,
    .size_v2 = 4u,
    .elements = CodingTableFields,
    .name = "coding/Input"};

struct Input {
  fidl_handle_t h;
};

template <>
struct fidl::TypeTraits<Input> {
  static constexpr const fidl_type_t* kType = &CodingTableStruct;
  static constexpr uint32_t kMaxNumHandles = 1;
  static constexpr uint32_t kPrimarySize = 4;
  static constexpr uint32_t kPrimarySizeV1 = 4;
  static constexpr uint32_t kMaxOutOfLineV1 = 0;
};

template <>
struct fidl::IsFidlType<Input> : public std::true_type {};

template <>
struct fidl::IsFidlObject<Input> : public std::true_type {};

TEST(Coding, EncodedDecode) {
  Input input{.h = 123};
  fidl::unstable::OwnedEncodedMessage<Input, TestTransport> encoded(
      fidl::internal::WireFormatVersion::kV1, &input);
  ASSERT_OK(encoded.status());
  auto& msg = encoded.GetOutgoingMessage();

  ASSERT_EQ(kTestMetadataValue, msg.handle_metadata<TestTransport>()[0].metadata);

  auto copied_bytes = encoded.GetOutgoingMessage().CopyBytes();
  fidl::unstable::DecodedMessage<Input, TestTransport> decoded(
      copied_bytes.data(), static_cast<uint32_t>(copied_bytes.size()), msg.handles(),
      msg.handle_metadata<TestTransport>(), msg.handle_actual());
  ASSERT_OK(decoded.status());

  ASSERT_EQ(123, decoded.PrimaryObject()->h);
}
