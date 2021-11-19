// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_TESTING_LIB_SIM_DEVICE_DEVICE_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_TESTING_LIB_SIM_DEVICE_DEVICE_H_

/* Add an abstracted device interface that can be used for wlan driver tests without involving
 * devmgr.
 */
#include <lib/ddk/device.h>
#include <lib/ddk/driver.h>
#include <zircon/assert.h>

#include <optional>
#include <thread>
#include <vector>

#ifdef DEBUG
#define DBG_PRT(fmt, ...) printf(fmt, ##__VA_ARGS__)
#else
#define DBG_PRT(fmt, ...)
#endif  // DEBUG

namespace wlan::simulation {

class FakeDevMgr;

// Provide storage and lifetime management for device_add_args_t and its fields. Use this to make
// copies of device_add_args_t with a lifetime that extends beyond the original device_add_args_t
// object. Note that some fields in device_add_args_t cannot be managed by this class, specifically
// fields that are of private types. This includes types like zx_device_prop_t (props field),
// and zx_device_str_prop_t (str_props field). In the future this list may expand as
// device_add_args_t changes.
class DeviceAddArgs {
 public:
  DeviceAddArgs() = default;
  DeviceAddArgs(const DeviceAddArgs& args);
  DeviceAddArgs& operator=(const DeviceAddArgs& other);

  explicit DeviceAddArgs(const device_add_args_t& args);
  DeviceAddArgs& operator=(const device_add_args_t& other);

  const device_add_args_t& Args() const { return args_; }

 private:
  void CopyRawDeviceAddArgs(const device_add_args_t& args);
  void SetRawPointers();

  device_add_args_t args_ = {};

  std::string name_;
  std::optional<zx_protocol_device_t> ops_;
  std::vector<device_power_state_info_t> power_states_;
  std::vector<device_performance_state_info_t> performance_states_;
  std::vector<std::string> fidl_protocol_offer_strings_;
  std::vector<const char*> fidl_protocol_offers_;
  std::string proxy_args_;
};

class FakeDevice {
 public:
  bool IsRootParent() const;
  zx_status_t AddChild(device_add_args_t* args, zx_device_t** out);
  void AsyncRemove();

  void AddRef() { ++ref_count_; }
  void Release() { --ref_count_; }
  uint32_t RefCount() const { return ref_count_; }

  uint64_t Id() const { return id_; }
  zx_device_t* Parent() const { return parent_; }
  const device_add_args_t& DevArgs() const { return dev_args_.Args(); }
  wlan::simulation::FakeDevMgr& DevMgr() { return *dev_mgr_; }

 protected:
  // Don't create instances of this class, it only serves as a base class for zx_device.
  FakeDevice(uint64_t id, zx_device_t* parent, device_add_args_t dev_args,
             wlan::simulation::FakeDevMgr* dev_mgr);
  ~FakeDevice() = default;

  // You should not need to copy this class, only pass around pointers to instances of it.
  FakeDevice(const FakeDevice&) = delete;
  FakeDevice& operator=(const FakeDevice&) = delete;

 private:
  uint64_t id_;
  zx_device_t* parent_;
  DeviceAddArgs dev_args_;
  uint32_t ref_count_;
  FakeDevMgr* dev_mgr_;
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

  using devices_t = std::vector<std::unique_ptr<zx_device_t>>;
  using iterator = devices_t::iterator;
  using const_iterator = devices_t::const_iterator;
  using Predicate = std::function<bool(zx_device_t*)>;

  // Default C++ iterator implementation:
  iterator begin() { return devices_.begin(); }
  iterator end() { return devices_.end(); }
  const_iterator begin() const { return devices_.begin(); }
  const_iterator end() const { return devices_.end(); }
  const_iterator cbegin() const { return devices_.cbegin(); }
  const_iterator cend() const { return devices_.cend(); }

  void DeviceInitReply(zx_device_t* device, zx_status_t status,
                       const device_init_reply_args_t* args);
  void DeviceUnbindReply(zx_device_t* device);
  zx_status_t DeviceAdd(zx_device_t* parent, device_add_args_t* args, zx_device_t** out);
  void DeviceAsyncRemove(zx_device_t* device);
  zx_device_t* FindFirst(const Predicate& pred) const;
  zx_device_t* FindFirstByProtocolId(uint32_t proto_id) const;
  zx_device_t* FindLatest(const Predicate& pred);
  zx_device_t* FindLatestByProtocolId(uint32_t proto_id);
  bool ContainsDevice(zx_device_t* device) const;
  bool ContainsDevice(uint64_t id) const;
  size_t DeviceCount();
  size_t DeviceCountByProtocolId(uint32_t proto_id);
  zx_device_t* GetRootDevice();

 private:
  enum class DdkCallState;

  bool DeviceUnreference(zx_device_t* device);
  void DeviceUnbind(zx_device_t* device);
  std::vector<zx_device_t*> GetChildren(zx_device_t* device);

  // The device id of fake root device, the value is 1, initialized with constructor.
  const uint64_t fake_root_dev_id_;

  // The device counter starts from 2, because 0 and 1 are reserved for fake root device.
  uint64_t dev_counter_ = 2;
  devices_t devices_;
  std::thread::id init_thread_id_;
  std::thread::id unbind_thread_id_;
  DdkCallState init_state_;
  DdkCallState unbind_state_;
  zx_status_t init_result_;
};

}  // namespace wlan::simulation

struct zx_device : public wlan::simulation::FakeDevice {
  zx_device(uint64_t id, zx_device_t* parent, device_add_args_t dev_args,
            wlan::simulation::FakeDevMgr* dev_mgr)
      : FakeDevice(id, parent, dev_args, dev_mgr) {}
};

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_TESTING_LIB_SIM_DEVICE_DEVICE_H_
