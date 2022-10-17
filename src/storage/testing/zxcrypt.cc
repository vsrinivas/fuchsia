// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/testing/zxcrypt.h"

#include <fcntl.h>
#include <lib/syslog/cpp/macros.h>

#include <ramdevice-client/ramdisk.h>

#include "src/security/zxcrypt/client.h"
#include "src/storage/lib/utils/topological_path.h"

namespace storage {

zx::result<std::string> CreateZxcryptVolume(const std::string& device_path) {
  fbl::unique_fd fd(open(device_path.c_str(), O_RDWR));
  if (!fd) {
    FX_LOGS(ERROR) << "Could not open test block device";
    return zx::error(ZX_ERR_BAD_STATE);
  }
  fbl::unique_fd dev_fd(open("/dev", O_RDONLY));
  if (!dev_fd) {
    FX_LOGS(ERROR) << "Could not open /dev";
    return zx::error(ZX_ERR_BAD_STATE);
  }

  zxcrypt::VolumeManager volume_manager(std::move(fd), std::move(dev_fd));
  zx::channel driver_chan;
  auto status = zx::make_result(volume_manager.OpenClient(zx::sec(2), driver_chan));
  if (status.is_error()) {
    FX_LOGS(ERROR) << "Could not bind zxcrypt driver on " << device_path;
    return status.take_error();
  }

  zxcrypt::EncryptedVolumeClient volume(std::move(driver_chan));
  status = zx::make_result(volume.FormatWithImplicitKey(0));
  if (status.is_error()) {
    FX_LOGS(ERROR) << "Could not create test zxcrypt volume on " << device_path;
    return status.take_error();
  }

  status = zx::make_result(volume.UnsealWithImplicitKey(0));
  if (status.is_error()) {
    FX_LOGS(ERROR) << "Could not unseal test zxcrypt volume";
    return status.take_error();
  }

  auto topological_path_or = storage::GetTopologicalPath(device_path);
  if (topological_path_or.is_error()) {
    FX_LOGS(ERROR) << "Could not get topological path for " << device_path;
    return topological_path_or.take_error();
  }
  std::string zxcrypt_device_path = topological_path_or.value() + "/zxcrypt/unsealed/block";
  status = zx::make_result(wait_for_device(zxcrypt_device_path.c_str(), zx::sec(2).get()));
  if (status.is_error()) {
    FX_LOGS(ERROR) << "Test zxcrypt device never appeared at " << zxcrypt_device_path;
  }
  return zx::ok(std::move(zxcrypt_device_path));
}

}  // namespace storage
