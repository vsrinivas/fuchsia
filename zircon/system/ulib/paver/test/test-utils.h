// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef ZIRCON_SYSTEM_ULIB_PAVER_TEST_TEST_UTILS_H_
#define ZIRCON_SYSTEM_ULIB_PAVER_TEST_TEST_UTILS_H_
#include <lib/fidl-utils/bind.h>
#include <lib/fzl/vmo-mapper.h>

#include <memory>

#include <fbl/ref_ptr.h>
#include <fbl/unique_fd.h>
#include <ramdevice-client/ramdisk.h>
#include <ramdevice-client/ramnand.h>
#include <zxtest/zxtest.h>

#include "device-partitioner.h"

constexpr uint64_t kBlockSize = 0x1000;
constexpr uint32_t kBlockCount = 0x100;

constexpr uint32_t kOobSize = 8;
constexpr uint32_t kPageSize = 2048;
constexpr uint32_t kPagesPerBlock = 128;
constexpr uint32_t kSkipBlockSize = kPageSize * kPagesPerBlock;
constexpr uint32_t kNumBlocks = 40;

class BlockDevice {
 public:
  static void Create(const fbl::unique_fd& devfs_root, const uint8_t* guid,
                     std::unique_ptr<BlockDevice>* device);

  static void Create(const fbl::unique_fd& devfs_root, const uint8_t* guid, uint64_t block_count,
                     std::unique_ptr<BlockDevice>* device);

  static void Create(const fbl::unique_fd& devfs_root, const uint8_t* guid, uint64_t block_count,
                     uint32_t block_size, std::unique_ptr<BlockDevice>* device);

  ~BlockDevice() { ramdisk_destroy(client_); }

  // Does not transfer ownership of the file descriptor.
  int fd() const { return ramdisk_get_block_fd(client_); }

  // Block count and block size of this device.
  uint64_t block_count() const { return block_count_; }
  uint32_t block_size() const { return block_size_; }

 private:
  BlockDevice(ramdisk_client_t* client, uint64_t block_count, uint32_t block_size)
      : client_(client), block_count_(block_count), block_size_(block_size) {}

  ramdisk_client_t* client_;
  const uint64_t block_count_;
  const uint32_t block_size_;
};

class SkipBlockDevice {
 public:
  static void Create(const fuchsia_hardware_nand_RamNandInfo& nand_info,
                     std::unique_ptr<SkipBlockDevice>* device);

  fbl::unique_fd devfs_root() { return ctl_->devfs_root().duplicate(); }

  fzl::VmoMapper& mapper() { return mapper_; }

  ~SkipBlockDevice() = default;

 private:
  SkipBlockDevice(fbl::RefPtr<ramdevice_client::RamNandCtl> ctl, ramdevice_client::RamNand ram_nand,
                  fzl::VmoMapper mapper)
      : ctl_(std::move(ctl)), ram_nand_(std::move(ram_nand)), mapper_(std::move(mapper)) {}

  fbl::RefPtr<ramdevice_client::RamNandCtl> ctl_;
  ramdevice_client::RamNand ram_nand_;
  fzl::VmoMapper mapper_;
};

// Returns the relative topological path for a test device's channel.
std::string GetTopologicalPath(const zx::channel& channel);

// Dummy DevicePartition implementation meant to be used for testing. All functions are no-ops, i.e.
// they silently pass without doing anything. Tests can inherit from this class and override
// functions that are relevant for their test cases; this class provides an easy way to inherit from
// DevicePartitioner which is an abstract class.
class FakeDevicePartitioner : public paver::DevicePartitioner {
 public:
  bool IsFvmWithinFtl() const override { return false; }

  bool SupportsPartition(const paver::PartitionSpec& spec) const override { return true; }

  zx_status_t FindPartition(const paver::PartitionSpec& spec,
                            std::unique_ptr<paver::PartitionClient>* out_partition) const override {
    return ZX_OK;
  }

  zx_status_t FinalizePartition(const paver::PartitionSpec& spec) const override { return ZX_OK; }

  zx_status_t AddPartition(const paver::PartitionSpec& spec,
                           std::unique_ptr<paver::PartitionClient>* out_partition) const override {
    return ZX_OK;
  }

  zx_status_t WipeFvm() const override { return ZX_OK; }

  zx_status_t InitPartitionTables() const override { return ZX_OK; }

  zx_status_t WipePartitionTables() const override { return ZX_OK; }

  zx_status_t ValidatePayload(const paver::PartitionSpec& spec,
                              fbl::Span<const uint8_t> data) const override {
    return ZX_OK;
  }
};

// Defines a PartitionClient that reads and writes to a partition backed by a VMO in memory.
// Used for testing.
class FakePartitionClient : public paver::PartitionClient {
 public:
  FakePartitionClient(size_t block_count, size_t block_size = PAGE_SIZE);

  zx_status_t GetBlockSize(size_t* out_size);
  zx_status_t GetPartitionSize(size_t* out_size);
  zx_status_t Read(const zx::vmo& vmo, size_t size);
  zx_status_t Write(const zx::vmo& vmo, size_t vmo_size);
  zx_status_t Trim();
  zx_status_t Flush();
  zx::channel GetChannel();
  fbl::unique_fd block_fd();

 protected:
  zx::vmo partition_;
  size_t block_size_;
  size_t partition_size_;
};

#endif  // ZIRCON_SYSTEM_ULIB_PAVER_TEST_TEST_UTILS_H_
