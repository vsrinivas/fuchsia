// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/lib/paver/test/test-utils.h"

#include <fidl/fuchsia.device/cpp/wire.h>
#include <lib/fdio/directory.h>
#include <lib/zx/vmo.h>
#include <limits.h>
#include <zircon/boot/image.h>

#include <memory>
#include <optional>
#include <string_view>

#include <fbl/string.h>
#include <fbl/vector.h>
#include <zxtest/zxtest.h>

#include "src/storage/lib/paver/device-partitioner.h"

namespace {

void CreateBadBlockMap(void* buffer) {
  // Set all entries in first BBT to be good blocks.
  constexpr uint8_t kBlockGood = 0;
  memset(buffer, kBlockGood, kPageSize);

  struct OobMetadata {
    uint32_t magic;
    int16_t program_erase_cycles;
    uint16_t generation;
  };

  constexpr size_t oob_offset{static_cast<size_t>(kPageSize) * kPagesPerBlock * kNumBlocks};
  auto* oob = reinterpret_cast<OobMetadata*>(reinterpret_cast<uintptr_t>(buffer) + oob_offset);
  oob->magic = 0x7462626E;  // "nbbt"
  oob->program_erase_cycles = 0;
  oob->generation = 1;
}

}  // namespace

void BlockDevice::Create(const fbl::unique_fd& devfs_root, const uint8_t* guid,
                         std::unique_ptr<BlockDevice>* device) {
  ramdisk_client_t* client;
  ASSERT_OK(ramdisk_create_at_with_guid(devfs_root.get(), kBlockSize, kBlockCount, guid,
                                        ZBI_PARTITION_GUID_LEN, &client));
  device->reset(new BlockDevice(client, kBlockCount, kBlockSize));
}

void BlockDevice::Create(const fbl::unique_fd& devfs_root, const uint8_t* guid,
                         uint64_t block_count, std::unique_ptr<BlockDevice>* device) {
  ramdisk_client_t* client;
  ASSERT_OK(ramdisk_create_at_with_guid(devfs_root.get(), kBlockSize, block_count, guid,
                                        ZBI_PARTITION_GUID_LEN, &client));
  device->reset(new BlockDevice(client, block_count, kBlockSize));
}

void BlockDevice::Create(const fbl::unique_fd& devfs_root, const uint8_t* guid,
                         uint64_t block_count, uint32_t block_size,
                         std::unique_ptr<BlockDevice>* device) {
  ramdisk_client_t* client;
  ASSERT_OK(ramdisk_create_at_with_guid(devfs_root.get(), block_size, block_count, guid,
                                        ZBI_PARTITION_GUID_LEN, &client));
  device->reset(new BlockDevice(client, block_count, block_size));
}

void SkipBlockDevice::Create(const fuchsia_hardware_nand_RamNandInfo& nand_info,
                             std::unique_ptr<SkipBlockDevice>* device) {
  fzl::VmoMapper mapper;
  zx::vmo vmo;
  ASSERT_OK(
      mapper.CreateAndMap(static_cast<size_t>(kPageSize + kOobSize) * kPagesPerBlock * kNumBlocks,
                          ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, nullptr, &vmo));
  memset(mapper.start(), 0xff, mapper.size());
  CreateBadBlockMap(mapper.start());
  vmo.op_range(ZX_VMO_OP_CACHE_CLEAN_INVALIDATE, 0, mapper.size(), nullptr, 0);
  zx::vmo dup;
  ASSERT_OK(vmo.duplicate(ZX_RIGHT_SAME_RIGHTS, &dup));

  fuchsia_hardware_nand_RamNandInfo info = nand_info;
  info.vmo = dup.release();
  fbl::RefPtr<ramdevice_client_test::RamNandCtl> ctl;
  ASSERT_OK(ramdevice_client_test::RamNandCtl::Create(&ctl));
  std::optional<ramdevice_client::RamNand> ram_nand;
  ASSERT_OK(ctl->CreateRamNand(&info, &ram_nand));
  fbl::unique_fd fd;
  ASSERT_OK(device_watcher::RecursiveWaitForFile(ctl->devfs_root(), "sys/platform", &fd));
  device->reset(new SkipBlockDevice(std::move(ctl), *std::move(ram_nand), std::move(mapper)));
}

FakePartitionClient::FakePartitionClient(size_t block_count, size_t block_size)
    : block_size_(block_size) {
  partition_size_ = block_count * block_size;
  zx_status_t status = zx::vmo::create(partition_size_, ZX_VMO_RESIZABLE, &partition_);
  if (status != ZX_OK) {
    partition_size_ = 0;
  }
}

FakePartitionClient::FakePartitionClient(size_t block_count)
    : FakePartitionClient(block_count, zx_system_get_page_size()) {}

zx::result<size_t> FakePartitionClient::GetBlockSize() { return zx::ok(block_size_); }

zx::result<size_t> FakePartitionClient::GetPartitionSize() { return zx::ok(partition_size_); }

zx::result<> FakePartitionClient::Read(const zx::vmo& vmo, size_t size) {
  if (partition_size_ == 0) {
    return zx::ok();
  }

  fzl::VmoMapper mapper;
  if (auto status = mapper.Map(vmo, 0, size, ZX_VM_PERM_WRITE); status != ZX_OK) {
    return zx::error(status);
  }
  return zx::make_result(partition_.read(mapper.start(), 0, size));
}

zx::result<> FakePartitionClient::Write(const zx::vmo& vmo, size_t size) {
  if (size > partition_size_) {
    size_t new_size = fbl::round_up(size, block_size_);
    zx_status_t status = partition_.set_size(new_size);
    if (status != ZX_OK) {
      return zx::error(status);
    }
    partition_size_ = new_size;
  }

  fzl::VmoMapper mapper;
  if (auto status = mapper.Map(vmo, 0, size, ZX_VM_PERM_READ); status != ZX_OK) {
    return zx::error(status);
  }
  return zx::make_result(partition_.write(mapper.start(), 0, size));
}

zx::result<> FakePartitionClient::Trim() {
  zx_status_t status = partition_.set_size(0);
  if (status != ZX_OK) {
    return zx::error(status);
  }
  partition_size_ = 0;
  return zx::ok();
}

zx::result<> FakePartitionClient::Flush() { return zx::ok(); }

fidl::ClientEnd<fuchsia_hardware_block::Block> FakePartitionClient::GetChannel() { return {}; }

fbl::unique_fd FakePartitionClient::block_fd() { return fbl::unique_fd(); }
