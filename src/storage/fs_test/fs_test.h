// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_FS_TEST_FS_TEST_H_
#define SRC_STORAGE_FS_TEST_FS_TEST_H_

#include <lib/zx/status.h>
#include <lib/zx/time.h>

#include <string>

#include <fs-management/format.h>

namespace fs_test {

class Filesystem;

struct TestFilesystemOptions {
  static TestFilesystemOptions DefaultMinfs();
  static TestFilesystemOptions DefaultMemfs();

  std::string description;
  bool use_fvm = false;
  uint64_t device_block_size = 0;
  uint64_t device_block_count = 0;
  uint64_t fvm_slice_size = 0;
  const Filesystem* file_system = nullptr;
};

__EXPORT std::ostream& operator<<(std::ostream& out, const TestFilesystemOptions& options);

__EXPORT std::vector<TestFilesystemOptions> AllTestFilesystems();

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

 protected:
  // A wrapper around fs-management that can be used by subclasses if they so wish.
  static zx::status<> Mount(const std::string& device_path, const std::string& mount_path,
                            disk_format_t format);
};

// Base class for all supported file systems. It is a factory class that generates
// instances of FilesystemInstance subclasses.
class Filesystem {
 public:
  struct Traits {
    bool can_unmount = false;
    zx::duration timestamp_granularity = zx::nsec(1);
    bool supports_hard_links = true;
  };

  virtual zx::status<std::unique_ptr<FilesystemInstance>> Make(
      const TestFilesystemOptions& options) const = 0;
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
  const Traits& GetTraits() const override {
    static Traits traits{
        .can_unmount = true,
        .timestamp_granularity = zx::nsec(1),
        .supports_hard_links = true,
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
        .can_unmount = false,
        .timestamp_granularity = zx::nsec(1),
        .supports_hard_links = true,
    };
    return traits;
  }
};

// Helper that creates a test file system with the given options and will clean-up upon destruction.
class __EXPORT TestFilesystem {
 public:
  // Creates and returns a mounted test file system.
  static zx::status<TestFilesystem> Create(const TestFilesystemOptions& options);

  TestFilesystem(TestFilesystem&&) = default;
  TestFilesystem& operator=(TestFilesystem&&) = default;

  ~TestFilesystem();

  const std::string& mount_path() const { return mount_path_; }
  bool is_mounted() const { return mounted_; }

  // Mounts the file system (only necessary after calling Unmount).
  zx::status<> Mount();

  // Unmounts a mounted file system.
  zx::status<> Unmount();

  // Runs fsck on the file system. Does not automatically unmount, so Unmount should be
  // called first if that is required.
  zx::status<> Fsck();

  const Filesystem::Traits& GetTraits() const { return options_.file_system->GetTraits(); }

 private:
  TestFilesystem(const TestFilesystemOptions& options,
                 std::unique_ptr<FilesystemInstance> file_system, const std::string& mount_path)
      : options_(options), file_system_(std::move(file_system)), mount_path_(mount_path) {}

  TestFilesystemOptions options_;
  std::unique_ptr<FilesystemInstance> file_system_;
  std::string mount_path_;
  bool mounted_ = false;
};

}  // namespace fs_test

#endif  // SRC_STORAGE_FS_TEST_FS_TEST_H_
