// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "macaddr.h"

namespace wlan {
namespace common {

const MacAddr kZeroMac({0x00, 0x00, 0x00, 0x00, 0x00, 0x00});
const MacAddr kBcastMac({0xff, 0xff, 0xff, 0xff, 0xff, 0xff});

}  // namespace common
}  // namespace wlan
