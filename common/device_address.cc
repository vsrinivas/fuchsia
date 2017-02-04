// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "device_address.h"

#include "lib/ftl/logging.h"
#include "lib/ftl/strings/string_printf.h"

namespace bluetooth {
namespace common {

DeviceAddress::DeviceAddress() {
  SetToZero();
}

DeviceAddress::DeviceAddress(std::initializer_list<uint8_t> bytes) {
  FTL_DCHECK(bytes.size() == bytes_.size());
  std::copy(bytes.begin(), bytes.end(), bytes_.begin());
}

std::string DeviceAddress::ToString() const {
  return ftl::StringPrintf("%02X:%02X:%02X:%02X:%02X:%02X", bytes_[5],
                           bytes_[4], bytes_[3], bytes_[2], bytes_[1],
                           bytes_[0]);
}

void DeviceAddress::SetToZero() {
  bytes_.fill(0);
}

}  // namespace common
}  // namespace bluetooth
