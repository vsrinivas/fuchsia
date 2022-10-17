// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "factory_reset.h"

#include <dirent.h>
#include <fcntl.h>
#include <fidl/fuchsia.hardware.block/cpp/wire.h>
#include <fuchsia/fs/cpp/fidl.h>
#include <fuchsia/hardware/block/c/fidl.h>
#include <lib/fdio/fdio.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/zx/channel.h>
#include <zircon/status.h>

#include "lib/fdio/cpp/caller.h"
#include "src/lib/storage/fs_management/cpp/format.h"
#include "src/lib/storage/fs_management/cpp/mount.h"
#include "src/security/kms-stateless/kms-stateless.h"
#include "src/security/zxcrypt/client.h"

namespace factory_reset {

const char* kBlockPath = "class/block";

zx_status_t ShredZxcryptDevice(fbl::unique_fd fd, fbl::unique_fd devfs_root_fd) {
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

zx_status_t ShredFxfsDevice(fbl::unique_fd fd) {
  // Overwrite the magic bytes of both superblocks.
  //
  // Note: This may occasionally be racy. Superblocks may be writen after
  // the overwrite below but before reboot. When we move this to fshost, we
  // will have access to the running filesystem and can wait for shutdown with
  // something like:
  //   fdio_cpp::FdioCaller caller(std::move(fd));
  //   if (zx::result<> status = fs_management::Shutdown(caller.directory()); !status.is_ok()) {
  //     return status.error_value();
  //   }
  // TODO(https://fxbug.dev/98889): Perform secure erase once we have keybag support.
  zx_status_t call_status;
  fdio_cpp::UnownedFdioCaller caller(fd.get());
  fuchsia_hardware_block_BlockInfo block_info;
  if (auto status =
          fuchsia_hardware_block_BlockGetInfo(caller.borrow_channel(), &call_status, &block_info);
      status != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to fetch block size.";
    return status;
  }
  ssize_t block_size = block_info.block_size;
  std::unique_ptr<uint8_t[]> block = std::make_unique<uint8_t[]>(block_size);
  memset(block.get(), 0, block_size);
  for (off_t offset : {0L, 512L << 10}) {
    if (auto status = ::lseek(fd.get(), offset, SEEK_SET); status < 0) {
      FX_LOGS(ERROR) << "Seek on fxfs device shred failed.";
      return ZX_ERR_IO;
    }
    if (auto status = ::write(fd.get(), block.get(), block_size); status < 0) {
      FX_LOGS(ERROR) << "Write to fxfs device at offset " << offset << " failed:" << status;
      return ZX_ERR_IO;
    }
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
    if (!block_fd) {
      continue;
    }

    zx_status_t status = ZX_OK;
    switch (fs_management::DetectDiskFormat(block_fd.get())) {
      case fs_management::kDiskFormatZxcrypt: {
        status = ShredZxcryptDevice(std::move(block_fd), dev_fd_.duplicate());
        break;
      }
      case fs_management::kDiskFormatFxfs: {
        status = ShredFxfsDevice(std::move(block_fd));
        break;
      }
      default:
        continue;
    }
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
  FX_LOGS(ERROR) << "Reset called. Starting shred.";
  zx_status_t status = Shred();
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Shred failed: " << status << " (" << zx_status_get_string(status) << ")";
    callback(std::move(status));
    return;
  }
  FX_LOGS(ERROR) << "Finished shred.";

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
  FX_LOGS(ERROR) << "Requesting reboot...";
  admin_->Reboot(fuchsia::hardware::power::statecontrol::RebootReason::FACTORY_DATA_RESET,
                 [callback{std::move(callback)}](
                     fuchsia::hardware::power::statecontrol::Admin_Reboot_Result status) {
                   if (status.is_err()) {
                     FX_LOGS(ERROR) << "Reboot call failed: " << status.err();
                     callback(status.err());
                   } else {
                     callback(ZX_OK);
                   }
                 });
}

}  // namespace factory_reset
