// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "inspect.h"

#include <driver-info/driver-info.h>
#include <fs/vmo_file.h>

InspectManager::InspectManager(async_dispatcher_t* dispatcher) {
  inspect_vmo_ = inspect_.DuplicateVmo();

  diagnostics_dir_ = fbl::MakeRefCounted<fs::PseudoDir>();
  auto driver_manager_dir = fbl::MakeRefCounted<fs::PseudoDir>();
  diagnostics_dir_->AddEntry("driver_manager", driver_manager_dir);

  driver_host_dir_ = fbl::MakeRefCounted<fs::PseudoDir>();
  driver_manager_dir->AddEntry("driver_host", driver_host_dir_);

  uint64_t vmo_size;
  ZX_ASSERT(inspect_vmo_.get_size(&vmo_size) == ZX_OK);

  auto vmo_file = fbl::MakeRefCounted<fs::VmoFile>(inspect_vmo_, 0, vmo_size);
  driver_manager_dir->AddEntry("dm.inspect", vmo_file);

  if (dispatcher) {
    zx::channel local;
    zx::channel::create(0, &diagnostics_client_, &local);
    diagnostics_vfs_ = std::make_unique<fs::SynchronousVfs>(dispatcher);
    diagnostics_vfs_->ServeDirectory(diagnostics_dir_, std::move(local));
  }

  devices_ = root_node().CreateChild("devices");
  device_count_ = root_node().CreateUint("device_count", 0);
}

DeviceInspect::DeviceInspect(inspect::Node& devices, inspect::UintProperty& device_count,
                             std::string name)
    : device_count_node_(device_count) {
  device_node_ = devices.CreateChild(name);
  // Increment device count.
  device_count_node_.Add(1);

  // create properties with default values
  state_ = device_node_.CreateString("state", "");
  local_id_ = device_node_.CreateUint("driver_host_local_id", 0);
}

DeviceInspect::~DeviceInspect() {
  // Decrement device count.
  device_count_node_.Subtract(1);
}

void DeviceInspect::set_properties(const fbl::Array<const zx_device_prop_t>& props) {
  inspect::Node properties_array;

  // Add a node only if there are any `props`
  if (props.size()) {
    properties_array = device_node_.CreateChild("properties");
  }

  for (uint32_t i = 0; i < props.size(); ++i) {
    const zx_device_prop_t* p = &props[i];
    const char* param_name = di_bind_param_name(p->id);
    auto property = properties_array.CreateChild(std::to_string(i));
    property.CreateUint("value", p->value, &static_values_);
    if (param_name) {
      property.CreateString("id", param_name, &static_values_);
    } else {
      property.CreateString("id", std::to_string(p->id), &static_values_);
    }
    static_values_.emplace(std::move(property));
  }

  // Place the node into value list as props will not change in the lifetime of the device.
  if (props.size()) {
    static_values_.emplace(std::move(properties_array));
  }
}
