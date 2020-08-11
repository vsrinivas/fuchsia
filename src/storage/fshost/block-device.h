// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_FSHOST_BLOCK_DEVICE_H_
#define SRC_STORAGE_FSHOST_BLOCK_DEVICE_H_

#include <zircon/types.h>

#include <memory>
#include <optional>
#include <string_view>

#include <fbl/algorithm.h>
#include <fbl/string_buffer.h>
#include <fs-management/mount.h>

#include "block-device-interface.h"
#include "filesystem-mounter.h"

namespace devmgr {

// A concrete implementation of the block device interface.
//
// Used by fshost to attach either drivers or filesystems to
// incoming block devices.
class BlockDevice final : public BlockDeviceInterface {
 public:
  BlockDevice(FilesystemMounter* mounter, fbl::unique_fd fd);

  disk_format_t GetFormat() final;
  void SetFormat(disk_format_t format) final;
  bool Netbooting() final;
  zx_status_t GetInfo(fuchsia_hardware_block_BlockInfo* out_info) final;
  zx_status_t GetTypeGUID(fuchsia_hardware_block_partition_GUID* out_guid) final;
  zx_status_t AttachDriver(const std::string_view& driver) final;
  zx_status_t UnsealZxcrypt() final;
  zx_status_t IsTopologicalPathSuffix(const std::string_view& path, bool* is_path) final;
  zx_status_t IsUnsealedZxcrypt(bool* is_unsealed_zxcrypt) final;
  zx_status_t FormatZxcrypt() final;
  bool ShouldCheckFilesystems() final;
  zx_status_t CheckFilesystem() final;
  zx_status_t FormatFilesystem() final;
  zx_status_t MountFilesystem() final;

 private:
  FilesystemMounter* mounter_ = nullptr;
  fbl::unique_fd fd_;
  std::optional<fuchsia_hardware_block_BlockInfo> info_ = {};
  disk_format_t format_ = DISK_FORMAT_UNKNOWN;
};

}  // namespace devmgr

#endif  // SRC_STORAGE_FSHOST_BLOCK_DEVICE_H_
