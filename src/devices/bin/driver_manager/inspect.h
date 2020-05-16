// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BIN_DRIVER_MANAGER_INSPECT_H_
#define SRC_DEVICES_BIN_DRIVER_MANAGER_INSPECT_H_

#include <lib/inspect/cpp/inspect.h>
#include <lib/zx/channel.h>

#include <ddk/binding.h>
#include <fbl/array.h>
#include <fbl/ref_ptr.h>
#include <fs/pseudo_dir.h>
#include <fs/synchronous_vfs.h>

class InspectManager {
 public:
  explicit InspectManager(async_dispatcher_t* dispatcher);

  InspectManager() = delete;

  zx::unowned_channel diagnostics_channel() { return zx::unowned_channel(diagnostics_client_); }

  fs::PseudoDir& diagnostics_dir() { return *diagnostics_dir_; }

  inspect::Node& root_node() { return inspect_.GetRoot(); }

  inspect::Node& devices() { return devices_; }

  inspect::UintProperty& device_count() { return device_count_; }

  // Public method for test purpose
  inspect::Inspector& inspector() { return inspect_; }

 private:
  fbl::RefPtr<fs::PseudoDir> diagnostics_dir_;
  std::unique_ptr<fs::SynchronousVfs> diagnostics_vfs_;
  fbl::RefPtr<fs::PseudoDir> driver_manager_dir_;
  zx::channel diagnostics_client_;

  inspect::Inspector inspect_;
  zx::vmo inspect_vmo_;

  inspect::UintProperty device_count_;
  inspect::Node devices_;
};

class DeviceInspect {
 public:
  // |devices| and |device_count| should outlive DeviceInspect class
  DeviceInspect(inspect::Node& devices, inspect::UintProperty& device_count, std::string name);

  ~DeviceInspect();

  inspect::Node& device_node() { return device_node_; }

  void set_state(std::string state) { state_.Set(state); }

  void set_local_id(uint64_t local_id) { local_id_.Set(local_id); }

  // These methods below are for static values and should be called only once. Calling it more than
  // once will lead to duplicate entries.

  void set_topological_path(std::string path) {
    device_node_.CreateString("topological_path", path, &static_values_);
  }

  void set_protocol_id(uint32_t value) {
    device_node_.CreateUint("protocol_id", value, &static_values_);
  }

  void set_type(std::string type) { device_node_.CreateString("type", type, &static_values_); }

  void set_flags(uint32_t flags) { device_node_.CreateUint("flags", flags, &static_values_); }

  void set_properties(const fbl::Array<const zx_device_prop_t>& props);

  void set_driver(std::string libname) {
    device_node_.CreateString("driver", libname, &static_values_);
  }

 private:
  inspect::UintProperty& device_count_node_;
  inspect::Node device_node_;

  // Reference to nodes with static properties
  inspect::ValueList static_values_;

  inspect::StringProperty state_;
  // Unique id of the device in a driver host
  inspect::UintProperty local_id_;
};

#endif  // SRC_DEVICES_BIN_DRIVER_MANAGER_INSPECT_H_
