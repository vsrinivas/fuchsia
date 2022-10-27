// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_TESTING_FVM_H_
#define SRC_STORAGE_TESTING_FVM_H_

#include <fidl/fuchsia.device/cpp/wire.h>
#include <lib/zx/result.h>
#include <zircon/device/block.h>

#include <array>
#include <optional>
#include <string>

namespace storage {

struct FvmOptions {
  std::string_view name = "fs-test-partition";

  // If not set, a test GUID type is used.
  std::optional<std::array<uint8_t, BLOCK_GUID_LEN>> type;

  uint64_t initial_fvm_slice_count = 1;
};

// Formats the given block device to be managed by FVM, and start up an FVM instance.
//
// Returns that path to the FVM device.
zx::result<std::string> CreateFvmInstance(const std::string& device_path, size_t slice_size);

// Formats the given block device to be FVM managed, and create a new partition on the device.
//
// Returns the path to the newly created block device.
zx::result<std::string> CreateFvmPartition(const std::string& device_path, size_t slice_size,
                                           const FvmOptions& options = {});

// Binds the FVM driver to the given device.
zx::result<> BindFvm(fidl::UnownedClientEnd<fuchsia_device::Controller> device);

}  // namespace storage

#endif  // SRC_STORAGE_TESTING_FVM_H_
