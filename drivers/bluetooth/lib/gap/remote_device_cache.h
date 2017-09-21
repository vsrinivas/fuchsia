// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <unordered_map>

#include "garnet/drivers/bluetooth/lib/common/device_address.h"
#include "garnet/drivers/bluetooth/lib/gap/gap.h"
#include "garnet/drivers/bluetooth/lib/hci/connection.h"
#include "lib/fxl/macros.h"

namespace bluetooth {

namespace common {
class ByteBuffer;
}  // namespace common

namespace hci {
struct LowEnergyScanResult;
}  // namespace hci

namespace gap {

class RemoteDevice;

// A RemoteDeviceCache provides access to remote Bluetooth devices that are known to the system.
// TODO(armansito): The current implementation is very simple but it will grow to support more
// complex features such as LE private address resolution.
class RemoteDeviceCache final {
 public:
  // TODO(armansito): Add an Observer interface to notify higher layers of device property changes
  // (e.g. temporary, connected, etc).

  RemoteDeviceCache() = default;

  // Creates or updates a Remote Device based on LE scan results. Creates a new RemoteDevice entry
  // if the device does not exist.
  RemoteDevice* StoreLowEnergyScanResult(const hci::LowEnergyScanResult& scan_result,
                                         const common::ByteBuffer& advertising_data);

  // Creates or updates a RemoteDevice entry based on a new connection.
  RemoteDevice* StoreLowEnergyConnection(const common::DeviceAddress& peer_address,
                                         hci::Connection::LinkType ll_type,
                                         const hci::Connection::LowEnergyParameters& le_params);

  // Creates a new device entry using the given parameters.
  RemoteDevice* NewDevice(const common::DeviceAddress& address, TechnologyType technology,
                          bool connectable, bool temporary);

  // Returns the remote device with identifier |identifier|. Returns nullptr if |identifier| is not
  // recognized.
  RemoteDevice* FindDeviceById(const std::string& identifier) const;

  // Finds and returns a RemoteDevice with address |address| if it exists, returns nullptr
  // otherwise.
  // TODO(armansito): This should perform address resolution for devices using LE privacy.
  RemoteDevice* FindDeviceByAddress(const common::DeviceAddress& address) const;

 private:
  // Maps unique device IDs to the corresponding RemoteDevice entry.
  using RemoteDeviceMap = std::unordered_map<std::string, std::unique_ptr<RemoteDevice>>;

  // TODO(armansito): Periodically clear temporary devices.

  // Stores all known remote devices.
  RemoteDeviceMap devices_;

  // Mapping from device addresses to unique device identifiers for all known devices. This is used
  // to look-up and update existing cached data for a particular scan result so as to avoid creating
  // duplicate entries for the same device.
  //
  // TODO(armansito): Replace this with an implementation that can resolve device identity, to
  // handle bonded LE devices that use privacy.
  std::unordered_map<common::DeviceAddress, std::string> address_map_;

  FXL_DISALLOW_COPY_AND_ASSIGN(RemoteDeviceCache);
};

}  // namespace gap
}  // namespace bluetooth
