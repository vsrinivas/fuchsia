// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_DRIVERS_BLUETOOTH_LIB_COMMON_UINT128_H_
#define GARNET_DRIVERS_BLUETOOTH_LIB_COMMON_UINT128_H_

#include <array>
#include <cstdint>

namespace btlib {
namespace common {

// Represents a 128-bit (16-octet) unsigned integer. This is commonly used for
// encryption keys and UUID values.
using UInt128 = std::array<uint8_t, 16>;

static_assert(sizeof(UInt128) == 16, "UInt128 must take up exactly 16 bytes");

// Returns a random 128-bit value.
UInt128 RandomUInt128();

}  // namespace common
}  // namespace btlib

#endif  // GARNET_DRIVERS_BLUETOOTH_LIB_COMMON_UINT128_H_
