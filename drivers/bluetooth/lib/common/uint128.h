// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <array>
#include <cstdint>
#include <initializer_list>

namespace bluetooth {
namespace common {

// Represents a 128-bit (16-octet) unsigned integer. This is commonly used for encryption keys and
// UUID values.
class UInt128 final {
 public:
  // The default constructor initializes contents to zero.
  UInt128();

  // Initializes the contents from |bytes|.
  explicit UInt128(std::initializer_list<uint8_t> bytes);

  // Random access operators.
  constexpr const uint8_t& operator[](size_t index) const { return bytes_[index]; }
  constexpr uint8_t& operator[](size_t index) { return bytes_[index]; }

  // Comparison operators.
  inline bool operator==(const UInt128& rhs) const { return bytes_ == rhs.bytes_; }
  inline bool operator!=(const UInt128& rhs) const { return !(*this == rhs); }

 private:
  // The raw bytes of the 128-bit integer are stored in little-endian byte order.
  std::array<uint8_t, 16> bytes_;
};

static_assert(sizeof(UInt128) == 16, "UInt128 must take up exactly 16 bytes");

}  // namespace common
}  // namespace bluetooth
