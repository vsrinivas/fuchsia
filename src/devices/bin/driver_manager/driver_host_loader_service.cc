// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/bin/driver_manager/driver_host_loader_service.h"

#include <zircon/errors.h>

#include "src/lib/files/path.h"

namespace {

static constexpr std::array<const char*, 3> kDriverAllowlist{
    "libasync-default.so",
    "libdriver.so",
    "libfdio.so",
};

// Check if the driver is in the allowlist.
bool InAllowlist(std::string path) {
  // path may have multiple path components, e.g. if loading the asan variant of a library, and
  // these should be allowed as long as the library name is in the allowlist.
  std::string base = files::GetBaseName(path);
  for (const char* entry : kDriverAllowlist) {
    if (base == entry) {
      return true;
    }
  }
  return false;
}

}  // namespace

// static
std::shared_ptr<DriverHostLoaderService> DriverHostLoaderService::Create(
    async_dispatcher_t* dispatcher, fbl::unique_fd lib_fd, std::string name) {
  // Can't use make_shared because constructor is private
  return std::shared_ptr<DriverHostLoaderService>(
      new DriverHostLoaderService(dispatcher, std::move(lib_fd), std::move(name)));
}

zx::status<zx::vmo> DriverHostLoaderService::LoadObjectImpl(std::string path) {
  if (!InAllowlist(path)) {
    return zx::error(ZX_ERR_ACCESS_DENIED);
  }
  return LoaderService::LoadObjectImpl(path);
}
