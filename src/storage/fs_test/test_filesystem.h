// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_FS_TEST_TEST_FILESYSTEM_H_
#define SRC_STORAGE_FS_TEST_TEST_FILESYSTEM_H_

#include "src/storage/fs_test/fs_test.h"

namespace fs_test {

// Helper that creates a test file system with the given options and will clean-up upon destruction.
class TestFilesystem {
 public:
  // Creates and returns a mounted test file system.
  static zx::status<TestFilesystem> Create(const TestFilesystemOptions& options);
  // Opens an existing instance of a file system.
  static zx::status<TestFilesystem> Open(const TestFilesystemOptions& options);

  TestFilesystem(TestFilesystem&&) = default;
  TestFilesystem& operator=(TestFilesystem&&) = default;

  ~TestFilesystem();

  const TestFilesystemOptions& options() const { return options_; }
  const std::string& mount_path() const { return mount_path_; }
  bool is_mounted() const { return mounted_; }

  // Mounts the file system (only necessary after calling Unmount).
  zx::status<> Mount(const MountOptions& mount_options = MountOptions());

  // Unmounts a mounted file system.
  zx::status<> Unmount();

  // Runs fsck on the file system. Does not automatically unmount, so Unmount should be
  // called first if that is required.
  zx::status<> Fsck();

  // Formats a file system instance.
  zx::status<> Format() { return filesystem_->Format(options_); }

  zx::status<std::string> DevicePath() const;

  const Filesystem::Traits& GetTraits() const { return options_.filesystem->GetTraits(); }

  fbl::unique_fd GetRootFd() const {
    return fbl::unique_fd(open(mount_path_.c_str(), O_RDONLY | O_DIRECTORY));
  }

  // Returns the ramdisk, or nullptr if one isn't being used.
  storage::RamDisk* GetRamDisk() const { return filesystem_->GetRamDisk(); }

  // Returns the ram-nand device, or nullptr if one isn't being used.
  ramdevice_client::RamNand* GetRamNand() const { return filesystem_->GetRamNand(); }

  zx::unowned_channel GetOutgoingDirectory() const { return filesystem_->GetOutgoingDirectory(); };

  fidl::ClientEnd<fuchsia_io::Directory> GetSvcDirectory() const;

  zx::status<uint64_t> GetFsInfoTotalBytes() const;
  zx::status<uint64_t> GetFsInfoUsedBytes() const;

 private:
  // Creates a mount point for the instance, mounts it and returns a TestFilesystem.
  static zx::status<TestFilesystem> FromInstance(const TestFilesystemOptions& options,
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
