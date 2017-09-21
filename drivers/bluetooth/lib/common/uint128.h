// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <array>
#include <cstdint>

namespace bluetooth {
namespace common {

// Represents a 128-bit (16-octet) unsigned integer. This is commonly used for
// encryption keys and UUID values.
using UInt128 = std::array<uint8_t, 16>;

static_assert(sizeof(UInt128) == 16, "UInt128 must take up exactly 16 bytes");

}  // namespace common
}  // namespace bluetooth
