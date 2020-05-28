// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_FS_TEST_FS_TEST_H_
#define SRC_STORAGE_FS_TEST_FS_TEST_H_

#include <lib/zx/status.h>

#include <fs-management/format.h>

#include "src/lib/isolated_devmgr/v2_component/ram_disk.h"

namespace fs_test {

class FileSystem;

struct TestFileSystemOptions {
  static TestFileSystemOptions DefaultMinfs();
  static TestFileSystemOptions DefaultMemfs();

  std::string description;
  bool use_fvm = false;
  uint64_t device_block_size = 0;
  uint64_t device_block_count = 0;
  uint64_t fvm_slice_size = 0;
  const FileSystem* file_system = nullptr;
};

std::ostream& operator<<(std::ostream& out, const TestFileSystemOptions& options);

std::vector<TestFileSystemOptions> AllTestFileSystems();

// A file system instance is a specific instance created for test purposes.
class FileSystemInstance {
 public:
  virtual ~FileSystemInstance() = default;
  virtual zx::status<> Mount(const std::string& mount_path) = 0;

 protected:
  // A wrapper around fs-management that can be used by subclasses if they so wish.
  static zx::status<> Mount(const std::string& device_path, const std::string& mount_path,
                            disk_format_t format);
};

// Base class for all supported file systems. It is a factory class that generates
// instances of FileSystemInstance subclasses.
class FileSystem {
 public:
  virtual zx::status<std::unique_ptr<FileSystemInstance>> Make(
      const TestFileSystemOptions& options) const = 0;

 protected:
  // A wrapper around fs-management that can be used by subclasses if they so wish.
  static zx::status<> Format(const std::string& device_path, disk_format_t format);
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
  zx::status<std::unique_ptr<FileSystemInstance>> Make(
      const TestFileSystemOptions& options) const override;
};

// Support for Memfs.
class MemfsFileSystem : public FileSystemImpl<MemfsFileSystem> {
 public:
  zx::status<std::unique_ptr<FileSystemInstance>> Make(
      const TestFileSystemOptions& options) const override;
};

// Helper that creates a test file system with the given options and will clean-up upon destruction.
class TestFileSystem {
 public:
  // Creates and returns a test file system.
  static zx::status<TestFileSystem> Create(const TestFileSystemOptions& options);

  TestFileSystem(TestFileSystem&&) = default;
  TestFileSystem& operator=(TestFileSystem&&) = default;

  ~TestFileSystem();

  const std::string& mount_path() const { return mount_path_; }

 private:
  TestFileSystem(const TestFileSystemOptions& options,
                 std::unique_ptr<FileSystemInstance> file_system, const std::string& mount_path)
      : options_(options), file_system_(std::move(file_system)), mount_path_(mount_path) {}

  TestFileSystemOptions options_;
  std::unique_ptr<FileSystemInstance> file_system_;
  std::string mount_path_;
};

}  // namespace fs_test

#endif  // SRC_STORAGE_FS_TEST_FS_TEST_H_
