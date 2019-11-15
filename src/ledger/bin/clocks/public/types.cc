// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/clocks/public/types.h"

#include <iostream>
#include <tuple>

namespace clocks {

bool operator==(const DeviceId& lhs, const DeviceId& rhs) {
  return std::tie(lhs.fingerprint, lhs.epoch) == std::tie(rhs.fingerprint, rhs.epoch);
}

bool operator!=(const DeviceId& lhs, const DeviceId& rhs) { return !(lhs == rhs); }

bool operator<(const DeviceId& lhs, const DeviceId& rhs) {
  return std::tie(lhs.fingerprint, lhs.epoch) < std::tie(rhs.fingerprint, rhs.epoch);
}

std::ostream& operator<<(std::ostream& os, const DeviceId& device_id) {
  return os << "DeviceId{fingerprint: " << device_id.fingerprint << ", epoch: " << device_id.epoch
            << "}";
}

}  // namespace clocks
