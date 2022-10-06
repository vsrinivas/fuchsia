// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/storage/fs_management/cpp/options.h"

#include <gtest/gtest.h>

namespace fs_management {

namespace {

const std::string kTestBinary = "/test/binary";

void AssertStartOptionsEqual(const fuchsia_fs_startup::wire::StartOptions& a,
                             const fuchsia_fs_startup::wire::StartOptions& b) {
  ASSERT_EQ(a.read_only, b.read_only);
  ASSERT_EQ(a.verbose, b.verbose);
  ASSERT_EQ(a.sandbox_decompression, b.sandbox_decompression);
  ASSERT_EQ(a.write_compression_algorithm, b.write_compression_algorithm);
  ASSERT_EQ(a.write_compression_level, b.write_compression_level);
  ASSERT_EQ(a.cache_eviction_policy_override, b.cache_eviction_policy_override);
}

void AssertFormatOptionsEqual(const fuchsia_fs_startup::wire::FormatOptions& a,
                              const fuchsia_fs_startup::wire::FormatOptions& b) {
  ASSERT_EQ(a.verbose, b.verbose);
  ASSERT_EQ(a.num_inodes, b.num_inodes);
  ASSERT_EQ(a.deprecated_padded_blobfs_format, b.deprecated_padded_blobfs_format);
}

TEST(MountOptionsTest, DefaultOptions) {
  MountOptions options;
  std::vector<std::string> expected_argv = {kTestBinary, "mount"};
  fuchsia_fs_startup::wire::StartOptions expected_start_options{
      // This is the default, but we explicitly enumerate it here to be clear that it's the default.
      .write_compression_algorithm = fuchsia_fs_startup::wire::CompressionAlgorithm::kZstdChunked,
      .write_compression_level = -1,
      .cache_eviction_policy_override = fuchsia_fs_startup::wire::EvictionPolicyOverride::kNone,
  };

  ASSERT_EQ(options.as_argv(kTestBinary.c_str()), expected_argv);

  auto start_options_or = options.as_start_options();
  ASSERT_TRUE(start_options_or.is_ok()) << start_options_or.status_string();
  AssertStartOptionsEqual(*start_options_or, expected_start_options);
}

TEST(MountOptionsTest, AllOptionsSet) {
  MountOptions options{
      .readonly = true,
      .verbose_mount = true,
      .write_compression_algorithm = "UNCOMPRESSED",
      .write_compression_level = 10,
      .cache_eviction_policy = "NEVER_EVICT",
      .fsck_after_every_transaction = true,
      .sandbox_decompression = true,
  };
  std::vector<std::string> expected_argv = {kTestBinary,
                                            "--verbose",
                                            "mount",
                                            "--readonly",
                                            "--compression",
                                            "UNCOMPRESSED",
                                            "--compression_level",
                                            "10",
                                            "--eviction_policy",
                                            "NEVER_EVICT",
                                            "--fsck_after_every_transaction",
                                            "--sandbox_decompression"};
  fuchsia_fs_startup::wire::StartOptions expected_start_options{
      .read_only = true,
      .verbose = true,
      .sandbox_decompression = true,
      .write_compression_algorithm = fuchsia_fs_startup::wire::CompressionAlgorithm::kUncompressed,
      .write_compression_level = 10,
      .cache_eviction_policy_override =
          fuchsia_fs_startup::wire::EvictionPolicyOverride::kNeverEvict,
  };

  ASSERT_EQ(options.as_argv(kTestBinary.c_str()), expected_argv);

  auto start_options_or = options.as_start_options();
  ASSERT_TRUE(start_options_or.is_ok()) << start_options_or.status_string();
  AssertStartOptionsEqual(*start_options_or, expected_start_options);
}

TEST(MountOptionsTest, ZstdChunkedEvictImmediately) {
  MountOptions options{
      .write_compression_algorithm = "ZSTD_CHUNKED",
      .cache_eviction_policy = "EVICT_IMMEDIATELY",
  };
  std::vector<std::string> expected_argv = {kTestBinary,         "mount",
                                            "--compression",     "ZSTD_CHUNKED",
                                            "--eviction_policy", "EVICT_IMMEDIATELY"};
  fuchsia_fs_startup::wire::StartOptions expected_start_options{
      .write_compression_algorithm = fuchsia_fs_startup::wire::CompressionAlgorithm::kZstdChunked,
      .write_compression_level = -1,
      .cache_eviction_policy_override =
          fuchsia_fs_startup::wire::EvictionPolicyOverride::kEvictImmediately,
  };

  ASSERT_EQ(options.as_argv(kTestBinary.c_str()), expected_argv);

  auto start_options_or = options.as_start_options();
  ASSERT_TRUE(start_options_or.is_ok()) << start_options_or.status_string();
  AssertStartOptionsEqual(*start_options_or, expected_start_options);
}

TEST(MkfsOptionsTest, DefaultOptions) {
  MkfsOptions options;
  std::vector<std::string> expected_argv = {kTestBinary, "mkfs"};
  fuchsia_fs_startup::wire::FormatOptions expected_format_options;

  ASSERT_EQ(options.as_argv(kTestBinary.c_str()), expected_argv);
  AssertFormatOptionsEqual(options.as_format_options(), expected_format_options);
}

TEST(MkfsOptionsTest, AllOptionsSet) {
  MkfsOptions options{
      .fvm_data_slices = 10,
      .verbose = true,
      .deprecated_padded_blobfs_format = true,
      .num_inodes = 100,
  };
  std::vector<std::string> expected_argv = {
      kTestBinary, "-v",  "--fvm_data_slices", "10", "--deprecated_padded_format", "--num_inodes",
      "100",       "mkfs"};
  fuchsia_fs_startup::wire::FormatOptions expected_format_options{
      .verbose = true,
      .deprecated_padded_blobfs_format = true,
      .num_inodes = 100,
  };

  ASSERT_EQ(options.as_argv(kTestBinary.c_str()), expected_argv);
  AssertFormatOptionsEqual(options.as_format_options(), expected_format_options);
}

TEST(FsckOptionsTest, DefaultOptions) {
  FsckOptions options;
  std::vector<std::string> expected_argv = {kTestBinary, "fsck"};
  std::vector<std::string> expected_argv_fat32 = {kTestBinary, "/device/path"};

  ASSERT_EQ(options.as_argv(kTestBinary.c_str()), expected_argv);
  ASSERT_EQ(options.as_argv_fat32(kTestBinary.c_str(), "/device/path"), expected_argv_fat32);
}

TEST(FsckOptionsTest, VerboseNeverModifyForce) {
  FsckOptions options{
      .verbose = true,
      .never_modify = true,
      .force = true,
  };
  // platform fsck only supports verbose
  std::vector<std::string> expected_argv = {kTestBinary, "-v", "fsck"};
  // fat32 fsck doesn't support verbose but does support never/always modify and force
  std::vector<std::string> expected_argv_fat32 = {kTestBinary, "-n", "-f", "/device/path"};

  ASSERT_EQ(options.as_argv(kTestBinary.c_str()), expected_argv);
  ASSERT_EQ(options.as_argv_fat32(kTestBinary.c_str(), "/device/path"), expected_argv_fat32);
}

TEST(FsckOptionsTest, AlwaysModify) {
  FsckOptions options{
      .always_modify = true,
  };
  // platform fsck only supports verbose
  std::vector<std::string> expected_argv = {kTestBinary, "fsck"};
  // fat32 fsck doesn't support verbose but does support never/always modify and force
  std::vector<std::string> expected_argv_fat32 = {kTestBinary, "-y", "/device/path"};

  ASSERT_EQ(options.as_argv(kTestBinary.c_str()), expected_argv);
  ASSERT_EQ(options.as_argv_fat32(kTestBinary.c_str(), "/device/path"), expected_argv_fat32);
}

}  // namespace
}  // namespace fs_management
