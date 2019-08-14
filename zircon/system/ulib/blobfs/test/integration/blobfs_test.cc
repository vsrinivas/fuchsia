// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "blobfs_test.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <sys/stat.h>

#include <fbl/unique_fd.h>
#include <fs-management/fvm.h>
#include <fs-management/mount.h>
#include <fuchsia/device/c/fidl.h>
#include <fuchsia/io/c/fidl.h>
#include <fvm/format.h>
#include <lib/fzl/fdio.h>
#include <zxtest/zxtest.h>

namespace {

constexpr char kFvmDriverLib[] = "/boot/driver/fvm.so";

bool GetFsInfo(fuchsia_io_FilesystemInfo* info) {
  fbl::unique_fd fd(open(kMountPath, O_RDONLY | O_DIRECTORY));
  if (!fd) {
    return false;
  }

  zx_status_t status;
  fzl::FdioCaller caller(std::move(fd));
  zx_status_t io_status =
      fuchsia_io_DirectoryAdminQueryFilesystem(caller.borrow_channel(), &status, info);
  if (io_status != ZX_OK) {
    status = io_status;
  }

  if (status != ZX_OK) {
    printf("Could not query block FS info: %s\n", zx_status_get_string(status));
    return false;
  }
  return true;
}

}  // namespace

BlobfsTest::BlobfsTest(FsTestType type)
    : type_(type), environment_(g_environment), device_path_(environment_->device_path()) {}

void BlobfsTest::SetUp() {
  ASSERT_TRUE(mkdir(kMountPath, 0755) == 0 || errno == EEXIST, "Could not create mount point");
  ASSERT_OK(
      mkfs(device_path_.c_str(), DISK_FORMAT_BLOBFS, launch_stdio_sync, &default_mkfs_options));
  Mount();
}

void BlobfsTest::TearDown() {
  if (environment_->ramdisk()) {
    environment_->ramdisk()->WakeUp();
  }
  CheckInfo();  // Failures here should not prevent unmount.
  ASSERT_NO_FAILURES(Unmount());
  ASSERT_OK(CheckFs());
}

void BlobfsTest::Remount() {
  ASSERT_NO_FAILURES(Unmount());
  ASSERT_OK(CheckFs());
  Mount();
}

void BlobfsTest::Mount() {
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
  ASSERT_OK(mount(fd.release(), kMountPath, DISK_FORMAT_BLOBFS, &options, launch_stdio_async));
  mounted_ = true;
}

void BlobfsTest::Unmount() {
  if (!mounted_) {
    return;
  }
  ASSERT_OK(umount(kMountPath));
  mounted_ = false;
}

zx_status_t BlobfsTest::CheckFs() {
  fsck_options_t test_fsck_options = {
      .verbose = false,
      .never_modify = true,
      .always_modify = false,
      .force = true,
      .apply_journal = true,
  };
  return fsck(device_path_.c_str(), DISK_FORMAT_BLOBFS, &test_fsck_options, launch_stdio_sync);
}

void BlobfsTest::CheckInfo() {
  fuchsia_io_FilesystemInfo info;
  ASSERT_TRUE(GetFsInfo(&info));

  const char kFsName[] = "blobfs";
  const char* name = reinterpret_cast<const char*>(info.name);
  ASSERT_STR_EQ(kFsName, name);
  ASSERT_LE(info.used_nodes, info.total_nodes, "Used nodes greater than free nodes");
  ASSERT_LE(info.used_bytes, info.total_bytes, "Used bytes greater than free bytes");
}

void BlobfsTestWithFvm::SetUp() {
  fvm_path_.assign(device_path_);
  fvm_path_.append("/fvm");

  // Minimum size required by ResizePartition test:
  const size_t kMinDataSize = 507 * kTestFvmSliceSize;
  const size_t kMinFvmSize =
      fvm::MetadataSize(kMinDataSize, kTestFvmSliceSize) * 2 + kMinDataSize;  // ~8.5mb
  ASSERT_GE(environment_->disk_size(), kMinFvmSize, "Insufficient disk space for FVM tests");

  CreatePartition();
  BlobfsTest::SetUp();
}

void BlobfsTestWithFvm::TearDown() {
  BlobfsTest::TearDown();
  ASSERT_OK(fvm_destroy(partition_path_.c_str()));
}

void BlobfsTestWithFvm::BindFvm() {
  fbl::unique_fd fd(open(device_path_.c_str(), O_RDWR));
  ASSERT_TRUE(fd, "Could not open test disk");
  ASSERT_OK(fvm_init(fd.get(), kTestFvmSliceSize));

  fzl::FdioCaller caller(std::move(fd));
  zx_status_t status;
  zx_status_t io_status = fuchsia_device_ControllerBind(caller.borrow_channel(), kFvmDriverLib,
                                                        sizeof(kFvmDriverLib) - 1, &status);
  ASSERT_OK(io_status, "Could not send bind to FVM driver");
  ASSERT_OK(status, "Could not bind disk to FVM driver");
  ASSERT_OK(wait_for_device(fvm_path_.c_str(), zx::sec(10).get()));
}

void BlobfsTestWithFvm::CreatePartition() {
  ASSERT_EQ(kTestFvmSliceSize % blobfs::kBlobfsBlockSize, 0);
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

void MakeBlob(const fs_test_utils::BlobInfo* info, fbl::unique_fd* fd) {
  fd->reset(open(info->path, O_CREAT | O_RDWR));
  ASSERT_TRUE(*fd, "Failed to create blob");
  ASSERT_EQ(ftruncate(fd->get(), info->size_data), 0);
  ASSERT_EQ(fs_test_utils::StreamAll(write, fd->get(), info->data.get(), info->size_data), 0,
            "Failed to write Data");
  ASSERT_TRUE(fs_test_utils::VerifyContents(fd->get(), info->data.get(), info->size_data));
}
