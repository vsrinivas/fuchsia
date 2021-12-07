// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/llcpp/types/test/cpp/fidl_v2.h>

#include <iostream>
#include <string>
#include <utility>
#include <vector>

#include <zxtest/zxtest.h>

TEST(NaturalResponse, DecodeMessage) {
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
  auto message = fidl::IncomingMessage::Create<fidl::internal::ChannelTransport>(
      bytes.data(), static_cast<uint32_t>(bytes.size()), nullptr, nullptr, 0);

  // Perform decoding.
  fitx::result result =
      fidl::Response<fidl_llcpp_types_test::Baz::Foo>::DecodeTransactional(std::move(message));
  ASSERT_TRUE(result.is_ok(), "Error decoding: %s",
              result.error_value().FormatDescription().c_str());
  fidl::Response<fidl_llcpp_types_test::Baz::Foo>& response = result.value();

  // Check decoded value.
  EXPECT_EQ(42, response->res().bar());
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
  auto message = fidl::IncomingMessage::Create<fidl::internal::ChannelTransport>(
      bytes.data(), static_cast<uint32_t>(bytes.size()), nullptr, nullptr, 0,
      fidl::IncomingMessage::kSkipMessageHeaderValidation);

  // Create a fake V2 |WireFormatMetadata|.
  fidl_message_header_t header;
  fidl_init_txn_header(&header, 0, 0);
  header.flags[0] = FIDL_MESSAGE_HEADER_FLAGS_0_USE_VERSION_V2;
  auto metadata = fidl::internal::WireFormatMetadata::FromTransactionalHeader(header);

  // Perform decoding.
  fitx::result result =
      fidl_llcpp_types_test::BazFooTopResponse::DecodeFrom(std::move(message), metadata);
  ASSERT_TRUE(result.is_ok(), "Error decoding: %s",
              result.error_value().FormatDescription().c_str());
  fidl_llcpp_types_test::BazFooTopResponse& response = result.value();

  // Check decoded value.
  EXPECT_EQ(42, response.res().bar());
}
