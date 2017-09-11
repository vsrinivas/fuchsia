// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <string>

#include "apps/bluetooth/lib/common/byte_buffer.h"
#include "apps/bluetooth/lib/common/device_address.h"
#include "apps/bluetooth/lib/gap/gap.h"
#include "lib/fxl/macros.h"

namespace bluetooth {
namespace gap {

// Represents a remote Bluetooth device that is known to the current system due to discovery and/or
// connection and bonding procedures. These devices can be LE-only, Classic-only, or dual-mode.
//
// Instances should not be created directly and must be obtained via a RemoteDeviceCache.
class RemoteDevice final {
 public:
  // 128-bit UUID that uniquely identifies this device on this system.
  const std::string& identifier() const { return identifier_; }

  // The Bluetooth technologies that are supported by this device.
  TechnologyType technology() const { return technology_; }

  // The known device address of this device.
  // TODO(armansito):
  //   - For paired devices this should return the identity address.
  //   - For temporary devices this is the address that was seen in the advertisement.
  //   - For classic devices this the BD_ADDR.
  const common::DeviceAddress& address() const { return address_; }

  // Returns true if this is a connectable device.
  bool connectable() const { return connectable_; }

  // Returns the advertising data for this device (including any scan response data).
  const common::BufferView advertising_data() const {
    return advertising_data_buffer_.view(0, advertising_data_length_);
  }

  // Returns the most recently observed RSSI for this remote device. Returns hci::kRSSIInvalid if
  // the value is unknown.
  int8_t rssi() const { return rssi_; }

  // Returns a string representation of this device.
  std::string ToString() const;

 private:
  friend class RemoteDeviceCache;

  // TODO(armansito): Add a constructor for classic devices.
  // TODO(armansito): Add constructor from persistent storage format.

  RemoteDevice(const std::string& identifier, const common::DeviceAddress& device_address);

  // Called by RemoteDeviceCache to update the contents of Low Energy device.
  void SetLowEnergyData(bool connectable, int8_t rssi, const common::ByteBuffer& advertising_data);

  std::string identifier_;
  TechnologyType technology_;
  common::DeviceAddress address_;
  bool connectable_;
  int8_t rssi_;

  size_t advertising_data_length_;
  common::DynamicByteBuffer advertising_data_buffer_;

  FXL_DISALLOW_COPY_AND_ASSIGN(RemoteDevice);
};

}  // namespace gap
}  // namespace bluetooth
