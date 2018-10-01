// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <limits.h>

#include <fbl/unique_fd.h>
#include <zircon/nand/c/fidl.h>

// The nand device that will be used as the parent of the broker device. This
// can be a ram-nand device instantiated for the test, or any nand device
// already on the system.
class ParentDevice {
  public:
    struct TestConfig {
        zircon_nand_Info info;  // Configuration for a new ram-nand.
        const char* path;       // Path to an existing device.
        bool is_broker;         // True is the device is a broker (not a nand).
        uint32_t num_blocks;    // Number of blocks to use.
        uint32_t first_block;   // First block to use.
    };

    explicit ParentDevice(const TestConfig& config);
    ~ParentDevice();

    const char* Path() const { return path_; }

    bool IsValid() const { return ram_nand_ || device_; }
    bool IsExternal() const { return device_ ?  true : false; }
    bool IsBroker() const { return config_.is_broker; }

    // Returns a file descriptor for the device.
    int get() const { return ram_nand_ ? ram_nand_.get() : device_.get(); }

    const zircon_nand_Info& Info() const { return config_.info; }
    void SetInfo(const zircon_nand_Info& info);

    uint32_t NumBlocks() const { return config_.num_blocks; }
    uint32_t FirstBlock() const { return config_.first_block; }

  private:
    fbl::unique_fd ram_nand_;
    fbl::unique_fd device_;
    TestConfig config_;
    char path_[PATH_MAX];
};

extern ParentDevice* g_parent_device_;
