// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zx/status.h>
#include <lib/zx/vmo.h>

#include <gtest/gtest.h>

#include "src/lib/isolated_devmgr/v2_component/fvm.h"
#include "src/lib/isolated_devmgr/v2_component/ram_disk.h"
#include "src/lib/testing/predicates/status.h"

namespace hwstress {
namespace {

constexpr size_t kBlockSize = 512;
constexpr size_t kDefaultRamDiskSize = 64 * 1024 * 1024;
constexpr size_t kDefaultFvmSliceSize = 1024 * 1024;

TEST(FlashStress, CreateDeletePartition) {
  // Create a RAM disk.
  zx::status<isolated_devmgr::RamDisk> ramdisk = isolated_devmgr::RamDisk::Create(
      /*block_size=*/kBlockSize, /*block_count=*/kDefaultRamDiskSize / kBlockSize);
  ASSERT_TRUE(ramdisk.is_ok());

  // Instantiate it as a FVM device.
  zx::status<std::string> fvm_path =
      isolated_devmgr::CreateFvmInstance(ramdisk->path(), kDefaultFvmSliceSize);
  ASSERT_TRUE(fvm_path.is_ok());

#if 0
  // TODO(smpham): Implement.
  StatusLine status;
  ASSERT_TRUE(StressFlash(&status, fvm_path.value()));
#endif
}

}  // namespace
}  // namespace hwstress
