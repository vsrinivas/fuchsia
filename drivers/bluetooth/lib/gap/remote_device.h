// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <string>

#include "garnet/drivers/bluetooth/lib/common/byte_buffer.h"
#include "garnet/drivers/bluetooth/lib/common/device_address.h"
#include "garnet/drivers/bluetooth/lib/common/optional.h"
#include "garnet/drivers/bluetooth/lib/gap/gap.h"
#include "garnet/drivers/bluetooth/lib/hci/connection.h"
#include "lib/fxl/macros.h"

namespace bluetooth {
namespace gap {

// Represents a remote Bluetooth device that is known to the current system due
// to discovery and/or connection and bonding procedures. These devices can be
// LE-only, Classic-only, or dual-mode.
//
// Instances should not be created directly and must be obtained via a
// RemoteDeviceCache.
class RemoteDevice final {
 public:
  // 128-bit UUID that uniquely identifies this device on this system.
  const std::string& identifier() const { return identifier_; }

  // The Bluetooth technologies that are supported by this device.
  TechnologyType technology() const { return technology_; }

  // The known device address of this device.
  // TODO(armansito):
  //   - For paired devices this should return the identity address.
  //   - For temporary devices this is the address that was seen in the
  //     advertisement.
  //   - For classic devices this the BD_ADDR.
  const common::DeviceAddress& address() const { return address_; }

  // Returns true if this is a connectable device.
  bool connectable() const { return connectable_; }

  // Returns the advertising data for this device (including any scan response
  // data).
  const common::BufferView advertising_data() const {
    return advertising_data_buffer_.view(0, advertising_data_length_);
  }

  // Returns the most recently observed RSSI for this remote device. Returns
  // hci::kRSSIInvalid if the value is unknown.
  int8_t rssi() const { return rssi_; }

  // Updates the advertising and scan response data for this device.
  // |rssi| corresponds to the most recent advertisement RSSI.
  // |advertising_data| should include any scan response data.
  void SetLEAdvertisingData(int8_t rssi,
                            const common::ByteBuffer& advertising_data);

  // Returns the most recently used connection parameters for this device.
  // Returns nullptr if these values are unknown.
  const hci::LEConnectionParameters* le_connection_params() const {
    return le_conn_params_.value();
  }
  void set_le_connection_params(const hci::LEConnectionParameters& params) {
    le_conn_params_ = params;
  }

  // Returns this device's preferred connection parameters, if known. LE
  // peripherals report their preferred connection parameters using one of the
  // GAP Connection Parameter Update procedures (e.g. L2CAP, Advertising, LL).
  const hci::LEPreferredConnectionParameters* le_preferred_connection_params()
      const {
    return le_preferred_conn_params_.value();
  }
  void set_le_preferred_connection_params(
      const hci::LEPreferredConnectionParameters& params) {
    le_preferred_conn_params_ = params;
  }

  // A temporary device is one that is never persisted, such as
  //
  //   1. A device that has never been connected to;
  //   2. A device that was connected but uses a Non-resolvable Private Address.
  //   3. A device that was connected, uses a Resolvable Private Address, but
  //      the local host has no Identity Resolving Key for it.
  //
  // All other devices can be considered bonded.
  bool temporary() const { return temporary_; }

  // Marks this device as non-temporary. This operation may fail due to one of
  // the conditions described above the |temporary()| method.
  //
  // TODO(armansito): Replace this with something more sophisticated when we
  // implement bonding procedures. This method is here to remind us that these
  // conditions are subtle and not fully supported yet.
  bool TryMakeNonTemporary();

  // Returns a string representation of this device.
  std::string ToString() const;

 private:
  friend class RemoteDeviceCache;

  // TODO(armansito): Add constructor from persistent storage format.

  RemoteDevice(const std::string& identifier,
               const common::DeviceAddress& address,
               bool connectable);

  std::string identifier_;
  TechnologyType technology_;
  common::DeviceAddress address_;
  bool connectable_;
  bool temporary_;
  int8_t rssi_;

  // TODO(armansito): Store device name and remote features.
  // TODO(armansito): Store discovered service UUIDs.
  // TODO(armansito): Store an AdvertisingData structure rather than the raw
  // payload.
  size_t advertising_data_length_;
  common::DynamicByteBuffer advertising_data_buffer_;

  // Most recently used LE connection parameters. Has no value if this device
  // has never been connected.
  common::Optional<hci::LEConnectionParameters> le_conn_params_;

  // Preferred LE connection parameters as reported by this device. Has no value
  // if this parameter is unknown.
  // TODO(armansito): Add a method for storing the preferred parameters.
  common::Optional<hci::LEPreferredConnectionParameters>
      le_preferred_conn_params_;

  FXL_DISALLOW_COPY_AND_ASSIGN(RemoteDevice);
};

}  // namespace gap
}  // namespace bluetooth
