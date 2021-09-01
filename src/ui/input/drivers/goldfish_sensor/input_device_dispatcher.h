// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_INPUT_DRIVERS_GOLDFISH_SENSOR_INPUT_DEVICE_DISPATCHER_H_
#define SRC_UI_INPUT_DRIVERS_GOLDFISH_SENSOR_INPUT_DEVICE_DISPATCHER_H_

#include <lib/ddk/debug.h>

#include <list>
#include <unordered_map>

#include "src/ui/input/drivers/goldfish_sensor/input_device.h"

namespace goldfish::sensor {

// InputDeviceDispatcher manages all sensor InputDevice instances and
// could dispatch a sensor report to its corresponding InputDevice.
class InputDeviceDispatcher {
 public:
  // Add new InputDevice with |name|.
  // Returns:
  // - If |device| or devices with the same |name| already exists, returns
  //   false, and the |device| will be not added.
  // - Otherwise, returns true.
  bool AddDevice(InputDevice* device, const std::string& name);

  // Remove the device with given |device| pointer.
  // Returns:
  // - Returns true if |device| is found and deleted.
  // - Otherwise returns false.
  bool RemoveDevice(InputDevice* device);

  // Remove the device with given |name|.
  // Returns:
  // - Returns true if the device with |name| is found and deleted.
  // - Otherwise returns false.
  bool RemoveDevice(const std::string& name);

  // Get the device pointer of given |name|.
  // Returns:
  // - Returns the device pointer if device with |name| is found.
  // - Otherwise returns nullptr.
  InputDevice* GetDevice(const std::string& name);

  // Dispatch a given SensorReport to device with |name|.
  // If device not found, returns |ZX_ERR_NOT_FOUND|. Otherwise returns
  // the result of the dispatch callback |OnReport()|.
  zx_status_t DispatchReportToDevice(const std::string& name, const SensorReport& rpt);

  // Dispatch a given SensorReport to all available devices.
  void DispatchReportToAllDevices(const SensorReport& rpt);

 private:
  struct DeviceEntry {
    InputDevice* device = nullptr;
    std::string name;
  };

  std::list<DeviceEntry> devices_;
  std::unordered_map<std::string, std::list<DeviceEntry>::iterator> name_map_;
  std::unordered_map<InputDevice*, std::list<DeviceEntry>::iterator> ptr_map_;
};

}  // namespace goldfish::sensor

#endif  // SRC_UI_INPUT_DRIVERS_GOLDFISH_SENSOR_INPUT_DEVICE_DISPATCHER_H_
