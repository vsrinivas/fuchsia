// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <unordered_map>

#include "apps/bluetooth/lib/common/device_address.h"
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
// TODO(armansito): The current implementation of this is very simply but this class will grow to
// support more complex features, such as LE private address resolution.
class RemoteDeviceCache final {
 public:
  RemoteDeviceCache() = default;

  // Stores a new temporary Low Energy scan result in the cache. Returns a pointer to the newly
  // created device.
  RemoteDevice* StoreLowEnergyScanResult(const hci::LowEnergyScanResult& scan_result,
                                         const common::ByteBuffer& advertising_data);

  // Returns the remote device with identifier |identifier|. Returns nullptr if |identifier| is not
  // recognized.
  RemoteDevice* FindDeviceById(const std::string& identifier) const;

 private:
  // Finds and returns a RemoteDevice with address |address|, if it exists.
  // TODO(armansito): This should perform address resolution for devices using LE privacy.
  RemoteDevice* FindDeviceByAddress(const common::DeviceAddress& address) const;

  // Maps unique device IDs to the corresponding RemoteDevice entry.
  using RemoteDeviceMap = std::unordered_map<std::string, std::unique_ptr<RemoteDevice>>;

  // Stores all non-connectable LE scan results. These are never persisted.
  RemoteDeviceMap non_conn_devices_;

  // Stores all connectable temporary scan/inquiry results.
  RemoteDeviceMap tmp_devices_;

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
