// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/fshost/minfs-manipulator.h"

#include <lib/fdio/directory.h>
#include <lib/fdio/fdio.h>
#include <lib/zx/channel.h>
#include <unistd.h>

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include <fbl/unique_fd.h>
#include <fs-management/admin.h>
#include <fs-management/format.h>
#include <fs-management/launch.h>
#include <gtest/gtest.h>

#include "src/lib/files/directory.h"
#include "src/lib/files/file.h"
#include "src/lib/testing/predicates/status.h"
#include "src/storage/testing/fvm.h"
#include "src/storage/testing/ram_disk.h"
#include "src/storage/testing/zxcrypt.h"

namespace fshost {
namespace {

constexpr uint64_t kBlockSize = 512;
constexpr uint64_t kDeviceSize = 15lu * 1024 * 1024;
constexpr uint64_t kBlockCount = kDeviceSize / kBlockSize;
constexpr uint64_t kFvmSliceSize = 32lu * 1024;
constexpr uint64_t kMinfsDefaultInodeCount = 4096;
constexpr uint64_t kMinfsPartitionSizeLimit = 13860864;
// Minfs will never have exactly 3 inodes which will force a resize to always happen.
constexpr uint64_t kForceResizeInodeCount = 3;

class FsManipulatorTest : public testing::Test {
 public:
  void SetUp() override {
    zx::status<storage::RamDisk> ram_disk = storage::RamDisk::Create(kBlockSize, kBlockCount);
    ASSERT_OK(ram_disk.status_value());

    zx::status<std::string> fvm_device_path =
        storage::CreateFvmPartition(ram_disk->path(), kFvmSliceSize);
    ASSERT_OK(fvm_device_path.status_value());

    zx::status<std::string> zxcrypt_device_path = storage::CreateZxcryptVolume(*fvm_device_path);
    ASSERT_OK(zxcrypt_device_path.status_value());

    MkfsOptions options;
    ASSERT_OK(mkfs(zxcrypt_device_path->c_str(), DISK_FORMAT_MINFS, launch_stdio_sync, options));

    fbl::unique_fd device_fd(open(zxcrypt_device_path->c_str(), O_RDWR));
    ASSERT_TRUE(device_fd.is_valid());

    zx::channel device;
    ASSERT_OK(fdio_get_service_handle(device_fd.release(), device.reset_and_get_address()));

    ram_disk_ = *std::move(ram_disk);
    device_ = std::move(device);
  }

  zx::channel device() { return zx::channel(fdio_service_clone(device_.get())); }

  zx::status<uint64_t> GetBlockDeviceSize() {
    auto block_device_info = GetBlockDeviceInfo(device_.borrow());
    if (block_device_info.is_error()) {
      return block_device_info.take_error();
    }
    return zx::ok(block_device_info->block_size * block_device_info->block_count);
  }

 private:
  storage::RamDisk ram_disk_;
  zx::channel device_;
};

bool CreateSizedFileAt(int dir, const char* filename, ssize_t file_size) {
  constexpr ssize_t kBufferSize = 8192;
  static const std::vector<uint8_t>* buffer = new std::vector<uint8_t>(kBufferSize, 0);
  fbl::unique_fd file(openat(dir, filename, O_CREAT | O_TRUNC | O_WRONLY));
  if (!file.is_valid()) {
    return false;
  }
  ssize_t to_write = file_size;
  while (to_write > 0) {
    ssize_t wrote = write(file.get(), buffer->data(), std::min(to_write, kBufferSize));
    if (wrote < 0) {
      return false;
    }
    to_write -= wrote;
  }
  return true;
}

TEST_F(FsManipulatorTest, MaybeResizeMinfsWithAcceptableSizeDoesNothing) {
  constexpr char kFilename[] = "1MiBfile";
  zx::status<uint64_t> initialize_size = GetBlockDeviceSize();
  ASSERT_OK(initialize_size.status_value());

  // Write a 1MiB file to minfs to cause it to allocate slices from fvm which will increase the size
  // of the block device.
  {
    zx::status<MountedMinfs> minfs = MountedMinfs::Mount(device());
    ASSERT_OK(minfs.status_value());
    zx::status<fbl::unique_fd> root = minfs->GetRootFd();
    ASSERT_OK(root.status_value());
    ASSERT_TRUE(CreateSizedFileAt(root->get(), kFilename, 1024l * 1024));
    // Delete the file so it won't be copied to the new minfs resulting in minfs growing again.
    ASSERT_EQ(unlinkat(root->get(), kFilename, 0), 0);
  }

  // Verify that slices were allocated.
  zx::status<uint64_t> filled_size = GetBlockDeviceSize();
  ASSERT_OK(filled_size.status_value());
  ASSERT_GT(*filled_size, *initialize_size);

  // Attempt to resize minfs.
  zx::status<> status =
      MaybeResizeMinfs(device(), kMinfsPartitionSizeLimit, kMinfsDefaultInodeCount);
  ASSERT_OK(status.status_value());

  // If minfs was resized then it would have given back all of its slices to fvm and the block
  // device would be back to the initial size.
  zx::status<MountedMinfs> minfs = MountedMinfs::Mount(device());
  ASSERT_OK(minfs.status_value());
  zx::status<uint64_t> final_size = GetBlockDeviceSize();
  ASSERT_OK(final_size.status_value());
  EXPECT_EQ(*final_size, *filled_size);
}

TEST_F(FsManipulatorTest, MaybeResizeMinfsWithTooManyInodesResizes) {
  // Write lots of files to minfs to increase the number of allocated inodes.
  {
    zx::status<MountedMinfs> minfs = MountedMinfs::Mount(device());
    ASSERT_OK(minfs.status_value());
    zx::status<fbl::unique_fd> root = minfs->GetRootFd();
    ASSERT_OK(root.status_value());

    std::vector<std::string> file_names;
    for (uint64_t i = 0; i < kMinfsDefaultInodeCount + 1; ++i) {
      file_names.push_back("file" + std::to_string(i));
      fbl::unique_fd fd(
          openat(root->get(), file_names.back().c_str(), O_CREAT | O_TRUNC | O_WRONLY));
      ASSERT_TRUE(fd.is_valid());
    }
    // Delete all of the files so the inodes will no longer be used.
    for (const std::string& file_name : file_names) {
      ASSERT_EQ(unlinkat(root->get(), file_name.c_str(), 0), 0);
    }
    // Verify that minfs now has more inodes than desired
    auto info = minfs->GetFilesystemInfo();
    ASSERT_OK(info.status_value());
    ASSERT_GT(info->total_nodes, kMinfsDefaultInodeCount);
  }

  // Resize minfs
  zx::status<> status =
      MaybeResizeMinfs(device(), kMinfsPartitionSizeLimit, kMinfsDefaultInodeCount);
  ASSERT_OK(status.status_value());

  // Minfs should have the desired number of inodes again.
  zx::status<MountedMinfs> minfs = MountedMinfs::Mount(device());
  ASSERT_OK(minfs.status_value());
  auto info = minfs->GetFilesystemInfo();
  ASSERT_OK(info.status_value());
  EXPECT_EQ(info->total_nodes, kMinfsDefaultInodeCount);
}

TEST_F(FsManipulatorTest, MaybeResizeMinfsWithTooManySlicesResizes) {
  constexpr char kFilename[] = "1MiBfile";
  zx::status<uint64_t> initialize_size = GetBlockDeviceSize();
  ASSERT_OK(initialize_size.status_value());

  // Write a 1MiB file to minfs to cause it to allocate slices from fvm which will increase the size
  // of the block device.
  {
    zx::status<MountedMinfs> minfs = MountedMinfs::Mount(device());
    ASSERT_OK(minfs.status_value());
    zx::status<fbl::unique_fd> root = minfs->GetRootFd();
    ASSERT_OK(root.status_value());
    ASSERT_TRUE(CreateSizedFileAt(root->get(), kFilename, 1024l * 1024));
    // Delete the file so resize will succeed.
    ASSERT_EQ(unlinkat(root->get(), kFilename, 0), 0);
  }

  // Verify that slices were allocated.
  zx::status<uint64_t> filled_size = GetBlockDeviceSize();
  ASSERT_OK(filled_size.status_value());
  ASSERT_GT(*filled_size, *initialize_size);

  // Use |initial_size| as the limit which should cause minfs to be resized.
  zx::status<> status = MaybeResizeMinfs(device(), *initialize_size, kMinfsDefaultInodeCount);
  ASSERT_OK(status.status_value());

  // If minfs was resized then it should be back to the initial size.
  zx::status<MountedMinfs> minfs = MountedMinfs::Mount(device());
  ASSERT_OK(minfs.status_value());
  zx::status<uint64_t> final_size = GetBlockDeviceSize();
  ASSERT_OK(final_size.status_value());
  EXPECT_EQ(*final_size, *initialize_size);
}

TEST_F(FsManipulatorTest, MaybeResizeMinfsResizingPreservesAllFiles) {
  const std::filesystem::path kFile1 = "file1.txt";
  const std::string kFile1Contents = "contents1";
  const std::filesystem::path kDirectory1 = "dir1";
  const std::filesystem::path kFile2 = "file2.txt";
  const std::string kFile2Contents = "contents2";

  // Create files in minfs:
  // /file1.txt
  // /dir1/file2.txt
  {
    zx::status<MountedMinfs> minfs = MountedMinfs::Mount(device());
    ASSERT_OK(minfs.status_value());
    zx::status<fbl::unique_fd> root = minfs->GetRootFd();
    ASSERT_OK(root.status_value());
    ASSERT_TRUE(
        files::WriteFileAt(root->get(), kFile1, kFile1Contents.data(), kFile1Contents.size()));
    ASSERT_TRUE(files::CreateDirectoryAt(root->get(), kDirectory1));
    ASSERT_TRUE(files::WriteFileAt(root->get(), kDirectory1 / kFile2, kFile2Contents.data(),
                                   kFile2Contents.size()));
  }

  // Force minfs to resize.
  zx::status<> status =
      MaybeResizeMinfs(device(), kMinfsPartitionSizeLimit, kForceResizeInodeCount);
  ASSERT_OK(status.status_value());

  // Verify that all of the files were preserved.
  zx::status<MountedMinfs> minfs = MountedMinfs::Mount(device());
  ASSERT_OK(minfs.status_value());
  zx::status<fbl::unique_fd> root = minfs->GetRootFd();
  ASSERT_OK(root.status_value());

  std::string file1NewContents;
  EXPECT_TRUE(files::ReadFileToStringAt(root->get(), kFile1, &file1NewContents));
  EXPECT_EQ(file1NewContents, kFile1Contents);

  std::string file2NewContents;
  EXPECT_TRUE(files::ReadFileToStringAt(root->get(), kDirectory1 / kFile2, &file2NewContents));
  EXPECT_EQ(file2NewContents, kFile2Contents);

  // Verify that the resize is no longer in progress.
  auto is_resize_in_progress = minfs->IsResizeInProgress();
  ASSERT_OK(is_resize_in_progress.status_value());
  EXPECT_FALSE(*is_resize_in_progress);
}

TEST_F(FsManipulatorTest, MaybeResizeMinfsWithResizeInProgressReformatsMinfs) {
  const std::filesystem::path kFile = "file.txt";
  const std::string kFileContents = "contents";
  {
    zx::status<MountedMinfs> minfs = MountedMinfs::Mount(device());
    ASSERT_OK(minfs.status_value());
    // Set writing in progress and add a file.
    ASSERT_OK(minfs->SetResizeInProgress().status_value());
    zx::status<fbl::unique_fd> root = minfs->GetRootFd();
    ASSERT_OK(root.status_value());
    ASSERT_TRUE(files::WriteFileAt(root->get(), kFile, kFileContents.data(), kFileContents.size()));
  }

  zx::status<> status =
      MaybeResizeMinfs(device(), kMinfsPartitionSizeLimit, kMinfsDefaultInodeCount);
  ASSERT_OK(status.status_value());

  zx::status<MountedMinfs> minfs = MountedMinfs::Mount(device());
  ASSERT_OK(minfs.status_value());
  zx::status<fbl::unique_fd> root = minfs->GetRootFd();
  ASSERT_OK(root.status_value());
  // Since writing was already in progress minfs was wiped and the file was lost.
  EXPECT_NE(faccessat(root->get(), kFile.c_str(), F_OK, /*flags=*/0), 0);
  EXPECT_EQ(errno, ENOENT);
  auto is_resize_in_progress = minfs->IsResizeInProgress();
  ASSERT_OK(is_resize_in_progress.status_value());
  EXPECT_FALSE(*is_resize_in_progress);
}

TEST_F(FsManipulatorTest, MaybeResizeMinfsResizeInProgressIsCorrectlyDetected) {
  {
    zx::status<MountedMinfs> minfs = MountedMinfs::Mount(device());
    ASSERT_OK(minfs.status_value());

    // The file doesn't exist in an empty minfs.
    auto is_resize_in_progress = minfs->IsResizeInProgress();
    ASSERT_OK(is_resize_in_progress.status_value());
    EXPECT_FALSE(*is_resize_in_progress);

    // Create the file.
    ASSERT_OK(minfs->SetResizeInProgress().status_value());
  }
  {
    zx::status<MountedMinfs> minfs = MountedMinfs::Mount(device());
    ASSERT_OK(minfs.status_value());

    // Ensure that the file exists.
    auto is_resize_in_progress = minfs->IsResizeInProgress();
    ASSERT_OK(is_resize_in_progress.status_value());
    EXPECT_TRUE(*is_resize_in_progress);

    // Remove the file.
    ASSERT_OK(minfs->ClearResizeInProgress().status_value());
  }

  zx::status<MountedMinfs> minfs = MountedMinfs::Mount(device());
  ASSERT_OK(minfs.status_value());

  // Ensure that the file no longer exists.
  auto is_resize_in_progress = minfs->IsResizeInProgress();
  ASSERT_OK(is_resize_in_progress.status_value());
  EXPECT_FALSE(*is_resize_in_progress);
}

}  // namespace
}  // namespace fshost
