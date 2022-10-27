// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/storage/fs_management/cpp/format.h"

#include <fcntl.h>
#include <lib/sys/component/cpp/service_client.h>
#include <lib/zx/vmo.h>

#include <fbl/unique_fd.h>
#include <gtest/gtest.h>

#include "src/storage/testing/ram_disk.h"

namespace fs_management {
namespace {

constexpr uint32_t kBlockSize = ZX_PAGE_SIZE;

constexpr uint8_t kGptMagic[] = {0x45, 0x46, 0x49, 0x20, 0x50, 0x41, 0x52, 0x54,
                                 0x00, 0x00, 0x01, 0x00, 0x5c, 0x00, 0x00, 0x00};

TEST(FormatDetectionTest, TestInvalidGptIgnored) {
  zx::vmo vmo;
  ASSERT_EQ(zx::vmo::create(2 * ZX_PAGE_SIZE, 0, &vmo), ZX_OK);
  vmo.write(kGptMagic, 0x200, sizeof(kGptMagic));
  auto ramdisk_or = storage::RamDisk::CreateWithVmo(std::move(vmo), kBlockSize);
  ASSERT_EQ(ramdisk_or.status_value(), ZX_OK);
  zx::result channel = component::Connect<fuchsia_hardware_block::Block>(ramdisk_or.value().path());
  ASSERT_TRUE(channel.is_ok()) << channel.status_string();
  ASSERT_EQ(DetectDiskFormat(channel.value()), kDiskFormatUnknown);
}

TEST(FormatDetectionTest, TestGptWithUnusualBlockSize) {
  zx::vmo vmo;
  ASSERT_EQ(zx::vmo::create(2 * ZX_PAGE_SIZE, 0, &vmo), ZX_OK);
  vmo.write(kGptMagic, ZX_PAGE_SIZE, sizeof(kGptMagic));
  auto ramdisk_or = storage::RamDisk::CreateWithVmo(std::move(vmo), kBlockSize);
  ASSERT_EQ(ramdisk_or.status_value(), ZX_OK);
  zx::result channel = component::Connect<fuchsia_hardware_block::Block>(ramdisk_or.value().path());
  ASSERT_TRUE(channel.is_ok()) << channel.status_string();
  ASSERT_EQ(DetectDiskFormat(channel.value()), kDiskFormatGpt);
}

TEST(FormatDetectionTest, TestVbmetaRecognised) {
  zx::vmo vmo;
  ASSERT_EQ(zx::vmo::create(2 * ZX_PAGE_SIZE, 0, &vmo), ZX_OK);

  // Write the vbmeta magic string at the start of the device.
  const unsigned char kVbmetaMagic[] = {'A', 'V', 'B', '0'};
  vmo.write(kVbmetaMagic, /*offset=*/0x0, sizeof(kVbmetaMagic));

  // Add the MBR magic string to the end of the first sector. These bytes in
  // vbmeta tend to be randomish, and previously we've had bugs where if these
  // bytes happened to match the MBR magic, we would misrecognise the partition.
  // (c.f. fxbug.dev/59374)
  const unsigned char kMbrMagic[] = {0x55, 0xaa};
  vmo.write(kMbrMagic, /*offset=*/510, sizeof(kMbrMagic));

  auto ramdisk_or = storage::RamDisk::CreateWithVmo(std::move(vmo), kBlockSize);
  ASSERT_EQ(ramdisk_or.status_value(), ZX_OK);
  zx::result channel = component::Connect<fuchsia_hardware_block::Block>(ramdisk_or.value().path());
  ASSERT_TRUE(channel.is_ok()) << channel.status_string();
  ASSERT_EQ(DetectDiskFormat(channel.value()), kDiskFormatVbmeta);
}

}  // namespace
}  // namespace fs_management
