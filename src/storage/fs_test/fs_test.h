// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_FS_TEST_FS_TEST_H_
#define SRC_STORAGE_FS_TEST_FS_TEST_H_

#include <fcntl.h>
#include <lib/zx/status.h>
#include <lib/zx/time.h>
#include <stdint.h>
#include <zircon/compiler.h>

#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <fbl/unique_fd.h>
#include <fs-management/format.h>
#include <minfs/format.h>
#include <ramdevice-client/ramnand.h>

#include "src/lib/isolated_devmgr/v2_component/ram_disk.h"
#include "src/storage/blobfs/include/blobfs/format.h"

namespace fs_test {

class Filesystem;

struct TestFilesystemOptions {
  static TestFilesystemOptions DefaultMinfs();
  static TestFilesystemOptions MinfsWithoutFvm();
  static TestFilesystemOptions DefaultMemfs();
  static TestFilesystemOptions DefaultFatfs();
  static TestFilesystemOptions DefaultBlobfs();
  static TestFilesystemOptions BlobfsWithoutFvm();

  std::string description;
  bool use_ram_nand = false;
  // If use_ram_nand is true, specifies a VMO to be used to back the device.  If supplied, its size
  // must match the device size (if device_block_count is non-zero), including the extra required
  // for OOB.
  zx::unowned_vmo ram_nand_vmo;
  bool use_fvm = false;
  uint64_t device_block_size = 0;
  uint64_t device_block_count = 0;
  uint64_t fvm_slice_size = 0;
  const Filesystem* filesystem = nullptr;
};

std::ostream& operator<<(std::ostream& out, const TestFilesystemOptions& options);

std::vector<TestFilesystemOptions> AllTestFilesystems();
// Provides the ability to map and filter all test file systems, using the supplied function.
std::vector<TestFilesystemOptions> MapAndFilterAllTestFilesystems(
    std::function<std::optional<TestFilesystemOptions>(const TestFilesystemOptions&)>);

// A file system instance is a specific instance created for test purposes.
class FilesystemInstance {
 public:
  FilesystemInstance() = default;
  FilesystemInstance(const FilesystemInstance&) = delete;
  FilesystemInstance& operator=(const FilesystemInstance&) = delete;
  virtual ~FilesystemInstance() = default;

  virtual zx::status<> Mount(const std::string& mount_path) = 0;
  virtual zx::status<> Unmount(const std::string& mount_path) = 0;
  virtual zx::status<> Fsck() = 0;
  virtual isolated_devmgr::RamDisk* GetRamDisk() { return nullptr; }
  virtual ramdevice_client::RamNand* GetRamNand() { return nullptr; }
};

// Base class for all supported file systems. It is a factory class that generates
// instances of FilesystemInstance subclasses.
class Filesystem {
 public:
  struct Traits {
    std::string_view name;
    bool can_unmount = false;
    zx::duration timestamp_granularity = zx::nsec(1);
    bool supports_hard_links = true;
    bool supports_mmap = false;
    bool supports_resize = false;
    uint64_t max_file_size = std::numeric_limits<uint64_t>::max();
    bool in_memory = false;
    bool is_case_sensitive = true;
    bool supports_sparse_files = true;
    bool is_fat = false;
  };

  virtual zx::status<std::unique_ptr<FilesystemInstance>> Make(
      const TestFilesystemOptions& options) const = 0;
  virtual zx::status<std::unique_ptr<FilesystemInstance>> Open(
      const TestFilesystemOptions& options) const {
    return zx::error(ZX_ERR_NOT_SUPPORTED);
  }
  virtual const Traits& GetTraits() const = 0;

 protected:
  // A wrapper around fs-management that can be used by subclasses if they so wish.
  static zx::status<> Format(const std::string& device_path, disk_format_t format);
};

// Template that implementations can use to gain the SharedInstance method.
template <typename T>
class FilesystemImpl : public Filesystem {
 public:
  static const T& SharedInstance() {
    static const auto* const kInstance = new T();
    return *kInstance;
  }
};

// Support for Minfs.
class MinfsFilesystem : public FilesystemImpl<MinfsFilesystem> {
 public:
  zx::status<std::unique_ptr<FilesystemInstance>> Make(
      const TestFilesystemOptions& options) const override;
  zx::status<std::unique_ptr<FilesystemInstance>> Open(
      const TestFilesystemOptions& options) const override;
  const Traits& GetTraits() const override {
    static Traits traits{
        .name = "minfs",
        .can_unmount = true,
        .timestamp_granularity = zx::nsec(1),
        .supports_hard_links = true,
        .supports_mmap = false,
        .supports_resize = true,
        .max_file_size = minfs::kMinfsMaxFileSize,
        .in_memory = false,
        .is_case_sensitive = true,
        .supports_sparse_files = true,
    };
    return traits;
  }
};

// Support for Memfs.
class MemfsFilesystem : public FilesystemImpl<MemfsFilesystem> {
 public:
  zx::status<std::unique_ptr<FilesystemInstance>> Make(
      const TestFilesystemOptions& options) const override;
  const Traits& GetTraits() const override {
    static Traits traits{
        .name = "memfs",
        .can_unmount = false,
        .timestamp_granularity = zx::nsec(1),
        .supports_hard_links = true,
        .supports_mmap = true,
        .supports_resize = false,
        .max_file_size = 512 * 1024 * 1024,
        .in_memory = true,
        .is_case_sensitive = true,
        .supports_sparse_files = true,
    };
    return traits;
  }
};

// Support for Fatfs.
class FatFilesystem : public FilesystemImpl<FatFilesystem> {
 public:
  zx::status<std::unique_ptr<FilesystemInstance>> Make(
      const TestFilesystemOptions& options) const override;
  const Traits& GetTraits() const override {
    static Traits traits{
        .name = "fatfs",
        .can_unmount = true,
        .timestamp_granularity = zx::sec(2),
        .supports_hard_links = false,
        .supports_mmap = false,
        .supports_resize = false,
        .max_file_size = 4'294'967'295,
        .in_memory = false,
        .is_case_sensitive = false,
        .supports_sparse_files = false,
        .is_fat = true,
    };
    return traits;
  }
};

// Support for blobfs.
class BlobfsFilesystem : public FilesystemImpl<BlobfsFilesystem> {
 public:
  zx::status<std::unique_ptr<FilesystemInstance>> Make(
      const TestFilesystemOptions& options) const override;
  const Traits& GetTraits() const override {
    static Traits traits{
        .can_unmount = true,
        .timestamp_granularity = zx::nsec(1),
        .supports_hard_links = false,
        .supports_mmap = true,
        .supports_resize = false,
        .max_file_size = blobfs::kBlobfsMaxFileSize,
        .in_memory = false,
        .is_case_sensitive = true,
        .supports_sparse_files = false,
    };
    return traits;
  }
};

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
  zx::status<> Mount();

  // Unmounts a mounted file system.
  zx::status<> Unmount();

  // Runs fsck on the file system. Does not automatically unmount, so Unmount should be
  // called first if that is required.
  zx::status<> Fsck();

  const Filesystem::Traits& GetTraits() const { return options_.filesystem->GetTraits(); }

  fbl::unique_fd GetRootFd() const {
    return fbl::unique_fd(open(mount_path_.c_str(), O_RDONLY | O_DIRECTORY));
  }

  // Returns the ramdisk, or nullptr if one isn't being used.
  isolated_devmgr::RamDisk* GetRamDisk() const { return filesystem_->GetRamDisk(); }

  // Returns the ram-nand device, or nullptr if one isn't being used.
  ramdevice_client::RamNand* GetRamNand() const { return filesystem_->GetRamNand(); }

 private:
  // Creates a mount point for the instance, mounts it and returns a TestFilesystem.
  static zx::status<TestFilesystem> FromInstance(const TestFilesystemOptions& options,
                                                 std::unique_ptr<FilesystemInstance> instance);

  TestFilesystem(const TestFilesystemOptions& options,
                 std::unique_ptr<FilesystemInstance> filesystem, const std::string& mount_path)
      : options_(options), filesystem_(std::move(filesystem)), mount_path_(mount_path) {}

  TestFilesystemOptions options_;
  std::unique_ptr<FilesystemInstance> filesystem_;
  std::string mount_path_;
  bool mounted_ = false;
};

}  // namespace fs_test

#endif  // SRC_STORAGE_FS_TEST_FS_TEST_H_
