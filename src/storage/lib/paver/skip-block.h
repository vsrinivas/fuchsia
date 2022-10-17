// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef SRC_STORAGE_LIB_PAVER_SKIP_BLOCK_H_
#define SRC_STORAGE_LIB_PAVER_SKIP_BLOCK_H_

#include <fidl/fuchsia.hardware.skipblock/cpp/wire.h>

#include "src/lib/uuid/uuid.h"
#include "src/storage/lib/paver/device-partitioner.h"
#include "src/storage/lib/paver/partition-client.h"

namespace paver {

class SkipBlockPartitionClient;

// DevicePartitioner implementation for devices which have fixed partition maps, but do not expose a
// block device interface. Instead they expose devices with skip-block IOCTL interfaces. Like the
// FixedDevicePartitioner, it will not attempt to write a partition map of any kind to the device.
// Assumes standardized partition layout structure (e.g. ZIRCON-A, ZIRCON-B,
// ZIRCON-R).
class SkipBlockDevicePartitioner {
 public:
  explicit SkipBlockDevicePartitioner(fbl::unique_fd devfs_root)
      : devfs_root_(std::move(devfs_root)) {}

  zx::result<std::unique_ptr<SkipBlockPartitionClient>> FindPartition(const uuid::Uuid& type) const;

  zx::result<std::unique_ptr<PartitionClient>> FindFvmPartition() const;

  zx::result<> WipeFvm() const;

  fbl::unique_fd& devfs_root() { return devfs_root_; }

 private:
  fbl::unique_fd devfs_root_;
};

class SkipBlockPartitionClient : public PartitionClient {
 public:
  explicit SkipBlockPartitionClient(
      fidl::ClientEnd<fuchsia_hardware_skipblock::SkipBlock> partition)
      : partition_(std::move(partition)) {}

  zx::result<size_t> GetBlockSize() override;
  zx::result<size_t> GetPartitionSize() override;
  zx::result<> Read(const zx::vmo& vmo, size_t size) override;
  zx::result<> Write(const zx::vmo& vmo, size_t vmo_size) override;
  zx::result<> Trim() override;
  zx::result<> Flush() override;

  fidl::ClientEnd<fuchsia_hardware_skipblock::SkipBlock> GetChannel();

  fbl::unique_fd block_fd() override;

  // No copy, no move.
  SkipBlockPartitionClient(const SkipBlockPartitionClient&) = delete;
  SkipBlockPartitionClient& operator=(const SkipBlockPartitionClient&) = delete;
  SkipBlockPartitionClient(SkipBlockPartitionClient&&) = delete;
  SkipBlockPartitionClient& operator=(SkipBlockPartitionClient&&) = delete;

 protected:
  zx::result<> WriteBytes(const zx::vmo& vmo, zx_off_t offset, size_t vmo_size);

 private:
  zx::result<> ReadPartitionInfo();

  fidl::WireSyncClient<fuchsia_hardware_skipblock::SkipBlock> partition_;
  std::optional<fuchsia_hardware_skipblock::wire::PartitionInfo> partition_info_;
};

}  // namespace paver

#endif  // SRC_STORAGE_LIB_PAVER_SKIP_BLOCK_H_
