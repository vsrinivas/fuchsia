// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_FSHOST_BLOCK_DEVICE_H_
#define SRC_STORAGE_FSHOST_BLOCK_DEVICE_H_

#include <fidl/fuchsia.io/cpp/wire.h>
#include <lib/zx/status.h>
#include <zircon/types.h>

#include <memory>
#include <optional>
#include <string_view>

#include <fbl/algorithm.h>
#include <fbl/string_buffer.h>
#include <fs-management/mount.h>

#include "src/storage/fshost/block-device-interface.h"
#include "src/storage/fshost/config.h"
#include "src/storage/fshost/filesystem-mounter.h"

namespace fshost {

// Get the topological path of the device backing |fd|.
std::string GetTopologicalPath(int fd);

// A concrete implementation of the block device interface.
//
// Used by fshost to attach either drivers or filesystems to incoming block devices.
class BlockDevice : public BlockDeviceInterface {
 public:
  BlockDevice(FilesystemMounter* mounter, fbl::unique_fd fd, const Config* device_config);
  BlockDevice(const BlockDevice&) = delete;
  BlockDevice& operator=(const BlockDevice&) = delete;

  disk_format_t GetFormat() override;
  void SetFormat(disk_format_t format) override;
  zx_status_t GetInfo(fuchsia_hardware_block_BlockInfo* out_info) const override;
  const fuchsia_hardware_block_partition_GUID& GetInstanceGuid() const override;
  const fuchsia_hardware_block_partition_GUID& GetTypeGuid() const override;
  zx_status_t AttachDriver(const std::string_view& driver) override;
  zx_status_t UnsealZxcrypt() override;
  zx_status_t FormatZxcrypt() override;
  bool ShouldCheckFilesystems() override;
  zx_status_t CheckFilesystem() override;
  zx_status_t FormatFilesystem() override;
  zx_status_t MountFilesystem() override;
  zx::status<std::string> VeritySeal() override;
  zx_status_t OpenBlockVerityForVerifiedRead(std::string seal_hex) override;
  bool ShouldAllowAuthoringFactory() override;
  zx_status_t SetPartitionMaxSize(const std::string& fvm_path, uint64_t max_size) override;
  bool IsNand() const override { return false; }

  disk_format_t content_format() const override;
  const std::string& topological_path() const override { return topological_path_; }
  const std::string& partition_name() const override;
  zx::status<fidl::ClientEnd<fuchsia_io::Node>> GetDeviceEndPoint() const;
  zx_status_t CheckCustomFilesystem(const std::string& binary_path) const;
  zx_status_t FormatCustomFilesystem(const std::string& binary_path) const;

 private:
  FilesystemMounter* mounter_ = nullptr;
  fbl::unique_fd fd_;
  const Config* device_config_;
  mutable std::optional<fuchsia_hardware_block_BlockInfo> info_;
  mutable std::optional<disk_format_t> content_format_;
  disk_format_t format_ = DISK_FORMAT_UNKNOWN;
  std::string topological_path_;
  mutable std::string partition_name_;
  mutable std::optional<fuchsia_hardware_block_partition_GUID> instance_guid_;
  mutable std::optional<fuchsia_hardware_block_partition_GUID> type_guid_;
};

}  // namespace fshost

#endif  // SRC_STORAGE_FSHOST_BLOCK_DEVICE_H_
