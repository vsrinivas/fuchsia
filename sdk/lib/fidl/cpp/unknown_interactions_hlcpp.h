// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_CPP_UNKNOWN_INTERACTIONS_HLCPP_H_
#define LIB_FIDL_CPP_UNKNOWN_INTERACTIONS_HLCPP_H_

#include <lib/fidl/cpp/coding_traits.h>
#include <lib/fidl/cpp/transport_err.h>
#include <zircon/types.h>

#include "lib/fidl/cpp/internal/unknown_interactions_table.h"

namespace fidl {
using TransportErr = ::fidl::internal::TransportErr;

template <>
struct CodingTraits<::fidl::internal::TransportErr> {
  static constexpr size_t inline_size_v2 = sizeof(::fidl::internal::TransportErr);
  static void Encode(Encoder* encoder, ::fidl::internal::TransportErr* value, size_t offset,
                     cpp17::optional<::fidl::HandleInformation> maybe_handle_info) {
    ZX_DEBUG_ASSERT(!maybe_handle_info);
    int32_t underlying = static_cast<int32_t>(*value);
    ::fidl::Encode(encoder, &underlying, offset);
  }
  static void Decode(Decoder* decoder, ::fidl::internal::TransportErr* value, size_t offset) {
    int32_t underlying = {};
    ::fidl::Decode(decoder, &underlying, offset);
    *value = static_cast<::fidl::internal::TransportErr>(underlying);
  }
};

inline zx_status_t Clone(::fidl::internal::TransportErr value,
                         ::fidl::internal::TransportErr* result) {
  *result = value;
  return ZX_OK;
}

template <>
struct Equality<::fidl::internal::TransportErr> {
  bool operator()(const ::fidl::internal::TransportErr& lhs,
                  const ::fidl::internal::TransportErr& rhs) const {
    return lhs == rhs;
  }
};

namespace internal {
// Encodes a FIDL union for the result union of a flexible method set to the
// transport_err variant with TransportErr::kUnknownMethod value.
::fidl::HLCPPOutgoingMessage EncodeUnknownMethodResponse(::fidl::MessageEncoder* encoder);
}  // namespace internal
}  // namespace fidl

#endif  // LIB_FIDL_CPP_UNKNOWN_INTERACTIONS_HLCPP_H_
