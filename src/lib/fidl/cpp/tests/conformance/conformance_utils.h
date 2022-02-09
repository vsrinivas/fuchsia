// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_FIDL_CPP_TESTS_CONFORMANCE_CONFORMANCE_UTILS_H_
#define SRC_LIB_FIDL_CPP_TESTS_CONFORMANCE_CONFORMANCE_UTILS_H_

#include <lib/fidl/cpp/natural_types.h>
#include <lib/fidl/internal.h>

#include <zxtest/zxtest.h>

namespace conformance_utils {

inline zx_handle_t HandleReplace(zx_handle_t handle, zx_rights_t rights) {
  zx_handle_t replaced_handle;
  ZX_ASSERT(zx_handle_replace(handle, rights, &replaced_handle) == ZX_OK);
  return replaced_handle;
}

inline zx_handle_t CreateChannel(zx_rights_t rights) {
  zx_handle_t c1, c2;
  ZX_ASSERT(zx_channel_create(0, &c1, &c2) == ZX_OK);
  zx_handle_close(c1);
  return HandleReplace(c2, rights);
}

inline zx_handle_t CreateEvent(zx_rights_t rights) {
  zx_handle_t e;
  ZX_ASSERT(zx_event_create(0, &e) == ZX_OK);
  return HandleReplace(e, rights);
}

template <class Input>
void ForgetHandles(fidl::internal::WireFormatVersion wire_format, Input input) {
  // Encode purely for the side effect of linearizing the handles.
  fidl::internal::EncodeResult result = fidl::internal::EncodeIntoResult(input);
  result.message().ReleaseHandles();
}

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
                   const std::vector<uint8_t>& expected_bytes,
                   const std::vector<zx_handle_disposition_t> expected_handles,
                   bool check_handle_rights) {
  fidl::internal::EncodeResult result = fidl::internal::EncodeIntoResult(obj);
  ASSERT_TRUE(result.message().ok(), "Error encoding: %s",
              result.message().error().FormatDescription().c_str());

  auto result_bytes = result.message().CopyBytes();
  ASSERT_EQ(expected_bytes.size(), result_bytes.size());
  ASSERT_BYTES_EQ(expected_bytes.data(), result_bytes.data(), expected_bytes.size());
  ASSERT_EQ(expected_handles.size(), result.message().handle_actual());
  for (uint32_t i = 0; i < expected_handles.size(); i++) {
    ASSERT_EQ(expected_handles[i].handle, result.message().handles()[i]);
    if (check_handle_rights) {
      ASSERT_EQ(expected_handles[i].type,
                result.message().handle_metadata<fidl::internal::ChannelTransport>()[i].obj_type);
      ASSERT_EQ(expected_handles[i].rights,
                result.message().handle_metadata<fidl::internal::ChannelTransport>()[i].rights);
    }
  }
}

template <typename FidlType, typename EqualityCheck>
void DecodeSuccess(fidl::internal::WireFormatVersion wire_format_version,
                   std::vector<uint8_t>& bytes, const std::vector<zx_handle_info_t> handle_infos,
                   EqualityCheck equality_check) {
  auto handles = std::make_unique<zx_handle_t[]>(handle_infos.size());
  auto handle_metadata = std::make_unique<fidl_channel_handle_metadata_t[]>(handle_infos.size());
  for (uint32_t i = 0; i < handle_infos.size(); i++) {
    handles[i] = handle_infos[i].handle;
    handle_metadata[i] = {
        .obj_type = handle_infos[i].type,
        .rights = handle_infos[i].rights,
    };
  }
  auto message = fidl::IncomingMessage::Create<fidl::internal::ChannelTransport>(
      bytes.data(), static_cast<uint32_t>(bytes.size()), handles.get(), handle_metadata.get(),
      static_cast<uint32_t>(handle_infos.size()),
      fidl::IncomingMessage::kSkipMessageHeaderValidation);
  auto result = fidl::internal::DecodeFrom<FidlType>(std::move(message),
                                                     CreateWireFormatMetadata(wire_format_version));
  ASSERT_TRUE(result.is_ok(), "Error decoding: %s",
              result.error_value().FormatDescription().c_str());

  equality_check(result.value());

  ForgetHandles(wire_format_version, std::move(result.value()));
}

template <typename FidlType>
void EncodeFailure(fidl::internal::WireFormatVersion wire_format_version, FidlType& obj) {
  fidl::internal::EncodeResult result = fidl::internal::EncodeIntoResult(obj);
  ASSERT_FALSE(result.message().ok());
}

template <typename FidlType>
void DecodeFailure(fidl::internal::WireFormatVersion wire_format_version,
                   std::vector<uint8_t>& bytes, const std::vector<zx_handle_info_t> handle_infos) {
  auto handles = std::make_unique<zx_handle_t[]>(handle_infos.size());
  auto handle_metadata = std::make_unique<fidl_channel_handle_metadata_t[]>(handle_infos.size());
  for (uint32_t i = 0; i < handle_infos.size(); i++) {
    handles[i] = handle_infos[i].handle;
    handle_metadata[i] = {
        .obj_type = handle_infos[i].type,
        .rights = handle_infos[i].rights,
    };
  }
  auto message = fidl::IncomingMessage::Create<fidl::internal::ChannelTransport>(
      bytes.data(), static_cast<uint32_t>(bytes.size()), handles.get(), handle_metadata.get(),
      static_cast<uint32_t>(handle_infos.size()),
      fidl::IncomingMessage::kSkipMessageHeaderValidation);
  auto result = fidl::internal::DecodeFrom<FidlType>(std::move(message),
                                                     CreateWireFormatMetadata(wire_format_version));
  ASSERT_TRUE(result.is_error());
}

}  // namespace conformance_utils

#endif  // SRC_LIB_FIDL_CPP_TESTS_CONFORMANCE_CONFORMANCE_UTILS_H_
