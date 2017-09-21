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
                                           TechnologyType technology, bool connectable,
                                           bool temporary) {
  auto* device = new RemoteDevice(fxl::GenerateUUID(), technology, address, connectable, temporary);
  devices_[device->identifier()] = std::unique_ptr<RemoteDevice>(device);
  address_map_[device->address()] = device->identifier();
  return device;
}

RemoteDevice* RemoteDeviceCache::StoreLowEnergyScanResult(
    const hci::LowEnergyScanResult& scan_result, const common::ByteBuffer& advertising_data) {
  // If the device already exists then update its contents.
  RemoteDevice* device = FindDeviceByAddress(scan_result.address);
  if (device) {
    if (device->connectable() != scan_result.connectable) {
      FXL_VLOG(1) << "gap: RemoteDeviceCache: device (id: " << device->identifier() << ") now "
                  << (scan_result.connectable ? "connectable" : "non-connectable");
      device->set_connectable(scan_result.connectable);
    }
    device->SetLowEnergyData(scan_result.rssi, advertising_data);
    return device;
  }

  device = NewDevice(scan_result.address, TechnologyType::kLowEnergy, scan_result.connectable,
                     true /* temporary */);
  device->SetLowEnergyData(scan_result.rssi, advertising_data);

  return device;
}

RemoteDevice* RemoteDeviceCache::StoreLowEnergyConnection(
    const common::DeviceAddress& peer_address, hci::Connection::LinkType ll_type,
    const hci::Connection::LowEnergyParameters& le_params) {
  FXL_DCHECK(ll_type == hci::Connection::LinkType::kLE);

  RemoteDevice* device = FindDeviceByAddress(peer_address);
  if (!device) {
    device = NewDevice(peer_address, TechnologyType::kLowEnergy, true /* connectable */,
                       false /* temporary */);
    device->SetLowEnergyData(hci::kRSSIInvalid, common::BufferView());
  }

  FXL_DCHECK(device->connectable());
  device->SetLowEnergyConnectionData(le_params);
  device->set_temporary(false);

  return device;
}

RemoteDevice* RemoteDeviceCache::FindDeviceById(const std::string& identifier) const {
  auto iter = devices_.find(identifier);
  return iter != devices_.end() ? iter->second.get() : nullptr;
}

RemoteDevice* RemoteDeviceCache::FindDeviceByAddress(const common::DeviceAddress& address) const {
  auto iter = address_map_.find(address);
  if (iter == address_map_.end()) return nullptr;

  return FindDeviceById(iter->second);
}

}  // namespace gap
}  // namespace bluetooth
