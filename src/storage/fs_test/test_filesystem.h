// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_FS_TEST_TEST_FILESYSTEM_H_
#define SRC_STORAGE_FS_TEST_TEST_FILESYSTEM_H_

#include <fidl/fuchsia.io/cpp/wire.h>
#include <lib/inspect/cpp/hierarchy.h>

#include "src/storage/fs_test/fs_test.h"

namespace fs_test {

// Helper that creates a test file system with the given options and will clean-up upon destruction.
class TestFilesystem {
 public:
  // Creates and returns a mounted test file system.
  static zx::result<TestFilesystem> Create(const TestFilesystemOptions& options);
  // Opens an existing instance of a file system.
  static zx::result<TestFilesystem> Open(const TestFilesystemOptions& options);

  TestFilesystem(TestFilesystem&&) = default;
  TestFilesystem& operator=(TestFilesystem&&) = default;

  ~TestFilesystem();

  const TestFilesystemOptions& options() const { return options_; }
  const std::string& mount_path() const { return mount_path_; }
  bool is_mounted() const { return mounted_; }

  fs_management::MountOptions DefaultMountOptions() const;

  // Mounts the file system (only necessary after calling Unmount).
  zx::result<> Mount(const fs_management::MountOptions& mount_options);
  zx::result<> Mount() { return Mount(DefaultMountOptions()); }

  // Unmounts a mounted file system.
  zx::result<> Unmount();

  // Runs fsck on the file system. Does not automatically unmount, so Unmount should be
  // called first if that is required.
  zx::result<> Fsck();

  // Formats a file system instance.
  zx::result<> Format() { return filesystem_->Format(options_); }

  zx::result<std::string> DevicePath() const;

  const Filesystem::Traits& GetTraits() const { return options_.filesystem->GetTraits(); }

  fbl::unique_fd GetRootFd() const {
    return fbl::unique_fd(open(mount_path_.c_str(), O_RDONLY | O_DIRECTORY));
  }

  // Returns the ramdisk, or nullptr if one isn't being used.
  storage::RamDisk* GetRamDisk() const { return filesystem_->GetRamDisk(); }

  // Returns the ram-nand device, or nullptr if one isn't being used.
  ramdevice_client::RamNand* GetRamNand() const { return filesystem_->GetRamNand(); }

  fidl::UnownedClientEnd<fuchsia_io::Directory> ServiceDirectory() const {
    return filesystem_->ServiceDirectory();
  }

  void Reset() { filesystem_->Reset(); }

  zx::result<fuchsia_io::wire::FilesystemInfo> GetFsInfo() const;

  // Obtain a snapshot from the underlying filesystem's inspect tree. Will cause an assertion if
  // the Inspect service could not be connected to or does not exist.
  inspect::Hierarchy TakeSnapshot() const;

 private:
  // Creates a mount point for the instance, mounts it and returns a TestFilesystem.
  static zx::result<TestFilesystem> FromInstance(const TestFilesystemOptions& options,
                                                 std::unique_ptr<FilesystemInstance> instance);

  TestFilesystem(TestFilesystemOptions options, std::unique_ptr<FilesystemInstance> filesystem,
                 std::string mount_path)
      : options_(std::move(options)),
        filesystem_(std::move(filesystem)),
        mount_path_(std::move(mount_path)) {}

  TestFilesystemOptions options_;
  std::unique_ptr<FilesystemInstance> filesystem_;
  std::string mount_path_;
  bool mounted_ = false;
};

}  // namespace fs_test

#endif  // SRC_STORAGE_FS_TEST_TEST_FILESYSTEM_H_
