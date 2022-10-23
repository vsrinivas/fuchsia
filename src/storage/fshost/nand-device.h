// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_FSHOST_NAND_DEVICE_H_
#define SRC_STORAGE_FSHOST_NAND_DEVICE_H_

#include <fidl/fuchsia.io/cpp/wire.h>
#include <lib/zx/result.h>
#include <zircon/types.h>

#include <memory>
#include <optional>
#include <string_view>

#include <fbl/algorithm.h>
#include <fbl/string_buffer.h>

#include "src/lib/storage/fs_management/cpp/mount.h"
#include "src/storage/fshost/block-device.h"
#include "src/storage/fshost/filesystem-mounter.h"
#include "src/storage/fshost/fshost_config.h"

namespace fshost {

// A concrete implementation of the block device interface for NAND devices.
class NandDevice : public BlockDevice {
 public:
  NandDevice(FilesystemMounter* mounter, fbl::unique_fd fd,
             const fshost_config::Config* device_config)
      : BlockDevice(mounter, std::move(fd), device_config) {}
  NandDevice(const NandDevice&) = delete;
  NandDevice& operator=(const NandDevice&) = delete;

  zx::result<std::unique_ptr<BlockDeviceInterface>> OpenBlockDevice(
      const char* topological_path) const override;
  fs_management::DiskFormat content_format() const override {
    return fs_management::DiskFormat::kDiskFormatUnknown;
  }
  zx::result<fuchsia_hardware_block::wire::BlockInfo> GetInfo() const override {
    return zx::error(ZX_ERR_NOT_SUPPORTED);
  }
  bool IsNand() const override { return true; }
  bool IsRamDisk() const override { return false; }

 private:
  fbl::unique_fd fd_;
};

}  // namespace fshost

#endif  // SRC_STORAGE_FSHOST_NAND_DEVICE_H_
