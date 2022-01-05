// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/bin/driver_manager/v1/firmware_loader.h"

#include <fcntl.h>

namespace {

constexpr char kBootFirmwarePath[] = "lib/firmware";
constexpr char kSystemFirmwarePath[] = "/system/lib/firmware";
constexpr char kSystemPrefix[] = "/system/";

zx_status_t LoadFirmwareAtPath(int fd, const char* path, zx::vmo* vmo, size_t* size) {
  fbl::unique_fd firmware_fd(openat(fd, path, O_RDONLY));
  if (firmware_fd.get() < 0) {
    return (errno != ENOENT) ? ZX_ERR_IO : ZX_ERR_NOT_FOUND;
  }

  *size = lseek(firmware_fd.get(), 0, SEEK_END);
  return fdio_get_vmo_clone(firmware_fd.get(), vmo->reset_and_get_address());
}

}  // namespace

FirmwareLoader::FirmwareLoader(Coordinator* coordinator, async_dispatcher_t* firmware_dispatcher,
                               std::string path_prefix)
    : coordinator_(coordinator),
      firmware_dispatcher_(firmware_dispatcher),
      path_prefix_(path_prefix) {}

void FirmwareLoader::LoadFirmware(const fbl::RefPtr<Device>& dev, const char* driver_libname,
                                  const char* path,
                                  fit::callback<void(zx::status<LoadFirmwareResult>)> cb) {
  const std::string fwdirs[] = {
      path_prefix_ + kBootFirmwarePath,
      kSystemFirmwarePath,
  };

  // Must be a relative path and no funny business.
  if (path[0] == '/' || path[0] == '.') {
    cb(zx::error(ZX_ERR_INVALID_ARGS));
    return;
  }

  // This is done ahead of time as it is not thread-safe.
  const Driver* driver = coordinator_->LibnameToDriver(driver_libname);
  fbl::unique_fd package_dir;
  if (driver != nullptr && driver->package_dir.is_valid()) {
    package_dir = driver->package_dir.duplicate();
  }

  bool is_system = strncmp(driver_libname, kSystemPrefix, std::size(kSystemPrefix) - 1) == 0;

  // This must occur in a separate thread as fdio operations may block when accessing /system or
  // /pkg, possibly deadlocking the system. See http://fxbug.dev/87127 for more context.
  async::PostTask(firmware_dispatcher_, [dev = std::move(dev), path = std::string(path),
                                         package_dir = std::move(package_dir), is_system, fwdirs,
                                         cb = std::move(cb)]() mutable {
    // We are only going to check /system/ if the driver was loaded out of /system.
    // This ensures that /system is available and loaded, as otherwise touching /system
    // will wait, potentially forever.
    size_t directories_to_check = 1;
    if (is_system) {
      directories_to_check = std::size(fwdirs);
    }

    zx::vmo vmo;
    size_t size;
    for (unsigned n = 0; n < directories_to_check; n++) {
      fbl::unique_fd fd(open(fwdirs[n].c_str(), O_RDONLY, O_DIRECTORY));
      if (fd.get() < 0) {
        continue;
      }

      zx_status_t status = LoadFirmwareAtPath(fd.get(), path.c_str(), &vmo, &size);
      if (status == ZX_OK) {
        cb(zx::ok(LoadFirmwareResult{std::move(vmo), size}));
        return;
      }
      if (status != ZX_ERR_NOT_FOUND) {
        cb(zx::error(status));
        return;
      }
    }

    if (!package_dir) {
      cb(zx::error(ZX_ERR_NOT_FOUND));
      return;
    }

    auto package_path = std::string("lib/firmware/") + path;
    zx_status_t status = LoadFirmwareAtPath(package_dir.get(), package_path.c_str(), &vmo, &size);
    if (status == ZX_OK) {
      cb(zx::ok(LoadFirmwareResult{std::move(vmo), size}));
      return;
    }

    cb(zx::error(status));
  });
}
