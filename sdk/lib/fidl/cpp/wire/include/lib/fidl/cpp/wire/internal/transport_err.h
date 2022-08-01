// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_CPP_WIRE_INCLUDE_LIB_FIDL_CPP_WIRE_INTERNAL_TRANSPORT_ERR_H_
#define LIB_FIDL_CPP_WIRE_INCLUDE_LIB_FIDL_CPP_WIRE_INTERNAL_TRANSPORT_ERR_H_

#include <lib/fidl/cpp/transport_err.h>
#include <lib/fidl/cpp/wire/internal/display_error.h>
#include <lib/fidl/cpp/wire/traits.h>
#include <lib/fidl/cpp/wire/wire_types.h>

namespace fidl {

template <>
struct IsFidlType<::fidl::internal::TransportErr> : public std::true_type {};
template <>
struct ContainsHandle<::fidl::internal::TransportErr> : public std::false_type {};

template <bool IsRecursive>
struct internal::WireCodingTraits<::fidl::internal::TransportErr,
                                  ::fidl::internal::WireCodingConstraintEmpty, IsRecursive> {
  static constexpr size_t inline_size = sizeof(int32_t);
  static constexpr bool is_memcpy_compatible = false;

  static void Encode(internal::WireEncoder* encoder, ::fidl::internal::TransportErr* value,
                     ::fidl::internal::WirePosition position,
                     RecursionDepth<IsRecursive> recursion_depth) {
    switch (*value) {
      case ::fidl::internal::TransportErr::kUnknownMethod:
        break;
      default:
        encoder->SetError(::fidl::internal::kCodingErrorUnknownEnumValue);
        return;
    }
    *position.As<::fidl::internal::TransportErr>() = *value;
  }
  static void Decode(internal::WireDecoder* decoder, ::fidl::internal::WirePosition position,
                     RecursionDepth<IsRecursive> recursion_depth) {
    ::fidl::internal::TransportErr value = *position.As<::fidl::internal::TransportErr>();
    switch (value) {
      case ::fidl::internal::TransportErr::kUnknownMethod:
        break;
      default:
        decoder->SetError(::fidl::internal::kCodingErrorUnknownEnumValue);
        return;
    }
  }
};

}  // namespace fidl

#endif  // LIB_FIDL_CPP_WIRE_INCLUDE_LIB_FIDL_CPP_WIRE_INTERNAL_TRANSPORT_ERR_H_
