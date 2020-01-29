// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "test/test-utils.h"

#include <fuchsia/device/llcpp/fidl.h>
#include <fuchsia/hardware/nand/c/fidl.h>
#include <lib/fdio/directory.h>
#include <lib/zx/vmo.h>
#include <limits.h>
#include <zircon/boot/image.h>

#include <memory>
#include <optional>

#include <fbl/string.h>
#include <fbl/string_piece.h>
#include <fbl/vector.h>
#include <zxtest/zxtest.h>

#include "device-partitioner.h"

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

  const size_t oob_offset = kPageSize * kPagesPerBlock * kNumBlocks;
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
  device->reset(new BlockDevice(client));
}

void BlockDevice::Create(const fbl::unique_fd& devfs_root, const uint8_t* guid,
                         uint64_t block_count, std::unique_ptr<BlockDevice>* device) {
  ramdisk_client_t* client;
  ASSERT_OK(ramdisk_create_at_with_guid(devfs_root.get(), kBlockSize, block_count, guid,
                                        ZBI_PARTITION_GUID_LEN, &client));
  device->reset(new BlockDevice(client));
}

void BlockDevice::Create(const fbl::unique_fd& devfs_root, const uint8_t* guid,
                         uint64_t block_count, uint64_t block_size,
                         std::unique_ptr<BlockDevice>* device) {
  ramdisk_client_t* client;
  ASSERT_OK(ramdisk_create_at_with_guid(devfs_root.get(), block_size, block_count, guid,
                                        ZBI_PARTITION_GUID_LEN, &client));
  device->reset(new BlockDevice(client));
}

void SkipBlockDevice::Create(const fuchsia_hardware_nand_RamNandInfo& nand_info,
                             std::unique_ptr<SkipBlockDevice>* device) {
  fzl::VmoMapper mapper;
  zx::vmo vmo;
  ASSERT_OK(mapper.CreateAndMap((kPageSize + kOobSize) * kPagesPerBlock * kNumBlocks,
                                ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, nullptr, &vmo));
  memset(mapper.start(), 0xff, mapper.size());
  CreateBadBlockMap(mapper.start());
  vmo.op_range(ZX_VMO_OP_CACHE_CLEAN_INVALIDATE, 0, mapper.size(), nullptr, 0);
  zx::vmo dup;
  ASSERT_OK(vmo.duplicate(ZX_RIGHT_SAME_RIGHTS, &dup));

  fuchsia_hardware_nand_RamNandInfo info = nand_info;
  info.vmo = dup.release();
  fbl::RefPtr<ramdevice_client::RamNandCtl> ctl;
  ASSERT_OK(ramdevice_client::RamNandCtl::Create(&ctl));
  std::optional<ramdevice_client::RamNand> ram_nand;
  ASSERT_OK(ramdevice_client::RamNand::Create(ctl, &info, &ram_nand));
  fbl::unique_fd fd;
  ASSERT_OK(devmgr_integration_test::RecursiveWaitForFile(ctl->devfs_root(), "sys/platform", &fd));
  device->reset(new SkipBlockDevice(std::move(ctl), *std::move(ram_nand), std::move(mapper)));
}

std::string GetTopologicalPath(const zx::channel& channel) {
  std::string path;
  auto result =
      llcpp::fuchsia::device::Controller::Call::GetTopologicalPath(zx::unowned_channel(channel));
  if (!result.ok() || result->result.is_err()) {
    return path;
  }

  auto& raw_path = result->result.response().path;

  constexpr char kDevRoot[] = "/dev/";
  if (strncmp(raw_path.data(), kDevRoot, strlen(kDevRoot)) != 0) {
    return path;
  }

  path.assign(raw_path.data() + strlen(kDevRoot), raw_path.size() - strlen(kDevRoot));
  return path;
}
