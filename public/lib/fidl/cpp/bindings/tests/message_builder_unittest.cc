// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gtest/gtest.h"
#include "lib/fidl/cpp/bindings/internal/bindings_serialization.h"
#include "lib/fidl/cpp/bindings/internal/message_builder.h"
#include "lib/fidl/cpp/bindings/internal/message_internal.h"

namespace fidl {
namespace test {
namespace {

TEST(MessageBuilderTest, MessageBuilder) {
  char payload[41];
  MessageBuilder b(123u, sizeof(payload));

  // The payload size should be a multiple of 8.
  EXPECT_EQ(48u, b.message()->payload_num_bytes());

  // payload size + message header size:
  EXPECT_EQ(48u + sizeof(internal::MessageHeader),
            b.message()->data_num_bytes());

  const auto* msg_hdr =
      reinterpret_cast<const internal::MessageHeader*>(b.message()->data());
  EXPECT_EQ(123u, msg_hdr->name);
  EXPECT_EQ(0u, msg_hdr->flags);
  EXPECT_EQ(0u, msg_hdr->version);

  EXPECT_EQ(sizeof(internal::MessageHeader), msg_hdr->num_bytes);
}

TEST(MessageBuilderTest, RequestMessageBuilder) {
  char payload[41];
  RequestMessageBuilder b(123u, sizeof(payload));

  // The payload size should be a multiple of 8.
  EXPECT_EQ(48u, b.message()->payload_num_bytes());
  // Total data size should be payload size + message header size.
  EXPECT_EQ(48u + sizeof(internal::MessageHeaderWithRequestID),
            b.message()->data_num_bytes());

  const auto* msg_hdr =
      reinterpret_cast<const internal::MessageHeaderWithRequestID*>(
          b.message()->data());
  EXPECT_EQ(123u, msg_hdr->name);
  EXPECT_EQ(internal::kMessageExpectsResponse, msg_hdr->flags);
  EXPECT_EQ(1u, msg_hdr->version);
  EXPECT_EQ(0ul, msg_hdr->request_id);
  EXPECT_EQ(sizeof(internal::MessageHeaderWithRequestID), msg_hdr->num_bytes);
}

TEST(MessageBuilderTest, ResponseMessageBuilder) {
  char payload[41];
  ResponseMessageBuilder b(123u, sizeof(payload), 0ull);

  // The payload size should be a multiple of 8.
  EXPECT_EQ(48u, b.message()->payload_num_bytes());
  // Total data size should be payload size + message header size.
  EXPECT_EQ(48u + sizeof(internal::MessageHeaderWithRequestID),
            b.message()->data_num_bytes());

  const auto* msg_hdr =
      reinterpret_cast<const internal::MessageHeaderWithRequestID*>(
          b.message()->data());
  EXPECT_EQ(123u, msg_hdr->name);
  EXPECT_EQ(internal::kMessageIsResponse, msg_hdr->flags);
  EXPECT_EQ(1u, msg_hdr->version);
  EXPECT_EQ(0ul, msg_hdr->request_id);
  EXPECT_EQ(sizeof(internal::MessageHeaderWithRequestID), msg_hdr->num_bytes);
}

}  // namespace
}  // namespace test
}  // namespace fidl
