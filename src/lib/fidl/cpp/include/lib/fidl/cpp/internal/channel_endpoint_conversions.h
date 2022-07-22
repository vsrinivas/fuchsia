// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_FIDL_CPP_INCLUDE_LIB_FIDL_CPP_INTERNAL_CHANNEL_ENDPOINT_CONVERSIONS_H_
#define SRC_LIB_FIDL_CPP_INCLUDE_LIB_FIDL_CPP_INTERNAL_CHANNEL_ENDPOINT_CONVERSIONS_H_

#include <lib/fidl/cpp/wire/channel.h>
#include <lib/fidl/cpp/wire_natural_conversions.h>
#include <lib/zx/object.h>

namespace fidl::internal {

// Specialize for Zircon handle types.
template <typename T>
struct NaturalTypeForWireType<T, std::enable_if_t<std::is_base_of_v<zx::object_base, T>>> {
  using type = T;
};
template <typename T>
struct WireTypeForNaturalType<T, std::enable_if_t<std::is_base_of_v<zx::object_base, T>>> {
  using type = T;
};

template <typename Protocol>
struct NaturalTypeForWireType<fidl::ClientEnd<Protocol>> {
  using type = fidl::ClientEnd<Protocol>;
};
template <typename Protocol>
struct WireTypeForNaturalType<fidl::ClientEnd<Protocol>> {
  using type = fidl::ClientEnd<Protocol>;
};

template <typename Protocol>
struct NaturalTypeForWireType<fidl::ServerEnd<Protocol>> {
  using type = fidl::ServerEnd<Protocol>;
};
template <typename Protocol>
struct WireTypeForNaturalType<fidl::ServerEnd<Protocol>> {
  using type = fidl::ServerEnd<Protocol>;
};

template <typename Protocol>
struct NaturalTypeForWireType<fidl::UnownedClientEnd<Protocol>> {
  using type = fidl::UnownedClientEnd<Protocol>;
};
template <typename Protocol>
struct WireTypeForNaturalType<fidl::UnownedClientEnd<Protocol>> {
  using type = fidl::UnownedClientEnd<Protocol>;
};

template <typename Protocol>
struct NaturalTypeForWireType<fidl::UnownedServerEnd<Protocol>> {
  using type = fidl::UnownedServerEnd<Protocol>;
};
template <typename Protocol>
struct WireTypeForNaturalType<fidl::UnownedServerEnd<Protocol>> {
  using type = fidl::UnownedServerEnd<Protocol>;
};

}  // namespace fidl::internal

#endif  // SRC_LIB_FIDL_CPP_INCLUDE_LIB_FIDL_CPP_INTERNAL_CHANNEL_ENDPOINT_CONVERSIONS_H_
