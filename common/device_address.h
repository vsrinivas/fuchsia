// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <array>
#include <initializer_list>

namespace bluetooth {
namespace common {

// Represents a 48-bit BD_ADDR.
class DeviceAddress {
 public:
  // The default constructor initializes the address to 00:00:00:00:00:00.
  DeviceAddress();

  // Initializes the contents from |bytes|.
  explicit DeviceAddress(std::initializer_list<uint8_t> bytes);

  // Initializes the contents from a string of the form XX:XX:XX:XX:XX:XX where each "XX" is an
  // ASCII encoded two-digit hexadecimal integer.
  explicit DeviceAddress(const std::string& bdaddr_string);

  // Resets the contents from a string of the form XX:XX:XX:XX:XX:XX where each "XX" is an
  // ASCII encoded two-digit hexadecimal integer. Returns false if |bdaddr_string| is badly
  // formatted.
  bool SetFromString(const std::string& bdaddr_string);

  // Returns a string representation of the device address. The bytes in
  // human-readable form will appear in big-endian byte order even though the
  // underlying array stores the bytes in little-endian. The returned string
  // will be of the form:
  //
  //   XX:XX:XX:XX:XX:XX
  std::string ToString() const;

  // Sets all bits of the BD_ADDR to 0.
  void SetToZero();

  // Comparison operators.
  inline bool operator==(const DeviceAddress& other) const { return bytes_ == other.bytes_; }
  inline bool operator!=(const DeviceAddress& other) const { return !(*this == other); }

 private:
  // The raw bytes of the BD_ADDR stored in little-endian byte order.
  std::array<uint8_t, 6> bytes_;
};

static_assert(sizeof(DeviceAddress) == 6, "DeviceAddress must take up exactly 6 bytes");

}  // namespace common
}  // namespace bluetooth
