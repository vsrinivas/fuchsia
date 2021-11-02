// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "avb.h"

#include <lib/efi/testing/fake_disk_io_protocol.h>
#include <lib/efi/testing/stub_boot_services.h>
#include <lib/zbi/zbi.h>
#include <zircon/boot/image.h>
#include <zircon/hw/gpt.h>

#include <efi/boot-services.h>
#include <efi/protocol/block-io.h>
#include <efi/protocol/device-path.h>
#include <efi/protocol/disk-io.h>
#include <efi/protocol/loaded-image.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "avb_test_data.h"
#include "diskio_test.h"

namespace gigaboot {

namespace {

using testing::NiceMock;

std::vector<gpt_entry_t> AbrTestPartitions() {
  return std::vector<gpt_entry_t>{
      kVbmetaAGptEntry,
      kZirconAGptEntry,
  };
}

zbi_result_t check_has_image_args(zbi_header_t* hdr, void* payload, void* cookie) {
  bool* found = static_cast<bool*>(cookie);
  if (hdr->type == ZBI_TYPE_IMAGE_ARGS) {
    *found = true;
  }
  return ZBI_RESULT_OK;
}

#define ZBI_SIZE (16 * 1024)
static_assert(ZBI_SIZE % ZBI_ALIGNMENT == 0, "ZBI_SIZE must align to ZBI_ALIGNMENT");
TEST(AvbTest, LoadsVbmeta) {
  NiceMock<efi::MockBootServices> mock_services;
  efi::FakeDiskIoProtocol fake_disk;

  // Set up an empty ZBI to use.
  // We can't have unique_ptr to void, so just use uint8_t.
  std::vector<uint8_t> zbi(ZBI_SIZE);
  zbi_init(zbi.data(), ZBI_SIZE);
  // Check that the boot items aren't present.
  bool found = false;
  zbi_for_each(zbi.data(), check_has_image_args, &found);
  ASSERT_EQ(found, false);

  auto state = SetupBootDisk(mock_services, fake_disk.protocol());
  ASSERT_NO_FATAL_FAILURE(SetupDiskPartitions(fake_disk, AbrTestPartitions()));

  // Populate the vbmeta_a partition on our fake disk.
  std::vector<uint8_t>& contents = fake_disk.contents(kBootMediaId);
  memcpy(&contents[kBootMediaBlockSize * kVbmetaAGptEntry.first], test_vbmeta, test_vbmeta_len);

  efi_system_table system_table = {.BootServices = mock_services.services()};
  append_avb_zbi_items(ImageHandle(), &system_table, zbi.data(), ZBI_SIZE, "-a");

  // Check that the boot items were added.
  zbi_for_each(zbi.data(), check_has_image_args, &found);
  ASSERT_EQ(found, true);
}

TEST(AvbTest, DoesntCrashWithNoVbmeta) {
  NiceMock<efi::MockBootServices> mock_services;
  efi::FakeDiskIoProtocol fake_disk;

  // Set up an empty ZBI to use.
  // We can't have unique_ptr to void, so just use uint8_t.
  std::vector<uint8_t> zbi(ZBI_SIZE);
  zbi_init(zbi.data(), ZBI_SIZE);
  // Check that the boot items aren't present.
  bool found = false;
  zbi_for_each(zbi.data(), check_has_image_args, &found);
  ASSERT_EQ(found, false);

  auto state = SetupBootDisk(mock_services, fake_disk.protocol());
  ASSERT_NO_FATAL_FAILURE(SetupDiskPartitions(fake_disk, AbrTestPartitions()));
  efi_system_table system_table = {.BootServices = mock_services.services()};
  append_avb_zbi_items(ImageHandle(), &system_table, zbi.data(), ZBI_SIZE, "-a");

  // Check that the boot items are still not present.
  zbi_for_each(zbi.data(), check_has_image_args, &found);
  ASSERT_EQ(found, false);
}

}  // namespace
}  // namespace gigaboot
