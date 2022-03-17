// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/storage/fs_management/cpp/options.h"

#include <gtest/gtest.h>

namespace fs_management {
namespace {

const std::string kTestBinary = "/test/binary";

TEST(MountOptionsTest, DefaultOptions) {
  MountOptions options;
  std::vector<std::string> expected_output = {kTestBinary, "mount"};

  ASSERT_EQ(options.as_argv(kTestBinary.c_str()), expected_output);
}

TEST(MountOptionsTest, AllOptionsSet) {
  MountOptions options{
      .readonly = true,
      .verbose_mount = true,
      .collect_metrics = true,
      .write_compression_algorithm = "UNCOMPRESSED",
      .write_compression_level = 10,
      .cache_eviction_policy = "NEVER_EVICT",
      .fsck_after_every_transaction = true,
      .sandbox_decompression = true,
  };
  std::vector<std::string> expected_output = {kTestBinary,
                                              "--verbose",
                                              "mount",
                                              "--readonly",
                                              "--metrics",
                                              "--compression",
                                              "UNCOMPRESSED",
                                              "--compression_level",
                                              "10",
                                              "--eviction_policy",
                                              "NEVER_EVICT",
                                              "--fsck_after_every_transaction",
                                              "--sandbox_decompression"};

  ASSERT_EQ(options.as_argv(kTestBinary.c_str()), expected_output);
}

TEST(MkfsOptionsTest, DefaultOptions) {
  MkfsOptions options;
  std::vector<std::string> expected_output = {kTestBinary, "mkfs"};

  ASSERT_EQ(options.as_argv(kTestBinary.c_str()), expected_output);
}

TEST(MkfsOptionsTest, AllOptionsSet) {
  MkfsOptions options{
      .fvm_data_slices = 10,
      .verbose = true,
      .deprecated_padded_blobfs_format = true,
      .num_inodes = 100,
  };
  std::vector<std::string> expected_output = {
      kTestBinary, "-v",  "--fvm_data_slices", "10", "--deprecated_padded_format", "--num_inodes",
      "100",       "mkfs"};

  ASSERT_EQ(options.as_argv(kTestBinary.c_str()), expected_output);
}

TEST(FsckOptionsTest, DefaultOptions) {
  FsckOptions options;
  std::vector<std::string> expected_output = {kTestBinary, "fsck"};
  std::vector<std::string> expected_output_fat32 = {kTestBinary, "/device/path"};

  ASSERT_EQ(options.as_argv(kTestBinary.c_str()), expected_output);
  ASSERT_EQ(options.as_argv_fat32(kTestBinary.c_str(), "/device/path"), expected_output_fat32);
}

TEST(FsckOptionsTest, VerboseNeverModifyForce) {
  FsckOptions options{
      .verbose = true,
      .never_modify = true,
      .force = true,
  };
  // platform fsck only supports verbose
  std::vector<std::string> expected_output = {kTestBinary, "-v", "fsck"};
  // fat32 fsck doesn't support verbose but does support never/always modify and force
  std::vector<std::string> expected_output_fat32 = {kTestBinary, "-n", "-f", "/device/path"};

  ASSERT_EQ(options.as_argv(kTestBinary.c_str()), expected_output);
  ASSERT_EQ(options.as_argv_fat32(kTestBinary.c_str(), "/device/path"), expected_output_fat32);
}

TEST(FsckOptionsTest, AlwaysModify) {
  FsckOptions options{
      .always_modify = true,
  };
  // platform fsck only supports verbose
  std::vector<std::string> expected_output = {kTestBinary, "fsck"};
  // fat32 fsck doesn't support verbose but does support never/always modify and force
  std::vector<std::string> expected_output_fat32 = {kTestBinary, "-y", "/device/path"};

  ASSERT_EQ(options.as_argv(kTestBinary.c_str()), expected_output);
  ASSERT_EQ(options.as_argv_fat32(kTestBinary.c_str(), "/device/path"), expected_output_fat32);
}

}  // namespace
}  // namespace fs_management
