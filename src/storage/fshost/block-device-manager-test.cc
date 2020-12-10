// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "block-device-manager.h"

#include <gtest/gtest.h>

#include "mock-block-device.h"

namespace devmgr {

TEST(BlockDeviceManager, BlobfsLimit) {
  BlockDeviceManager::Options opts = BlockDeviceManager::DefaultOptions();
  opts.options[BlockDeviceManager::Options::kBlobfsMaxBytes] = "7654321";
  BlockDeviceManager manager(opts);

  // When there's no FVM we expect no match and no max size call.
  MockBlobfsDevice blobfs_device;
  manager.AddDevice(blobfs_device);
  ASSERT_FALSE(blobfs_device.max_size());

  // Add FVM and re-try. This should call the limit set function.
  MockBlockDevice fvm_device(MockBlockDevice::FvmOptions());
  EXPECT_EQ(manager.AddDevice(fvm_device), ZX_OK);
  manager.AddDevice(blobfs_device);
  ASSERT_TRUE(blobfs_device.max_size());
  EXPECT_EQ(7654321u, *blobfs_device.max_size());

  // Make a blobfs that looks like it's in a ramdisk, the limit should not be set.
  MockBlockDevice::Options ramdisk_opts = MockBlobfsDevice::BlobfsOptions();
  ramdisk_opts.topological_path = "/dev/misc/ramctl" + ramdisk_opts.topological_path;
  MockBlockDevice ramdisk_blobfs(ramdisk_opts);
  manager.AddDevice(ramdisk_blobfs);
  ASSERT_FALSE(ramdisk_blobfs.max_size());
}

TEST(BlockDeviceManager, MinfsLimit) {
  BlockDeviceManager::Options opts = BlockDeviceManager::DefaultOptions();
  opts.options[BlockDeviceManager::Options::kMinfsMaxBytes] = "7654321";
  BlockDeviceManager manager(opts);

  MockBlockDevice fvm_device(MockBlockDevice::FvmOptions());
  EXPECT_EQ(manager.AddDevice(fvm_device), ZX_OK);

  MockBlockDevice::Options options = MockZxcryptDevice::ZxcryptOptions();
  options.content_format = DISK_FORMAT_UNKNOWN;
  MockZxcryptDevice zxcrypt_device(options);
  EXPECT_EQ(manager.AddDevice(zxcrypt_device), ZX_OK);

  MockMinfsDevice minfs_device;
  EXPECT_EQ(manager.AddDevice(minfs_device), ZX_OK);
  ASSERT_TRUE(minfs_device.max_size());
  EXPECT_EQ(7654321u, *minfs_device.max_size());
}

TEST(BlockDeviceManager, ReadOptions) {
  std::stringstream stream;
  stream << "# A comment" << std::endl
         << BlockDeviceManager::Options::kDefault << std::endl
         << BlockDeviceManager::Options::kNoZxcrypt
         << std::endl
         // Duplicate keys should be de-duped.
         << BlockDeviceManager::Options::kNoZxcrypt << std::endl
         << BlockDeviceManager::Options::kMinfsMaxBytes << "=1"
         << std::endl
         // Duplicates should overwrite the value.
         << BlockDeviceManager::Options::kMinfsMaxBytes << "=12345"
         << std::endl
         // Empty value.
         << BlockDeviceManager::Options::kBlobfsMaxBytes << "=" << std::endl
         << "-" << BlockDeviceManager::Options::kBlobfs << std::endl
         << "-" << BlockDeviceManager::Options::kFormatMinfsOnCorruption;

  const auto options = BlockDeviceManager::ReadOptions(stream);
  auto expected_options = BlockDeviceManager::DefaultOptions();
  expected_options.options[BlockDeviceManager::Options::kNoZxcrypt] = std::string();
  expected_options.options[BlockDeviceManager::Options::kMinfsMaxBytes] = "12345";
  expected_options.options[BlockDeviceManager::Options::kBlobfsMaxBytes] = std::string();
  expected_options.options.erase(BlockDeviceManager::Options::kBlobfs);
  expected_options.options.erase(BlockDeviceManager::Options::kFormatMinfsOnCorruption);

  EXPECT_EQ(expected_options.options, options.options);
}

}  // namespace devmgr
