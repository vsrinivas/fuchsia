// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "device_address.h"

#include "lib/ftl/logging.h"
#include "lib/ftl/strings/split_string.h"
#include "lib/ftl/strings/string_number_conversions.h"
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

DeviceAddress::DeviceAddress(const std::string& bdaddr_string) {
  // Use FTL_CHECK to prevent this from being compiled out on non-debug builds.
  FTL_CHECK(SetFromString(bdaddr_string));
}

bool DeviceAddress::SetFromString(const std::string& bdaddr_string) {
  // There are 17 characters in XX:XX:XX:XX:XX:XX
  if (bdaddr_string.size() != 17) return false;

  auto split = ftl::SplitString(bdaddr_string, ":", ftl::kKeepWhitespace, ftl::kSplitWantAll);
  if (split.size() != 6) return false;

  size_t index = 5;
  for (const auto& octet_str : split) {
    uint8_t octet;
    if (!ftl::StringToNumberWithError<uint8_t>(octet_str, &octet, ftl::Base::k16)) return false;
    bytes_[index--] = octet;
  }

  return true;
}

std::string DeviceAddress::ToString() const {
  return ftl::StringPrintf("%02X:%02X:%02X:%02X:%02X:%02X", bytes_[5], bytes_[4], bytes_[3],
                           bytes_[2], bytes_[1], bytes_[0]);
}

void DeviceAddress::SetToZero() {
  bytes_.fill(0);
}

}  // namespace common
}  // namespace bluetooth
