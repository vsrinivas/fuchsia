// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "device_class.h"

#include "lib/fxl/logging.h"

namespace btlib {
namespace common {

DeviceClass::DeviceClass()
    : bytes_{0x00, uint8_t(MajorClass::kUnspecified), 0x00} {}

DeviceClass::DeviceClass(std::initializer_list<uint8_t> bytes) {
  FXL_DCHECK(bytes.size() == bytes_.size());
  std::copy(bytes.begin(), bytes.end(), bytes_.begin());
}

std::string DeviceClass::ToString() const {
  switch (major_class()) {
    case MajorClass::kMiscellaneous:
      return "Miscellaneous";
    case MajorClass::kComputer:
      return "Computer";
    case MajorClass::kPhone:
      return "Phone";
    case MajorClass::kLAN:
      return "LAN";
    case MajorClass::kAudioVideo:
      return "A/V";
    case MajorClass::kPeripheral:
      return "Peripheral";
    case MajorClass::kImaging:
      return "Imaging";
    case MajorClass::kWearable:
      return "Wearable";
    case MajorClass::kToy:
      return "Toy";
    case MajorClass::kHealth:
      return "Health Device";
    case MajorClass::kUnspecified:
      return "Unspecified";
  };
}

}  // namespace common
}  // namespace btlib

namespace std {

ostream& operator<<(ostream& os, const ::btlib::common::DeviceClass& d) {
  os << d.ToString();
  return os;
};

}  // namespace std
