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
    return result.take_error();
  }
  Driver* driver = nullptr;
  DriverLoadCallback callback = [&driver](Driver* d, const char* version) mutable { driver = d; };
  load_driver(boot_args_, result.value().c_str(), std::move(callback));
  if (driver == nullptr) {
    return zx::error(ZX_ERR_INTERNAL);
  }
  return zx::ok(std::unique_ptr<Driver>(driver));
}

}  // namespace internal
