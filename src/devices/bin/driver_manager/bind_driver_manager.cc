// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/bin/driver_manager/bind_driver_manager.h"

#include <errno.h>
#include <lib/fit/function.h>
#include <zircon/status.h>

#include "src/devices/lib/log/log.h"

BindDriverManager::BindDriverManager(Coordinator* coordinator, AttemptBindFunc attempt_bind)
    : coordinator_(coordinator), attempt_bind_(std::move(attempt_bind)) {}

BindDriverManager::~BindDriverManager() {}

zx_status_t BindDriverManager::BindDriverToDevice(const MatchedDriver& driver,
                                                  const fbl::RefPtr<Device>& dev) {
  if (driver.composite) {
    return BindDriverToDeviceDriverIndex(driver, dev);
  }

  zx_status_t status = attempt_bind_(driver.driver, dev);
  // If we get this here it means we've successfully bound one driver
  // and the device isn't multi-bind.
  if (status == ZX_ERR_ALREADY_BOUND) {
    return ZX_OK;
  }

  if (status != ZX_OK) {
    LOGF(ERROR, "%s: Failed to bind driver '%s' to device '%s': %s", __func__,
         driver.driver->libname.data(), dev->name().data(), zx_status_get_string(status));
  }
  return status;
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
  return BindDriverToDevice(MatchedDriver{.driver = drv}, dev);
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
      auto matched = MatchedDriver{.driver = &driver};
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

zx_status_t BindDriverManager::BindDeviceWithDriverIndex(
    const fbl::RefPtr<Device>& dev, const DriverLoader::MatchDeviceConfig& config) {
  if (dev->IsAlreadyBound()) {
    return ZX_ERR_ALREADY_BOUND;
  }

  if (dev->should_skip_autobind()) {
    return ZX_ERR_NEXT;
  }

  if (!dev->is_bindable() && !(dev->is_composite_bindable())) {
    return ZX_ERR_NEXT;
  }

  auto drivers = coordinator_->driver_loader().MatchDeviceDriverIndex(dev, config);
  for (auto driver : drivers) {
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
  zx_status_t status = BindDeviceWithDriverIndex(coordinator_->root_device(), config);
  if (status != ZX_OK && status != ZX_ERR_NEXT) {
    LOGF(ERROR, "DriverIndex failed to match root_device: %d", status);
    return;
  }

  for (auto& dev : coordinator_->devices()) {
    auto dev_ref = fbl::RefPtr(&dev);
    zx_status_t status = BindDeviceWithDriverIndex(dev_ref, config);
    if (status == ZX_ERR_NEXT || status == ZX_ERR_ALREADY_BOUND) {
      continue;
    }
    if (status != ZX_OK) {
      return;
    }
  }
}

zx_status_t BindDriverManager::BindDriverToDeviceDriverIndex(const MatchedDriver& driver,
                                                             const fbl::RefPtr<Device>& dev) {
  ZX_ASSERT(driver.composite);
  std::string name(driver.driver->libname.c_str());
  if (driver_index_composite_devices_.count(name) == 0) {
    std::unique_ptr<CompositeDevice> dev;
    zx_status_t status = CompositeDevice::CreateFromDriverIndex(driver, &dev);
    if (status != ZX_OK) {
      LOGF(ERROR, "%s: Failed to create CompositeDevice from DriverIndex: %s", __func__,
           zx_status_get_string(status));
      return status;
    }
    driver_index_composite_devices_[name] = std::move(dev);
  }

  auto& composite = driver_index_composite_devices_[name];
  zx_status_t status = composite->BindFragment(driver.composite->node, dev);
  if (status != ZX_OK) {
    LOGF(ERROR, "%s: Failed to BindFragment for '%s': %s", __func__, dev->name().data(),
         zx_status_get_string(status));
  }
  return status;
}
