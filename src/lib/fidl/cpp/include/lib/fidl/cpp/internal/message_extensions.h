// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_FIDL_CPP_INCLUDE_LIB_FIDL_CPP_INTERNAL_MESSAGE_EXTENSIONS_H_
#define SRC_LIB_FIDL_CPP_INCLUDE_LIB_FIDL_CPP_INTERNAL_MESSAGE_EXTENSIONS_H_

#include <lib/fidl/cpp/message.h>
#include <lib/fidl/llcpp/message.h>
#include <lib/stdcompat/span.h>

namespace fidl {
namespace internal {

// Given a transactional message, extracts the parts following the transaction
// header, and re-package that as another |fidl::IncomingMessage|. This is
// useful for decoding the request/response payload of a method.
::fidl::IncomingMessage SkipTransactionHeader(::fidl::IncomingMessage message);

// Converts a transactional |IncomingMessage| into the HLCPP equivalent.
// The message must use the Zircon channel transport.
// |handle_storage| is a caller-allocated array for storing handle metadata.
::fidl::HLCPPIncomingMessage ConvertToHLCPPIncomingMessage(
    ::fidl::IncomingMessage message,
    ::cpp20::span<zx_handle_info_t, ZX_CHANNEL_MAX_MSG_HANDLES> handle_storage);

// Converts a non-transactional |IncomingMessage| into the HLCPP equivalent.
// |handle_storage| is a caller-allocated array for storing handle metadata.
::fidl::HLCPPIncomingBody ConvertToHLCPPIncomingBody(
    fidl::IncomingMessage message,
    ::cpp20::span<zx_handle_info_t, ZX_CHANNEL_MAX_MSG_HANDLES> handle_storage);

// Converts an |HLCPPOutgoingMessage| into |OutgoingMessage|.
// The resulting message uses the Zircon channel transport.
//
// |type| is used to validate the message.
// |handles| is a caller-allocated array for storing handles.
// |handle_metadata| is a caller-allocated array for storing handle metadata.
::fidl::OutgoingMessage ConvertFromHLCPPOutgoingMessage(
    const fidl_type_t* type, HLCPPOutgoingMessage&& message, zx_handle_t* handles,
    fidl_channel_handle_metadata_t* handle_metadata);

::fidl::OutgoingMessage ConvertFromHLCPPOutgoingBody(
    const internal::WireFormatVersion& wire_format_version, const fidl_type_t* type,
    HLCPPOutgoingBody&& body, zx_handle_t* handles,
    fidl_channel_handle_metadata_t* handle_metadata);

// Converts an |HLCPPOutgoingMessage| into |OutgoingMessage|, then invoke
// |callback| with it. Returns the return value of the callback.
// The resulting message uses the Zircon channel transport.
//
// |type| is used to validate the message.
template <typename Callable>
auto ConvertFromHLCPPOutgoingMessageThen(const fidl_type_t* type, HLCPPOutgoingMessage&& message,
                                         Callable&& callback) {
  FIDL_INTERNAL_DISABLE_AUTO_VAR_INIT zx_handle_t handles[ZX_CHANNEL_MAX_MSG_HANDLES];
  FIDL_INTERNAL_DISABLE_AUTO_VAR_INIT fidl_channel_handle_metadata_t
      handle_metadata[ZX_CHANNEL_MAX_MSG_HANDLES];
  return callback(
      ConvertFromHLCPPOutgoingMessage(type, std::move(message), handles, handle_metadata));
}

}  // namespace internal
}  // namespace fidl

#endif  // SRC_LIB_FIDL_CPP_INCLUDE_LIB_FIDL_CPP_INTERNAL_MESSAGE_EXTENSIONS_H_
