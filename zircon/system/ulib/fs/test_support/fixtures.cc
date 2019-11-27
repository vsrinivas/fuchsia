// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fs/test_support/fixtures.h"

#include <fcntl.h>
#include <fuchsia/device/c/fidl.h>
#include <fuchsia/device/llcpp/fidl.h>
#include <fuchsia/hardware/block/partition/c/fidl.h>
#include <fuchsia/hardware/block/volume/c/fidl.h>
#include <lib/fzl/fdio.h>
#include <limits.h>

#include <fbl/unique_fd.h>
#include <fs-management/fvm.h>
#include <fvm/format.h>
#include <zxtest/zxtest.h>

namespace {

constexpr char kFvmDriverLib[] = "/boot/driver/fvm.so";

}  // namespace

namespace fs {

FilesystemTest::FilesystemTest(FsTestType type)
    : type_(type), environment_(g_environment), device_path_(environment_->device_path()) {}

void FilesystemTest::SetUp() {
  ASSERT_OK(mkfs(device_path_.c_str(), format_type(), launch_stdio_sync, &default_mkfs_options));
  Mount();
}

void FilesystemTest::TearDown() {
  if (environment_->ramdisk()) {
    environment_->ramdisk()->WakeUp();
  }
  if (mounted_) {
    CheckInfo();  // Failures here should not prevent unmount.
  }
  ASSERT_NO_FAILURES(Unmount());
  ASSERT_OK(CheckFs());
}

void FilesystemTest::Remount() {
  ASSERT_NO_FAILURES(Unmount());
  ASSERT_OK(CheckFs());
  Mount();
}

void FilesystemTest::Mount() {
  ASSERT_FALSE(mounted_);
  int flags = read_only_ ? O_RDONLY : O_RDWR;

  fbl::unique_fd fd(open(device_path_.c_str(), flags));
  ASSERT_TRUE(fd);

  mount_options_t options = default_mount_options;
  options.enable_journal = environment_->use_journal();
  options.enable_pager = environment_->use_pager();

  if (read_only_) {
    options.readonly = true;
  }

  // fd consumed by mount. By default, mount waits until the filesystem is
  // ready to accept commands.
  ASSERT_OK(mount(fd.release(), mount_path(), format_type(), &options, launch_stdio_async));
  mounted_ = true;
}

void FilesystemTest::Unmount() {
  if (!mounted_) {
    return;
  }

  // Unmount will propagate the result of sync; for cases where the filesystem is disconnected
  // from the underlying device, ZX_ERR_IO_REFUSED is expected.
  zx_status_t status = umount(mount_path());
  ASSERT_TRUE(status == ZX_OK || status == ZX_ERR_IO_REFUSED);
  mounted_ = false;
}

void FilesystemTest::GetFsInfo(fuchsia_io_FilesystemInfo* info) {
  fbl::unique_fd fd(open(mount_path(), O_RDONLY | O_DIRECTORY));
  ASSERT_TRUE(fd);

  zx_status_t status;
  fzl::FdioCaller caller(std::move(fd));
  ASSERT_OK(fuchsia_io_DirectoryAdminQueryFilesystem(caller.borrow_channel(), &status, info));
  ASSERT_OK(status);
}

zx_status_t FilesystemTest::CheckFs() {
  fsck_options_t test_fsck_options = {
      .verbose = false,
      .never_modify = true,
      .always_modify = false,
      .force = true,
      .apply_journal = true,
  };
  return fsck(device_path_.c_str(), format_type(), &test_fsck_options, launch_stdio_sync);
}

void FilesystemTestWithFvm::SetUp() {
  ASSERT_NO_FAILURES(FvmSetUp());
  FilesystemTest::SetUp();
}

void FilesystemTestWithFvm::TearDown() {
  FilesystemTest::TearDown();
  ASSERT_OK(fvm_destroy(partition_path_.c_str()));
}

void FilesystemTestWithFvm::FvmSetUp() {
  fvm_path_.assign(device_path_);
  fvm_path_.append("/fvm");

  ASSERT_NO_FAILURES(CheckPartitionSize());

  CreatePartition();
}

void FilesystemTestWithFvm::BindFvm() {
  fbl::unique_fd fd(open(device_path_.c_str(), O_RDWR));
  ASSERT_TRUE(fd, "Could not open test disk");
  ASSERT_OK(fvm_init(fd.get(), GetSliceSize()));

  fzl::FdioCaller caller(std::move(fd));
  zx_status_t status;
  auto resp = ::llcpp::fuchsia::device::Controller::Call::Bind(
      zx::unowned_channel(caller.borrow_channel()),
      ::fidl::StringView(kFvmDriverLib, strlen(kFvmDriverLib)));
  status = resp.status();

  ASSERT_OK(status, "Could not send bind to FVM driver");
  if (resp->result.is_err()) {
    status = resp->result.err();
  }
  // TODO(fxb/39460) Prevent ALREADY_BOUND from being an option
  if (!(status == ZX_OK || status == ZX_ERR_ALREADY_BOUND)) {
    ASSERT_TRUE(false, "Could not bind disk to FVM driver (or failed to find existing bind)");
  }
  ASSERT_OK(wait_for_device(fvm_path_.c_str(), zx::sec(10).get()));
}

void FilesystemTestWithFvm::CreatePartition() {
  ASSERT_NO_FAILURES(BindFvm());

  fbl::unique_fd fd(open(fvm_path_.c_str(), O_RDWR));
  ASSERT_TRUE(fd, "Could not open FVM driver");

  std::string name("fs-test-partition");
  fzl::FdioCaller caller(std::move(fd));
  auto type = reinterpret_cast<const fuchsia_hardware_block_partition_GUID*>(kTestPartGUID);
  auto guid = reinterpret_cast<const fuchsia_hardware_block_partition_GUID*>(kTestUniqueGUID);
  zx_status_t status;
  zx_status_t io_status = fuchsia_hardware_block_volume_VolumeManagerAllocatePartition(
      caller.borrow_channel(), 1, type, guid, name.c_str(), name.size() + 1, 0, &status);
  ASSERT_OK(io_status, "Could not send message to FVM driver");
  ASSERT_OK(status, "Could not allocate FVM partition");

  std::string path(fvm_path_);
  path.append("/");
  path.append(name);
  path.append("-p-1/block");

  ASSERT_OK(wait_for_device(path.c_str(), zx::sec(10).get()));

  // The base test must see the FVM volume as the device to work with.
  partition_path_.swap(device_path_);
  device_path_.assign(path);
}

FixedDiskSizeTest::FixedDiskSizeTest(uint64_t disk_size) {
  const int kBlockSize = 512;
  uint64_t num_blocks = disk_size / kBlockSize;
  ramdisk_ = std::make_unique<RamDisk>(environment_->devfs_root(), kBlockSize, num_blocks);
  device_path_ = ramdisk_->path();
}

FixedDiskSizeTestWithFvm::FixedDiskSizeTestWithFvm(uint64_t disk_size) {
  const int kBlockSize = 512;
  uint64_t num_blocks = disk_size / kBlockSize;
  ramdisk_ = std::make_unique<RamDisk>(environment_->devfs_root(), kBlockSize, num_blocks);
  device_path_ = ramdisk_->path();
}

}  // namespace fs
