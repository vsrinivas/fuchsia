// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_BIN_STORAGE_BENCHMARK_BLOCK_DEVICE_
#define SRC_STORAGE_BIN_STORAGE_BENCHMARK_BLOCK_DEVICE_

#include <fidl/fuchsia.hardware.block.volume/cpp/wire.h>
#include <fidl/fuchsia.io/cpp/wire.h>
#include <lib/fidl/cpp/wire/traits.h>
#include <lib/zx/result.h>

#include <cstdint>
#include <string>

#include "src/lib/storage/fs_management/cpp/format.h"
#include "src/lib/storage/fs_management/cpp/mount.h"
#include "src/lib/uuid/uuid.h"
#include "src/storage/bin/start-storage-benchmark/running-filesystem.h"

namespace storage_benchmark {

// RAII wrapper around a fvm volume. The volume is destroyed when this object is destroyed.
class FvmVolume {
 public:
  FvmVolume(FvmVolume &&other) noexcept;
  FvmVolume &operator=(FvmVolume &&) noexcept;
  FvmVolume(const FvmVolume &) = delete;
  FvmVolume &operator=(const FvmVolume &) = delete;
  ~FvmVolume();

  // Creates a new fvm volume of at least |partition_size| bytes.
  static zx::result<FvmVolume> Create(
      fidl::ClientEnd<fuchsia_hardware_block_volume::VolumeManager> &fvm_client,
      uint64_t partition_size);

  // Returns the path to the volume in /dev/class/block.
  const std::string &path() const { return path_; }

 private:
  explicit FvmVolume(std::string path) : path_(std::move(path)) {}
  std::string path_;
};

// Searches through /dev/class/block for a block device that looks like fvm and returns the path to
// it.
zx::result<std::string> FindFvmBlockDevicePath();

// Opens a connection to fvm's VolumeManager. |fvm_block_device_path| is the path to fvm's block
// device in /dev/class/block. Requires access to all of /dev.
zx::result<fidl::ClientEnd<fuchsia_hardware_block_volume::VolumeManager>> ConnectToFvm(
    const std::string &fvm_block_device_path);

// Formats the block device at |block_device_path| with the filesystem specified by |format|.
zx::result<> FormatBlockDevice(const std::string &block_device_path,
                               fs_management::DiskFormat format);

// Mounts the filesystem at |block_device_path|. The returned |RunningFilesystem| takes ownership of
// |fvm_volume| to ensure that it outlives the mounted filesystem. The path in |fvm_volume| may be
// different from |block_device_path| if other drivers like zxcrypt were put on top of it.
zx::result<std::unique_ptr<RunningFilesystem>> StartBlockDeviceFilesystem(
    const std::string &block_device_path, fs_management::DiskFormat format, FvmVolume fvm_volume);

// Creates a zxcrypt volume on top of the block device at |device_path|. Returns the path to the
// block device exposed by zxcrypt.
zx::result<std::string> CreateZxcryptVolume(const std::string &device_path);

}  // namespace storage_benchmark

#endif  // SRC_STORAGE_BIN_STORAGE_BENCHMARK_BLOCK_DEVICE_
