// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file

#include "src/devices/bin/driver_manager/base_package_resolver.h"

#include "src/devices/bin/driver_manager/manifest_parser.h"
#include "src/devices/lib/log/log.h"
#include "src/zircon/lib/zircon/include/zircon/status.h"

namespace internal {

zx::status<std::unique_ptr<Driver>> BasePackageResolver::FetchDriver(
    const std::string& package_url) {
  auto result = GetPathFromUrl(package_url);
  if (result.is_error()) {
    LOGF(ERROR, "Failed to get path from '%s' %s", package_url.c_str(), result.status_string());
    return result.take_error();
  }
  Driver* driver = nullptr;
  DriverLoadCallback callback = [&driver](Driver* d, const char* version) mutable { driver = d; };
  load_driver(boot_args_, result.value().c_str(), std::move(callback));
  if (driver == nullptr) {
    LOGF(ERROR, "Failed to load driver: %s", result.value().c_str());
    return zx::error(ZX_ERR_INTERNAL);
  }

  auto base_path_result = GetBasePathFromUrl(package_url);
  if (result.is_error()) {
    return result.take_error();
  }

  int fd;
  if ((fd = open(base_path_result.value().c_str(), O_RDONLY, O_DIRECTORY)) < 0) {
    LOGF(ERROR, "Failed to open package dir: '%s'", base_path_result.value().c_str());
    return zx::error(ZX_ERR_INTERNAL);
  }
  driver->package_dir = fbl::unique_fd(fd);

  return zx::ok(std::unique_ptr<Driver>(driver));
}

}  // namespace internal
