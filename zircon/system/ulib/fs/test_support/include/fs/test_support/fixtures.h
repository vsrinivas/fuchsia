// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FS_TEST_SUPPORT_FIXTURES_H_
#define FS_TEST_SUPPORT_FIXTURES_H_

#include <fuchsia/io/llcpp/fidl.h>

#include <string>
#include <optional>

#include <fbl/macros.h>
#include <fbl/unique_fd.h>
#include <fs-management/mount.h>
#include <zxtest/zxtest.h>

#include "environment.h"

namespace fs {

constexpr uint8_t kTestUniqueGUID[] = {0xFF, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
                                       0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f};

constexpr uint8_t kTestPartGUID[] = {0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
                                     0xFF, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07};

enum class FsTestType {
  kGeneric,  // Use a generic block device.
  kFvm       // Use an FVM device.
};

class FilesystemTest : public zxtest::Test {
 public:
  explicit FilesystemTest(FsTestType type = FsTestType::kGeneric);

  // zxtest::Test interface:
  void SetUp() override;
  void TearDown() override;

  // Unmounts and remounts the filesystem, verifying integrity in between.
  void Remount();

  // Mounts the filesystem.
  void Mount();

  // Unmounts the filesystem, without performing any additional test.
  void Unmount();

  // Queries the filesystem for generic info.
  void GetFsInfo(::llcpp::fuchsia::io::FilesystemInfo* info);

  void set_read_only(bool read_only) { read_only_ = read_only; }
  const std::string& device_path() const { return device_path_; }
  disk_format_type format_type() const { return environment_->format_type(); }
  const char* mount_path() const { return environment_->mount_path(); }

  DISALLOW_COPY_ASSIGN_AND_MOVE(FilesystemTest);


 protected:
  // Helper function for launching a filesystem and exposing it to tests. Including:
  // - Parse the filesystem from |device_id|, assuming it is in |disk_format|.
  // - Populate |export_root_| with the outgoing directory of the filesystem server.
  // - Mount the data root directory at |mount_path| using the deprecated
  //   |fuchsia.io/DirectoryAdmin| mounting utility. TODO(fxb/34530): This will be
  //   replaced by process-local mounting.
  // It does not register the filesystem with the |fuchsia.fshost/Registry|,
  // as registration is generally meant for production filesystem instances.
  zx_status_t MountInternal(fbl::unique_fd device_fd, const char* mount_path,
                            disk_format_t disk_format, const init_options_t* init_options);
  zx_status_t CheckFs();
  virtual void CheckInfo() {}

  FsTestType type_;
  Environment* environment_;
  std::string device_path_;
  bool read_only_ = false;
  bool mounted_ = false;
  std::optional<llcpp::fuchsia::io::Directory::SyncClient> export_root_;
};

class FilesystemTestWithFvm : public FilesystemTest {
 public:
  FilesystemTestWithFvm() : FilesystemTest(FsTestType::kFvm) {}

  // zxtest::Test interface:
  void SetUp() override;
  void TearDown() override;

  const std::string& partition_path() const { return partition_path_; }

  // Derived fixtures can define any slice size.
  virtual size_t GetSliceSize() const { return 1 << 16; }

  DISALLOW_COPY_ASSIGN_AND_MOVE(FilesystemTestWithFvm);

 protected:
  void FvmSetUp();

 private:
  void BindFvm();
  void CreatePartition();
  virtual void CheckPartitionSize() {}

  std::string fvm_path_;
  std::string partition_path_;
};

// Base class for tests that create a dedicated disk of a given size.
class FixedDiskSizeTest : public FilesystemTest {
 public:
  explicit FixedDiskSizeTest(uint64_t disk_size);

 private:
  std::unique_ptr<RamDisk> ramdisk_;
};

// Base class for tests that create a dedicated disk of a given size.
class FixedDiskSizeTestWithFvm : public FilesystemTestWithFvm {
 public:
  explicit FixedDiskSizeTestWithFvm(uint64_t disk_size);

 private:
  std::unique_ptr<RamDisk> ramdisk_;
};

}  // namespace fs

#endif  // FS_TEST_SUPPORT_FIXTURES_H_
