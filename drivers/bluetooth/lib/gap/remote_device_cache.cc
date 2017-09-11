// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "remote_device_cache.h"

#include "apps/bluetooth/lib/hci/low_energy_scanner.h"
#include "lib/fxl/random/uuid.h"

#include "remote_device.h"

namespace bluetooth {
namespace gap {

RemoteDevice* RemoteDeviceCache::StoreLowEnergyScanResult(
    const hci::LowEnergyScanResult& scan_result, const common::ByteBuffer& advertising_data) {
  // If the device already exists then update its contents.
  RemoteDevice* device = FindDeviceByAddress(scan_result.address);
  if (device) {
    device->SetLowEnergyData(scan_result.connectable, scan_result.rssi, advertising_data);
    return device;
  }

  device = new RemoteDevice(fxl::GenerateUUID(), scan_result.address);
  FXL_DCHECK(device->technology() == TechnologyType::kLowEnergy);
  device->SetLowEnergyData(scan_result.connectable, scan_result.rssi, advertising_data);

  if (!device->connectable()) {
    non_conn_devices_[device->identifier()] = std::unique_ptr<RemoteDevice>(device);
  } else {
    tmp_devices_[device->identifier()] = std::unique_ptr<RemoteDevice>(device);
  }

  address_map_[device->address()] = device->identifier();

  return device;
}

RemoteDevice* RemoteDeviceCache::FindDeviceById(const std::string& identifier) const {
  auto iter = tmp_devices_.find(identifier);
  if (iter != tmp_devices_.end()) return iter->second.get();

  iter = non_conn_devices_.find(identifier);
  if (iter != non_conn_devices_.end()) return iter->second.get();

  return nullptr;
}

RemoteDevice* RemoteDeviceCache::FindDeviceByAddress(const common::DeviceAddress& address) const {
  auto iter = address_map_.find(address);
  if (iter == address_map_.end()) return nullptr;

  return FindDeviceById(iter->second);
}

}  // namespace gap
}  // namespace bluetooth
