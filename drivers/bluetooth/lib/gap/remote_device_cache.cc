// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "remote_device_cache.h"

#include "garnet/drivers/bluetooth/lib/hci/connection.h"
#include "garnet/drivers/bluetooth/lib/hci/low_energy_scanner.h"
#include "lib/fxl/random/uuid.h"

#include "remote_device.h"

namespace bluetooth {
namespace gap {

RemoteDevice* RemoteDeviceCache::NewDevice(const common::DeviceAddress& address,
                                           bool connectable) {
  if (address_map_.find(address) != address_map_.end())
    return nullptr;

  auto* device = new RemoteDevice(fxl::GenerateUUID(), address, connectable);
  devices_[device->identifier()] = std::unique_ptr<RemoteDevice>(device);
  address_map_[device->address()] = device->identifier();
  return device;
}

RemoteDevice* RemoteDeviceCache::FindDeviceById(
    const std::string& identifier) const {
  auto iter = devices_.find(identifier);
  return iter != devices_.end() ? iter->second.get() : nullptr;
}

RemoteDevice* RemoteDeviceCache::FindDeviceByAddress(
    const common::DeviceAddress& address) const {
  auto iter = address_map_.find(address);
  if (iter == address_map_.end())
    return nullptr;

  auto* dev = FindDeviceById(iter->second);
  FXL_DCHECK(dev);

  return dev;
}

}  // namespace gap
}  // namespace bluetooth
