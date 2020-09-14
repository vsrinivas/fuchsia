// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FS_TEST_SUPPORT_ENVIRONMENT_H_
#define FS_TEST_SUPPORT_ENVIRONMENT_H_

#include <lib/devmgr-integration-test/fixture.h>

#include <optional>
#include <string>

#include <fbl/macros.h>
#include <fs-management/fvm.h>
#include <fs-management/mount.h>
#include <ramdevice-client/ramdisk.h>
#include <zxtest/zxtest.h>

namespace fs {

// Simple wrapper around a ramdisk.
class RamDisk {
 public:
  RamDisk(const fbl::unique_fd& devfs_root, uint32_t page_size, uint32_t num_pages);
  ~RamDisk();

  const char* path() const { return path_.c_str(); }
  uint32_t page_size() const { return page_size_; }

  // Expose the ramdisk client functionality.
  zx_status_t SleepAfter(uint32_t block_count) const;
  zx_status_t WakeUp() const;
  zx_status_t GetBlockCounts(ramdisk_block_write_counts_t* counts) const;

  DISALLOW_COPY_ASSIGN_AND_MOVE(RamDisk);

 private:
  uint32_t page_size_;
  uint32_t num_pages_;
  ramdisk_client_t* ramdisk_ = nullptr;
  std::string path_;
};

// Process-wide environment for tests. This takes care of dealing with a
// physical or emulated block device for the tests in addition to configuration
// parameters.
class Environment : public zxtest::Environment {
 public:
  // Controls how the executable is running. The basic choice is between using
  // a real block device (physical_device_path) or a ram-disk device of a given
  // size (ramdisk_block_count, when the path is null).
  struct TestConfig {
    // Indicates that this test is running in a packaged environment which affects the paths of
    // different things.
    bool is_packaged = true;

    uint64_t ramdisk_block_count;
    const char* physical_device_path;  // Path to an existing device.

    // Options that apply to any kind of device:
    const char* mount_path;
    disk_format_type format_type;
    bool show_help;
    bool use_journal = true;
    bool use_pager = false;
    const char* write_compression_algorithm = nullptr;
    std::optional<int> write_compression_level = std::nullopt;

    // Power-failure related tests.
    uint32_t power_stride = 1;  // Number of steps to skip between runs.
    uint32_t power_start = 1;   // First step to run.
    uint32_t power_cycles = 5;  // Last step to run.

    // Updates the configuration with options from the command line.
    // Returns false as soon as an option is not recognized.
    bool GetOptions(int argc, char** argv);

    // Returns the help message.
    const char* HelpMessage() const;
  };

  explicit Environment(const TestConfig& config) : config_(config) {}
  ~Environment() {}

  // zxtest::Environment interface:
  void SetUp() override;
  void TearDown() override;

  bool use_journal() const { return config_.use_journal; }

  bool use_pager() const { return config_.use_pager; }

  std::optional<const char*> write_compression_algorithm() const {
    if (config_.write_compression_algorithm == nullptr) {
      return std::nullopt;
    }
    return config_.write_compression_algorithm;
  }

  std::optional<int> write_compression_level() const { return config_.write_compression_level; }

  disk_format_type format_type() const { return config_.format_type; }

  const char* mount_path() const { return config_.mount_path; }

  uint64_t disk_size() const { return block_size_ * block_count_; }

  const char* device_path() const { return path_.c_str(); }

  // Returns the path of the underlying device with the caveat that if the test
  // is using a ramdisk, the returned path is not usable to access the device
  // because it will not be rooted on the correct device manager. This only
  // makes sense when comparing against a path provided by the filesystem.
  const char* GetRelativeDevicePath() const;

  const RamDisk* ramdisk() const { return ramdisk_.get(); }

  const fbl::unique_fd& devfs_root() const { return devmgr_.devfs_root(); }
  const TestConfig& config() const { return config_; }

  DISALLOW_COPY_ASSIGN_AND_MOVE(Environment);

 private:
  bool OpenDevice(const char* path);
  void CreateDevmgr();

  TestConfig config_;

  devmgr_integration_test::IsolatedDevmgr devmgr_;
  std::unique_ptr<RamDisk> ramdisk_;
  std::string path_;

  uint32_t block_size_ = 512;
  uint64_t block_count_ = 0;
};

extern Environment* g_environment;

}  // namespace fs

#endif  // FS_TEST_SUPPORT_ENVIRONMENT_H_
