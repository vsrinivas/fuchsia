// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "factory_reset.h"

#include <dirent.h>
#include <fcntl.h>
#include <lib/fdio/fdio.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/zx/channel.h>
#include <zircon/status.h>

#include "src/lib/storage/fs_management/cpp/mount.h"
#include "src/security/kms-stateless/kms-stateless.h"
#include "src/security/zxcrypt/client.h"

namespace factory_reset {

const char* kBlockPath = "class/block";

zx_status_t ShredBlockDevice(fbl::unique_fd fd, fbl::unique_fd devfs_root_fd) {
  zx_status_t status;
  zxcrypt::VolumeManager volume(std::move(fd), std::move(devfs_root_fd));

  // Note: the access to /dev/sys/platform from the manifest is load-bearing
  // here, because we can only find the related zxcrypt device for a particular
  // block device via appending "/zxcrypt" to its topological path, and the
  // canonical topological path sits under sys/platform.
  zx::channel driver_chan;
  status = volume.OpenClient(zx::sec(5), driver_chan);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Couldn't open channel to zxcrypt volume manager: " << status << " ("
                   << zx_status_get_string(status) << ")";
    return status;
  }

  zxcrypt::EncryptedVolumeClient zxc_manager(std::move(driver_chan));
  status = zxc_manager.Shred();
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Couldn't shred volume: " << status << " (" << zx_status_get_string(status)
                   << ")";
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
    FX_LOGS(ERROR) << "Error opening " << kBlockPath;
    return ZX_ERR_NOT_FOUND;
  }
  struct dirent* de;
  DIR* dir = fdopendir(block_dir.get());
  // Attempts to shred every zxcrypt volume found.
  while ((de = readdir(dir)) != nullptr) {
    fbl::unique_fd block_fd(openat(dirfd(dir), de->d_name, O_RDWR));
    if (!block_fd ||
        fs_management::DetectDiskFormat(block_fd.get()) != fs_management::kDiskFormatZxcrypt) {
      continue;
    }

    zx_status_t status = ShredBlockDevice(std::move(block_fd), dev_fd_.duplicate());
    if (status != ZX_OK) {
      FX_LOGS(ERROR) << "Error shredding " << de->d_name;
      closedir(dir);
      return status;
    } else {
      FX_LOGS(INFO) << "Successfully shredded " << de->d_name;
    }
  }
  closedir(dir);
  return ZX_OK;
}

void FactoryReset::Reset(ResetCallback callback) {
  zx_status_t status = Shred();
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Shred failed: " << status << " (" << zx_status_get_string(status) << ")";
    callback(std::move(status));
    return;
  }

  uint8_t key_info[kms_stateless::kExpectedKeyInfoSize] = "zxcrypt";
  status = kms_stateless::RotateHardwareDerivedKeyFromService(key_info);
  if (status == ZX_ERR_NOT_SUPPORTED) {
    FX_LOGS(ERROR)
        << "FactoryReset: The device does not support rotatable hardware keys. Ignoring.";
    status = ZX_OK;
  } else if (status != ZX_OK) {
    FX_LOGS(ERROR) << "FactoryReset: RotateHardwareDerivedKey() failed: " << status << " ("
                   << zx_status_get_string(status) << ")";
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
