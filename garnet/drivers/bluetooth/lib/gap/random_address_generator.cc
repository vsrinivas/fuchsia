// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "random_address_generator.h"

#include <zircon/assert.h>
#include <zircon/syscalls.h>

#include "garnet/drivers/bluetooth/lib/sm/util.h"

namespace btlib {
namespace gap {

RandomAddressGenerator::RandomAddressGenerator() {
  static_ = sm::util::GenerateRandomAddress(true);
}

common::DeviceAddress RandomAddressGenerator::StaticAddress() const {
  return static_;
}

common::DeviceAddress RandomAddressGenerator::PrivateAddress() {
  return sm::util::GenerateRandomAddress(false);
}

}  // namespace gap
}  // namespace btlib
