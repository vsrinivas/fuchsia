// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BIN_DRIVER_MANAGER_BIND_DRIVER_MANAGER_H_
#define SRC_DEVICES_BIN_DRIVER_MANAGER_BIND_DRIVER_MANAGER_H_

#include <lib/ddk/device.h>

#include <memory>

#include "src/devices/bin/driver_manager/coordinator.h"
#include "src/devices/bin/driver_manager/driver_loader.h"
#include "src/devices/bin/driver_manager/v1/init_task.h"
#include "src/devices/bin/driver_manager/v1/resume_task.h"
#include "src/devices/bin/driver_manager/v1/suspend_task.h"
#include "src/devices/bin/driver_manager/v1/unbind_task.h"

class CompositeDevice;

// Function that is invoked to request a driver try to bind to a device
using AttemptBindFunc =
    fit::function<zx_status_t(const Driver* drv, const fbl::RefPtr<Device>& dev)>;

class BindDriverManager {
 public:
  BindDriverManager(const BindDriverManager&) = delete;
  BindDriverManager& operator=(const BindDriverManager&) = delete;
  BindDriverManager(BindDriverManager&&) = delete;
  BindDriverManager& operator=(BindDriverManager&&) = delete;

  BindDriverManager(Coordinator* coordinator, AttemptBindFunc attempt_bind);
  ~BindDriverManager();

  // Returns ZX_OK if |device| and |driver| are a match for binding.
  zx_status_t MatchDevice(const fbl::RefPtr<Device>& dev, const Driver* driver,
                          bool autobind) const;

  zx_status_t BindDriverToDevice(const MatchedDriver& driver, const fbl::RefPtr<Device>& dev);

  // Try binding a driver to the device. Returns ZX_ERR_ALREADY_BOUND if there
  // is a driver bound to the device and the device is not allowed to be bound multiple times.
  zx_status_t BindDevice(const fbl::RefPtr<Device>& dev, std::string_view drvlibname,
                         bool new_device);

  // Attempts to bind the given driver to the given device.  Returns ZX_OK on
  // success, ZX_ERR_ALREADY_BOUND if there is a driver bound to the device
  // and the device is not allowed to be bound multiple times, ZX_ERR_NEXT if
  // the driver is not capable of binding to the device, and a different error
  // if the driver was capable of binding but failed to bind.
  zx_status_t MatchAndBind(const fbl::RefPtr<Device>& dev, const Driver* driver, bool autobind);

  // Given a device, return all of the Drivers whose bind programs match with the device.
  // The returned vector is organized by priority, so if only one driver is being bound it
  // should be the first in the vector.
  // If |drvlibname| is not empty then the device will only be checked against the driver
  // with that specific name.
  zx::status<std::vector<MatchedDriver>> GetMatchingDrivers(const fbl::RefPtr<Device>& dev,
                                                            std::string_view drvlibname);

  // Binds all the devices to the drivers in the Driver Index.
  void BindAllDevicesDriverIndex(const DriverLoader::MatchDeviceConfig& config);

  // Public for testing only.
  void set_attempt_bind(AttemptBindFunc attempt_bind) { attempt_bind_ = std::move(attempt_bind); }

 private:
  // Find and return matching drivers for |dev| in the Driver Index.
  zx::status<std::vector<MatchedDriver>> MatchDeviceWithDriverIndex(
      const fbl::RefPtr<Device>& dev, const DriverLoader::MatchDeviceConfig& config) const;

  // Find matching drivers for |dev| through the Driver Index and then bind them.
  zx_status_t MatchAndBindWithDriverIndex(const fbl::RefPtr<Device>& dev,
                                          const DriverLoader::MatchDeviceConfig& config);

  // Binds the matched fragment in |driver| to |dev|. If a CompositeDevice for |driver| doesn't
  // exists in |driver_index_composite_devices_|, this function creates and adds it.
  zx_status_t BindDriverToFragment(const MatchedCompositeDriverInfo& driver,
                                   const fbl::RefPtr<Device>& dev);

  // Owner. Must outlive BindDriverManager.
  Coordinator* coordinator_;

  // Callback function. Used to attempt binding a driver to a device.
  // TODO(fxb/90932): Remove this callback.
  AttemptBindFunc attempt_bind_;

  // All the composite devices received from the DriverIndex.
  // This maps driver URLs to the CompositeDevice object.
  std::unordered_map<std::string, std::unique_ptr<CompositeDevice>> driver_index_composite_devices_;
};

#endif  // SRC_DEVICES_BIN_DRIVER_MANAGER_BIND_DRIVER_MANAGER_H_
