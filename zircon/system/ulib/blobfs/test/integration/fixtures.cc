// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fixtures.h"

#include <fcntl.h>
#include <fuchsia/device/c/fidl.h>
#include <fuchsia/io/c/fidl.h>
#include <lib/fzl/fdio.h>
#include <limits.h>

#include <fbl/unique_fd.h>
#include <fs-management/fvm.h>
#include <fvm/format.h>
#include <zxtest/zxtest.h>

namespace {

constexpr char kFvmDriverLib[] = "/boot/driver/fvm.so";

}  // namespace

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
  CheckInfo();  // Failures here should not prevent unmount.
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
  ASSERT_TRUE(fd, "Could not open ramdisk");

  mount_options_t options = default_mount_options;
  options.enable_journal = environment_->use_journal();

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
  fvm_path_.assign(device_path_);
  fvm_path_.append("/fvm");

  ASSERT_NO_FAILURES(CheckPartitionSize());

  CreatePartition();
  FilesystemTest::SetUp();
}

void FilesystemTestWithFvm::TearDown() {
  FilesystemTest::TearDown();
  ASSERT_OK(fvm_destroy(partition_path_.c_str()));
}

void FilesystemTestWithFvm::BindFvm() {
  fbl::unique_fd fd(open(device_path_.c_str(), O_RDWR));
  ASSERT_TRUE(fd, "Could not open test disk");
  ASSERT_OK(fvm_init(fd.get(), GetSliceSize()));

  fzl::FdioCaller caller(std::move(fd));
  zx_status_t status;
  zx_status_t io_status = fuchsia_device_ControllerBind(caller.borrow_channel(), kFvmDriverLib,
                                                        sizeof(kFvmDriverLib) - 1, &status);
  ASSERT_OK(io_status, "Could not send bind to FVM driver");
  ASSERT_OK(status, "Could not bind disk to FVM driver");
  ASSERT_OK(wait_for_device(fvm_path_.c_str(), zx::sec(10).get()));
}

void FilesystemTestWithFvm::CreatePartition() {
  ASSERT_NO_FAILURES(BindFvm());

  fbl::unique_fd fd(open(fvm_path_.c_str(), O_RDWR));
  ASSERT_TRUE(fd, "Could not open FVM driver");

  alloc_req_t request = {};
  request.slice_count = 1;
  strcpy(request.name, "fs-test-partition");
  memcpy(request.type, kTestPartGUID, sizeof(request.type));
  memcpy(request.guid, kTestUniqueGUID, sizeof(request.guid));

  fd.reset(fvm_allocate_partition(fd.get(), &request));
  ASSERT_TRUE(fd, "Could not allocate FVM partition");

  char path[PATH_MAX];
  fd.reset(open_partition(kTestUniqueGUID, kTestPartGUID, 0, path));
  ASSERT_TRUE(fd, "Could not locate FVM partition");

  // The base test must see the FVM volume as the device to work with.
  partition_path_.swap(device_path_);
  device_path_.assign(path);
}
