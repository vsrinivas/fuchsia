// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <string>

#include <fbl/macros.h>
#include <fs-management/fvm.h>
#include <ramdevice-client/ramdisk.h>
#include <zxtest/zxtest.h>

// Simple wrapper around a ramdisk.
class RamDisk {
  public:
    RamDisk(uint32_t page_size, uint32_t num_pages);
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
    struct TestConfig {
        const char* path;  // Path to an existing device.
        bool use_journal = true;
    };

    explicit Environment(const TestConfig& config) : config_(config) {}
    ~Environment() {}

    // zxtest::Environment interface:
    void SetUp() override;
    void TearDown() override;

    bool use_journal() const { return config_.use_journal; }

    uint64_t disk_size() const { return block_size_ * block_count_; }

    const char* device_path() const { return path_.c_str(); }

    const RamDisk* ramdisk() const { return ramdisk_.get(); }

    DISALLOW_COPY_ASSIGN_AND_MOVE(Environment);

  private:
    bool OpenDevice(const char* path);

    TestConfig config_;

    std::unique_ptr<RamDisk> ramdisk_;
    std::string path_;

    uint32_t block_size_ = 512;
    uint64_t block_count_ = 1 << 20;  // TODO(ZX-4203): Reduce this value.
};

extern Environment* g_environment;
