// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_FIDL_C_WALKER_TESTS_CONFORMANCE_TEST_UTILS_H_
#define SRC_LIB_FIDL_C_WALKER_TESTS_CONFORMANCE_TEST_UTILS_H_

#include <lib/fidl/coding.h>
#include <lib/fidl/internal.h>
#include <lib/fidl/llcpp/message.h>
#include <lib/fit/function.h>
#include <zircon/fidl.h>
#include <zircon/status.h>
#include <zircon/types.h>

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <vector>

#ifdef __Fuchsia__
#include <lib/fidl/llcpp/internal/transport_channel.h>
#else
#include <lib/fidl/llcpp/internal/transport_channel_host.h>

#include "lib/fidl/llcpp/traits.h"
// The current build rules for zircon/system/ulib/zircon don't allow linking
// zx_status_get_string on host. Consider changing in the future.
#define zx_status_get_string(status) ((status))
#endif

namespace c_conformance_utils {

inline bool operator==(zx_handle_disposition_t a, zx_handle_disposition_t b) {
  return a.operation == b.operation && a.handle == b.handle && a.type == b.type &&
         a.rights == b.rights && a.result == b.result;
}
inline bool operator!=(zx_handle_disposition_t a, zx_handle_disposition_t b) { return !(a == b); }
inline std::ostream& operator<<(std::ostream& os, const zx_handle_disposition_t& hd) {
  return os << "zx_handle_disposition_t{\n"
            << "  .operation = " << hd.operation << "\n"
            << "  .handle = " << hd.handle << "\n"
            << "  .type = " << hd.type << "\n"
            << "  .rights = " << hd.rights << "\n"
            << "  .result = " << hd.result << "\n"
            << "}\n";
}

template <typename T>
bool ComparePayload(const T* actual, size_t actual_size, const T* expected, size_t expected_size) {
  bool pass = true;
  for (size_t i = 0; i < actual_size && i < expected_size; i++) {
    if (actual[i] != expected[i]) {
      pass = false;
      if constexpr (std::is_same_v<T, zx_handle_disposition_t>) {
        std::cout << std::dec << "element[" << i << "]: actual=" << actual[i]
                  << " expected=" << expected[i];
      } else {
        std::cout << std::dec << "element[" << i << "]: " << std::hex << "actual=0x" << +actual[i]
                  << " "
                  << "expected=0x" << +expected[i] << "\n";
      }
    }
  }
  if (actual_size != expected_size) {
    pass = false;
    std::cout << std::dec << "element[...]: "
              << "actual.size=" << +actual_size << " "
              << "expected.size=" << +expected_size << "\n";
  }
  return pass;
}

template <typename FidlType>
bool EncodeSuccess(FidlWireFormatVersion wire_format_version, FidlType* value,
                   const std::vector<uint8_t>& expected_bytes,
                   const std::vector<zx_handle_disposition_t>& expected_handle_dispositions,
                   bool check_handle_rights) {
  static_assert(!fidl::IsFidlTransactionalMessage<FidlType>::value,
                "EncodeSuccess assumes non-transactional messages");

  // Linearize the built objects using LLCPP encode -> decode.
  fidl::unstable::OwnedEncodedMessage<FidlType> llcpp_encoded(
      fidl::internal::WireFormatVersion::kV1, value);
  auto& outgoing_msg = llcpp_encoded.GetOutgoingMessage();
  auto copied_bytes = outgoing_msg.CopyBytes();
  fidl::unstable::DecodedMessage<FidlType> llcpp_decoded(
      copied_bytes.data(), static_cast<uint32_t>(copied_bytes.size()), outgoing_msg.handles(),
      outgoing_msg.template handle_metadata<fidl::internal::ChannelTransport>(),
      outgoing_msg.handle_actual());

  if (llcpp_decoded.status() != ZX_OK) {
    std::cout << "Decoding target success value failed ("
              << zx_status_get_string(llcpp_decoded.status())
              << "): " << llcpp_decoded.FormatDescription().c_str() << std::endl;
    return false;
  }

  // Handles are now owned by |llcpp_decoded|.
  outgoing_msg.ReleaseHandles();

  zx_handle_disposition_t handle_dispositions[ZX_CHANNEL_MAX_MSG_HANDLES];
  uint32_t actual_handles;
  const char* error_msg = nullptr;
  zx_status_t status =
      fidl_encode_etc(fidl::TypeTraits<FidlType>::kType, llcpp_decoded.PrimaryObject(),
                      static_cast<uint32_t>(copied_bytes.size()), handle_dispositions,
                      std::size(handle_dispositions), &actual_handles, &error_msg);
  // The decoded message is consumed by |fidl_encode_etc|, and handles are moved
  // to |handle_dispositions|.
  llcpp_decoded.ReleasePrimaryObject();
  if (status != ZX_OK) {
    std::cout << "Encoding failed (" << zx_status_get_string(status) << "): " << error_msg
              << std::endl;
    return false;
  }
  if (error_msg != nullptr) {
    std::cout << "error message unexpectedly non-null when status is ZX_OK: " << error_msg
              << std::endl;
    return false;
  }

  bool bytes_match = ComparePayload(copied_bytes.data(), copied_bytes.size(), expected_bytes.data(),
                                    expected_bytes.size());
  bool handles_match = false;
  if (check_handle_rights) {
    handles_match =
        ComparePayload(handle_dispositions, actual_handles, expected_handle_dispositions.data(),
                       expected_handle_dispositions.size());
  } else {
    std::vector<zx_handle_t> handles;
    std::vector<zx_handle_t> expected_handles;
    for (size_t i = 0; i < actual_handles; i++) {
      handles.push_back(handle_dispositions[i].handle);
    }
    expected_handles.reserve(expected_handle_dispositions.size());
    for (const auto& handle_disposition : expected_handle_dispositions) {
      expected_handles.push_back(handle_disposition.handle);
    }
    handles_match = ComparePayload(handles.data(), handles.size(), expected_handles.data(),
                                   expected_handles.size());
  }

  FidlHandleDispositionCloseMany(handle_dispositions, actual_handles);
  return bytes_match && handles_match;
}

// Verifies that |bytes| and |handles| successfully decodes.
// EqualityCheck takes a raw pointer to the input in its FIDL decoded form,
// and checks deep equality and compares handles based on koid, type and rights.
inline bool DecodeSuccess(FidlWireFormatVersion wire_format_version, const fidl_type* type,
                          std::vector<uint8_t> bytes, std::vector<zx_handle_info_t> handles,
                          fit::callback<bool(void* actual)> equality_check) {
  const char* error_msg = nullptr;
  zx_status_t status;
  switch (wire_format_version) {
    case FIDL_WIRE_FORMAT_VERSION_V1: {
      status = fidl_decode_etc(type, bytes.data(), static_cast<uint32_t>(bytes.size()),
                               handles.data(), static_cast<uint32_t>(handles.size()), &error_msg);
      break;
    }
    case FIDL_WIRE_FORMAT_VERSION_V2: {
      status = internal_fidl_decode_etc__v2__may_break(
          type, bytes.data(), static_cast<uint32_t>(bytes.size()), handles.data(),
          static_cast<uint32_t>(handles.size()), &error_msg);
      break;
    }
    default:
      ZX_PANIC("unknown wire format");
  }
  if (status != ZX_OK) {
    std::cout << "Decoding failed (" << zx_status_get_string(status) << "): " << error_msg
              << std::endl;
    return false;
  }
  if (error_msg != nullptr) {
    std::cout << "error message unexpectedly non-null when status is ZX_OK: " << error_msg
              << std::endl;
    return false;
  }

  if (!equality_check(static_cast<void*>(bytes.data()))) {
    std::cout << "decode results does not equal to expected" << std::endl;
    return false;
  }

  return true;
}

// Verifies that |bytes| and |handles| successfully validates.
inline bool ValidateSuccess(FidlWireFormatVersion wire_format_version, const fidl_type* type,
                            std::vector<uint8_t> bytes,
                            const std::vector<zx_handle_info_t>& handles) {
  const char* error_msg = nullptr;
  zx_status_t status;
  switch (wire_format_version) {
    case FIDL_WIRE_FORMAT_VERSION_V1: {
      status = internal__fidl_validate__v1__may_break(
          type, bytes.data(), static_cast<uint32_t>(bytes.size()),
          static_cast<uint32_t>(handles.size()), &error_msg);
      break;
    }
    case FIDL_WIRE_FORMAT_VERSION_V2: {
      status = internal__fidl_validate__v2__may_break(
          type, bytes.data(), static_cast<uint32_t>(bytes.size()),
          static_cast<uint32_t>(handles.size()), &error_msg);
      break;
    }
    default:
      ZX_PANIC("unknown wire format");
  }
  if (status != ZX_OK) {
    std::cout << "Validating failed (" << zx_status_get_string(status) << "): " << error_msg
              << std::endl;
    return false;
  }
  if (error_msg != nullptr) {
    std::cout << "error message unexpectedly non-null when status is ZX_OK: " << error_msg
              << std::endl;
    return false;
  }
  return true;
}

// Verifies that |bytes| and |handles| fails to decode.
inline bool DecodeFailure(FidlWireFormatVersion wire_format_version, const fidl_type* type,
                          std::vector<uint8_t> bytes, std::vector<zx_handle_info_t> handles,
                          zx_status_t expected_error_code) {
  const char* error_msg = nullptr;
  zx_status_t status;
  switch (wire_format_version) {
    case FIDL_WIRE_FORMAT_VERSION_V1: {
      status = fidl_decode_etc(type, bytes.data(), static_cast<uint32_t>(bytes.size()),
                               handles.data(), static_cast<uint32_t>(handles.size()), &error_msg);
      break;
    }
    case FIDL_WIRE_FORMAT_VERSION_V2: {
      status = internal_fidl_decode_etc__v2__may_break(
          type, bytes.data(), static_cast<uint32_t>(bytes.size()), handles.data(),
          static_cast<uint32_t>(handles.size()), &error_msg);
      break;
    }
    default:
      ZX_PANIC("unknown wire format");
  }
  if (status == ZX_OK) {
    std::cout << "Decoding unexpectedly succeeded" << std::endl;
    return false;
  }
  if (status != expected_error_code) {
    std::cout << "Decoding failed with error code " << zx_status_get_string(status) << " ("
              << error_msg << "), but expected error code "
              << zx_status_get_string(expected_error_code) << std::endl;
    return false;
  }
  return true;
}

// Verifies that |bytes| and |handles| fails to validate.
inline bool ValidateFailure(FidlWireFormatVersion wire_format_version, const fidl_type* type,
                            std::vector<uint8_t> bytes,
                            const std::vector<zx_handle_info_t>& handles,
                            zx_status_t expected_error_code) {
  const char* error_msg = nullptr;
  zx_status_t status;
  switch (wire_format_version) {
    case FIDL_WIRE_FORMAT_VERSION_V1: {
      status = internal__fidl_validate__v1__may_break(
          type, bytes.data(), static_cast<uint32_t>(bytes.size()),
          static_cast<uint32_t>(handles.size()), &error_msg);
      break;
    }
    case FIDL_WIRE_FORMAT_VERSION_V2: {
      status = internal__fidl_validate__v2__may_break(
          type, bytes.data(), static_cast<uint32_t>(bytes.size()),
          static_cast<uint32_t>(handles.size()), &error_msg);
      break;
    }
    default:
      ZX_PANIC("unknown wire format");
  }
  if (status == ZX_OK) {
    std::cout << "Validating unexpectedly succeeded" << std::endl;
    return false;
  }
  if (status != expected_error_code) {
    std::cout << "Validating failed with error code " << zx_status_get_string(status) << " ("
              << error_msg << "), but expected error code "
              << zx_status_get_string(expected_error_code) << std::endl;
    return false;
  }
  return true;
}

}  // namespace c_conformance_utils

#endif  // SRC_LIB_FIDL_C_WALKER_TESTS_CONFORMANCE_TEST_UTILS_H_
