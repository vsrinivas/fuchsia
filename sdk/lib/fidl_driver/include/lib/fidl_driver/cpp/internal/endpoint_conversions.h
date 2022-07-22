// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_DRIVER_INCLUDE_LIB_FIDL_DRIVER_CPP_INTERNAL_ENDPOINT_CONVERSIONS_H_
#define LIB_FIDL_DRIVER_INCLUDE_LIB_FIDL_DRIVER_CPP_INTERNAL_ENDPOINT_CONVERSIONS_H_

#include <lib/fdf/cpp/channel.h>
#include <lib/fidl/cpp/wire_natural_conversions.h>
#include <lib/fidl_driver/cpp/transport.h>

namespace fidl::internal {

// Specialize for fdf handle types.
template <>
struct NaturalTypeForWireType<fdf::Channel> {
  using type = fdf::Channel;
};
template <>
struct WireTypeForNaturalType<fdf::Channel> {
  using type = fdf::Channel;
};

template <typename Protocol>
struct NaturalTypeForWireType<fdf::ClientEnd<Protocol>> {
  using type = fdf::ClientEnd<Protocol>;
};
template <typename Protocol>
struct WireTypeForNaturalType<fdf::ClientEnd<Protocol>> {
  using type = fdf::ClientEnd<Protocol>;
};

template <typename Protocol>
struct NaturalTypeForWireType<fdf::ServerEnd<Protocol>> {
  using type = fdf::ServerEnd<Protocol>;
};
template <typename Protocol>
struct WireTypeForNaturalType<fdf::ServerEnd<Protocol>> {
  using type = fdf::ServerEnd<Protocol>;
};

template <typename Protocol>
struct NaturalTypeForWireType<fdf::UnownedClientEnd<Protocol>> {
  using type = fdf::UnownedClientEnd<Protocol>;
};
template <typename Protocol>
struct WireTypeForNaturalType<fdf::UnownedClientEnd<Protocol>> {
  using type = fdf::UnownedClientEnd<Protocol>;
};

template <typename Protocol>
struct NaturalTypeForWireType<fdf::UnownedServerEnd<Protocol>> {
  using type = fdf::UnownedServerEnd<Protocol>;
};
template <typename Protocol>
struct WireTypeForNaturalType<fdf::UnownedServerEnd<Protocol>> {
  using type = fdf::UnownedServerEnd<Protocol>;
};

}  // namespace fidl::internal

#endif  // LIB_FIDL_DRIVER_INCLUDE_LIB_FIDL_DRIVER_CPP_INTERNAL_ENDPOINT_CONVERSIONS_H_
