// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/fshost/nand-device.h"

#include <fcntl.h>
#include <fidl/fuchsia.device/cpp/wire.h>
#include <lib/fdio/cpp/caller.h>
#include <lib/syslog/cpp/macros.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <memory>

#include "src/storage/fshost/block-device.h"

namespace fshost {

zx::result<std::unique_ptr<BlockDeviceInterface>> NandDevice::OpenBlockDevice(
    const char* topological_path) const {
  fbl::unique_fd fd(::open(topological_path, O_RDWR, S_IFBLK));
  if (!fd) {
    FX_LOGS(WARNING) << "Failed to open block device " << topological_path << ": "
                     << strerror(errno);
    return zx::error(ZX_ERR_INVALID_ARGS);
  }
  return zx::ok(std::make_unique<NandDevice>(mounter_, std::move(fd), device_config_));
}

}  // namespace fshost
