// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_DRIVER_INCLUDE_LIB_FIDL_DRIVER_CPP_NATURAL_TYPES_H_
#define LIB_FIDL_DRIVER_INCLUDE_LIB_FIDL_DRIVER_CPP_NATURAL_TYPES_H_

#include <lib/fidl/cpp/natural_coding_traits.h>
#include <lib/fidl/cpp/natural_types.h>
#include <lib/fidl_driver/cpp/transport.h>

namespace fidl::internal {

template <typename T, typename Constraint>
struct NaturalCodingTraits<::fdf::ServerEnd<T>, Constraint> {
  static void Encode(NaturalEncoder* encoder, ::fdf::ServerEnd<T>* value, size_t offset) {
    static_assert(!std::is_same_v<T, T>, "Not implemented");
  }

  static void Decode(NaturalDecoder* decoder, ::fdf::ServerEnd<T>* value, size_t offset) {
    static_assert(!std::is_same_v<T, T>, "Not implemented");
  }
};

template <typename T, typename Constraint>
struct NaturalCodingTraits<::fdf::ClientEnd<T>, Constraint> {
  static void Encode(NaturalEncoder* encoder, ::fdf::ClientEnd<T>* value, size_t offset) {
    static_assert(!std::is_same_v<T, T>, "Not implemented");
  }

  static void Decode(NaturalDecoder* decoder, ::fdf::ClientEnd<T>* value, size_t offset) {
    static_assert(!std::is_same_v<T, T>, "Not implemented");
  }
};

}  // namespace fidl::internal

#endif  // LIB_FIDL_DRIVER_INCLUDE_LIB_FIDL_DRIVER_CPP_NATURAL_TYPES_H_
