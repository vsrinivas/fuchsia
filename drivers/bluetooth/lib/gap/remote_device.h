// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <string>

#include "apps/bluetooth/lib/common/byte_buffer.h"
#include "apps/bluetooth/lib/common/device_address.h"
#include "apps/bluetooth/lib/common/optional.h"
#include "apps/bluetooth/lib/gap/gap.h"
#include "apps/bluetooth/lib/hci/connection.h"
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

  // Returns the most recently used connection parameters for this device. Returns nullptr if these
  // values are unknown.
  const hci::Connection::LowEnergyParameters* le_connection_params() const {
    return le_conn_params_.value();
  }

  // Returns true if this device has not been connected to.
  bool temporary() const { return temporary_; }

  // Returns a string representation of this device.
  std::string ToString() const;

 private:
  friend class RemoteDeviceCache;

  // TODO(armansito): Add a constructor for classic devices.
  // TODO(armansito): Add constructor from persistent storage format.

  RemoteDevice(const std::string& identifier, TechnologyType technology,
               const common::DeviceAddress& address, bool connectable, bool temporary);

  // Called by RemoteDeviceCache to update the contents of a LE device.
  void SetLowEnergyData(int8_t rssi, const common::ByteBuffer& advertising_data);

  // Called by RemoteDeviceCache to update the contents of a LE device after a connection is
  // established.
  void SetLowEnergyConnectionData(const hci::Connection::LowEnergyParameters& params);

  // Called by RemoteDeviceCache
  void set_connectable(bool value) { connectable_ = value; }
  void set_temporary(bool value) { temporary_ = value; }

  std::string identifier_;
  TechnologyType technology_;
  common::DeviceAddress address_;
  bool connectable_;
  bool temporary_;
  int8_t rssi_;

  // TODO(armansito): Store device name and remote features.
  // TODO(armansito): Store discovered service UUIDs.
  // TODO(armansito): Store an AdvertisingData structure rather than the raw payload.
  size_t advertising_data_length_;
  common::DynamicByteBuffer advertising_data_buffer_;

  // Most recently used LE connection parameters. Has no value if this device has never been
  // connected.
  common::Optional<hci::Connection::LowEnergyParameters> le_conn_params_;

  FXL_DISALLOW_COPY_AND_ASSIGN(RemoteDevice);
};

}  // namespace gap
}  // namespace bluetooth
