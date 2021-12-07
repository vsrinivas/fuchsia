// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_FIDL_CPP_INCLUDE_LIB_FIDL_CPP_INTERNAL_CODABLE_BASE_H_
#define SRC_LIB_FIDL_CPP_INCLUDE_LIB_FIDL_CPP_INTERNAL_CODABLE_BASE_H_

#include <lib/fidl/cpp/decoder.h>
#include <lib/fidl/cpp/encoder.h>
#include <lib/fidl/cpp/internal/message_extensions.h>
#include <lib/fidl/cpp/message.h>
#include <lib/fidl/cpp/natural_types.h>
#include <lib/fidl/llcpp/message.h>
#include <lib/fitx/result.h>

#include <cstdint>

namespace fidl {
namespace internal {

// |EncodeResult| holds an encoded message along with the required storage.
// Success/failure information is stored in |message|.
class EncodeResult {
 public:
  EncodeResult(const fidl_type_t* type, ::fidl::Encoder&& storage)
      : storage_(std::move(storage)),
        message_(ConvertFromHLCPPOutgoingMessage(type, storage_.GetMessage(), handles_,
                                                 handle_metadata_)) {}

  ::fidl::OutgoingMessage& message() { return message_; }

 private:
  zx_handle_t handles_[ZX_CHANNEL_MAX_MSG_HANDLES];
  fidl_channel_handle_metadata_t handle_metadata_[ZX_CHANNEL_MAX_MSG_HANDLES];
  ::fidl::Encoder storage_;
  ::fidl::OutgoingMessage message_;
};

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
//
// |FidlType| must contain an accessible member function with signature:
//
//     void FidlType::EncodeWithoutValidating(
//         ::fidl::Encoder& encoder, size_t offset);
//
// which encodes the current instance into the storage into an empty |encoder|,
// consuming any handles in the process, without performing validation.
template <typename FidlType>
class CodableBase {
 public:
  // Encodes an instance of |FidlType|. Supported types are structs, tables, and
  // unions.
  //
  // Handles in the current instance are moved to the returned |EncodeResult|,
  // if any.
  //
  // Errors during encoding (e.g. constraint validation) are reflected in the
  // |message| of the returned |EncodeResult|.
  //
  // TODO(fxbug.dev/82681): Make this API comply with the requirements in FIDL-at-rest.
  EncodeResult Internal__Encode() {
    static_assert(::fidl::IsFidlType<FidlType>::value, "Only FIDL types are supported");
    const fidl_type_t* coding_table = TypeTraits<FidlType>::kCodingTable;
    // Since a majority of the domain objects are HLCPP objects, for now
    // the wire format version of the encoded message is the same as the one
    // used in HLCPP.
    ::fidl::Encoder encoder(::fidl::Encoder::NO_HEADER, DefaultHLCPPEncoderWireFormat());
    static_cast<FidlType*>(this)->EncodeWithoutValidating(encoder, 0);
    return EncodeResult(coding_table, std::move(encoder));
  }

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
