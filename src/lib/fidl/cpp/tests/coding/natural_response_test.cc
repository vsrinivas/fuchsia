// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/test.types/cpp/fidl.h>

#include <iostream>
#include <string>
#include <utility>
#include <vector>

#include <zxtest/zxtest.h>

#include "test_util.h"

TEST(NaturalResponse, DecodePayloadThenConvertToMessage) {
  // Set up a message.
  // clang-format off
  std::vector<uint8_t> bytes = {
      // Transaction header.
      // Txid, flags, magic
      1, 0, 0, 0, 2, 0, 0, 1,

      // Ordinal. Leaving them zero is fine since they are validated in dispatch
      // logic at upper layers.
      0, 0, 0, 0, 0, 0, 0, 0,

      // Payload, a single uint32_t.
      42, 0, 0, 0, 0, 0, 0, 0,
  };
  // clang-format on
  EXPECT_EQ(bytes.size(), 24U);
  auto message = fidl::IncomingHeaderAndMessage::Create<fidl::internal::ChannelTransport>(
      bytes.data(), static_cast<uint32_t>(bytes.size()), nullptr, nullptr, 0);

  // Perform decoding.
  fit::result result =
      fidl::internal::DecodeTransactionalMessage<test_types::BazFooResponse>(std::move(message));
  ASSERT_TRUE(result.is_ok(), "Error decoding: %s",
              result.error_value().FormatDescription().c_str());
  fidl::Response<test_types::Baz::Foo> response = fidl::internal::NaturalMessageConverter<
      fidl::Response<test_types::Baz::Foo>>::FromDomainObject(std::move(result.value()));

  // Check decoded value.
  EXPECT_EQ(42, response.res().bar());
}

TEST(NaturalResponsePayload, Decode) {
  // Set up a message.
  // clang-format off
  std::vector<uint8_t> bytes = {
      // Payload, a single uint32_t.
      42, 0, 0, 0, 0, 0, 0, 0,
  };
  // clang-format on
  EXPECT_EQ(bytes.size(), 8U);
  auto message = fidl::EncodedMessage::Create<fidl::internal::ChannelTransport>(
      cpp20::span(bytes), nullptr, nullptr, 0);

  // Create a V2 |WireFormatMetadata|.
  auto metadata =
      fidl::internal::WireFormatMetadataForVersion(fidl::internal::WireFormatVersion::kV2);

  // Perform decoding.
  fit::result result = fidl::Decode<test_types::BazFooResponse>(std::move(message), metadata);
  ASSERT_TRUE(result.is_ok(), "Error decoding: %s",
              result.error_value().FormatDescription().c_str());
  test_types::BazFooResponse& response = result.value();

  // Check decoded value.
  EXPECT_EQ(42, response.res().bar());
}

TEST(NaturalResponsePayload, Encode) {
  // Set up an object.
  test_types::BazFooResponse response;
  response.res() = test_types::FooResponse{{.bar = 42}};

  // Perform encoding.
  fidl::OwnedEncodeResult result = fidl::Encode(response);
  ASSERT_TRUE(result.message().ok(), "Error encoding: %s",
              result.message().error().FormatDescription().c_str());

  // Expected message.
  // clang-format off
  std::vector<uint8_t> bytes = {
      // Payload, a single uint32_t.
      42, 0, 0, 0, 0, 0, 0, 0,
  };
  // clang-format on
  EXPECT_EQ(bytes.size(), 8U);

  // Check encoded bytes.
  fidl::OutgoingMessage::CopiedBytes actual = result.message().CopyBytes();
  ASSERT_NO_FAILURES(
      fidl_testing::ComparePayload(cpp20::span(actual.data(), actual.size()), cpp20::span(bytes)));
}

TEST(NaturalResponseWithHandle, Encode) {
  // Expected message.
  // clang-format off
  std::vector<uint8_t> bytes = {
      // Payload, a union with the handle variant selected.
      3, 0, 0, 0, 0, 0, 0, 0,  // tag
      0xff, 0xff, 0xff, 0xff, 0x1, 0x0, 0x1, 0x0  // inlined data, num_handles, flags
  };
  // clang-format on
  EXPECT_EQ(bytes.size(), 16U);

  zx::event event;
  ASSERT_OK(zx::event::create(0, &event));
  zx_handle_t handles[1] = {event.get()};
  fidl_channel_handle_metadata_t handle_metadata[1] = {
      fidl_channel_handle_metadata_t{
          .obj_type = ZX_OBJ_TYPE_NONE,
          .rights = ZX_RIGHT_SAME_RIGHTS,
      },
  };

  // Set up an object.
  test_types::MsgWrapperTestXUnionResponse response{{
      .result = ::test_types::TestXUnion::WithH(std::move(event)),
  }};

  // Perform encoding.
  fidl::OwnedEncodeResult result = fidl::Encode(std::move(response));
  ASSERT_TRUE(result.message().ok(), "Error encoding: %s",
              result.message().error().FormatDescription().c_str());
  // Handles are moved.
  ASSERT_EQ(test_types::TestXUnion::Tag::kH, response.result().Which());
  ASSERT_EQ(zx::handle(), response.result().h().value());

  // Check encoded bytes.
  fidl::OutgoingMessage& message = result.message();
  fidl::OutgoingMessage::CopiedBytes actual = message.CopyBytes();
  ASSERT_NO_FAILURES(
      fidl_testing::ComparePayload(cpp20::span(actual.data(), actual.size()), cpp20::span(bytes)));

  // Check encoded handles.
  ASSERT_EQ(FIDL_TRANSPORT_TYPE_CHANNEL, message.transport_type());
  ASSERT_NO_FAILURES(fidl_testing::ComparePayload<zx_handle_t>(
      cpp20::span(message.handles(), message.handle_actual()), cpp20::span(handles)));
  ASSERT_NO_FAILURES(fidl_testing::ComparePayload<fidl_channel_handle_metadata_t>(
      cpp20::span(message.handle_metadata<fidl::internal::ChannelTransport>(),
                  message.handle_actual()),
      cpp20::span(handle_metadata)));
}
