// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_FIDL_CPP_INCLUDE_LIB_FIDL_CPP_INTERNAL_TRANSPORT_ERR_H_
#define SRC_LIB_FIDL_CPP_INCLUDE_LIB_FIDL_CPP_INTERNAL_TRANSPORT_ERR_H_

#include <lib/fidl/cpp/transport_err.h>
#include <lib/fidl/cpp/wire/internal/transport_err.h>

#include "lib/fidl/cpp/natural_coding_traits.h"

namespace fidl {
namespace internal {

template <>
struct NaturalCodingTraits<::fidl::internal::TransportErr,
                           ::fidl::internal::NaturalCodingConstraintEmpty> {
  static constexpr size_t inline_size_v2 = sizeof(int32_t);
  static constexpr bool is_memcpy_compatible = false;

  static void Encode(internal::NaturalEncoder* encoder, ::fidl::internal::TransportErr* value,
                     size_t offset, size_t recursion_depth) {
    switch (*value) {
      case ::fidl::internal::TransportErr::kUnknownMethod:
        break;
      default:
        encoder->SetError(::fidl::internal::kCodingErrorUnknownEnumValue);
        return;
    }
    *encoder->template GetPtr<::fidl::internal::TransportErr>(offset) = *value;
  }
  static void Decode(internal::NaturalDecoder* decoder, ::fidl::internal::TransportErr* value,
                     size_t offset, size_t recursion_depth) {
    *value = *decoder->template GetPtr<::fidl::internal::TransportErr>(offset);
    switch (*value) {
      case ::fidl::internal::TransportErr::kUnknownMethod:
        break;
      default:
        decoder->SetError(::fidl::internal::kCodingErrorUnknownEnumValue);
        return;
    }
  }
};

}  // namespace internal
}  // namespace fidl

#endif  // SRC_LIB_FIDL_CPP_INCLUDE_LIB_FIDL_CPP_INTERNAL_TRANSPORT_ERR_H_
