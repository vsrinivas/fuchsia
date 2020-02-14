// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_NAND_DRIVERS_BROKER_TEST_PARENT_H_
#define SRC_STORAGE_NAND_DRIVERS_BROKER_TEST_PARENT_H_

#include <fuchsia/hardware/nand/c/fidl.h>
#include <limits.h>

#include <fbl/string_buffer.h>
#include <fbl/unique_fd.h>
#include <ramdevice-client/ramnand.h>

// The nand device that will be used as the parent of the broker device. This
// can be a ram-nand device instantiated for the test, or any nand device
// already on the system.
class ParentDevice {
 public:
  struct TestConfig {
    fuchsia_hardware_nand_Info info;                   // Configuration for a new ram-nand.
    fuchsia_hardware_nand_PartitionMap partition_map;  // Configuration for a new ram-nand.
    const char* path;                                  // Path to an existing device.
    bool is_broker;        // True is the device is a broker (not a nand).
    uint32_t num_blocks;   // Number of blocks to use.
    uint32_t first_block;  // First block to use.
  };

  explicit ParentDevice(const TestConfig& config);
  ~ParentDevice() = default;

  const char* Path() const { return path_.c_str(); }

  bool IsValid() const { return ram_nand_ || device_; }
  bool IsExternal() const { return device_ ? true : false; }
  bool IsBroker() const { return config_.is_broker; }

  // Returns a file descriptor for the device.
  int get() { return ram_nand_ ? ram_nand_->fd().get() : device_.get(); }

  const fuchsia_hardware_nand_Info& Info() const { return config_.info; }
  void SetInfo(const fuchsia_hardware_nand_Info& info);

  const fuchsia_hardware_nand_PartitionMap& PartitionMap() const { return config_.partition_map; }
  void SetPartitionMap(const fuchsia_hardware_nand_PartitionMap& partition_map);

  uint32_t NumBlocks() const { return config_.num_blocks; }
  uint32_t FirstBlock() const { return config_.first_block; }

 private:
  std::optional<ramdevice_client::RamNand> ram_nand_;
  fbl::unique_fd device_;
  TestConfig config_;
  fbl::StringBuffer<PATH_MAX> path_;
};

extern ParentDevice* g_parent_device_;

#endif  // SRC_STORAGE_NAND_DRIVERS_BROKER_TEST_PARENT_H_
