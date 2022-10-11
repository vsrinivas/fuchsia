// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/bin/driver_manager/bind_driver_manager.h"

#include <errno.h>
#include <lib/fit/function.h>
#include <zircon/status.h>

#include "src/devices/bin/driver_manager/binding_internal.h"
#include "src/devices/bin/driver_manager/devfs.h"
#include "src/devices/bin/driver_manager/v1/device_group_v1.h"
#include "src/devices/lib/log/log.h"

BindDriverManager::BindDriverManager(Coordinator* coordinator, AttemptBindFunc attempt_bind)
    : coordinator_(coordinator), attempt_bind_(std::move(attempt_bind)) {}

BindDriverManager::~BindDriverManager() {}

zx_status_t BindDriverManager::BindDriverToDevice(const MatchedDriver& driver,
                                                  const fbl::RefPtr<Device>& dev) {
  if (auto info = std::get_if<MatchedCompositeDriverInfo>(&driver); info) {
    return BindDriverToFragment(*info, dev);
  }

  if (auto info = std::get_if<fdi::MatchedDeviceGroupNodeInfo>(&driver); info) {
    auto device_ptr = std::shared_ptr<DeviceV1Wrapper>(new DeviceV1Wrapper{
        .device = dev,
    });
    return coordinator_->device_group_manager()
        ->BindDeviceGroupNode(*info, device_ptr)
        .status_value();
  }

  if (!std::holds_alternative<MatchedDriverInfo>(driver)) {
    return ZX_ERR_INTERNAL;
  }
  auto driver_info = std::get<MatchedDriverInfo>(driver);
  zx_status_t status = attempt_bind_(driver_info, dev);

  // If we get this here it means we've successfully bound one driver
  // and the device isn't multi-bind.
  if (status == ZX_ERR_ALREADY_BOUND) {
    return ZX_OK;
  }

  if (status != ZX_OK) {
    LOGF(ERROR, "Failed to bind driver '%s' to device '%.*s': %s", driver_info.name(),
         static_cast<uint32_t>(dev->name().size()), dev->name().data(),
         zx_status_get_string(status));
  }
  return status;
}

zx_status_t BindDriverManager::BindDevice(const fbl::RefPtr<Device>& dev,
                                          std::string_view drvlibname, bool new_device) {
  // shouldn't be possible to get a bind request for a proxy device
  if (dev->flags & DEV_CTX_PROXY) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  // A libname of "" means a general rebind request instead of a specific request.
  bool autobind = drvlibname.size() == 0;
  if (autobind && (dev->flags & DEV_CTX_SKIP_AUTOBIND)) {
    return ZX_OK;
  }

  // Attempt composite device matching first. This is unnecessary if a
  // specific driver has been requested.
  if (autobind) {
    for (auto& composite : coordinator_->device_manager()->composite_devices()) {
      auto status = composite.TryMatchBindFragments(dev);
      if (status != ZX_OK) {
        return status;
      }
    }
  }

  // TODO: disallow if we're in the middle of enumeration, etc
  zx::status<std::vector<MatchedDriver>> result = GetMatchingDrivers(dev, drvlibname);
  if (!result.is_ok()) {
    return result.error_value();
  }

  auto drivers = std::move(result.value());
  for (auto& driver : drivers) {
    zx_status_t status = BindDriverToDevice(driver, dev);
    if (status != ZX_OK) {
      return status;
    }
  }

  // Notify observers that this device is available again
  // Needed for non-auto-binding drivers like GPT against block, etc
  if (!new_device && autobind) {
    coordinator_->devfs().advertise_modified(*dev);
  }

  return ZX_OK;
}

zx_status_t BindDriverManager::MatchDevice(const fbl::RefPtr<Device>& dev, const Driver* driver,
                                           bool autobind) const {
  if (dev->IsAlreadyBound()) {
    return ZX_ERR_ALREADY_BOUND;
  }

  if (autobind && dev->should_skip_autobind()) {
    return ZX_ERR_NEXT;
  }

  if (!dev->is_bindable() && !(dev->is_composite_bindable())) {
    return ZX_ERR_NEXT;
  }

  if (!can_driver_bind(driver, dev->protocol_id(), dev->props(), dev->str_props(), autobind)) {
    return ZX_ERR_NEXT;
  }

  return ZX_OK;
}

zx_status_t BindDriverManager::MatchAndBind(const fbl::RefPtr<Device>& dev, const Driver* drv,
                                            bool autobind) {
  zx_status_t status = MatchDevice(dev, drv, autobind);
  if (status != ZX_OK) {
    return status;
  }
  return BindDriverToDevice(MatchedDriverInfo{.driver = drv}, dev);
}

zx::status<std::vector<MatchedDriver>> BindDriverManager::GetMatchingDrivers(
    const fbl::RefPtr<Device>& dev, std::string_view drvlibname) {
  // It shouldn't be possible to get a bind request for a proxy device.
  if (dev->flags & DEV_CTX_PROXY) {
    return zx::error(ZX_ERR_NOT_SUPPORTED);
  }

  if (dev->IsAlreadyBound()) {
    return zx::error(ZX_ERR_ALREADY_BOUND);
  }

  std::vector<MatchedDriver> matched_drivers;

  // A libname of "" means a general rebind request
  // instead of a specific request
  bool autobind = drvlibname.size() == 0;

  // Check for drivers outside of the Driver-index.
  for (const Driver& driver : coordinator_->drivers()) {
    if (!autobind && drvlibname.compare(driver.libname)) {
      continue;
    }

    zx_status_t status = MatchDevice(dev, &driver, autobind);
    if (status == ZX_ERR_ALREADY_BOUND) {
      return zx::error(ZX_ERR_ALREADY_BOUND);
    }

    if (status == ZX_ERR_NEXT) {
      continue;
    }

    if (status == ZX_OK) {
      auto matched = MatchedDriverInfo{.driver = &driver};
      matched_drivers.push_back(std::move(matched));
    }

    // If the device doesn't support multibind (this is a devmgr-internal setting),
    // then return on first match or failure.
    // Otherwise, keep checking all the drivers.
    if (!(dev->flags & DEV_CTX_MULTI_BIND)) {
      if (status != ZX_OK) {
        return zx::error(status);
      }
      return zx::ok(std::move(matched_drivers));
    }
  }

  // Check for drivers in the Driver-index.
  {
    DriverLoader::MatchDeviceConfig config;
    config.libname = drvlibname;
    auto drivers = coordinator_->driver_loader().MatchDeviceDriverIndex(dev, config);
    for (auto driver : drivers) {
      matched_drivers.push_back(driver);
    }
  }

  return zx::ok(std::move(matched_drivers));
}

zx::status<std::vector<MatchedDriver>> BindDriverManager::MatchDeviceWithDriverIndex(
    const fbl::RefPtr<Device>& dev, const DriverLoader::MatchDeviceConfig& config) const {
  if (dev->IsAlreadyBound()) {
    return zx::error(ZX_ERR_ALREADY_BOUND);
  }

  if (dev->should_skip_autobind()) {
    return zx::error(ZX_ERR_NEXT);
  }

  if (!dev->is_bindable() && !(dev->is_composite_bindable())) {
    return zx::error(ZX_ERR_NEXT);
  }

  return zx::ok(coordinator_->driver_loader().MatchDeviceDriverIndex(dev, config));
}

zx_status_t BindDriverManager::MatchAndBindWithDriverIndex(
    const fbl::RefPtr<Device>& dev, const DriverLoader::MatchDeviceConfig& config) {
  auto result = MatchDeviceWithDriverIndex(dev, config);
  if (!result.is_ok()) {
    return result.error_value();
  }

  auto matched_drivers = std::move(result.value());
  for (auto driver : matched_drivers) {
    zx_status_t status = BindDriverToDevice(driver, dev);

    // If we get this here it means we've successfully bound one driver
    // and the device isn't multi-bind.
    if (status == ZX_ERR_ALREADY_BOUND) {
      return ZX_OK;
    }
  }

  return ZX_OK;
}

void BindDriverManager::BindAllDevicesDriverIndex(const DriverLoader::MatchDeviceConfig& config) {
  // This call is not strictly necessary -- we do not bind anything to the root device.
  // However, it guarantees that we connect to the driver index and wait for it to start.
  // Some tests become flaky if we don't do this here.
  zx_status_t status = MatchAndBindWithDriverIndex(coordinator_->root_device(), config);
  if (status != ZX_OK && status != ZX_ERR_NEXT) {
    LOGF(ERROR, "DriverIndex failed to match root_device: %d", status);
    return;
  }

  for (auto& dev : coordinator_->device_manager()->devices()) {
    auto dev_ref = fbl::RefPtr(&dev);
    zx_status_t status = MatchAndBindWithDriverIndex(dev_ref, config);
    if (status == ZX_ERR_NEXT || status == ZX_ERR_ALREADY_BOUND) {
      continue;
    }
    if (status != ZX_OK) {
      return;
    }
  }
}

zx_status_t BindDriverManager::MatchAndBindDeviceGroups(const fbl::RefPtr<Device>& dev) {
  DriverLoader::MatchDeviceConfig config;
  auto result = MatchDeviceWithDriverIndex(dev, config);
  if (!result.is_ok()) {
    if (result.error_value() == ZX_ERR_NEXT || result.error_value() == ZX_ERR_ALREADY_BOUND) {
      return ZX_OK;
    }
    return result.error_value();
  }

  auto matched_drivers = std::move(result.value());
  for (auto driver : matched_drivers) {
    if (!std::holds_alternative<fdi::MatchedDeviceGroupNodeInfo>(driver)) {
      continue;
    }

    auto device_ptr = std::shared_ptr<DeviceV1Wrapper>(new DeviceV1Wrapper{
        .device = dev,
    });
    auto bind_result = coordinator_->device_group_manager()->BindDeviceGroupNode(
        std::get<fdi::MatchedDeviceGroupNodeInfo>(driver), device_ptr);
    if (bind_result.is_error()) {
      LOGF(ERROR, "Failed to bind device group node: %d", bind_result.status_value());
    }
  }

  return ZX_OK;
}

zx_status_t BindDriverManager::BindDriverToFragment(const MatchedCompositeDriverInfo& driver,
                                                    const fbl::RefPtr<Device>& dev) {
  // Check if the driver already exists in |driver_index_composite_devices_|. If
  // it doesn't, create and add a new CompositeDevice.
  auto name = std::string(driver.driver_info.name());
  if (driver_index_composite_devices_.count(name) == 0) {
    driver_index_composite_devices_[name] =
        CompositeDevice::CreateFromDriverIndex(driver, fbl::Array<std::unique_ptr<Metadata>>());
  }

  // Bind the matched fragment to the device.
  auto& composite = driver_index_composite_devices_[name];
  zx_status_t status = composite->BindFragment(driver.composite.node, dev);
  if (status != ZX_OK) {
    LOGF(ERROR, "Failed to BindFragment for '%.*s': %s", static_cast<uint32_t>(dev->name().size()),
         dev->name().data(), zx_status_get_string(status));
  }
  return status;
}
