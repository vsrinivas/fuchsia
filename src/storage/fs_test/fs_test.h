// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_FS_TEST_FS_TEST_H_
#define SRC_STORAGE_FS_TEST_FS_TEST_H_

#include <lib/zx/status.h>

#include "src/lib/isolated_devmgr/v2_component/ram_disk.h"

namespace fs_test {

// Base class for all supported file systems.
class FileSystem {
 public:
  virtual zx::status<> Format(const std::string& device_path) const = 0;
  virtual zx::status<> Mount(const std::string& device_path,
                             const std::string& mount_path) const = 0;
};

// Template that implementations can use to gain the SharedInstance method.
template <typename T>
class FileSystemImpl : public FileSystem {
 public:
  static const T& SharedInstance() {
    static const auto* const kInstance = new T();
    return *kInstance;
  }
};

// Support for Minfs.
class MinfsFileSystem : public FileSystemImpl<MinfsFileSystem> {
 public:
  zx::status<> Format(const std::string& device_path) const override;
  zx::status<> Mount(const std::string& device_path, const std::string& mount_path) const override;
};

// Helper that creates a ram-disk with, optionally, an FVM partition and then formats and mounts a
// file system.
class TestFileSystem {
 public:
  struct Options {
    bool use_fvm = true;
    uint64_t device_block_size = 512;
    uint64_t device_block_count = 131'072;
    uint64_t fvm_slice_size = 1'048'576;
    const FileSystem* file_system = &MinfsFileSystem::SharedInstance();
  };

  // Creates and returns a test file system.
  static zx::status<TestFileSystem> Create(const Options& options);

  TestFileSystem(TestFileSystem&&) = default;
  TestFileSystem& operator=(TestFileSystem&&) = default;

  ~TestFileSystem();

  const std::string& mount_path() const { return mount_path_; }

 private:
  TestFileSystem(isolated_devmgr::RamDisk ram_disk, const std::string& mount_path)
      : ram_disk_(std::move(ram_disk)), mount_path_(mount_path) {}

  isolated_devmgr::RamDisk ram_disk_;
  std::string mount_path_;
};

}  // namespace fs_test

#endif  // SRC_STORAGE_FS_TEST_FS_TEST_H_
