// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_DRIVER_INCLUDE_LIB_FIDL_DRIVER_CPP_NATURAL_TYPES_H_
#define LIB_FIDL_DRIVER_INCLUDE_LIB_FIDL_DRIVER_CPP_NATURAL_TYPES_H_

#include <lib/fdf/cpp/channel.h>
#include <lib/fidl/cpp/natural_coding_traits.h>
#include <lib/fidl/cpp/natural_types.h>
#include <lib/fidl_driver/cpp/transport.h>

namespace fidl::internal {

template <typename Constraint>
struct NaturalCodingTraits<::fdf::Channel, Constraint> {
  static constexpr bool is_memcpy_compatible = false;

  static void Encode(NaturalEncoder* encoder, ::fdf::Channel* value, size_t offset,
                     size_t recursion_depth) {
    encoder->EncodeHandle(value->release(), {}, offset, Constraint::is_optional);
  }

  static void Decode(NaturalDecoder* decoder, ::fdf::Channel* value, size_t offset,
                     size_t recursion_depth) {
    zx_handle_t handle = ZX_HANDLE_INVALID;
    decoder->DecodeHandle(&handle, {}, offset, Constraint::is_optional);
    value->reset(handle);
  }
};

template <typename T, typename Constraint>
struct NaturalCodingTraits<::fdf::ServerEnd<T>, Constraint> {
  static constexpr bool is_memcpy_compatible = false;

  static void Encode(NaturalEncoder* encoder, ::fdf::ServerEnd<T>* value, size_t offset,
                     size_t recursion_depth) {
    encoder->EncodeHandle(value->TakeHandle().release(), {}, offset, Constraint::is_optional);
  }

  static void Decode(NaturalDecoder* decoder, ::fdf::ServerEnd<T>* value, size_t offset,
                     size_t recursion_depth) {
    zx_handle_t handle = ZX_HANDLE_INVALID;
    decoder->DecodeHandle(&handle, {}, offset, Constraint::is_optional);
    value->reset(handle);
  }
};

template <typename T, typename Constraint>
struct NaturalCodingTraits<::fdf::ClientEnd<T>, Constraint> {
  static constexpr bool is_memcpy_compatible = false;

  static void Encode(NaturalEncoder* encoder, ::fdf::ClientEnd<T>* value, size_t offset,
                     size_t recursion_depth) {
    encoder->EncodeHandle(value->TakeHandle().release(), {}, offset, Constraint::is_optional);
  }

  static void Decode(NaturalDecoder* decoder, ::fdf::ClientEnd<T>* value, size_t offset,
                     size_t recursion_depth) {
    zx_handle_t handle = ZX_HANDLE_INVALID;
    decoder->DecodeHandle(&handle, {}, offset, Constraint::is_optional);
    value->reset(handle);
  }
};

}  // namespace fidl::internal

#endif  // LIB_FIDL_DRIVER_INCLUDE_LIB_FIDL_DRIVER_CPP_NATURAL_TYPES_H_
