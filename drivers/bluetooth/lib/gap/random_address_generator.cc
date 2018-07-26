// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "random_address_generator.h"

#include <zircon/syscalls.h>

#include "lib/fxl/logging.h"

namespace btlib {

namespace gap {

namespace {

// Generate a completely random address with the two most significant bits set
// to either one or zero based on |set_or_clear_bits|
common::DeviceAddress GenerateRandomAddress(bool set_or_clear) {
  // Generate the static address
  uint8_t bytes[6];
  zx_cprng_draw(bytes, std::extent<decltype(bytes)>::value);
  // Have at least one zero bit and one bit.
  FXL_DCHECK(bytes[0] | bytes[1] | bytes[2] | bytes[3] | bytes[4] | bytes[5]);
  FXL_DCHECK(~bytes[0] | ~bytes[1] | ~bytes[2] | ~bytes[3] | ~bytes[4] |
             ~bytes[5]);
  // Fill the 2 most significant bits
  if (set_or_clear) {
    bytes[5] |= 0xC0;
  } else {
    bytes[5] &= 0x3F;
  }
  return common::DeviceAddress(
      common::DeviceAddress::Type::kLERandom,
      common::DeviceAddressBytes{bytes[0], bytes[1], bytes[2], bytes[3],
                                 bytes[4], bytes[5]});
}

}  // namespace

RandomAddressGenerator::RandomAddressGenerator() {
  static_ = GenerateRandomAddress(true);
}

common::DeviceAddress RandomAddressGenerator::StaticAddress() const {
  return static_;
}

common::DeviceAddress RandomAddressGenerator::PrivateAddress() {
  return GenerateRandomAddress(false);
}

}  // namespace gap

}  // namespace btlib
