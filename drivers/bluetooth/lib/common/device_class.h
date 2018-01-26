// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <array>
#include <initializer_list>
#include <string>

namespace btlib {
namespace common {

// Represents a 24-bit "Class of Device/Service" field.
// This data structure can be directly serialized into HCI command payloads.
// See the Bluetooth SIG Assigned Numbers for the Baseband
// (https://www.bluetooth.com/specifications/assigned-numbers/baseband)
// for the format.
class DeviceClass {
 public:
  // Initializes the device to an uncategorized device with no services.
  DeviceClass();

  // Initializes the contents from |bytes|.
  explicit DeviceClass(std::initializer_list<uint8_t> bytes);

  enum class MajorClass : uint8_t {
    kMiscellaneous = 0x00,
    kComputer = 0x01,
    kPhone = 0x02,
    kLAN = 0x03,
    kAudioVideo = 0x04,
    kPeripheral = 0x05,
    kImaging = 0x06,
    kWearable = 0x07,
    kToy = 0x08,
    kHealth = 0x09,
    kUnspecified = 0x1F,
  };

  MajorClass major_class() const { return MajorClass(bytes_[1] & 0x1F); }

  // Returns a string describing the device, like "Computer" or "Headphones"
  std::string ToString() const;

  // TODO(jamuraa): add MinorClass and Service classes
 private:
  std::array<uint8_t, 3> bytes_;
};

static_assert(sizeof(DeviceClass) == 3,
              "DeviceClass must take up exactly 3 bytes");

}  // namespace common
}  // namespace btlib

namespace std {
// Stream operator for easy logging
ostream& operator<<(ostream& os, const ::btlib::common::DeviceClass& d);
}  // namespace std
