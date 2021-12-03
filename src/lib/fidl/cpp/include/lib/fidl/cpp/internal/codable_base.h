// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_FIDL_CPP_INCLUDE_LIB_FIDL_CPP_INTERNAL_CODABLE_BASE_H_
#define SRC_LIB_FIDL_CPP_INCLUDE_LIB_FIDL_CPP_INTERNAL_CODABLE_BASE_H_

#include <lib/fidl/cpp/decoder.h>
#include <lib/fidl/cpp/internal/message_extensions.h>
#include <lib/fidl/cpp/message.h>
#include <lib/fidl/cpp/natural_types.h>
#include <lib/fidl/llcpp/message.h>
#include <lib/fitx/result.h>

#include <cstdint>

namespace fidl {
namespace internal {

// |CodableBase| is a mixin that conveniently adds encoding/decoding support to
// a subclass. Only structs, tables, and unions should inherit from it.
//
// |FidlType| must contain an accessible constructor with signature:
//
//     FidlType::FidlType(::fidl::Decoder& decoder);
//
// which "inflates" the natural domain object from a |decoder|, referencing a
// message in decoded form. Handles in the message referenced by |decoder| are
// always consumed.
template <typename FidlType>
class CodableBase {
 public:
  // |DecodeFrom| decodes a non-transactional incoming message to a natural
  // domain object |FidlType|. Supported types are structs, tables, and unions.
  //
  // |message| is always consumed.
  // |metadata| informs the wire format of the encoded message.
  static ::fitx::result<::fidl::Error, FidlType> DecodeFrom(
      ::fidl::IncomingMessage&& message, ::fidl::internal::WireFormatMetadata metadata) {
    static_assert(::fidl::IsFidlType<FidlType>::value, "Only FIDL types are supported");
    const fidl_type_t* coding_table = TypeTraits<FidlType>::kCodingTable;
    FIDL_INTERNAL_DISABLE_AUTO_VAR_INIT zx_handle_info_t handles[ZX_CHANNEL_MAX_MSG_HANDLES];
    ::fidl::HLCPPIncomingMessage hlcpp_msg =
        ConvertToHLCPPIncomingMessage(std::move(message), handles);
    const char* error_msg = nullptr;
    zx_status_t status = hlcpp_msg.Decode(metadata, coding_table, &error_msg);
    if (status != ZX_OK) {
      return ::fitx::error(::fidl::Result::DecodeError(status, error_msg));
    }
    ::fidl::Decoder decoder{std::move(hlcpp_msg)};
    return ::fitx::ok(FidlType(decoder));
  }
};

}  // namespace internal
}  // namespace fidl

#endif  // SRC_LIB_FIDL_CPP_INCLUDE_LIB_FIDL_CPP_INTERNAL_CODABLE_BASE_H_
