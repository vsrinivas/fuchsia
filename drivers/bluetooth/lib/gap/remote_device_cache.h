// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <unordered_map>

#include <lib/async/cpp/task.h>

#include "garnet/drivers/bluetooth/lib/common/device_address.h"
#include "garnet/drivers/bluetooth/lib/hci/connection.h"
#include "lib/fxl/macros.h"
#include "remote_device.h"

namespace btlib {

namespace common {
class ByteBuffer;
}  // namespace common

namespace hci {
struct LowEnergyScanResult;
}  // namespace hci

namespace gap {

// A RemoteDeviceCache provides access to remote Bluetooth devices that are
// known to the system.
// TODO(armansito): The current implementation is very simple but it will grow
// to support more complex features such as LE private address resolution.
class RemoteDeviceCache final {
 public:
  using DeviceUpdatedCallback = fit::function<void(const RemoteDevice& device)>;
  using DeviceRemovedCallback =
      fit::function<void(const std::string& identifier)>;

  RemoteDeviceCache() = default;

  // Creates a new device entry using the given parameters. Returns nullptr if
  // an entry matching |address| already exists in the cache.
  RemoteDevice* NewDevice(const common::DeviceAddress& address,
                          bool connectable);

  // Returns the remote device with identifier |identifier|. Returns nullptr if
  // |identifier| is not recognized.
  RemoteDevice* FindDeviceById(const std::string& identifier) const;

  // Finds and returns a RemoteDevice with address |address| if it exists,
  // returns nullptr otherwise.
  // TODO(armansito): This should perform address resolution for devices using
  // LE privacy.
  RemoteDevice* FindDeviceByAddress(const common::DeviceAddress& address) const;

  // When set, |callback| will be invoked whenever a device is added
  // or updated.
  void set_device_updated_callback(DeviceUpdatedCallback callback) {
    device_updated_callback_ = std::move(callback);
  }

  // When set, |callback| will be invoked whenever a device is
  // removed.
  void set_device_removed_callback(DeviceRemovedCallback callback) {
    device_removed_callback_ = std::move(callback);
  }

 private:
  friend class RemoteDeviceRecord;
  class RemoteDeviceRecord final {
   public:
    RemoteDeviceRecord(std::unique_ptr<RemoteDevice> device,
                       fbl::Closure remove_device_callback)
        : device_(std::move(device)),
          removal_task_(std::move(remove_device_callback)) {}
    RemoteDeviceRecord(const RemoteDeviceRecord&) = delete;
    RemoteDeviceRecord(RemoteDeviceRecord&&) = delete;
    RemoteDevice* device() const { return device_.get(); }
    async::TaskClosure* removal_task() { return &removal_task_; }

   private:
    std::unique_ptr<RemoteDevice> device_;
    async::TaskClosure removal_task_;
  };

  // Notifies interested parties that |device| has seen a significant change.
  // |device| must already exist in the cache.
  void NotifyDeviceUpdated(const RemoteDevice& device);

  // Updates the expiration time for |device|, if a temporary. Cancels expiry,
  // if a non-temporary. Pre-conditions:
  // - |device| must already exist in the cache
  // - can only be called from the thread that created |device|
  void UpdateExpiry(const RemoteDevice& device);

  // Removes |device| from this cache, and notifies listeners of the
  // removal.
  void RemoveDevice(RemoteDevice* device);

  // Mapping from unique device IDs to RemoteDeviceRecords.
  // Owns the corresponding RemoteDevices.
  std::unordered_map<std::string, RemoteDeviceRecord> devices_;
  // Mapping from device addresses to unique device identifiers for all known
  // devices. This is used to look-up and update existing cached data for a
  // particular scan result so as to avoid creating duplicate entries for the
  // same device.
  //
  // TODO(armansito): Replace this with an implementation that can resolve
  // device identity, to handle bonded LE devices that use privacy.
  std::unordered_map<common::DeviceAddress, std::string> address_map_;

  DeviceUpdatedCallback device_updated_callback_;
  DeviceRemovedCallback device_removed_callback_;

  FXL_DISALLOW_COPY_AND_ASSIGN(RemoteDeviceCache);
};

}  // namespace gap
}  // namespace btlib
