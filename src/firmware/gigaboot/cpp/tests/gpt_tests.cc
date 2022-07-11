// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include <efi/types.h>
#include <gtest/gtest.h>

#include "gpt.h"
#include "mock_boot_service.h"
#include "src/lib/utf_conversion/utf_conversion.h"
#include "utils.h"

efi_loaded_image_protocol* gEfiLoadedImage = nullptr;
efi_system_table* gEfiSystemTable = nullptr;
efi_handle gEfiImageHandle;

namespace gigaboot {
namespace {

void SetGptEntryName(const char* name, gpt_entry_t& entry) {
  size_t dst_len = sizeof(entry.name) / sizeof(uint16_t);
  utf8_to_utf16(reinterpret_cast<const uint8_t*>(name), strlen(name),
                reinterpret_cast<uint16_t*>(entry.name), &dst_len);
}

TEST(GigabootTest, FindEfiGptDevice) {
  MockStubService stub_service;
  Device image_device({"path-A", "path-B", "path-C", "image"});
  BlockDevice block_device({"path-A", "path-B", "path-C"}, 1024);
  auto cleanup = SetupEfiGlobalState(stub_service, image_device);

  stub_service.AddDevice(&image_device);
  stub_service.AddDevice(&block_device);

  auto res = FindEfiGptDevice();
  ASSERT_TRUE(res.is_ok());
}

TEST(GigabootTest, FindEfiGptDeviceNoMatchingDevicePath) {
  MockStubService stub_service;
  Device image_device({"path-A", "path-B", "path-C", "image"});
  BlockDevice block_device({"path-A", "path-B", "path-D"}, 1024);
  auto cleanup = SetupEfiGlobalState(stub_service, image_device);

  stub_service.AddDevice(&image_device);
  stub_service.AddDevice(&block_device);

  // The device path doesn't match. Should fail.
  auto res = FindEfiGptDevice();
  ASSERT_TRUE(res.is_error());
}

TEST(GigabootTest, FindEfiGptDeviceIgnoreLogicalPartition) {
  MockStubService stub_service;
  Device image_device({"path-A", "path-B", "path-C", "image"});
  BlockDevice block_device({"path-A", "path-B", "path-C"}, 1024);
  auto cleanup = SetupEfiGlobalState(stub_service, image_device);

  stub_service.AddDevice(&image_device);
  stub_service.AddDevice(&block_device);

  block_device.block_io_media().LogicalPartition = true;

  auto res = FindEfiGptDevice();
  ASSERT_TRUE(res.is_error());
}

TEST(GigabootTest, FindEfiGptDeviceIgnoreNotPresentMedia) {
  MockStubService stub_service;
  Device image_device({"path-A", "path-B", "path-C", "image"});
  BlockDevice block_device({"path-A", "path-B", "path-C"}, 1024);
  auto cleanup = SetupEfiGlobalState(stub_service, image_device);

  stub_service.AddDevice(&image_device);
  stub_service.AddDevice(&block_device);

  block_device.block_io_media().MediaPresent = false;

  auto res = FindEfiGptDevice();
  ASSERT_TRUE(res.is_error());
}

TEST(GigabootTest, FindPartition) {
  MockStubService stub_service;
  Device image_device({"path-A", "path-B", "path-C", "image"});
  BlockDevice block_device({"path-A", "path-B", "path-C"}, 1024);
  auto cleanup = SetupEfiGlobalState(stub_service, image_device);

  stub_service.AddDevice(&image_device);
  stub_service.AddDevice(&block_device);

  block_device.InitializeGpt();
  gpt_entry_t zircon_a_entry{{}, {}, kGptFirstUsableBlocks, kGptFirstUsableBlocks + 5, 0, {}};
  SetGptEntryName(GPT_ZIRCON_A_NAME, zircon_a_entry);
  block_device.AddGptPartition(zircon_a_entry);
  gpt_entry_t zircon_b_entry{{}, {}, kGptFirstUsableBlocks + 10, kGptFirstUsableBlocks + 20, 0, {}};
  SetGptEntryName(GPT_ZIRCON_B_NAME, zircon_b_entry);
  block_device.AddGptPartition(zircon_b_entry);
  block_device.FinalizeGpt();

  // Try to find the zircon_a partition.
  auto res = FindEfiGptDevice();
  ASSERT_TRUE(res.is_ok());
  ASSERT_TRUE(res.value().Load().is_ok());

  const gpt_entry_t* find_res = res.value().FindPartition(GPT_ZIRCON_A_NAME);
  ASSERT_NE(find_res, nullptr);
  ASSERT_EQ(memcmp(find_res, &zircon_a_entry, sizeof(gpt_entry_t)), 0);

  find_res = res.value().FindPartition(GPT_ZIRCON_B_NAME);
  ASSERT_NE(find_res, nullptr);
  ASSERT_EQ(memcmp(find_res, &zircon_b_entry, sizeof(gpt_entry_t)), 0);

  // Non-existing partition returns nullptr.
  ASSERT_EQ(res.value().FindPartition(GPT_ZIRCON_R_NAME), nullptr);
}

TEST(GigabootTest, FindEfiGptDeviceNoGpt) {
  MockStubService stub_service;
  Device image_device({"path-A", "path-B", "path-C", "image"});
  BlockDevice block_device({"path-A", "path-B", "path-C"}, 1024);
  auto cleanup = SetupEfiGlobalState(stub_service, image_device);

  stub_service.AddDevice(&image_device);
  stub_service.AddDevice(&block_device);

  auto res = FindEfiGptDevice();
  ASSERT_TRUE(res.is_ok());
  ASSERT_TRUE(res.value().Load().is_error());
}

}  // namespace

}  // namespace gigaboot
