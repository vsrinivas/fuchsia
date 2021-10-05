// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/fshost/minfs-manipulator.h"

#include <fcntl.h>
#include <fidl/fuchsia.hardware.block.partition/cpp/wire.h>
#include <fidl/fuchsia.hardware.block.volume/cpp/wire.h>
#include <lib/fdio/cpp/caller.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fdio.h>
#include <lib/fidl/llcpp/client_end.h>
#include <lib/inspect/cpp/inspect.h>
#include <lib/inspect/testing/cpp/inspect.h>
#include <lib/zx/channel.h>
#include <lib/zx/status.h>
#include <unistd.h>
#include <zircon/errors.h>

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include <fbl/unique_fd.h>
#include <fs-management/admin.h>
#include <fs-management/format.h>
#include <fs-management/launch.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/lib/files/directory.h"
#include "src/lib/files/file.h"
#include "src/lib/testing/predicates/status.h"
#include "src/storage/fshost/inspect-manager.h"
#include "src/storage/testing/fvm.h"
#include "src/storage/testing/ram_disk.h"
#include "src/storage/testing/zxcrypt.h"

namespace fshost {
namespace {

using MinfsUpgradeState = InspectManager::MinfsUpgradeState;
using ::inspect::testing::BoolIs;
using ::inspect::testing::ChildrenMatch;
using ::inspect::testing::NameMatches;
using ::inspect::testing::NodeMatches;
using ::inspect::testing::PropertyList;
using ::testing::AllOf;
using ::testing::ElementsAre;
using ::testing::ElementsAreArray;
using ::testing::IsEmpty;
using ::testing::Matcher;
using ::testing::Not;
using ::testing::SizeIs;
using ::testing::UnorderedElementsAre;

constexpr uint64_t kBlockSize = 512;
constexpr uint64_t kDeviceSize = 15lu * 1024 * 1024;
constexpr uint64_t kBlockCount = kDeviceSize / kBlockSize;
constexpr uint64_t kFvmSliceSize = 32lu * 1024;
constexpr uint64_t kMinfsDefaultInodeCount = 4096;
constexpr uint64_t kMinfsPartitionSizeLimit = 13860864;
constexpr uint64_t kMinfsDataSizeLimit = 10223616;
// Minfs will never have exactly 3 inodes which will force a resize to always happen.
constexpr uint64_t kForceResizeInodeCount = 3;

class MinfsManipulatorTest : public testing::Test {
 public:
  const std::vector<std::filesystem::path> kNoExcludedPaths = {};

  void SetUp() override {
    zx::status<storage::RamDisk> ram_disk = storage::RamDisk::Create(kBlockSize, kBlockCount);
    ASSERT_OK(ram_disk.status_value());

    zx::status<std::string> fvm_partition_path =
        storage::CreateFvmPartition(ram_disk->path(), kFvmSliceSize);
    ASSERT_OK(fvm_partition_path.status_value());

    zx::status<std::string> zxcrypt_device_path = storage::CreateZxcryptVolume(*fvm_partition_path);
    ASSERT_OK(zxcrypt_device_path.status_value());

    MkfsOptions options;
    ASSERT_OK(mkfs(zxcrypt_device_path->c_str(), DISK_FORMAT_MINFS, launch_stdio_sync, options));

    fbl::unique_fd device_fd(open(zxcrypt_device_path->c_str(), O_RDWR));
    ASSERT_TRUE(device_fd.is_valid());

    zx::channel device;
    ASSERT_OK(fdio_get_service_handle(device_fd.release(), device.reset_and_get_address()));

    ram_disk_ = *std::move(ram_disk);
    device_ = std::move(device);
    fvm_partition_path_ = *std::move(fvm_partition_path);
    zxcrypt_device_path_ = *std::move(zxcrypt_device_path);
  }

  zx::channel device() { return zx::channel(fdio_service_clone(device_.get())); }

  InspectManager& inspect() { return inspect_; }

  void ExpectLoggedStates(const std::vector<MinfsUpgradeState>& states) {
    std::vector<Matcher<const inspect::PropertyValue&>> state_matchers;
    state_matchers.reserve(states.size());
    for (const auto& state : states) {
      state_matchers.push_back(BoolIs(InspectManager::MinfsUpgradeStateString(state), true));
    }
    auto hierarchy_result = inspect::ReadFromVmo(inspect_.inspector().DuplicateVmo());
    ASSERT_TRUE(hierarchy_result.is_ok());
    EXPECT_THAT(
        hierarchy_result.take_value(),
        AllOf(
            NodeMatches(NameMatches("root")),
            ChildrenMatch(UnorderedElementsAre(NodeMatches(AllOf(
                NameMatches("minfs_upgrade"), PropertyList(ElementsAreArray(state_matchers))))))));
  }

  zx::status<> SetPartitionLimit(uint64_t byte_count) {
    fidl::UnownedClientEnd<fuchsia_hardware_block_partition::Partition> block_client(
        device_.borrow());
    auto guid_result = fidl::WireCall(block_client).GetInstanceGuid();
    if (guid_result.status() != ZX_OK) {
      return zx::error(guid_result.status());
    }
    if (guid_result->status != ZX_OK) {
      return zx::error(guid_result->status);
    }

    std::string fvm_path = ram_disk_.path() + "/fvm";
    fbl::unique_fd fvm_fd(open(fvm_path.c_str(), O_RDONLY));
    if (!fvm_fd.is_valid()) {
      return zx::error(ZX_ERR_IO);
    }
    fdio_cpp::UnownedFdioCaller caller(fvm_fd.get());
    fidl::UnownedClientEnd<fuchsia_hardware_block_volume::VolumeManager> volume_client(
        caller.channel());
    auto result = fidl::WireCall(volume_client).SetPartitionLimit(*guid_result->guid, byte_count);
    if (result.status() != ZX_OK) {
      return zx::error(result.status());
    }
    if (result->status != ZX_OK) {
      return zx::error(result->status);
    }
    return zx::ok();
  }

  zx::status<uint64_t> GetBlockDeviceSize() {
    auto block_device_info = GetBlockDeviceInfo(device_.borrow());
    if (block_device_info.is_error()) {
      return block_device_info.take_error();
    }
    return zx::ok(block_device_info->block_size * block_device_info->block_count);
  }

  zx::status<> MinfsFsck() {
    return zx::make_status(
        fsck(zxcrypt_device_path_.c_str(), DISK_FORMAT_MINFS, {}, launch_stdio_sync));
  }

  void ExpectThatZxcrypWasShredded() {
    // Shredding zxcrypt fills the superblock with random data. To verify that zxcrypt was shredded
    // the first block is read in and the magic is checked to not be zxcrypt's.
    fbl::unique_fd fd(open(fvm_partition_path_.c_str(), O_RDONLY));
    fdio_cpp::UnownedFdioCaller caller(fd);
    auto info = GetBlockDeviceInfo(caller.channel());
    ASSERT_OK(info.status_value());
    std::vector<uint8_t> buf(info->block_size, 0);
    ASSERT_EQ(read(fd.get(), buf.data(), info->block_size), info->block_size);
    buf.resize(sizeof(zxcrypt_magic));
    EXPECT_THAT(buf, Not(ElementsAreArray(zxcrypt_magic)));
  }

 private:
  storage::RamDisk ram_disk_;
  zx::channel device_;
  InspectManager inspect_;
  std::string fvm_partition_path_;
  std::string zxcrypt_device_path_;
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

TEST_F(MinfsManipulatorTest, MaybeResizeMinfsWithAcceptableSizeDoesNothing) {
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
  MaybeResizeMinfsResult result =
      MaybeResizeMinfs(device(), kMinfsPartitionSizeLimit, kMinfsDefaultInodeCount,
                       kMinfsDataSizeLimit, kNoExcludedPaths, inspect());
  ASSERT_EQ(result, MaybeResizeMinfsResult::kMinfsMountable);

  // If minfs was resized then it would have given back all of its slices to fvm and the block
  // device would be back to the initial size.
  zx::status<MountedMinfs> minfs = MountedMinfs::Mount(device());
  ASSERT_OK(minfs.status_value());
  zx::status<uint64_t> final_size = GetBlockDeviceSize();
  ASSERT_OK(final_size.status_value());
  EXPECT_EQ(*final_size, *filled_size);

  // Nothing should be logged.
  ExpectLoggedStates({
      MinfsUpgradeState::kSkipped,
  });
}

TEST_F(MinfsManipulatorTest, MaybeResizeMinfsWithTooManyInodesResizes) {
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

  MaybeResizeMinfsResult result =
      MaybeResizeMinfs(device(), kMinfsPartitionSizeLimit, kMinfsDefaultInodeCount,
                       kMinfsDataSizeLimit, kNoExcludedPaths, inspect());
  ASSERT_EQ(result, MaybeResizeMinfsResult::kMinfsMountable);

  // Minfs should have the desired number of inodes again.
  zx::status<MountedMinfs> minfs = MountedMinfs::Mount(device());
  ASSERT_OK(minfs.status_value());
  auto info = minfs->GetFilesystemInfo();
  ASSERT_OK(info.status_value());
  EXPECT_EQ(info->total_nodes, kMinfsDefaultInodeCount);

  ExpectLoggedStates({
      MinfsUpgradeState::kReadOldPartition,
      MinfsUpgradeState::kWriteNewPartition,
      MinfsUpgradeState::kFinished,
  });
}

TEST_F(MinfsManipulatorTest, MaybeResizeMinfsWithTooManySlicesResizes) {
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
  MaybeResizeMinfsResult result =
      MaybeResizeMinfs(device(), *initialize_size, kMinfsDefaultInodeCount, kMinfsDataSizeLimit,
                       kNoExcludedPaths, inspect());
  ASSERT_EQ(result, MaybeResizeMinfsResult::kMinfsMountable);

  // If minfs was resized then it should be back to the initial size.
  zx::status<MountedMinfs> minfs = MountedMinfs::Mount(device());
  ASSERT_OK(minfs.status_value());
  zx::status<uint64_t> final_size = GetBlockDeviceSize();
  ASSERT_OK(final_size.status_value());
  EXPECT_EQ(*final_size, *initialize_size);

  ExpectLoggedStates({
      MinfsUpgradeState::kReadOldPartition,
      MinfsUpgradeState::kWriteNewPartition,
      MinfsUpgradeState::kFinished,
  });
}

TEST_F(MinfsManipulatorTest, MaybeResizeMinfsResizingWithNoExcludedPathsPreservesAllFiles) {
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
  MaybeResizeMinfsResult result =
      MaybeResizeMinfs(device(), kMinfsPartitionSizeLimit, kForceResizeInodeCount,
                       kMinfsDataSizeLimit, kNoExcludedPaths, inspect());
  ASSERT_EQ(result, MaybeResizeMinfsResult::kMinfsMountable);

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

  ExpectLoggedStates({
      MinfsUpgradeState::kReadOldPartition,
      MinfsUpgradeState::kWriteNewPartition,
      MinfsUpgradeState::kFinished,
  });
}

TEST_F(MinfsManipulatorTest, MaybeResizeMinfsResizingWithExcludedPathsIsCorrect) {
  {
    zx::status<MountedMinfs> minfs = MountedMinfs::Mount(device());
    ASSERT_OK(minfs.status_value());
    zx::status<fbl::unique_fd> root = minfs->GetRootFd();
    ASSERT_OK(root.status_value());
    ASSERT_TRUE(files::CreateDirectoryAt(root->get(), "cache"));
    ASSERT_TRUE(files::WriteFileAt(root->get(), "cache/file", "contents1", 9));
    ASSERT_TRUE(files::CreateDirectoryAt(root->get(), "p"));
    ASSERT_TRUE(files::CreateDirectoryAt(root->get(), "p/m1"));
    ASSERT_TRUE(files::WriteFileAt(root->get(), "p/m1/file", "contents2", 9));
    ASSERT_TRUE(files::CreateDirectoryAt(root->get(), "p/m2"));
    ASSERT_TRUE(files::WriteFileAt(root->get(), "p/m2/file", "contents3", 9));
    ASSERT_TRUE(files::CreateDirectoryAt(root->get(), "p/m2/db"));
    ASSERT_TRUE(files::WriteFileAt(root->get(), "p/m2/db/file", "contents4", 9));
  }

  std::vector<std::filesystem::path> excluded_paths = {"cache", "p/m1/file", "p/m2/db"};
  // Force minfs to resize.
  MaybeResizeMinfsResult result =
      MaybeResizeMinfs(device(), kMinfsPartitionSizeLimit, kForceResizeInodeCount,
                       kMinfsDataSizeLimit, excluded_paths, inspect());
  ASSERT_EQ(result, MaybeResizeMinfsResult::kMinfsMountable);

  zx::status<MountedMinfs> minfs = MountedMinfs::Mount(device());
  ASSERT_OK(minfs.status_value());
  zx::status<fbl::unique_fd> root = minfs->GetRootFd();
  ASSERT_OK(root.status_value());

  // Verify that only non-excluded files were copied over.
  std::string contents;
  EXPECT_FALSE(files::ReadFileToStringAt(root->get(), "cache/file", &contents));
  EXPECT_FALSE(files::ReadFileToStringAt(root->get(), "p/m1/file", &contents));
  EXPECT_FALSE(files::ReadFileToStringAt(root->get(), "p/m2/db/file", &contents));

  EXPECT_TRUE(files::ReadFileToStringAt(root->get(), "p/m2/file", &contents));
  EXPECT_EQ(contents, "contents3");

  ExpectLoggedStates({
      MinfsUpgradeState::kReadOldPartition,
      MinfsUpgradeState::kWriteNewPartition,
      MinfsUpgradeState::kFinished,
  });
}

TEST_F(MinfsManipulatorTest, MaybeResizeMinfsWithResizeInProgressReformatsMinfs) {
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

  MaybeResizeMinfsResult result =
      MaybeResizeMinfs(device(), kMinfsPartitionSizeLimit, kMinfsDefaultInodeCount,
                       kMinfsDataSizeLimit, kNoExcludedPaths, inspect());
  ASSERT_EQ(result, MaybeResizeMinfsResult::kRebootRequired);
  ExpectThatZxcrypWasShredded();
  ExpectLoggedStates({
      MinfsUpgradeState::kDetectedFailedUpgrade,
  });
}

TEST_F(MinfsManipulatorTest, MaybeResizeMinfsResizeInProgressIsCorrectlyDetected) {
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

TEST_F(MinfsManipulatorTest, MaybeResizeMinfsWithLargeDataDoesNotResize) {
  // Put a 1MiB file in minfs and restrict the data size to 512KiB.
  const std::string kFilename = "file";
  constexpr ssize_t kFileSize = 1024l * 1024;
  constexpr uint64_t kMinfsLimitedDataSize = 512lu * 1024;
  {
    zx::status<MountedMinfs> minfs = MountedMinfs::Mount(device());
    ASSERT_OK(minfs.status_value());
    zx::status<fbl::unique_fd> root = minfs->GetRootFd();
    ASSERT_OK(root.status_value());
    ASSERT_TRUE(CreateSizedFileAt(root->get(), kFilename.c_str(), kFileSize));
  }
  MaybeResizeMinfsResult result =
      MaybeResizeMinfs(device(), kMinfsPartitionSizeLimit, kForceResizeInodeCount,
                       kMinfsLimitedDataSize, kNoExcludedPaths, inspect());
  ASSERT_EQ(result, MaybeResizeMinfsResult::kMinfsMountable);

  zx::status<MountedMinfs> minfs = MountedMinfs::Mount(device());
  ASSERT_OK(minfs.status_value());
  zx::status<fbl::unique_fd> root = minfs->GetRootFd();
  ASSERT_OK(root.status_value());

  // The data exceeded the minfs data limit so minfs was not resized.
  std::string contents;
  EXPECT_TRUE(files::ReadFileToStringAt(root->get(), kFilename, &contents));
  EXPECT_THAT(contents, SizeIs(kFileSize));
  ExpectLoggedStates({
      MinfsUpgradeState::kReadOldPartition,
      MinfsUpgradeState::kSkipped,
  });
}

TEST_F(MinfsManipulatorTest, MaybeResizeMinfsWithLargeDataThatIsFilteredOutDoesResize) {
  // Put two 1MiB files in minfs and restrict the data size to 1.5MiB.
  const std::string kFile1Name = "file1";
  const std::string kFile2Name = "file2";
  constexpr ssize_t kFileSize = 1024l * 1024;
  constexpr uint64_t kMinfsLimitedDataSize = (512lu + 1024) * 1024;
  {
    zx::status<MountedMinfs> minfs = MountedMinfs::Mount(device());
    ASSERT_OK(minfs.status_value());
    zx::status<fbl::unique_fd> root = minfs->GetRootFd();
    ASSERT_OK(root.status_value());
    ASSERT_TRUE(CreateSizedFileAt(root->get(), kFile1Name.c_str(), kFileSize));
    ASSERT_TRUE(CreateSizedFileAt(root->get(), kFile2Name.c_str(), kFileSize));
  }
  // Resize with file2 filtered out.
  MaybeResizeMinfsResult result =
      MaybeResizeMinfs(device(), kMinfsPartitionSizeLimit, kForceResizeInodeCount,
                       kMinfsLimitedDataSize, {kFile2Name}, inspect());
  ASSERT_EQ(result, MaybeResizeMinfsResult::kMinfsMountable);

  zx::status<MountedMinfs> minfs = MountedMinfs::Mount(device());
  ASSERT_OK(minfs.status_value());
  zx::status<fbl::unique_fd> root = minfs->GetRootFd();
  ASSERT_OK(root.status_value());

  // With file2 filtered out file1 alone fits within the data limit allowing minfs to be resized.
  std::string contents;
  EXPECT_TRUE(files::ReadFileToStringAt(root->get(), kFile1Name, &contents));
  EXPECT_THAT(contents, SizeIs(kFileSize));
  EXPECT_FALSE(files::ReadFileToStringAt(root->get(), kFile2Name, &contents));
  ExpectLoggedStates({
      MinfsUpgradeState::kReadOldPartition,
      MinfsUpgradeState::kWriteNewPartition,
      MinfsUpgradeState::kFinished,
  });
}

TEST_F(MinfsManipulatorTest, MaybeResizeMinfsFailingToFormatMinfsLeavesMinfsUnmountable) {
  // Set the partition limit to 1MiB which is far less than minfs requires. Resizing should fail
  // when calling mkfs.
  ASSERT_OK(SetPartitionLimit(1024lu * 1024).status_value());
  MaybeResizeMinfsResult result =
      MaybeResizeMinfs(device(), kMinfsPartitionSizeLimit, kForceResizeInodeCount,
                       kMinfsDataSizeLimit, kNoExcludedPaths, inspect());
  ASSERT_EQ(result, MaybeResizeMinfsResult::kRebootRequired);

  // Minfs should fail fsck which will cause it be formatted again during the next boot.
  zx::status<> status = MinfsFsck();
  ASSERT_NE(status.status_value(), ZX_OK);

  ExpectLoggedStates({
      MinfsUpgradeState::kReadOldPartition,
      MinfsUpgradeState::kWriteNewPartition,
  });
}

TEST(ParseExcludedPaths, WithEmptyStringProducesEmptyList) {
  std::vector<std::filesystem::path> paths = ParseExcludedPaths("");
  EXPECT_THAT(paths, IsEmpty());
}

TEST(ParseExcludedPaths, RemovesEmptyPaths) {
  std::vector<std::filesystem::path> paths = ParseExcludedPaths(",foo,,bar,");
  EXPECT_THAT(paths, ElementsAre("foo", "bar"));
}

TEST(ParseExcludedPaths, RemovesWhitespace) {
  std::vector<std::filesystem::path> paths = ParseExcludedPaths("  foo , bar,baz ");
  EXPECT_THAT(paths, ElementsAre("foo", "bar", "baz"));
}

}  // namespace
}  // namespace fshost
