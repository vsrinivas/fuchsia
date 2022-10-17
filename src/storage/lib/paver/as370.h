// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef SRC_STORAGE_LIB_PAVER_AS370_H_
#define SRC_STORAGE_LIB_PAVER_AS370_H_

#include "src/storage/lib/paver/skip-block.h"

namespace paver {

class As370Partitioner : public DevicePartitioner {
 public:
  static zx::result<std::unique_ptr<DevicePartitioner>> Initialize(fbl::unique_fd devfs_root);

  bool IsFvmWithinFtl() const override { return true; }

  bool SupportsPartition(const PartitionSpec& spec) const override;

  zx::result<std::unique_ptr<PartitionClient>> AddPartition(
      const PartitionSpec& spec) const override;

  zx::result<std::unique_ptr<PartitionClient>> FindPartition(
      const PartitionSpec& spec) const override;

  zx::result<> FinalizePartition(const PartitionSpec& spec) const override { return zx::ok(); }

  zx::result<> WipeFvm() const override;

  zx::result<> InitPartitionTables() const override;

  zx::result<> WipePartitionTables() const override;

  zx::result<> ValidatePayload(const PartitionSpec& spec,
                               cpp20::span<const uint8_t> data) const override;

  zx::result<> Flush() const override { return zx::ok(); }

 private:
  As370Partitioner(std::unique_ptr<SkipBlockDevicePartitioner> skip_block)
      : skip_block_(std::move(skip_block)) {}

  std::unique_ptr<SkipBlockDevicePartitioner> skip_block_;
};

class As370PartitionerFactory : public DevicePartitionerFactory {
 public:
  zx::result<std::unique_ptr<DevicePartitioner>> New(
      fbl::unique_fd devfs_root, fidl::UnownedClientEnd<fuchsia_io::Directory> svc_root, Arch arch,
      std::shared_ptr<Context> context, const fbl::unique_fd& block_device) final;
};

}  // namespace paver

#endif  // SRC_STORAGE_LIB_PAVER_AS370_H_
