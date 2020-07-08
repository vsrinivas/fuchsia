// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_HWSTRESS_FLASH_STRESS_H_
#define GARNET_BIN_HWSTRESS_FLASH_STRESS_H_

#include <string>

#include <src/lib/uuid/uuid.h>

#include "status.h"

namespace hwstress {

// Creates and manages the lifetime of a new partition backed by a
// Fuchsia Volume Manager instance.
class TemporaryFvmPartition {
 public:
  ~TemporaryFvmPartition();

  // Create a new partition.
  //
  // |fvm_path| should be the path to an FVM instance, such as
  // "/dev/sys/pci/00:00.0/ahci/sata0/block/fvm".
  //
  // |bytes_requested| is the maximum number of bytes callers will be able to use on the partition.
  // The returned partition may have greater than the requested number of bytes due to rounding and
  // overheads, or it may have less as space is lazily allocated by FVM, so the requested number of
  // bytes may not actually be available.
  //
  // Returns nullptr on failure.
  static std::unique_ptr<TemporaryFvmPartition> Create(const std::string& fvm_path,
                                                       uint64_t bytes_requested);

  // Get the path to the created partition.
  std::string GetPartitionPath();

  // Get the size of the partition in bytes.
  uint64_t GetPartitionSize();

  // Get the size of a slice in bytes.
  uint64_t GetSliceSize();

 private:
  std::string partition_path_;
  uint64_t partition_size_;
  uint64_t slice_size_;
  uuid::Uuid unique_guid_;

  TemporaryFvmPartition(std::string partition_path, uint64_t partition_size, uint64_t slice_size,
                        uuid::Uuid unique_guid);
};

// Start a stress test.
bool StressFlash(StatusLine* status, const std::string& fvm_path, uint64_t bytes_to_test);

}  // namespace hwstress

#endif  // GARNET_BIN_HWSTRESS_FLASH_STRESS_H_
