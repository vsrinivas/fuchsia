// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "remote_device_cache.h"

#include <fbl/function.h>
#include <zircon/assert.h>

#include "garnet/drivers/bluetooth/lib/gap/remote_device.h"
#include "garnet/drivers/bluetooth/lib/hci/connection.h"
#include "garnet/drivers/bluetooth/lib/hci/low_energy_scanner.h"
#include "lib/async/default.h"
#include "lib/fxl/random/uuid.h"
#include "lib/zx/time.h"

namespace {
constexpr auto kCacheTimeout = zx::sec(60);
}

namespace btlib {
namespace gap {

RemoteDevice* RemoteDeviceCache::NewDevice(const common::DeviceAddress& address,
                                           bool connectable) {
  if (address_map_.find(address) != address_map_.end())
    return nullptr;

  auto* device = new RemoteDevice(
      fit::bind_member(this, &RemoteDeviceCache::NotifyDeviceUpdated),
      fit::bind_member(this, &RemoteDeviceCache::UpdateExpiry),
      fxl::GenerateUUID(), address, connectable);
  devices_.emplace(
      std::piecewise_construct, std::forward_as_tuple(device->identifier()),
      std::forward_as_tuple(std::unique_ptr<RemoteDevice>(device),
                            [this, device] { RemoveDevice(device); }));
  address_map_[device->address()] = device->identifier();
  UpdateExpiry(*device);
  NotifyDeviceUpdated(*device);

  return device;
}

bool RemoteDeviceCache::AddBondedDevice(std::string identifier,
                                        const common::DeviceAddress& address,
                                        const sm::LTK& key) {
  bool id_exists = FindDeviceById(identifier);
  bool addr_exists = FindDeviceByAddress(address);
  if (id_exists || addr_exists) {
    bt_log(WARN, "gap", "bonded device with %s %s already in device cache",
           id_exists ? "identifier" : "address",
           id_exists ? identifier.c_str() : address.ToString().c_str());
    return false;
  }
  auto* device = new RemoteDevice(
      fit::bind_member(this, &RemoteDeviceCache::NotifyDeviceUpdated),
      fit::bind_member(this, &RemoteDeviceCache::UpdateExpiry),
      identifier, address, true);
  devices_.emplace(
      std::piecewise_construct, std::forward_as_tuple(device->identifier()),
      std::forward_as_tuple(std::unique_ptr<RemoteDevice>(device),
                            [this, device] { RemoveDevice(device); }));
  address_map_[device->address()] = device->identifier();
  device->set_ltk(key);
  NotifyDeviceUpdated(*device);
  return true;
}

bool RemoteDeviceCache::StoreLTK(std::string device_id, const sm::LTK& key) {
  bt_log(TRACE, "gap", "StoreLTK");
  auto device = FindDeviceById(device_id);
  if (!device)
    return false;

  device->set_ltk(key);
  NotifyDeviceBonded(*device);
  return true;
}

RemoteDevice* RemoteDeviceCache::FindDeviceById(
    const std::string& identifier) const {
  auto iter = devices_.find(identifier);
  return iter != devices_.end() ? iter->second.device() : nullptr;
}

RemoteDevice* RemoteDeviceCache::FindDeviceByAddress(
    const common::DeviceAddress& address) const {
  auto iter = address_map_.find(address);
  if (iter == address_map_.end())
    return nullptr;

  auto* dev = FindDeviceById(iter->second);
  ZX_DEBUG_ASSERT(dev);

  return dev;
}

// Private methods below.

void RemoteDeviceCache::NotifyDeviceBonded(const RemoteDevice& device) {
  ZX_DEBUG_ASSERT(devices_.find(device.identifier()) != devices_.end());
  ZX_DEBUG_ASSERT(devices_.at(device.identifier()).device() == &device);

  bt_log(INFO, "gap", "peer bonded %s", device.ToString().c_str());
  if (device_bonded_callback_) {
    device_bonded_callback_(device);
  }
}

void RemoteDeviceCache::NotifyDeviceUpdated(const RemoteDevice& device) {
  ZX_DEBUG_ASSERT(devices_.find(device.identifier()) != devices_.end());
  ZX_DEBUG_ASSERT(devices_.at(device.identifier()).device() == &device);
  if (device_updated_callback_)
    device_updated_callback_(device);
}

void RemoteDeviceCache::UpdateExpiry(const RemoteDevice& device) {
  auto device_record_iter = devices_.find(device.identifier());
  ZX_DEBUG_ASSERT(device_record_iter != devices_.end());

  auto& device_record = device_record_iter->second;
  ZX_DEBUG_ASSERT(device_record.device() == &device);

  const auto cancel_res = device_record.removal_task()->Cancel();
  ZX_DEBUG_ASSERT(cancel_res == ZX_OK || cancel_res == ZX_ERR_NOT_FOUND);

  if (!device.temporary() ||
      device.le_connection_state() ==
          RemoteDevice::ConnectionState::kConnected ||
      device.bredr_connection_state() ==
          RemoteDevice::ConnectionState::kConnected) {
    return;
  }

  const auto schedule_res = device_record.removal_task()->PostDelayed(
      async_get_default_dispatcher(), kCacheTimeout);
  ZX_DEBUG_ASSERT(schedule_res == ZX_OK || schedule_res == ZX_ERR_BAD_STATE);
}

void RemoteDeviceCache::RemoveDevice(RemoteDevice* device) {
  ZX_DEBUG_ASSERT(device);

  auto device_record_it = devices_.find(device->identifier());
  ZX_DEBUG_ASSERT(device_record_it != devices_.end());
  ZX_DEBUG_ASSERT(device_record_it->second.device() == device);

  const std::string identifier_copy = device->identifier();
  address_map_.erase(device->address());
  devices_.erase(device_record_it);  // Destroys |device|.
  if (device_removed_callback_) {
    device_removed_callback_(identifier_copy);
  }
}

}  // namespace gap
}  // namespace btlib
