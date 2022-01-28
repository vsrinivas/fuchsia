// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/test.types/cpp/fidl.h>
#include <lib/zx/event.h>

#include <iostream>
#include <string>
#include <utility>
#include <vector>

#include <zxtest/zxtest.h>

#include "test_util.h"

namespace {

constexpr fidl_message_header_t kV2Header = {
    .flags = {FIDL_MESSAGE_HEADER_FLAGS_0_USE_VERSION_V2, 0, 0},
    .magic_number = kFidlWireFormatMagicNumberInitial,
};

}  // namespace

TEST(NaturalStruct, Decode) {
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

  // Indicate V2 wire format.
  auto wire_format = ::fidl::internal::WireFormatMetadata::FromTransactionalHeader(kV2Header);

  // Perform decoding.
  fitx::result result =
      ::fidl::internal::DecodeFrom<test_types::CopyableStruct>(std::move(message), wire_format);
  ASSERT_TRUE(result.is_ok(), "Error decoding: %s",
              result.error_value().FormatDescription().c_str());
  test_types::CopyableStruct& obj = result.value();

  // Check decoded value.
  EXPECT_EQ(42, obj.x());
}

TEST(NaturalStructWithHandle, Decode) {
  // Set up a message.
  // clang-format off
  std::vector<uint8_t> bytes = {
      // Payload, a single handle.
      0xff, 0xff, 0xff, 0xff, 0, 0, 0, 0,
  };
  // clang-format on
  EXPECT_EQ(bytes.size(), 8U);

  zx::event event;
  ASSERT_OK(zx::event::create(0, &event));
  zx_handle_t handles[1] = {event.get()};
  // Instruct the decoder to not check/downscope rights.
  fidl_channel_handle_metadata_t handle_metadata[1] = {
      fidl_channel_handle_metadata_t{
          .obj_type = ZX_OBJ_TYPE_NONE,
          .rights = ZX_RIGHT_SAME_RIGHTS,
      },
  };
  uint32_t handle_actual = 1;

  auto message = fidl::IncomingMessage::Create<fidl::internal::ChannelTransport>(
      bytes.data(), static_cast<uint32_t>(bytes.size()), handles, handle_metadata, handle_actual,
      fidl::IncomingMessage::kSkipMessageHeaderValidation);

  // Indicate V2 wire format.
  auto wire_format = ::fidl::internal::WireFormatMetadata::FromTransactionalHeader(kV2Header);

  // Perform decoding.
  fitx::result result =
      ::fidl::internal::DecodeFrom<test_types::MoveOnlyStruct>(std::move(message), wire_format);
  ASSERT_TRUE(result.is_ok(), "Error decoding: %s",
              result.error_value().FormatDescription().c_str());
  test_types::MoveOnlyStruct& obj = result.value();

  // Check decoded value.
  EXPECT_EQ(event.get(), obj.h().get());
  // Handle is moved into |obj|.
  (void)event.release();
}

TEST(NaturalStruct, Encode) {
  // Set up an object.
  test_types::CopyableStruct obj;
  obj.x() = 42;

  // Perform encoding.
  fidl::internal::EncodeResult result = fidl::internal::EncodeIntoResult(obj);
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

TEST(NaturalStructWithHandle, Encode) {
  // Expected message.
  // clang-format off
  std::vector<uint8_t> bytes = {
      // Payload, a single handle.
      0xff, 0xff, 0xff, 0xff, 0, 0, 0, 0,
  };
  // clang-format on
  EXPECT_EQ(bytes.size(), 8U);

  zx::event event;
  ASSERT_OK(zx::event::create(0, &event));
  zx_handle_t handles[1] = {event.get()};
  fidl_channel_handle_metadata_t handle_metadata[1] = {
      fidl_channel_handle_metadata_t{
          .obj_type = ZX_OBJ_TYPE_EVENT,
          .rights = ZX_RIGHT_SAME_RIGHTS,
      },
  };

  // Set up an object.
  test_types::HandleStruct obj;
  obj.h() = std::move(event);

  // Perform encoding.
  fidl::internal::EncodeResult result = fidl::internal::EncodeIntoResult(obj);
  ASSERT_TRUE(result.message().ok(), "Error encoding: %s",
              result.message().error().FormatDescription().c_str());
  // Handles are moved.
  ASSERT_EQ(zx::event(), obj.h());

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
