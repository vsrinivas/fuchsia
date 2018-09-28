// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_DRIVERS_BLUETOOTH_LIB_GAP_REMOTE_DEVICE_CACHE_H_
#define GARNET_DRIVERS_BLUETOOTH_LIB_GAP_REMOTE_DEVICE_CACHE_H_

#include <unordered_map>

#include <lib/async/cpp/task.h>

#include "garnet/drivers/bluetooth/lib/common/device_address.h"
#include "garnet/drivers/bluetooth/lib/gap/remote_device.h"
#include "garnet/drivers/bluetooth/lib/hci/connection.h"
#include "garnet/drivers/bluetooth/lib/sm/types.h"
#include "lib/fxl/macros.h"
#include "lib/fxl/synchronization/thread_checker.h"

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
  using DeviceCallback = fit::function<void(const RemoteDevice& device)>;
  using DeviceIdCallback = fit::function<void(const std::string& identifier)>;

  RemoteDeviceCache() = default;

  // Creates a new device entry using the given parameters, and returns a
  // (non-owning) pointer to that device. The caller must not retain the pointer
  // beyond the current dispatcher task, as the underlying RemoteDevice is owned
  // by |this| RemoveDeviceCache, and may be invalidated spontaneously.
  //
  // Returns nullptr if an entry matching |address| already exists in the cache.
  RemoteDevice* NewDevice(const common::DeviceAddress& address,
                          bool connectable);

  // Iterates over all current devices in the map, running |f| on each entry
  // synchronously. This is intended for IPC methods that request a list of
  // devices.
  //
  // Clients should use the FindDeviceBy*() methods below to interact with
  // RemoteDevice objects.
  void ForEach(DeviceCallback f);

  // Creates a new non-temporary device entry using the given |identifier| and
  // identity |address|. This is intended to initialize this RemoteDeviceCache
  // with previously bonded devices while bootstrapping a bt-host device.
  //
  // This method is not intended for updating the bonding data of a device that
  // already exists the cache and returns false if a mapping for |identifier| or
  // |address| is already present. Use Store*Bond() methods to update pairing
  // information of an existing device.
  //
  // TODO(armansito): Pass in BR/EDR link key here as well, if present.
  bool AddBondedDevice(const std::string& identifier,
                       const common::DeviceAddress& address,
                       const sm::PairingData& bond_data);

  // Update the device with the given identifier with new LE bonding
  // information. The device will be considered "bonded" and the bonded callback
  // will be notified. If the device is already bonded then bonding data will be
  // updated.
  //
  // If |bond_data| contains an |identity_address|, the device cache will be
  // updated with a new mapping from that address to this device identifier. If
  // the identity address already maps to an existing device, this method will
  // return false. TODO(armansito): Merge the devices instead of failing? What
  // happens if we obtain a LE identity address from a dual-mode device that
  // matches the BD_ADDR previously obtained from it over BR/EDR?
  bool StoreLowEnergyBond(const std::string& identifier,
                          const sm::PairingData& bond_data);

  // TODO(armansito): Add StoreBrEdrBond() method.

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
  void set_device_updated_callback(DeviceCallback callback) {
    ZX_DEBUG_ASSERT(thread_checker_.IsCreationThreadCurrent());
    device_updated_callback_ = std::move(callback);
  }

  // When set, |callback| will be invoked whenever a device is
  // removed.
  void set_device_removed_callback(DeviceIdCallback callback) {
    ZX_DEBUG_ASSERT(thread_checker_.IsCreationThreadCurrent());
    device_removed_callback_ = std::move(callback);
  }

  // When this callback is set, |callback| will be invoked whenever the bonding
  // data of a device is updated and should be persisted. The caller must ensure
  // that |callback| outlives |this|.
  void set_device_bonded_callback(DeviceCallback callback) {
    ZX_DEBUG_ASSERT(thread_checker_.IsCreationThreadCurrent());
    device_bonded_callback_ = std::move(callback);
  }

  // Returns the number of devices that are currently in the device cache.
  size_t count() const { return devices_.size(); }

 private:
  // Maps unique device IDs to the corresponding RemoteDevice entry.
  using RemoteDeviceMap =
      std::unordered_map<std::string, std::unique_ptr<RemoteDevice>>;

 private:
  class RemoteDeviceRecord final {
   public:
    RemoteDeviceRecord(std::unique_ptr<RemoteDevice> device,
                       fbl::Closure remove_device_callback)
        : device_(std::move(device)),
          removal_task_(std::move(remove_device_callback)) {}

    // The copy and move ctors cannot be implicitly defined, since
    // async::TaskClosure does not support those operations. Nor is any
    // meaningful explicit definition possible.
    RemoteDeviceRecord(const RemoteDeviceRecord&) = delete;
    RemoteDeviceRecord(RemoteDeviceRecord&&) = delete;

    RemoteDevice* device() const { return device_.get(); }

    // Returns a pointer to removal_task_, which can be used to (re-)schedule or
    // cancel |remove_device_callback|.
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

  // Notifies interested parties that |device| has bonded
  // |device| must already exist in the cache.
  void NotifyDeviceBonded(const RemoteDevice& device);

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

  DeviceCallback device_updated_callback_;
  DeviceIdCallback device_removed_callback_;
  DeviceCallback device_bonded_callback_;

  fxl::ThreadChecker thread_checker_;

  FXL_DISALLOW_COPY_AND_ASSIGN(RemoteDeviceCache);
};

}  // namespace gap
}  // namespace btlib

#endif  // GARNET_DRIVERS_BLUETOOTH_LIB_GAP_REMOTE_DEVICE_CACHE_H_
