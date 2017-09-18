// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "device_interface.h"

#include <cstring>

namespace wlan {

uint64_t DeviceAddress::to_u64() const {
    uint64_t m = addr_[0];
    for (size_t i = 1; i < DeviceAddress::kSize; i++) {
        m <<= 8;
        m |= addr_[i];
    }
    return m;
}

}  // namespace wlan
