// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_FIDL_CPP_TESTS_CONFORMANCE_CONFORMANCE_UTILS_H_
#define SRC_LIB_FIDL_CPP_TESTS_CONFORMANCE_CONFORMANCE_UTILS_H_

#include <lib/fidl/cpp/natural_types.h>
#include <lib/fidl/internal.h>

#include <zxtest/zxtest.h>

namespace conformance_utils {

inline fidl::internal::WireFormatMetadata CreateWireFormatMetadata(
    fidl::internal::WireFormatVersion wire_format_version) {
  uint8_t flag_byte_0 = (wire_format_version == fidl::internal::WireFormatVersion::kV2)
                            ? FIDL_MESSAGE_HEADER_FLAGS_0_USE_VERSION_V2
                            : 0;
  return fidl::internal::WireFormatMetadata::FromTransactionalHeader({
      .flags = {flag_byte_0, 0, 0},
      .magic_number = kFidlWireFormatMagicNumberInitial,
  });
}

template <typename FidlType>
void EncodeSuccess(fidl::internal::WireFormatVersion wire_format_version, FidlType& obj,
                   const std::vector<uint8_t>& expected_bytes) {
  fidl::internal::EncodeResult result = fidl::internal::EncodeIntoResult(obj);
  ASSERT_TRUE(result.message().ok(), "Error encoding: %s",
              result.message().error().FormatDescription().c_str());

  auto result_bytes = result.message().CopyBytes();
  ASSERT_EQ(expected_bytes.size(), result_bytes.size());
  ASSERT_BYTES_EQ(expected_bytes.data(), result_bytes.data(), expected_bytes.size());
}

template <typename FidlType>
void DecodeSuccess(fidl::internal::WireFormatVersion wire_format_version,
                   std::vector<uint8_t>& bytes, FidlType& expected_obj) {
  auto message = fidl::IncomingMessage::Create<fidl::internal::ChannelTransport>(
      bytes.data(), static_cast<uint32_t>(bytes.size()), nullptr, nullptr, 0,
      fidl::IncomingMessage::kSkipMessageHeaderValidation);
  auto result = fidl::internal::DecodeFrom<FidlType>(std::move(message),
                                                     CreateWireFormatMetadata(wire_format_version));
  ASSERT_TRUE(result.is_ok(), "Error decoding: %s",
              result.error_value().FormatDescription().c_str());
}

template <typename FidlType>
void EncodeFailure(fidl::internal::WireFormatVersion wire_format_version, FidlType& obj) {
  fidl::internal::EncodeResult result = fidl::internal::EncodeIntoResult(obj);
  ASSERT_FALSE(result.message().ok());
}

template <typename FidlType>
void DecodeFailure(fidl::internal::WireFormatVersion wire_format_version,
                   std::vector<uint8_t>& bytes) {
  auto message = fidl::IncomingMessage::Create<fidl::internal::ChannelTransport>(
      bytes.data(), static_cast<uint32_t>(bytes.size()), nullptr, nullptr, 0,
      fidl::IncomingMessage::kSkipMessageHeaderValidation);
  auto result = fidl::internal::DecodeFrom<FidlType>(std::move(message),
                                                     CreateWireFormatMetadata(wire_format_version));
  ASSERT_TRUE(result.is_error());
}

}  // namespace conformance_utils

#endif  // SRC_LIB_FIDL_CPP_TESTS_CONFORMANCE_CONFORMANCE_UTILS_H_
