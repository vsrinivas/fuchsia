// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "garnet/drivers/bluetooth/lib/common/device_address.h"

namespace bluetooth {

namespace gap {

// Generates Bluetooth addresses as defined in the specification in
// Vol 6, Part B, Sec 1.3.2.
class RandomAddressGenerator {
 public:
  RandomAddressGenerator();

  // Generates a static device address.  See Section 1.3.2.1.
  // Returns the same address for the whole lifetime of this object.
  // We expect the generator to be re-created when the adapter
  // power cycles.
  common::DeviceAddress StaticAddress() const;

  // Generates a non-resolalble private address as specified by Section 1.3.2.2.
  static common::DeviceAddress PrivateAddress();

  // TODO(jamuraa): implement
  // common::DeviceAddress ResolvablePrivateAddress(const UInt128& irk) const;

 private:
  common::DeviceAddress static_;
};
}  // namespace gap

}  // namespace bluetooth
