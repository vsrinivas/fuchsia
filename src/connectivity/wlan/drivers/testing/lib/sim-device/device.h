// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_TESTING_LIB_SIM_DEVICE_DEVICE_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_TESTING_LIB_SIM_DEVICE_DEVICE_H_

/* Add an abstracted device interface that can be used for wlan driver tests without involving
 * devmgr.
 */
#include <zircon/assert.h>

#include <unordered_map>

#include <ddk/device.h>
#include <ddk/driver.h>

namespace wlan::simulation {

#ifdef DEBUG
#define DBG_PRT(fmt, ...) printf(fmt, ##__VA_ARGS__)
#else
#define DBG_PRT(fmt, ...)
#endif  // DEBUG

struct DeviceIdHasher;

class DeviceId {
 public:
  friend DeviceIdHasher;

  explicit DeviceId(uint64_t id) : id_(id) {}

  static DeviceId FromDevice(zx_device_t* device) {
    return DeviceId(reinterpret_cast<uint64_t>(device));
  }

  zx_device_t* as_device() const { return reinterpret_cast<zx_device_t*>(id_); }

  bool operator==(const DeviceId& other) const { return id_ == other.id_; }

 private:
  uint64_t id_;
};

struct DeviceIdHasher {
  std::size_t operator()(const DeviceId& deviceId) const {
    return std::hash<uint64_t>()(deviceId.id_);
  }
};

struct wlan_sim_dev_info_t {
  zx_device_t* parent;
  device_add_args_t dev_args;
  uint32_t ref_count;
};

// Fake DeviceManager is a drop-in replacement for Fuchsia's DeviceManager in unit tests.
// In particular, this fake DeviceManager provides functionality to add and remove devices,
// as well as iterate through the device list. It also provides some convenience methods
// for accessing previously added devices.
// Note: Devices are *not* ordered in any particular way.
class FakeDevMgr {
 public:
  FakeDevMgr();
  ~FakeDevMgr();

  using devices_t = std::unordered_map<DeviceId, wlan_sim_dev_info_t, DeviceIdHasher>;
  using iterator = devices_t::iterator;
  using const_iterator = devices_t::const_iterator;
  using Predicate = std::function<bool(DeviceId, wlan_sim_dev_info_t&)>;

  // Default C++ iterator implementation:
  iterator begin() { return devices_.begin(); }
  iterator end() { return devices_.end(); }
  const_iterator begin() const { return devices_.begin(); }
  const_iterator end() const { return devices_.end(); }
  const_iterator cbegin() const { return devices_.cbegin(); }
  const_iterator cend() const { return devices_.cend(); }

  zx_status_t DeviceAdd(zx_device_t* parent, device_add_args_t* args, zx_device_t** out);
  void DeviceAsyncRemove(zx_device_t* device);
  std::optional<wlan_sim_dev_info_t> FindFirst(const Predicate& pred);
  std::optional<wlan_sim_dev_info_t> FindFirstByProtocolId(uint32_t proto_id);
  std::optional<DeviceId> FindFirstDev(const Predicate& pred);
  std::optional<DeviceId> FindFirstDevByProtocolId(uint32_t proto_id);
  std::optional<wlan_sim_dev_info_t> GetDevice(zx_device_t* device);
  size_t DeviceCount();
  zx_device_t* GetRootDevice();

  // The device id of fake root device, the value is 1, initialized with constructor.
  const DeviceId fake_root_dev_id_;

 private:
  bool DeviceUnreference(devices_t::iterator iter);

  // The device counter starts from 2, because 0 and 1 are reserved for fake root device.
  uint64_t dev_counter_ = 2;
  devices_t devices_;
};
}  // namespace wlan::simulation
#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_TESTING_LIB_SIM_DEVICE_DEVICE_H_
