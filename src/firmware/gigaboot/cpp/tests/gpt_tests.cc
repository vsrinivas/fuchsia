// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include <efi/types.h>
#include <gtest/gtest.h>

#include "gpt.h"
#include "mock_boot_service.h"
#include "utils.h"

efi_loaded_image_protocol* gEfiLoadedImage = nullptr;
efi_system_table* gEfiSystemTable = nullptr;
efi_handle gEfiImageHandle;

namespace gigaboot {
namespace {

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

}  // namespace

}  // namespace gigaboot
