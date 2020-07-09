// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flash_stress.h"

#include <lib/zx/status.h>
#include <lib/zx/vmo.h>

#include <fbl/unique_fd.h>
#include <fs-management/fvm.h>
#include <gtest/gtest.h>

#include "src/lib/isolated_devmgr/v2_component/fvm.h"
#include "src/lib/isolated_devmgr/v2_component/ram_disk.h"
#include "src/lib/testing/predicates/status.h"
#include "status.h"

namespace hwstress {
namespace {

constexpr size_t kBlockSize = 512;
constexpr size_t kDefaultRamDiskSize = 64 * 1024 * 1024;
constexpr size_t kDefaultFvmSliceSize = 1024 * 1024;

TEST(Flash, FlashStress) {
  // Create a RAM disk.
  zx::status<isolated_devmgr::RamDisk> ramdisk = isolated_devmgr::RamDisk::Create(
      /*block_size=*/kBlockSize, /*block_count=*/kDefaultRamDiskSize / kBlockSize);
  ASSERT_TRUE(ramdisk.is_ok());

  // Instantiate it as a FVM device.
  zx::status<std::string> fvm_path =
      isolated_devmgr::CreateFvmInstance(ramdisk->path(), kDefaultFvmSliceSize);
  ASSERT_TRUE(fvm_path.is_ok());

  StatusLine status;
  ASSERT_TRUE(StressFlash(&status, fvm_path.value(), /*bytes_to_test=*/16 * 1024 * 1024));
}

TEST(Flash, DeletePartition) {
  // Create a RAM disk.
  zx::status<isolated_devmgr::RamDisk> ramdisk = isolated_devmgr::RamDisk::Create(
      /*block_size=*/kBlockSize, /*block_count=*/kDefaultRamDiskSize / kBlockSize);
  ASSERT_TRUE(ramdisk.is_ok());

  // Instantiate it as a FVM device.
  zx::status<std::string> fvm_path =
      isolated_devmgr::CreateFvmInstance(ramdisk->path(), kDefaultFvmSliceSize);
  ASSERT_TRUE(fvm_path.is_ok());

  // Access FVM.
  fbl::unique_fd fvm_fd(open(fvm_path.value().c_str(), O_RDWR));
  ASSERT_TRUE(fvm_fd);

  alloc_req_t request{.slice_count = 1, .name = "test-fs"};
  memcpy(request.guid, uuid::Uuid::Generate().bytes(), sizeof(request.guid));
  memcpy(request.type, kTestPartGUID.bytes(), sizeof(request.type));

  // Create a partition.
  fbl::unique_fd fd(fvm_allocate_partition(fvm_fd.get(), &request));
  ASSERT_TRUE(fd);

  StatusLine status;
  DestroyFlashTestPartitions(&status);
  ASSERT_TRUE(open_partition(nullptr, kTestPartGUID.bytes(), 0, nullptr) != ZX_OK);
}

}  // namespace
}  // namespace hwstress
