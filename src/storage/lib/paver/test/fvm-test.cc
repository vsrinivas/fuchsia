// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/lib/paver/fvm.h"

#include <lib/devmgr-integration-test/fixture.h>

#include <fs-management/fvm.h>
#include <fvm/format.h>
#include <fvm/fvm-sparse.h>
#include <zxtest/zxtest.h>

#include "src/storage/lib/paver/test/test-utils.h"

namespace {

using devmgr_integration_test::IsolatedDevmgr;
using devmgr_integration_test::RecursiveWaitForFile;

constexpr size_t kSliceSize = kBlockSize * 2;
constexpr uint8_t kFvmType[GPT_GUID_LEN] = GUID_FVM_VALUE;

constexpr fvm::SparseImage SparseHeaderForSliceSize(size_t slice_size) {
  fvm::SparseImage header = {};
  header.slice_size = slice_size;
  return header;
}

constexpr fvm::SparseImage SparseHeaderForSliceSizeAndMaxDiskSize(size_t slice_size,
                                                                  size_t max_disk_size) {
  fvm::SparseImage header = SparseHeaderForSliceSize(slice_size);
  header.maximum_disk_size = max_disk_size;
  return header;
}

class FvmTest : public zxtest::Test {
 public:
  FvmTest() {
    devmgr_launcher::Args args;
    args.sys_device_driver = IsolatedDevmgr::kSysdevDriver;
    args.driver_search_paths.push_back("/boot/driver");
    args.driver_search_paths.push_back("/boot/driver/test");
    args.disable_block_watcher = true;
    args.path_prefix = "/pkg/";
    ASSERT_OK(IsolatedDevmgr::Create(std::move(args), &devmgr_));

    fbl::unique_fd ctl;
    ASSERT_OK(RecursiveWaitForFile(devmgr_.devfs_root(), "misc/ramctl", &ctl));
  }

  void CreateRamdisk() { CreateRamdiskWithBlockCount(); }

  void CreateRamdiskWithBlockCount(size_t block_count = kBlockCount) {
    ASSERT_NO_FATAL_FAILURES(
        BlockDevice::Create(devmgr_.devfs_root(), kFvmType, block_count, &device_));
    ASSERT_TRUE(device_);
  }

  int borrow_fd() { return device_->fd(); }

  fbl::unique_fd fd() { return fbl::unique_fd(dup(device_->fd())); }

  const fbl::unique_fd& devfs_root() { return devmgr_.devfs_root(); }

 private:
  IsolatedDevmgr devmgr_;
  std::unique_ptr<BlockDevice> device_;
};

TEST_F(FvmTest, FormatFvmEmpty) {
  ASSERT_NO_FAILURES(CreateRamdisk());
  fbl::unique_fd fvm_part = FvmPartitionFormat(
      devfs_root(), fd(), SparseHeaderForSliceSize(kSliceSize), paver::BindOption::Reformat);
  ASSERT_TRUE(fvm_part.is_valid());
}

TEST_F(FvmTest, TryBindEmpty) {
  ASSERT_NO_FAILURES(CreateRamdisk());
  fbl::unique_fd fvm_part = FvmPartitionFormat(
      devfs_root(), fd(), SparseHeaderForSliceSize(kSliceSize), paver::BindOption::TryBind);
  ASSERT_TRUE(fvm_part.is_valid());
}

TEST_F(FvmTest, TryBindAlreadyFormatted) {
  ASSERT_NO_FAILURES(CreateRamdisk());
  ASSERT_OK(fvm_init(borrow_fd(), kSliceSize));
  fbl::unique_fd fvm_part = FvmPartitionFormat(
      devfs_root(), fd(), SparseHeaderForSliceSize(kSliceSize), paver::BindOption::TryBind);
  ASSERT_TRUE(fvm_part.is_valid());
}

TEST_F(FvmTest, TryBindAlreadyBound) {
  ASSERT_NO_FAILURES(CreateRamdisk());
  fbl::unique_fd fvm_part = FvmPartitionFormat(
      devfs_root(), fd(), SparseHeaderForSliceSize(kSliceSize), paver::BindOption::Reformat);
  ASSERT_TRUE(fvm_part.is_valid());

  fvm_part = FvmPartitionFormat(devfs_root(), fd(), SparseHeaderForSliceSize(kSliceSize),
                                paver::BindOption::TryBind);
  ASSERT_TRUE(fvm_part.is_valid());
}

TEST_F(FvmTest, TryBindAlreadyFormattedWrongSliceSize) {
  ASSERT_NO_FAILURES(CreateRamdisk());
  ASSERT_OK(fvm_init(borrow_fd(), kSliceSize * 2));
  fbl::unique_fd fvm_part = FvmPartitionFormat(
      devfs_root(), fd(), SparseHeaderForSliceSize(kSliceSize), paver::BindOption::TryBind);
  ASSERT_TRUE(fvm_part.is_valid());
}

TEST_F(FvmTest, TryBindAlreadyFormattedWithSmallerSize) {
  constexpr size_t kBlockDeviceInitialSize = 1000 * kSliceSize;
  constexpr size_t kBlockDeviceMaxSize = 100000 * kSliceSize;
  ASSERT_NO_FAILURES(CreateRamdiskWithBlockCount(kBlockDeviceMaxSize / kBlockSize));
  ASSERT_OK(
      fvm_init_preallocated(borrow_fd(), kBlockDeviceInitialSize, kBlockDeviceMaxSize, kSliceSize));
  // Same slice size but can reference up to 200 Slices, which is far less than what the
  // preallocated can have.
  fvm::SparseImage header =
      SparseHeaderForSliceSizeAndMaxDiskSize(kSliceSize, 2 * kBlockDeviceInitialSize);
  paver::FormatResult result;
  fbl::unique_fd fvm_part =
      FvmPartitionFormat(devfs_root(), fd(), header, paver::BindOption::TryBind, &result);
  ASSERT_TRUE(fvm_part.is_valid());
  ASSERT_EQ(paver::FormatResult::kPreserved, result);
}

TEST_F(FvmTest, TryBindAlreadyFormattedWithBiggerSize) {
  constexpr size_t kBlockDeviceInitialSize = 1000 * kSliceSize;
  constexpr size_t kBlockDeviceMaxSize = 100000 * kSliceSize;
  ASSERT_NO_FAILURES(CreateRamdiskWithBlockCount(kBlockDeviceMaxSize / kBlockSize));
  ASSERT_OK(fvm_init_preallocated(borrow_fd(), kBlockDeviceInitialSize, kBlockDeviceMaxSize / 100,
                                  kSliceSize));
  // Same slice size but can reference up to 200 Slices, which is far less than what the
  // preallocated can have.
  fvm::SparseImage header = SparseHeaderForSliceSizeAndMaxDiskSize(kSliceSize, kBlockDeviceMaxSize);
  paver::FormatResult result;
  fbl::unique_fd fvm_part =
      FvmPartitionFormat(devfs_root(), fd(), header, paver::BindOption::TryBind, &result);
  ASSERT_TRUE(fvm_part.is_valid());
  ASSERT_EQ(paver::FormatResult::kReformatted, result);
}

TEST_F(FvmTest, AllocateEmptyPartitions) {
  ASSERT_NO_FAILURES(CreateRamdisk());
  fbl::unique_fd fvm_part = FvmPartitionFormat(
      devfs_root(), fd(), SparseHeaderForSliceSize(kSliceSize), paver::BindOption::Reformat);
  ASSERT_TRUE(fvm_part.is_valid());

  ASSERT_OK(paver::AllocateEmptyPartitions(devfs_root(), fvm_part));

  fbl::unique_fd blob(
      openat(devfs_root().get(), "misc/ramctl/ramdisk-0/block/fvm/blobfs-p-1/block", O_RDONLY));
  ASSERT_TRUE(blob.is_valid());

  fbl::unique_fd data(
      openat(devfs_root().get(), "misc/ramctl/ramdisk-0/block/fvm/minfs-p-2/block", O_RDONLY));
  ASSERT_TRUE(data.is_valid());
}

TEST_F(FvmTest, Unbind) {
  ASSERT_NO_FAILURES(CreateRamdisk());
  fbl::unique_fd fvm_part = FvmPartitionFormat(
      devfs_root(), fd(), SparseHeaderForSliceSize(kSliceSize), paver::BindOption::Reformat);
  ASSERT_TRUE(fvm_part.is_valid());

  ASSERT_OK(paver::AllocateEmptyPartitions(devfs_root(), fvm_part));

  fbl::unique_fd blob(
      openat(devfs_root().get(), "misc/ramctl/ramdisk-0/block/fvm/blobfs-p-1/block", O_RDONLY));
  ASSERT_TRUE(blob.is_valid());

  fbl::unique_fd data(
      openat(devfs_root().get(), "misc/ramctl/ramdisk-0/block/fvm/minfs-p-2/block", O_RDONLY));
  ASSERT_TRUE(data.is_valid());
  ASSERT_OK(paver::FvmUnbind(devfs_root(), "/dev/misc/ramctl/ramdisk-0/block"));
  fvm_part.reset();
  blob.reset();
  data.reset();
}

TEST_F(FvmTest, UnbindInvalidPath) {
  ASSERT_NO_FAILURES(CreateRamdisk());
  fbl::unique_fd fvm_part = FvmPartitionFormat(
      devfs_root(), fd(), SparseHeaderForSliceSize(kSliceSize), paver::BindOption::Reformat);
  ASSERT_TRUE(fvm_part.is_valid());

  ASSERT_OK(paver::AllocateEmptyPartitions(devfs_root(), fvm_part));

  fbl::unique_fd blob(
      openat(devfs_root().get(), "misc/ramctl/ramdisk-0/block/fvm/blobfs-p-1/block", O_RDONLY));
  ASSERT_TRUE(blob.is_valid());

  fbl::unique_fd data(
      openat(devfs_root().get(), "misc/ramctl/ramdisk-0/block/fvm/minfs-p-2/block", O_RDONLY));
  ASSERT_TRUE(data.is_valid());

  // Path too short
  ASSERT_EQ(paver::FvmUnbind(devfs_root(), "/dev"), ZX_ERR_INVALID_ARGS);

  // Path too long
  char path[PATH_MAX + 2];
  memset(path, 'a', sizeof(path));
  path[sizeof(path) - 1] = '\0';
  ASSERT_EQ(paver::FvmUnbind(devfs_root(), path), ZX_ERR_INVALID_ARGS);

  ASSERT_OK(paver::FvmUnbind(devfs_root(), "/dev/misc/ramctl/ramdisk-0/block"));
  fvm_part.reset();
  blob.reset();
  data.reset();
}

}  // namespace
