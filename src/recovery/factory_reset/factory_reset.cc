// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "factory_reset.h"

#include <dirent.h>
#include <fcntl.h>
#include <fidl/fuchsia.hardware.block/cpp/wire.h>
#include <fuchsia/fs/cpp/fidl.h>
#include <lib/component/cpp/incoming/service_client.h>
#include <lib/fdio/cpp/caller.h>
#include <lib/fdio/fdio.h>
#include <lib/fit/defer.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/zx/channel.h>
#include <zircon/errors.h>
#include <zircon/status.h>

#include "src/lib/storage/block_client/cpp/remote_block_device.h"
#include "src/lib/storage/fs_management/cpp/format.h"
#include "src/security/lib/kms-stateless/kms-stateless.h"
#include "src/security/lib/zxcrypt/client.h"

namespace factory_reset {

const char* kBlockPath = "class/block";

zx_status_t ShredZxcryptDevice(fbl::unique_fd fd, fbl::unique_fd devfs_root_fd) {
  zxcrypt::VolumeManager volume(std::move(fd), std::move(devfs_root_fd));

  // Note: the access to /dev/sys/platform from the manifest is load-bearing
  // here, because we can only find the related zxcrypt device for a particular
  // block device via appending "/zxcrypt" to its topological path, and the
  // canonical topological path sits under sys/platform.
  zx::channel driver_chan;
  if (zx_status_t status = volume.OpenClient(zx::sec(5), driver_chan); status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "Couldn't open channel to zxcrypt volume manager";
    return status;
  }

  zxcrypt::EncryptedVolumeClient zxc_manager(std::move(driver_chan));
  if (zx_status_t status = zxc_manager.Shred(); status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "Couldn't shred volume";
    return status;
  }

  return ZX_OK;
}

zx_status_t ShredFxfsDevice(const fidl::ClientEnd<fuchsia_hardware_block::Block>& device) {
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
  const fidl::WireResult result = fidl::WireCall(device)->GetInfo();
  if (!result.ok()) {
    FX_PLOGS(ERROR, result.status()) << "Failed to fetch block size";
    return result.status();
  }
  const fidl::WireResponse response = result.value();
  if (zx_status_t status = response.status; status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "Failed to fetch block size";
    return status;
  }
  size_t block_size = response.info->block_size;
  std::unique_ptr<uint8_t[]> block = std::make_unique<uint8_t[]>(block_size);
  memset(block.get(), 0, block_size);
  for (off_t offset : {0L, 512L << 10}) {
    if (zx_status_t status =
            block_client::SingleWriteBytes(device, block.get(), block_size, offset);
        status != ZX_OK) {
      FX_PLOGS(ERROR, status) << "Failed to write to fxfs device at offset " << offset;
      return status;
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
  int fd = openat(dev_fd_.get(), kBlockPath, O_RDONLY);
  if (fd < 0) {
    FX_LOGS(ERROR) << "Error opening " << kBlockPath << ": " << strerror(errno);
    return ZX_ERR_NOT_FOUND;
  }
  DIR* dir = fdopendir(fd);
  fdio_cpp::UnownedFdioCaller caller(dirfd(dir));
  auto cleanup = fit::defer([dir]() { closedir(dir); });
  // Attempts to shred every zxcrypt volume found.
  while (true) {
    dirent* de = readdir(dir);
    if (de == nullptr) {
      return ZX_OK;
    }
    zx::result block =
        component::ConnectAt<fuchsia_hardware_block::Block>(caller.directory(), de->d_name);
    if (block.is_error()) {
      FX_PLOGS(WARNING, block.status_value()) << "Error opening " << de->d_name;
      continue;
    }
    zx_status_t status;
    switch (fs_management::DetectDiskFormat(block.value())) {
      case fs_management::kDiskFormatZxcrypt: {
        fbl::unique_fd block_fd;
        if (zx_status_t status = fdio_fd_create(block.value().TakeChannel().release(),
                                                block_fd.reset_and_get_address());
            status != ZX_OK) {
          FX_PLOGS(WARNING, status) << "Error creating file descriptor from " << de->d_name;
          continue;
        }

        status = ShredZxcryptDevice(std::move(block_fd), dev_fd_.duplicate());
        break;
      }
      case fs_management::kDiskFormatFxfs: {
        status = ShredFxfsDevice(block.value());
        break;
      }
      default:
        continue;
    }
    if (status != ZX_OK) {
      FX_PLOGS(ERROR, status) << "Error shredding " << de->d_name;
      return status;
    }
    FX_LOGS(INFO) << "Successfully shredded " << de->d_name;
  }
}

void FactoryReset::Reset(ResetCallback callback) {
  FX_LOGS(ERROR) << "Reset called. Starting shred";
  if (zx_status_t status = Shred(); status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "Shred failed";
    callback(status);
    return;
  }
  FX_LOGS(ERROR) << "Finished shred";

  uint8_t key_info[kms_stateless::kExpectedKeyInfoSize] = "zxcrypt";
  switch (zx_status_t status = kms_stateless::RotateHardwareDerivedKeyFromService(key_info);
          status) {
    case ZX_OK:
      break;
    case ZX_ERR_NOT_SUPPORTED:
      FX_LOGS(ERROR)
          << "FactoryReset: The device does not support rotatable hardware keys. Ignoring";
      break;
    default:
      FX_PLOGS(ERROR, status) << "FactoryReset: RotateHardwareDerivedKey() failed";
      callback(status);
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
