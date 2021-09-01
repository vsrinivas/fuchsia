// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/input/drivers/goldfish_sensor/input_device_dispatcher.h"

namespace goldfish::sensor {

bool InputDeviceDispatcher::AddDevice(InputDevice* device, const std::string& name) {
  if (name_map_.find(name) != name_map_.end() || ptr_map_.find(device) != ptr_map_.end()) {
    return false;
  }
  devices_.push_back({
      .device = device,
      .name = name,
  });
  name_map_[name] = ptr_map_[device] = --devices_.end();
  return true;
}

bool InputDeviceDispatcher::RemoveDevice(InputDevice* device) {
  if (ptr_map_.find(device) == ptr_map_.end()) {
    return false;
  }
  DeviceEntry entry_to_delete = *ptr_map_[device];
  devices_.erase(ptr_map_[device]);
  name_map_.erase(entry_to_delete.name);
  ptr_map_.erase(entry_to_delete.device);
  return true;
}

bool InputDeviceDispatcher::RemoveDevice(const std::string& name) {
  if (name_map_.find(name) == name_map_.end()) {
    return false;
  }
  DeviceEntry entry_to_delete = *name_map_[name];
  devices_.erase(name_map_[name]);
  name_map_.erase(entry_to_delete.name);
  ptr_map_.erase(entry_to_delete.device);
  return true;
}

InputDevice* InputDeviceDispatcher::GetDevice(const std::string& name) {
  if (name_map_.find(name) == name_map_.end()) {
    return nullptr;
  }
  return name_map_[name]->device;
}

zx_status_t InputDeviceDispatcher::DispatchReportToDevice(const std::string& name,
                                                          const SensorReport& rpt) {
  if (name_map_.find(name) == name_map_.end()) {
    return ZX_ERR_NOT_FOUND;
  }
  return name_map_[name]->device->OnReport(rpt);
}

void InputDeviceDispatcher::DispatchReportToAllDevices(const SensorReport& rpt) {
  for (const auto& kv : name_map_) {
    auto status = kv.second->device->OnReport(rpt);
    if (status != ZX_OK) {
      zxlogf(ERROR, "Cannot dispatch report to device \"%s\": %d", kv.first.c_str(), status);
    }
  }
}

}  // namespace goldfish::sensor
