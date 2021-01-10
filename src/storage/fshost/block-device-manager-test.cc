// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "block-device-manager.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "mock-block-device.h"

namespace devmgr {
namespace {

using ::testing::ContainerEq;

TEST(BlockDeviceManager, BlobfsLimit) {
  Config::Options options = Config::DefaultOptions();
  options[Config::kBlobfsMaxBytes] = "7654321";
  Config config(options);
  BlockDeviceManager manager(&config);

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
  Config::Options options = Config::DefaultOptions();
  options[Config::kMinfsMaxBytes] = "7654321";
  Config config(options);
  BlockDeviceManager manager(&config);

  MockBlockDevice fvm_device(MockBlockDevice::FvmOptions());
  EXPECT_EQ(manager.AddDevice(fvm_device), ZX_OK);

  MockBlockDevice::Options device_options = MockZxcryptDevice::ZxcryptOptions();
  device_options.content_format = DISK_FORMAT_UNKNOWN;
  MockZxcryptDevice zxcrypt_device(device_options);
  EXPECT_EQ(manager.AddDevice(zxcrypt_device), ZX_OK);

  MockMinfsDevice minfs_device;
  EXPECT_EQ(manager.AddDevice(minfs_device), ZX_OK);
  ASSERT_TRUE(minfs_device.max_size());
  EXPECT_EQ(7654321u, *minfs_device.max_size());
}

TEST(BlockDeviceManager, ReadOptions) {
  std::stringstream stream;
  stream << "# A comment" << std::endl
         << Config::kDefault << std::endl
         << Config::kNoZxcrypt
         << std::endl
         // Duplicate keys should be de-duped.
         << Config::kNoZxcrypt << std::endl
         << Config::kMinfsMaxBytes << "=1"
         << std::endl
         // Duplicates should overwrite the value.
         << Config::kMinfsMaxBytes << "=12345"
         << std::endl
         // Empty value.
         << Config::kBlobfsMaxBytes << "=" << std::endl
         << "-" << Config::kBlobfs << std::endl
         << "-" << Config::kFormatMinfsOnCorruption;

  auto expected_options = Config::DefaultOptions();
  expected_options[Config::kNoZxcrypt] = std::string();
  expected_options[Config::kMinfsMaxBytes] = "12345";
  expected_options[Config::kBlobfsMaxBytes] = std::string();
  expected_options.erase(Config::kBlobfs);
  expected_options.erase(Config::kFormatMinfsOnCorruption);

  EXPECT_THAT(Config::ReadOptions(stream), ContainerEq(expected_options));
}

}  // namespace
}  // namespace devmgr
