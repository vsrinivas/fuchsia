// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_DRIVER_INCLUDE_LIB_FIDL_DRIVER_CPP_NATURAL_TYPES_H_
#define LIB_FIDL_DRIVER_INCLUDE_LIB_FIDL_DRIVER_CPP_NATURAL_TYPES_H_

#include <lib/fidl/cpp/coding_traits.h>
#include <lib/fidl/cpp/natural_types.h>
#include <lib/fidl_driver/cpp/transport.h>

namespace fidl {
template <typename T>
struct CodingTraits<::fdf::ServerEnd<T>> {
  template <class EncoderImpl>
  static void Encode(EncoderImpl* encoder, ::fdf::ServerEnd<T>* value, size_t offset,
                     cpp17::optional<HandleInformation> maybe_handle_info = cpp17::nullopt) {
    ZX_PANIC("Not Implemented");
  }

  static void Decode(Decoder* decoder, ::fdf::ServerEnd<T>* value, size_t offset) {
    ZX_PANIC("Not Implemented");
  }
};

template <typename T>
struct CodingTraits<::fdf::ClientEnd<T>> {
  template <class EncoderImpl>
  static void Encode(EncoderImpl* encoder, ::fdf::ClientEnd<T>* value, size_t offset,
                     cpp17::optional<HandleInformation> maybe_handle_info = cpp17::nullopt) {
    ZX_PANIC("Not Implemented");
  }

  static void Decode(Decoder* decoder, ::fdf::ClientEnd<T>* value, size_t offset) {
    ZX_PANIC("Not Implemented");
  }
};

}  // namespace fidl

#endif  // LIB_FIDL_DRIVER_INCLUDE_LIB_FIDL_DRIVER_CPP_NATURAL_TYPES_H_
