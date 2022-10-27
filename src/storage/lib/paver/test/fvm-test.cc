// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/lib/paver/fvm.h"

#include <lib/driver-integration-test/fixture.h>
#include <lib/fdio/fd.h>
#include <lib/sys/component/cpp/service_client.h>
#include <lib/zx/result.h>

#include <zxtest/zxtest.h>

#include "src/lib/storage/fs_management/cpp/fvm.h"
#include "src/storage/fvm/format.h"
#include "src/storage/fvm/fvm_sparse.h"
#include "src/storage/lib/paver/test/test-utils.h"

namespace {

using device_watcher::RecursiveWaitForFile;
using driver_integration_test::IsolatedDevmgr;

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
    IsolatedDevmgr::Args args;
    args.disable_block_watcher = true;

    ASSERT_OK(IsolatedDevmgr::Create(&args, &devmgr_));

    fbl::unique_fd ctl;
    ASSERT_OK(RecursiveWaitForFile(devmgr_.devfs_root(), "sys/platform/00:00:2d/ramctl", &ctl));
  }

  void CreateRamdisk() { CreateRamdiskWithBlockCount(); }

  void CreateRamdiskWithBlockCount(size_t block_count = kBlockCount) {
    ASSERT_NO_FATAL_FAILURE(
        BlockDevice::Create(devmgr_.devfs_root(), kFvmType, block_count, &device_));
    ASSERT_TRUE(device_);
  }

  fidl::UnownedClientEnd<fuchsia_hardware_block::Block> block_interface() {
    return device_->block_interface();
  }

  zx::result<fbl::unique_fd> fd() {
    // TODO(https://fxbug.dev/112484): this relies on multiplexing.
    fidl::UnownedClientEnd client = device_->block_interface();
    zx::result owned = component::Clone(client, component::AssumeProtocolComposesNode);
    if (owned.is_error()) {
      return owned.take_error();
    }
    fbl::unique_fd fd;
    zx_status_t status =
        fdio_fd_create(owned.value().TakeChannel().release(), fd.reset_and_get_address());
    return zx::make_result(status, std::move(fd));
  }

  const fbl::unique_fd& devfs_root() { return devmgr_.devfs_root(); }

 protected:
  IsolatedDevmgr devmgr_;
  std::unique_ptr<BlockDevice> device_;
};

TEST_F(FvmTest, FormatFvmEmpty) {
  ASSERT_NO_FAILURES(CreateRamdisk());
  zx::result fd = this->fd();
  ASSERT_OK(fd.status_value());
  fbl::unique_fd fvm_part =
      FvmPartitionFormat(devfs_root(), std::move(fd.value()), SparseHeaderForSliceSize(kSliceSize),
                         paver::BindOption::Reformat);
  ASSERT_TRUE(fvm_part.is_valid());
}

TEST_F(FvmTest, TryBindEmpty) {
  ASSERT_NO_FAILURES(CreateRamdisk());
  zx::result fd = this->fd();
  ASSERT_OK(fd.status_value());
  fbl::unique_fd fvm_part =
      FvmPartitionFormat(devfs_root(), std::move(fd.value()), SparseHeaderForSliceSize(kSliceSize),
                         paver::BindOption::TryBind);
  ASSERT_TRUE(fvm_part.is_valid());
}

TEST_F(FvmTest, TryBindAlreadyFormatted) {
  ASSERT_NO_FAILURES(CreateRamdisk());
  ASSERT_OK(fs_management::FvmInit(block_interface(), kSliceSize));
  zx::result fd = this->fd();
  ASSERT_OK(fd.status_value());
  fbl::unique_fd fvm_part =
      FvmPartitionFormat(devfs_root(), std::move(fd.value()), SparseHeaderForSliceSize(kSliceSize),
                         paver::BindOption::TryBind);
  ASSERT_TRUE(fvm_part.is_valid());
}

TEST_F(FvmTest, TryBindAlreadyBound) {
  ASSERT_NO_FAILURES(CreateRamdisk());
  zx::result fd1 = fd();
  ASSERT_OK(fd1.status_value());
  fbl::unique_fd fvm_part =
      FvmPartitionFormat(devfs_root(), std::move(fd1.value()), SparseHeaderForSliceSize(kSliceSize),
                         paver::BindOption::Reformat);
  ASSERT_TRUE(fvm_part.is_valid());

  zx::result fd2 = fd();
  ASSERT_OK(fd2.status_value());
  fvm_part = FvmPartitionFormat(devfs_root(), std::move(fd2.value()),
                                SparseHeaderForSliceSize(kSliceSize), paver::BindOption::TryBind);
  ASSERT_TRUE(fvm_part.is_valid());
}

TEST_F(FvmTest, TryBindAlreadyFormattedWrongSliceSize) {
  ASSERT_NO_FAILURES(CreateRamdisk());
  zx::result fd = this->fd();
  ASSERT_OK(fd.status_value());
  ASSERT_OK(fs_management::FvmInit(block_interface(), kSliceSize * 2));
  fbl::unique_fd fvm_part =
      FvmPartitionFormat(devfs_root(), std::move(fd.value()), SparseHeaderForSliceSize(kSliceSize),
                         paver::BindOption::TryBind);
  ASSERT_TRUE(fvm_part.is_valid());
}

TEST_F(FvmTest, TryBindAlreadyFormattedWithSmallerSize) {
  constexpr size_t kBlockDeviceInitialSize = 1000 * kSliceSize;
  constexpr size_t kBlockDeviceMaxSize = 100000 * kSliceSize;
  ASSERT_NO_FAILURES(CreateRamdiskWithBlockCount(kBlockDeviceMaxSize / kBlockSize));
  ASSERT_OK(fs_management::FvmInitPreallocated(block_interface(), kBlockDeviceInitialSize,
                                               kBlockDeviceMaxSize, kSliceSize));
  // Same slice size but can reference up to 200 Slices, which is far less than what the
  // preallocated can have.
  fvm::SparseImage header =
      SparseHeaderForSliceSizeAndMaxDiskSize(kSliceSize, 2 * kBlockDeviceInitialSize);
  paver::FormatResult result;
  zx::result fd = this->fd();
  ASSERT_OK(fd.status_value());
  fbl::unique_fd fvm_part = FvmPartitionFormat(devfs_root(), std::move(fd.value()), header,
                                               paver::BindOption::TryBind, &result);
  ASSERT_TRUE(fvm_part.is_valid());
  ASSERT_EQ(paver::FormatResult::kPreserved, result);
}

TEST_F(FvmTest, TryBindAlreadyFormattedWithBiggerSize) {
  constexpr size_t kBlockDeviceInitialSize = 1000 * kSliceSize;
  constexpr size_t kBlockDeviceMaxSize = 100000 * kSliceSize;
  ASSERT_NO_FAILURES(CreateRamdiskWithBlockCount(kBlockDeviceMaxSize / kBlockSize));
  ASSERT_OK(fs_management::FvmInitPreallocated(block_interface(), kBlockDeviceInitialSize,
                                               kBlockDeviceMaxSize / 100, kSliceSize));
  // Same slice size but can reference up to 200 Slices, which is far less than what the
  // preallocated can have.
  fvm::SparseImage header = SparseHeaderForSliceSizeAndMaxDiskSize(kSliceSize, kBlockDeviceMaxSize);
  paver::FormatResult result;
  zx::result fd = this->fd();
  ASSERT_OK(fd.status_value());
  fbl::unique_fd fvm_part = FvmPartitionFormat(devfs_root(), std::move(fd.value()), header,
                                               paver::BindOption::TryBind, &result);
  ASSERT_TRUE(fvm_part.is_valid());
  ASSERT_EQ(paver::FormatResult::kReformatted, result);
}

TEST_F(FvmTest, AllocateEmptyPartitions) {
  ASSERT_NO_FAILURES(CreateRamdisk());
  zx::result fd = this->fd();
  ASSERT_OK(fd.status_value());
  fbl::unique_fd fvm_part =
      FvmPartitionFormat(devfs_root(), std::move(fd.value()), SparseHeaderForSliceSize(kSliceSize),
                         paver::BindOption::Reformat);
  ASSERT_TRUE(fvm_part.is_valid());

  ASSERT_OK(paver::AllocateEmptyPartitions(devfs_root(), fvm_part));

  fbl::unique_fd blob(openat(devfs_root().get(),
                             "sys/platform/00:00:2d/ramctl/ramdisk-0/block/fvm/blobfs-p-1/block",
                             O_RDONLY));
  ASSERT_TRUE(blob.is_valid());

  fbl::unique_fd data(openat(devfs_root().get(),
                             "sys/platform/00:00:2d/ramctl/ramdisk-0/block/fvm/data-p-2/block",
                             O_RDONLY));
  ASSERT_TRUE(data.is_valid());
}

TEST_F(FvmTest, WipeWithMultipleFvm) {
  ASSERT_NO_FAILURES(CreateRamdisk());
  zx::result fd1 = fd();
  ASSERT_OK(fd1.status_value());
  fbl::unique_fd fvm_part1 =
      FvmPartitionFormat(devfs_root(), std::move(fd1.value()), SparseHeaderForSliceSize(kSliceSize),
                         paver::BindOption::Reformat);
  ASSERT_TRUE(fvm_part1.is_valid());

  ASSERT_OK(paver::AllocateEmptyPartitions(devfs_root(), fvm_part1));

  {
    fbl::unique_fd blob(openat(devfs_root().get(),
                               "sys/platform/00:00:2d/ramctl/ramdisk-0/block/fvm/blobfs-p-1/block",
                               O_RDONLY));
    ASSERT_TRUE(blob.is_valid());

    fbl::unique_fd data(openat(devfs_root().get(),
                               "sys/platform/00:00:2d/ramctl/ramdisk-0/block/fvm/data-p-2/block",
                               O_RDONLY));
    ASSERT_TRUE(data.is_valid());
  }

  // Save the old device.
  std::unique_ptr<BlockDevice> first_device = std::move(device_);

  ASSERT_NO_FAILURES(CreateRamdisk());
  zx::result fd2 = fd();
  ASSERT_OK(fd2.status_value());
  fbl::unique_fd fvm_part2 =
      FvmPartitionFormat(devfs_root(), std::move(fd2.value()), SparseHeaderForSliceSize(kSliceSize),
                         paver::BindOption::Reformat);
  ASSERT_TRUE(fvm_part2.is_valid());

  ASSERT_OK(paver::AllocateEmptyPartitions(devfs_root(), fvm_part2));

  {
    fbl::unique_fd blob(openat(devfs_root().get(),
                               "sys/platform/00:00:2d/ramctl/ramdisk-1/block/fvm/blobfs-p-1/block",
                               O_RDONLY));
    ASSERT_TRUE(blob.is_valid());

    fbl::unique_fd data(openat(devfs_root().get(),
                               "sys/platform/00:00:2d/ramctl/ramdisk-1/block/fvm/data-p-2/block",
                               O_RDONLY));
    ASSERT_TRUE(data.is_valid());
  }

  std::array<uint8_t, fvm::kGuidSize> blobfs_guid = GUID_BLOB_VALUE;
  ASSERT_OK(paver::WipeAllFvmPartitionsWithGuid(fvm_part2, blobfs_guid.data()));

  // Check we can still open the first ramdisk's blobfs:
  {
    fbl::unique_fd blob(openat(devfs_root().get(),
                               "sys/platform/00:00:2d/ramctl/ramdisk-0/block/fvm/blobfs-p-1/block",
                               O_RDONLY));
    ASSERT_TRUE(blob.is_valid());
  }

  // But not the second's.
  {
    fbl::unique_fd blob(openat(devfs_root().get(),
                               "sys/platform/00:00:2d/ramctl/ramdisk-1/block/fvm/blobfs-p-1/block",
                               O_RDONLY));
    ASSERT_TRUE(blob.is_valid());
  }
}

TEST_F(FvmTest, Unbind) {
  ASSERT_NO_FAILURES(CreateRamdisk());
  zx::result fd = this->fd();
  ASSERT_OK(fd.status_value());
  fbl::unique_fd fvm_part =
      FvmPartitionFormat(devfs_root(), std::move(fd.value()), SparseHeaderForSliceSize(kSliceSize),
                         paver::BindOption::Reformat);
  ASSERT_TRUE(fvm_part.is_valid());

  ASSERT_OK(paver::AllocateEmptyPartitions(devfs_root(), fvm_part));

  fbl::unique_fd blob(openat(devfs_root().get(),
                             "sys/platform/00:00:2d/ramctl/ramdisk-0/block/fvm/blobfs-p-1/block",
                             O_RDONLY));
  ASSERT_TRUE(blob.is_valid());

  fbl::unique_fd data(openat(devfs_root().get(),
                             "sys/platform/00:00:2d/ramctl/ramdisk-0/block/fvm/data-p-2/block",
                             O_RDONLY));
  ASSERT_TRUE(data.is_valid());
  ASSERT_OK(paver::FvmUnbind(devfs_root(), "/dev/sys/platform/00:00:2d/ramctl/ramdisk-0/block"));
  fvm_part.reset();
  blob.reset();
  data.reset();
}

TEST_F(FvmTest, UnbindInvalidPath) {
  ASSERT_NO_FAILURES(CreateRamdisk());
  zx::result fd = this->fd();
  ASSERT_OK(fd.status_value());
  fbl::unique_fd fvm_part =
      FvmPartitionFormat(devfs_root(), std::move(fd.value()), SparseHeaderForSliceSize(kSliceSize),
                         paver::BindOption::Reformat);
  ASSERT_TRUE(fvm_part.is_valid());

  ASSERT_OK(paver::AllocateEmptyPartitions(devfs_root(), fvm_part));

  fbl::unique_fd blob(openat(devfs_root().get(),
                             "sys/platform/00:00:2d/ramctl/ramdisk-0/block/fvm/blobfs-p-1/block",
                             O_RDONLY));
  ASSERT_TRUE(blob.is_valid());

  fbl::unique_fd data(openat(devfs_root().get(),
                             "sys/platform/00:00:2d/ramctl/ramdisk-0/block/fvm/data-p-2/block",
                             O_RDONLY));
  ASSERT_TRUE(data.is_valid());

  // Path too short
  ASSERT_EQ(paver::FvmUnbind(devfs_root(), "/dev"), ZX_ERR_INVALID_ARGS);

  // Path too long
  char path[PATH_MAX + 2];
  memset(path, 'a', sizeof(path));
  path[sizeof(path) - 1] = '\0';
  ASSERT_EQ(paver::FvmUnbind(devfs_root(), path), ZX_ERR_INVALID_ARGS);

  ASSERT_OK(paver::FvmUnbind(devfs_root(), "/dev/sys/platform/00:00:2d/ramctl/ramdisk-0/block"));
  fvm_part.reset();
  blob.reset();
  data.reset();
}

}  // namespace
