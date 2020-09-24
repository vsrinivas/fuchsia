// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fdio/directory.h>
#include <lib/zx/vmo.h>

#include <fs-management/format.h>
#include <ramdevice-client/ramdisk.h>
#include <zxtest/zxtest.h>

namespace {

static constexpr uint8_t kGptMagic[] = {0x45, 0x46, 0x49, 0x20, 0x50, 0x41, 0x52, 0x54,
                                        0x00, 0x00, 0x01, 0x00, 0x5c, 0x00, 0x00, 0x00};

void CreateEmptyRamdisk(zx::vmo vmo, ramdisk_client **client) {
  zx::channel local, remote;
  ASSERT_OK(zx::channel::create(0, &local, &remote));
  ASSERT_OK(
      fdio_service_connect("/svc/fuchsia.fsmanagement.devmgr.IsolatedDevmgr", remote.release()));
  int fd;
  fdio_fd_create(local.release(), &fd);

  ASSERT_OK(ramdisk_create_at_from_vmo(fd, vmo.release(), client));
}

TEST(FormatDetectionTest, TestInvalidGptIgnored) {
  ramdisk_client *client;
  zx::vmo vmo;
  ASSERT_OK(zx::vmo::create(2 * ZX_PAGE_SIZE, 0, &vmo));
  vmo.write(kGptMagic, 0x200, sizeof(kGptMagic));
  ASSERT_NO_FATAL_FAILURES(CreateEmptyRamdisk(std::move(vmo), &client));
  int fd = ramdisk_get_block_fd(client);
  ASSERT_EQ(detect_disk_format(fd), DISK_FORMAT_UNKNOWN);
  ASSERT_EQ(ramdisk_destroy(client), ZX_OK);
}

TEST(FormatDetectionTest, TestGptWithUnusualBlockSize) {
  ramdisk_client *client;
  zx::vmo vmo;
  ASSERT_OK(zx::vmo::create(2 * ZX_PAGE_SIZE, 0, &vmo));
  vmo.write(kGptMagic, ZX_PAGE_SIZE, sizeof(kGptMagic));
  ASSERT_NO_FATAL_FAILURES(CreateEmptyRamdisk(std::move(vmo), &client));

  int fd = ramdisk_get_block_fd(client);
  ASSERT_EQ(detect_disk_format(fd), DISK_FORMAT_GPT);
  ASSERT_EQ(ramdisk_destroy(client), ZX_OK);
}

TEST(FormatDetectionTest, TestVbmetaRecognised) {
  ramdisk_client *client;
  zx::vmo vmo;
  ASSERT_OK(zx::vmo::create(2 * ZX_PAGE_SIZE, 0, &vmo));

  // Write the vbmeta magic string at the start of the device.
  const unsigned char kVbmetaMagic[] = {'A','V','B','0'};
  vmo.write(kVbmetaMagic, /*offset=*/0x0, sizeof(kVbmetaMagic));

  // Add the MBR magic string to the end of the first sector. These bytes in
  // vbmeta tend to be randomish, and previously we've had bugs where if these
  // bytes happened to match the MBR magic, we would misrecognise the partition.
  // (c.f. fxbug.dev/59374)
  const unsigned char kMbrMagic[] = {0x55, 0xaa};
  vmo.write(kMbrMagic, /*offset=*/510, sizeof(kMbrMagic));

  ASSERT_NO_FATAL_FAILURES(CreateEmptyRamdisk(std::move(vmo), &client));
  int fd = ramdisk_get_block_fd(client);
  ASSERT_EQ(detect_disk_format(fd), DISK_FORMAT_VBMETA);
  ASSERT_EQ(ramdisk_destroy(client), ZX_OK);
}

}  // namespace
