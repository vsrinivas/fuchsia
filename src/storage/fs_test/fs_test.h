// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_FS_TEST_FS_TEST_H_
#define SRC_STORAGE_FS_TEST_FS_TEST_H_

#include <fcntl.h>
#include <fuchsia/io/llcpp/fidl.h>
#include <lib/zx/status.h>
#include <lib/zx/time.h>
#include <stdint.h>
#include <zircon/compiler.h>

#include <functional>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <fbl/unique_fd.h>
#include <fs-management/admin.h>
#include <fs-management/format.h>
#include <fs-management/mount.h>
#include <ramdevice-client/ramnand.h>

#include "src/storage/blobfs/blob_layout.h"
#include "src/storage/testing/ram_disk.h"

namespace fs_test {

class Filesystem;

using RamDevice = std::variant<storage::RamDisk, ramdevice_client::RamNand>;

struct TestFilesystemOptions {
  static TestFilesystemOptions DefaultMemfs();
  static TestFilesystemOptions DefaultBlobfs();
  static TestFilesystemOptions BlobfsWithoutFvm();

  std::string description;
  bool use_ram_nand = false;
  // If set specifies a VMO to be used to back the device.  If used for ram-nand, Its size must
  // match the device size (if device_block_count is non-zero), including the extra required for
  // OOB.
  zx::unowned_vmo vmo;
  bool use_fvm = false;

  // If non-zero, create a dummy FVM partition which has the effect of moving the location of the
  // partition under test to be at a different offset on the underlying device.
  uint64_t dummy_fvm_partition_size = 0;

  uint64_t device_block_size = 0;
  uint64_t device_block_count = 0;
  uint64_t fvm_slice_size = 0;
  uint64_t initial_fvm_slice_count = 1;
  // Only supported for blobfs for now.
  uint64_t num_inodes = 0;
  const Filesystem* filesystem = nullptr;
  // By default the ram-disk we create is filled with a non-zero value (so that we don't
  // inadvertently depend on it), but that won't work for very large ram-disks (they will trigger
  // OOMs), in which case they can be zero filled.
  bool zero_fill = false;

  // The format blobfs should store blobs in.
  std::optional<blobfs::BlobLayoutFormat> blob_layout_format;

  // If using ram_nand, the number of writes after which writes should fail.
  uint32_t fail_after;

  // If true, when the ram-disk is disconnected it will discard random writes performed since the
  // last flush (which is all that any device will guarantee).
  bool ram_disk_discard_random_after_last_flush = false;
};

std::ostream& operator<<(std::ostream& out, const TestFilesystemOptions& options);

std::vector<TestFilesystemOptions> AllTestFilesystems();
// Provides the ability to map and filter all test file systems, using the supplied function.
std::vector<TestFilesystemOptions> MapAndFilterAllTestFilesystems(
    std::function<std::optional<TestFilesystemOptions>(const TestFilesystemOptions&)>);
TestFilesystemOptions OptionsWithDescription(std::string_view description);

// Returns device and device path.
zx::status<std::pair<RamDevice, std::string>> CreateRamDevice(const TestFilesystemOptions& options);

// A file system instance is a specific instance created for test purposes.
class FilesystemInstance {
 public:
  FilesystemInstance() = default;
  FilesystemInstance(const FilesystemInstance&) = delete;
  FilesystemInstance& operator=(const FilesystemInstance&) = delete;
  virtual ~FilesystemInstance() = default;

  virtual zx::status<> Format(const TestFilesystemOptions&) = 0;
  virtual zx::status<> Mount(const std::string& mount_path, const mount_options_t& options) = 0;
  virtual zx::status<> Unmount(const std::string& mount_path);
  virtual zx::status<> Fsck() = 0;

  // Returns path of the device on which the filesystem is created. For filesystem that are not
  // block device based, like memfs, the function returns an error.
  virtual zx::status<std::string> DevicePath() const = 0;
  virtual storage::RamDisk* GetRamDisk() { return nullptr; }
  virtual ramdevice_client::RamNand* GetRamNand() { return nullptr; }
  virtual zx::unowned_channel GetOutgoingDirectory() const { return {}; }
};

// Base class for all supported file systems. It is a factory class that generates
// instances of FilesystemInstance subclasses.
class Filesystem {
 public:
  struct Traits {
    std::string name;
    bool can_unmount = false;
    zx::duration timestamp_granularity = zx::nsec(1);
    bool supports_hard_links = true;
    bool supports_mmap = false;
    bool supports_resize = false;
    off_t max_file_size = std::numeric_limits<off_t>::max();
    bool in_memory = false;
    bool is_case_sensitive = true;
    bool supports_sparse_files = true;
    bool is_slow = false;
    bool supports_fsck_after_every_transaction = false;
    bool has_directory_size_limit = false;
    bool is_journaled = true;
    bool supports_fs_query = true;
    bool supports_watch_event_deleted = true;
  };

  virtual zx::status<std::unique_ptr<FilesystemInstance>> Make(
      const TestFilesystemOptions& options) const = 0;
  virtual zx::status<std::unique_ptr<FilesystemInstance>> Open(
      const TestFilesystemOptions& options) const {
    return zx::error(ZX_ERR_NOT_SUPPORTED);
  }
  virtual const Traits& GetTraits() const = 0;
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

template <typename T>
class FilesystemImplWithDefaultMake : public FilesystemImpl<T> {
 public:
  virtual std::unique_ptr<FilesystemInstance> Create(RamDevice device,
                                                     std::string device_path) const = 0;

  zx::status<std::unique_ptr<FilesystemInstance>> Make(
      const TestFilesystemOptions& options) const override {
    auto result = CreateRamDevice(options);
    if (result.is_error()) {
      return result.take_error();
    }
    auto [device, device_path] = std::move(result).value();
    auto instance = Create(std::move(device), std::move(device_path));
    zx::status<> status = instance->Format(options);
    if (status.is_error()) {
      return status.take_error();
    }
    return zx::ok(std::move(instance));
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
        .supports_fs_query = false,
        .supports_watch_event_deleted = false,
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
  zx::status<> Mount() { return MountWithOptions(default_mount_options); }
  zx::status<> MountWithOptions(const mount_options_t&);

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

// -- Default implementations that use fs-management --

zx::status<> FsFormat(const std::string& device_path, disk_format_t format,
                      const mkfs_options_t& options);

zx::status<> FsMount(const std::string& device_path, const std::string& mount_path,
                     disk_format_t format, const mount_options_t& mount_options,
                     zx::channel* outgoing_directory = nullptr);

// Unmounts using fs/Admin.Shutdown.
zx::status<> FsAdminUnmount(const std::string& mount_path, const zx::channel& outgoing_directory);

zx::status<std::pair<RamDevice, std::string>> OpenRamDevice(const TestFilesystemOptions& options);

}  // namespace fs_test

#endif  // SRC_STORAGE_FS_TEST_FS_TEST_H_
