// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/lib/paver/as370.h"

#include <gpt/gpt.h>

#include "src/storage/lib/paver/pave-logging.h"
#include "src/storage/lib/paver/utils.h"

namespace paver {

zx::status<std::unique_ptr<DevicePartitioner>> As370Partitioner::Initialize(
    fbl::unique_fd devfs_root) {
  zx::status<> status = IsBoard(devfs_root, "visalia");
  if (status.is_error() && (status = IsBoard(devfs_root, "as370")).is_error()) {
    return status.take_error();
  }
  LOG("Successfully initialized As370Partitioner Device Partitioner\n");

  std::unique_ptr<SkipBlockDevicePartitioner> skip_block(
      new SkipBlockDevicePartitioner(std::move(devfs_root)));
  return zx::ok(new As370Partitioner(std::move(skip_block)));
}

bool As370Partitioner::SupportsPartition(const PartitionSpec& spec) const {
  const PartitionSpec supported_specs[] = {
      PartitionSpec(paver::Partition::kBootloaderA), PartitionSpec(paver::Partition::kZirconA),
      PartitionSpec(paver::Partition::kZirconB), PartitionSpec(paver::Partition::kZirconR),
      PartitionSpec(paver::Partition::kFuchsiaVolumeManager)};

  for (const auto& supported : supported_specs) {
    if (SpecMatches(spec, supported)) {
      return true;
    }
  }

  return false;
}

zx::status<std::unique_ptr<PartitionClient>> As370Partitioner::AddPartition(
    const PartitionSpec& spec) const {
  ERROR("Cannot add partitions to an as370.\n");
  return zx::error(ZX_ERR_NOT_SUPPORTED);
}

zx::status<std::unique_ptr<PartitionClient>> As370Partitioner::FindPartition(
    const PartitionSpec& spec) const {
  if (!SupportsPartition(spec)) {
    ERROR("Unsupported partition %s\n", spec.ToString().c_str());
    return zx::error(ZX_ERR_NOT_SUPPORTED);
  }

  switch (spec.partition) {
    case Partition::kBootloaderA:
      return skip_block_->FindPartition(GUID_BOOTLOADER_VALUE);
    case Partition::kZirconA:
      return skip_block_->FindPartition(GUID_ZIRCON_A_VALUE);
    case Partition::kZirconB:
      return skip_block_->FindPartition(GUID_ZIRCON_B_VALUE);
    case Partition::kZirconR:
      return skip_block_->FindPartition(GUID_ZIRCON_R_VALUE);
    case Partition::kFuchsiaVolumeManager:
      return skip_block_->FindFvmPartition();
    default:
      ERROR("partition_type is invalid!\n");
      return zx::error(ZX_ERR_NOT_SUPPORTED);
  }
}

zx::status<> As370Partitioner::WipeFvm() const { return skip_block_->WipeFvm(); }

zx::status<> As370Partitioner::InitPartitionTables() const {
  return zx::error(ZX_ERR_NOT_SUPPORTED);
}

zx::status<> As370Partitioner::WipePartitionTables() const {
  return zx::error(ZX_ERR_NOT_SUPPORTED);
}

zx::status<> As370Partitioner::ValidatePayload(const PartitionSpec& spec,
                                               fbl::Span<const uint8_t> data) const {
  if (!SupportsPartition(spec)) {
    ERROR("Unsupported partition %s\n", spec.ToString().c_str());
    return zx::error(ZX_ERR_NOT_SUPPORTED);
  }

  return zx::ok();
}

zx::status<std::unique_ptr<DevicePartitioner>> As370PartitionerFactory::New(
    fbl::unique_fd devfs_root, const zx::channel& svc_root, Arch arch,
    std::shared_ptr<Context> context, const fbl::unique_fd& block_device) {
  return As370Partitioner::Initialize(std::move(devfs_root));
}

}  // namespace paver
