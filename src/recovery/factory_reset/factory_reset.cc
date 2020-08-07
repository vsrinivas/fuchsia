// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "factory_reset.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <fuchsia/device/c/fidl.h>
#include <fuchsia/hardware/block/c/fidl.h>
#include <fuchsia/hardware/block/encrypted/c/fidl.h>
#include <fuchsia/sysinfo/c/fidl.h>
#include <lib/fdio/cpp/caller.h>
#include <lib/fdio/fdio.h>
#include <lib/zx/channel.h>
#include <limits.h>
#include <stdio.h>
#include <unistd.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/system.h>
#include <zxcrypt/fdio-volume.h>

#include <fbl/string_buffer.h>
#include <fbl/string_piece.h>
#include <fs-management/mount.h>

namespace factory_reset {

const char* kBlockPath = "class/block";

zx_status_t ShredBlockDevice(fbl::unique_fd fd, fbl::unique_fd devfs_root_fd) {
  std::unique_ptr<zxcrypt::FdioVolume> volume;
  zx_status_t status;
  status = zxcrypt::FdioVolume::Init(std::move(fd), std::move(devfs_root_fd), &volume);
  if (status != ZX_OK) {
    fprintf(stderr, "Couldn't init FdioVolume: %d (%s)\n", status, zx_status_get_string(status));
    return status;
  }

  // Note: the access to /dev/sys/platform from the manifest is load-bearing
  // here, because we can only find the related zxcrypt device for a particular
  // block device via appending "/zxcrypt" to its topological path, and the
  // canonical topological path sits under sys/platform.
  zx::channel driver_chan;
  status = volume->OpenManager(zx::sec(5), driver_chan.reset_and_get_address());
  if (status != ZX_OK) {
    fprintf(stderr, "Couldn't open channel to zxcrypt volume manager: %d (%s)\n",
            status, zx_status_get_string(status));
    return status;
  }

  zxcrypt::FdioVolumeManager zxc_manager(std::move(driver_chan));
  status = zxc_manager.Shred();
  if (status != ZX_OK) {
    fprintf(stderr, "Couldn't shred volume: %d (%s)\n", status, zx_status_get_string(status));
    return status;
  }

  return ZX_OK;
}

FactoryReset::FactoryReset(fbl::unique_fd dev_fd,
                           fuchsia::hardware::power::statecontrol::AdminPtr admin) {
  dev_fd_ = std::move(dev_fd);
  admin_ = std::move(admin);
}

zx_status_t FactoryReset::Shred() const {
  fbl::unique_fd block_dir(openat(dev_fd_.get(), kBlockPath, O_RDONLY | O_DIRECTORY));
  if (!block_dir) {
    fprintf(stderr, "Error opening %s\n", kBlockPath);
    return ZX_ERR_NOT_FOUND;
  }
  struct dirent* de;
  DIR* dir = fdopendir(block_dir.get());
  // Attempts to shred every zxcrypt volume found.
  while ((de = readdir(dir)) != nullptr) {
    fbl::unique_fd block_fd(openat(dirfd(dir), de->d_name, O_RDWR));
    if (!block_fd || detect_disk_format(block_fd.get()) != DISK_FORMAT_ZXCRYPT) {
      continue;
    }

    zx_status_t status = ShredBlockDevice(std::move(block_fd), dev_fd_.duplicate());
    if (status != ZX_OK) {
      closedir(dir);
      return status;
    }
  }
  closedir(dir);
  return ZX_OK;
}

void FactoryReset::Reset(ResetCallback callback) {
  zx_status_t status = Shred();
  if (status != ZX_OK) {
    fprintf(stderr, "FactoryReset: Shred failed: %d (%s)\n", status, zx_status_get_string(status));
    callback(std::move(status));
    return;
  }

  // Reboot to initiate the recovery.
  admin_->Reboot(fuchsia::hardware::power::statecontrol::RebootReason::FACTORY_DATA_RESET,
                 [callback{std::move(callback)}](
                     fuchsia::hardware::power::statecontrol::Admin_Reboot_Result status) {
                   if (status.is_err()) {
                     callback(status.err());
                   } else {
                     callback(ZX_OK);
                   }
                 });
}

}  // namespace factory_reset
