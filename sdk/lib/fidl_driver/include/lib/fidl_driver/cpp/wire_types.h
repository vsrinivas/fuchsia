// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_DRIVER_INCLUDE_LIB_FIDL_DRIVER_CPP_WIRE_TYPES_H_
#define LIB_FIDL_DRIVER_INCLUDE_LIB_FIDL_DRIVER_CPP_WIRE_TYPES_H_

#include <lib/fidl/cpp/wire/wire_coding_traits.h>
#include <lib/fidl/cpp/wire/wire_types.h>
#include <lib/fidl_driver/cpp/transport.h>

namespace fidl::internal {

template <typename Constraint, bool IsRecursive>
struct WireCodingTraits<::fdf::Channel, Constraint, IsRecursive> {
  static constexpr size_t inline_size = sizeof(fdf_handle_t);
  static constexpr bool is_memcpy_compatible = false;

  static void Encode(WireEncoder* encoder, ::fdf::Channel* value, WirePosition position,
                     RecursionDepth<IsRecursive> recursion_depth) {
    encoder->EncodeHandle(value->release(), {}, position, Constraint::is_optional);
  }

  static void Decode(WireDecoder* decoder, WirePosition position,
                     RecursionDepth<IsRecursive> recursion_depth) {
    decoder->DecodeHandle(position, {}, Constraint::is_optional);
  }
};

template <typename T, typename Constraint, bool IsRecursive>
struct WireCodingTraits<::fdf::ServerEnd<T>, Constraint, IsRecursive> {
  static constexpr size_t inline_size = sizeof(fdf_handle_t);
  static constexpr bool is_memcpy_compatible = false;

  static void Encode(WireEncoder* encoder, ::fdf::ServerEnd<T>* value, WirePosition position,
                     RecursionDepth<IsRecursive> recursion_depth) {
    encoder->EncodeHandle(value->TakeHandle().release(), {}, position, Constraint::is_optional);
  }

  static void Decode(WireDecoder* decoder, WirePosition position,
                     RecursionDepth<IsRecursive> recursion_depth) {
    decoder->DecodeHandle(position, {}, Constraint::is_optional);
  }
};

template <typename T, typename Constraint, bool IsRecursive>
struct WireCodingTraits<::fdf::ClientEnd<T>, Constraint, IsRecursive> {
  static constexpr size_t inline_size = sizeof(fdf_handle_t);
  static constexpr bool is_memcpy_compatible = false;

  static void Encode(WireEncoder* encoder, ::fdf::ClientEnd<T>* value, WirePosition position,
                     RecursionDepth<IsRecursive> recursion_depth) {
    encoder->EncodeHandle(value->TakeHandle().release(), {}, position, Constraint::is_optional);
  }

  static void Decode(WireDecoder* decoder, WirePosition position,
                     RecursionDepth<IsRecursive> recursion_depth) {
    decoder->DecodeHandle(position, {}, Constraint::is_optional);
  }
};

}  // namespace fidl::internal

#endif  // LIB_FIDL_DRIVER_INCLUDE_LIB_FIDL_DRIVER_CPP_WIRE_TYPES_H_
