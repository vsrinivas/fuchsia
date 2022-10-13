// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <fidl/fuchsia.blobfs/cpp/markers.h>
#include <fidl/fuchsia.fs/cpp/wire.h>
#include <fidl/fuchsia.io/cpp/wire.h>
#include <fuchsia/blobfs/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async-loop/loop.h>
#include <lib/async/cpp/executor.h>
#include <lib/async/default.h>
#include <lib/fdio/cpp/caller.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fidl-utils/bind.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/inspect/cpp/hierarchy.h>
#include <lib/inspect/service/cpp/reader.h>
#include <lib/inspect/testing/cpp/inspect.h>
#include <lib/sync/completion.h>
#include <lib/sys/component/cpp/service_client.h>
#include <lib/zx/vmo.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <utime.h>
#include <zircon/errors.h>
#include <zircon/fidl.h>
#include <zircon/time.h>

#include <array>
#include <atomic>
#include <memory>
#include <string_view>
#include <thread>
#include <vector>

#include <fbl/algorithm.h>
#include <fbl/array.h>
#include <fbl/unique_fd.h>
#include <gtest/gtest.h>
#include <safemath/safe_conversions.h>

#include "src/lib/digest/digest.h"
#include "src/lib/digest/node-digest.h"
#include "src/lib/storage/block_client/cpp/remote_block_device.h"
#include "src/lib/storage/fs_management/cpp/launch.h"
#include "src/lib/storage/fs_management/cpp/mount.h"
#include "src/storage/blobfs/format.h"
#include "src/storage/blobfs/test/blob_utils.h"
#include "src/storage/blobfs/test/integration/blobfs_fixtures.h"
#include "src/storage/blobfs/test/integration/fdio_test.h"
#include "src/storage/fvm/format.h"
#include "src/storage/lib/utils/topological_path.h"

namespace blobfs {
namespace {

namespace fio = fuchsia_io;
using BlobfsIntegrationTest = ParameterizedBlobfsTest;
using ::testing::UnitTest;

// Class emulating a corruption handler service.
class CorruptBlobHandlerImpl final : public fuchsia::blobfs::CorruptBlobHandler {
 public:
  void CorruptBlob(::std::vector<uint8_t> merkleroot) override {
    sync_completion_signal(&notified_);
  }

  bool WasCalled() {
    zx_status_t status = sync_completion_wait(&notified_, ZX_TIME_INFINITE);
    return status == ZX_OK;
  }

 private:
  sync_completion_t notified_;
};

// Go over the parent device logic and test fixture.
TEST_P(BlobfsIntegrationTest, Trivial) {}

TEST_P(BlobfsIntegrationTest, Basics) {
  for (unsigned int i = 10; i < 16; i++) {
    std::unique_ptr<BlobInfo> info = GenerateRandomBlob(fs().mount_path(), 1 << i);

    fbl::unique_fd fd;
    ASSERT_NO_FATAL_FAILURE(MakeBlob(*info, &fd));
    ASSERT_EQ(close(fd.release()), 0);

    // We can re-open and verify the Blob as read-only
    fd.reset(open(info->path, O_RDONLY));
    ASSERT_TRUE(fd) << "Failed to-reopen blob";
    ASSERT_NO_FATAL_FAILURE(VerifyContents(fd.get(), info->data.get(), info->size_data));
    ASSERT_EQ(close(fd.release()), 0);

    // We cannot re-open the blob as writable
    fd.reset(open(info->path, O_RDWR | O_CREAT, S_IRUSR | S_IWUSR));
    ASSERT_FALSE(fd) << "Shouldn't be able to re-create blob that exists";
    fd.reset(open(info->path, O_RDWR));
    ASSERT_FALSE(fd) << "Shouldn't be able to re-open blob as writable";
    fd.reset(open(info->path, O_WRONLY));
    ASSERT_FALSE(fd) << "Shouldn't be able to re-open blob as writable";

    ASSERT_EQ(unlink(info->path), 0);
  }
}

TEST_P(BlobfsIntegrationTest, CorruptBlobNotify) {
  ssize_t device_block_size = fs().options().device_block_size;

  // Create a small blob and add it to blobfs.
  std::unique_ptr<BlobInfo> info = GenerateRandomBlob(fs().mount_path(), device_block_size);
  fbl::unique_fd blob_fd;
  ASSERT_NO_FATAL_FAILURE(MakeBlob(*info, &blob_fd));
  blob_fd.reset();

  // Unmount blobfs before corrupting the blob. Blobfs needs to be remounted to ensure that the
  // uncorrupted blob wasn't cached.
  ASSERT_EQ(fs().Unmount().status_value(), ZX_OK);

  // Find the blob within the block device and corrupt it.
  fbl::unique_fd device_fd(open(fs().DevicePath().value().c_str(), O_RDWR));
  ASSERT_TRUE(device_fd.is_valid());
  // Read the superblock to find where the data blocks start.
  Superblock superblock;
  ssize_t bytes_read = pread(device_fd.get(), &superblock, kBlobfsBlockSize, 0);
  ASSERT_EQ(bytes_read, static_cast<ssize_t>(kBlobfsBlockSize));
  uint64_t data_start_block = DataStartBlock(superblock);
  uint64_t data_block_count = DataBlocks(superblock);
  auto data = std::make_unique<uint8_t[]>(device_block_size);
  bool was_blob_corrupted = false;
  // Loop through the data blocks looking for the blob. Blobs always start on a block boundary.
  for (uint64_t block = 0; block < data_block_count; ++block) {
    off_t device_offset =
        safemath::checked_cast<off_t>((data_start_block + block) * kBlobfsBlockSize);
    ssize_t bytes_read = pread(device_fd.get(), data.get(), device_block_size, device_offset);
    ASSERT_EQ(bytes_read, device_block_size);
    if (memcmp(info->data.get(), data.get(), device_block_size) == 0) {
      // Corrupt the first byte by flipping all of the bits.
      data[0] = ~data[0];
      ssize_t bytes_written = pwrite(device_fd.get(), data.get(), device_block_size, device_offset);
      ASSERT_EQ(bytes_written, device_block_size);
      was_blob_corrupted = true;
      break;
    }
  }
  ASSERT_TRUE(was_blob_corrupted) << "The blob didn't get corrupted";

  ASSERT_EQ(fs().Mount().status_value(), ZX_OK);

  // Start the corrupt blob handler server.
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  ASSERT_EQ(ZX_OK, loop.StartThread("corruption-dispatcher"));
  CorruptBlobHandlerImpl corrupt_blob_handler;
  fidl::Binding<fuchsia::blobfs::CorruptBlobHandler> binding(&corrupt_blob_handler);
  auto client_end = fidl::ClientEnd<fuchsia_blobfs::CorruptBlobHandler>(
      binding.NewBinding(loop.dispatcher()).TakeChannel());

  // Pass the corrupt blob handler server to blobfs.
  auto blobfs = component::ConnectAt<fuchsia_blobfs::Blobfs>(fs().ServiceDirectory());
  ASSERT_EQ(blobfs.status_value(), ZX_OK);

  ASSERT_EQ(fidl::WireCall(*blobfs)->SetCorruptBlobHandler(std::move(client_end)).status(), ZX_OK);

  blob_fd.reset(open(info->path, O_RDONLY));
  ASSERT_TRUE(blob_fd.is_valid());
  EXPECT_EQ(pread(blob_fd.get(), data.get(), device_block_size, 0), -1);

  EXPECT_TRUE(corrupt_blob_handler.WasCalled());

  // Format blobfs to remove the corruption so the fsck that is run in the destructor will pass.
  ASSERT_EQ(fs().Unmount().status_value(), ZX_OK);
  EXPECT_EQ(fs().Format().status_value(), ZX_OK);
}

TEST_P(BlobfsIntegrationTest, UnallocatedBlob) {
  std::unique_ptr<BlobInfo> info = GenerateRandomBlob(fs().mount_path(), 1 << 10);

  // We can create a blob with a name.
  ASSERT_TRUE(fbl::unique_fd(open(info->path, O_CREAT | O_EXCL | O_RDWR, S_IRUSR | S_IWUSR)));
  // It won't exist if we close it before allocating space.
  ASSERT_FALSE(fbl::unique_fd(open(info->path, O_RDWR)));
  ASSERT_FALSE(fbl::unique_fd(open(info->path, O_RDONLY)));
  // We can "re-use" the name.
  {
    fbl::unique_fd fd(open(info->path, O_CREAT | O_EXCL | O_RDWR, S_IRUSR | S_IWUSR));
    ASSERT_TRUE(fd);
    ASSERT_EQ(ftruncate(fd.get(), info->size_data), 0);
  }
}

TEST_P(BlobfsIntegrationTest, NullBlobCreateUnlink) {
  std::unique_ptr<BlobInfo> info = GenerateRandomBlob(fs().mount_path(), 0);

  fbl::unique_fd fd(open(info->path, O_CREAT | O_EXCL | O_RDWR, S_IRUSR | S_IWUSR));
  ASSERT_TRUE(fd);
  ASSERT_EQ(ftruncate(fd.get(), 0), 0);
  std::array<char, 1> buf;
  ASSERT_EQ(read(fd.get(), buf.data(), 1), 0) << "Null Blob should reach EOF immediately";
  ASSERT_EQ(close(fd.release()), 0);

  fd.reset(open(info->path, O_CREAT | O_EXCL | O_RDWR, S_IRUSR | S_IWUSR));
  EXPECT_FALSE(fd) << "Null Blob should already exist";
  fd.reset(open(info->path, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR));
  EXPECT_FALSE(fd) << "Null Blob should not be openable as writable";

  fd.reset(open(info->path, O_RDONLY));
  ASSERT_TRUE(fd) << "Null blob should be re-openable";

  DIR* dir = opendir(fs().mount_path().c_str());
  ASSERT_NE(dir, nullptr);
  auto cleanup = fit::defer([dir]() { closedir(dir); });
  struct dirent* entry = readdir(dir);
  ASSERT_NE(entry, nullptr);
  constexpr std::string_view kEmptyBlobName =
      "15ec7bf0b50732b49f8228e07d24365338f9e3ab994b00af08e5a3bffe55fd8b";
  EXPECT_EQ(entry->d_name, kEmptyBlobName) << "Unexpected name from readdir";
  EXPECT_EQ(readdir(dir), nullptr);

  ASSERT_EQ(close(fd.release()), 0);
  ASSERT_EQ(unlink(info->path), 0) << "Null Blob should be unlinkable";
}

TEST_P(BlobfsIntegrationTest, NullBlobCreateRemount) {
  std::unique_ptr<BlobInfo> info = GenerateRandomBlob(fs().mount_path(), 0);

  // Create the null blob.
  fbl::unique_fd fd(open(info->path, O_CREAT | O_EXCL | O_RDWR, S_IRUSR | S_IWUSR));
  ASSERT_TRUE(fd);
  ASSERT_EQ(ftruncate(fd.get(), 0), 0);
  ASSERT_EQ(close(fd.release()), 0);
  EXPECT_EQ(fs().Unmount().status_value(), ZX_OK);
  EXPECT_EQ(fs().Mount().status_value(), ZX_OK);
  fd.reset(open(info->path, O_RDONLY));
  ASSERT_TRUE(fd) << "Null blob lost after reboot";
  ASSERT_EQ(close(fd.release()), 0);
  ASSERT_EQ(unlink(info->path), 0) << "Null Blob should be unlinkable";
}

TEST_P(BlobfsIntegrationTest, ExclusiveCreate) {
  std::unique_ptr<BlobInfo> info = GenerateRandomBlob(fs().mount_path(), 1 << 17);
  fbl::unique_fd fd(open(info->path, O_CREAT | O_EXCL | O_RDWR, S_IRUSR | S_IWUSR));
  ASSERT_TRUE(fd);

  fbl::unique_fd fd2(open(info->path, O_CREAT | O_EXCL | O_RDWR, S_IRUSR | S_IWUSR));
  EXPECT_FALSE(fd2) << "Should not be able to exclusively create twice";

  // But a second open should work.
  fd2.reset(open(info->path, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR));
  ASSERT_TRUE(fd2);
}

TEST_P(BlobfsIntegrationTest, CompressibleBlob) {
  for (size_t i = 10; i < 22; i++) {
    // Create blobs which are trivially compressible.
    std::unique_ptr<BlobInfo> info = GenerateBlob(
        [](uint8_t* data, size_t length) {
          size_t i = 0;
          while (i < length) {
            size_t j = (rand() % (length - i)) + 1;
            memset(data, static_cast<uint8_t>(j), j);
            data += j;
            i += j;
          }
        },
        fs().mount_path(), 1 << i);

    fbl::unique_fd fd;
    ASSERT_NO_FATAL_FAILURE(MakeBlob(*info, &fd));

    // We can re-open and verify the Blob as read-only.
    fd.reset(open(info->path, O_RDONLY));
    ASSERT_TRUE(fd) << "Failed to-reopen blob";
    ASSERT_NO_FATAL_FAILURE(VerifyContents(fd.get(), info->data.get(), info->size_data));

    // Force decompression by remounting, re-accessing blob.
    EXPECT_EQ(fs().Unmount().status_value(), ZX_OK);
    EXPECT_EQ(fs().Mount().status_value(), ZX_OK);
    fd.reset(open(info->path, O_RDONLY));
    ASSERT_TRUE(fd) << "Failed to-reopen blob";
    ASSERT_NO_FATAL_FAILURE(VerifyContents(fd.get(), info->data.get(), info->size_data));

    ASSERT_EQ(0, unlink(info->path));
  }
}

TEST_P(BlobfsIntegrationTest, Mmap) {
  for (size_t i = 10; i < 16; i++) {
    std::unique_ptr<BlobInfo> info = GenerateRandomBlob(fs().mount_path(), 1 << i);

    fbl::unique_fd fd;
    ASSERT_NO_FATAL_FAILURE(MakeBlob(*info, &fd));
    fd.reset(open(info->path, O_RDONLY));
    ASSERT_TRUE(fd) << "Failed to-reopen blob";

    void* addr = mmap(nullptr, info->size_data, PROT_READ, MAP_PRIVATE, fd.get(), 0);
    ASSERT_NE(addr, MAP_FAILED) << "Could not mmap blob";
    ASSERT_EQ(memcmp(addr, info->data.get(), info->size_data), 0);
    ASSERT_EQ(0, munmap(addr, info->size_data));
    ASSERT_EQ(0, unlink(info->path));
  }
}

TEST_P(BlobfsIntegrationTest, MmapUseAfterClose) {
  for (size_t i = 10; i < 16; i++) {
    std::unique_ptr<BlobInfo> info = GenerateRandomBlob(fs().mount_path(), 1 << i);

    fbl::unique_fd fd;
    ASSERT_NO_FATAL_FAILURE(MakeBlob(*info, &fd));
    fd.reset(open(info->path, O_RDONLY));
    ASSERT_TRUE(fd) << "Failed to-reopen blob";

    void* addr = mmap(nullptr, info->size_data, PROT_READ, MAP_PRIVATE, fd.get(), 0);
    ASSERT_NE(addr, MAP_FAILED) << "Could not mmap blob";
    fd.reset();

    // We should be able to access the mapped data after the file is closed.
    ASSERT_EQ(memcmp(addr, info->data.get(), info->size_data), 0);

    // We should be able to re-open and remap the file.
    //
    // Although this isn't being tested explicitly (we lack a mechanism to
    // check that the second mapping uses the same underlying pages as the
    // first) the memory usage should avoid duplication in the second
    // mapping.
    fd.reset(open(info->path, O_RDONLY));
    void* addr2 = mmap(nullptr, info->size_data, PROT_READ, MAP_PRIVATE, fd.get(), 0);
    ASSERT_NE(addr2, MAP_FAILED) << "Could not mmap blob";
    fd.reset();
    ASSERT_EQ(memcmp(addr2, info->data.get(), info->size_data), 0);

    ASSERT_EQ(munmap(addr, info->size_data), 0) << "Could not unmap blob";
    ASSERT_EQ(munmap(addr2, info->size_data), 0) << "Could not unmap blob";

    ASSERT_EQ(0, unlink(info->path));
  }
}

TEST_P(BlobfsIntegrationTest, ReadDirectory) {
  constexpr size_t kMaxEntries = 50;
  constexpr size_t kBlobSize = 1 << 10;

  std::unique_ptr<BlobInfo> info[kMaxEntries];

  // Try to readdir on an empty directory.
  DIR* dir = opendir(fs().mount_path().c_str());
  ASSERT_NE(dir, nullptr);
  auto cleanup = fit::defer([dir]() { closedir(dir); });
  ASSERT_EQ(readdir(dir), nullptr) << "Expected blobfs to start empty";

  // Fill a directory with entries.
  for (std::unique_ptr<BlobInfo>& entry : info) {
    entry = GenerateRandomBlob(fs().mount_path(), kBlobSize);
    fbl::unique_fd fd;
    ASSERT_NO_FATAL_FAILURE(MakeBlob(*entry, &fd));
  }

  // Check that we see the expected number of entries
  size_t entries_seen = 0;
  struct dirent* dir_entry;
  while ((dir_entry = readdir(dir)) != nullptr) {
    entries_seen++;
  }
  ASSERT_EQ(kMaxEntries, entries_seen);
  entries_seen = 0;
  rewinddir(dir);

  // Readdir on a directory which contains entries, removing them as we go
  // along.
  while ((dir_entry = readdir(dir)) != nullptr) {
    bool found = false;
    for (const std::unique_ptr<BlobInfo>& entry : info) {
      if ((entry->size_data != 0) &&
          strcmp(strrchr(entry->path, '/') + 1, dir_entry->d_name) == 0) {
        ASSERT_EQ(0, unlink(entry->path));
        // It's a bit hacky, but we set 'size_data' to zero
        // to identify the entry has been unlinked.
        entry->size_data = 0;
        found = true;
        break;
      }
    }
    ASSERT_TRUE(found) << "Unknown directory entry";
    entries_seen++;
  }
  ASSERT_EQ(kMaxEntries, entries_seen);

  ASSERT_EQ(readdir(dir), nullptr) << "Directory should be empty";
  cleanup.cancel();
  ASSERT_EQ(0, closedir(dir));
}

fs_test::TestFilesystemOptions MinimumDiskSizeOptions() {
  auto options = fs_test::TestFilesystemOptions::BlobfsWithoutFvm();
  Superblock info;
  info.data_block_count = kMinimumDataBlocks;
  info.journal_block_count = kMinimumJournalBlocks;
  info.flags = 0;
  info.inode_count = options.num_inodes;
  options.device_block_count = TotalBlocks(info) * kBlobfsBlockSize / options.device_block_size;
  return options;
}

TEST(SmallDiskTest, SmallestValidDisk) {
  auto options = MinimumDiskSizeOptions();
  EXPECT_EQ(fs_test::TestFilesystem::Create(MinimumDiskSizeOptions()).status_value(), ZX_OK);
}

TEST(SmallDiskTest, DiskTooSmall) {
  auto options = MinimumDiskSizeOptions();
  options.device_block_count -= kBlobfsBlockSize / options.device_block_size;
  EXPECT_NE(fs_test::TestFilesystem::Create(options).status_value(), ZX_OK);
}

fs_test::TestFilesystemOptions MinimumFvmDiskSizeOptions() {
  auto options = fs_test::TestFilesystemOptions::DefaultBlobfs();
  size_t blocks_per_slice = options.fvm_slice_size / kBlobfsBlockSize;

  // Calculate slices required for data blocks based on minimum requirement and slice size.
  uint64_t required_data_slices =
      fbl::round_up(kMinimumDataBlocks, blocks_per_slice) / blocks_per_slice;
  uint64_t required_journal_slices =
      fbl::round_up(kMinimumJournalBlocks, blocks_per_slice) / blocks_per_slice;
  uint64_t required_inode_slices =
      fbl::round_up(BlocksRequiredForInode(options.num_inodes), blocks_per_slice) /
      blocks_per_slice;

  // Require an additional 1 slice each for super and block bitmaps.
  uint64_t blobfs_slices =
      required_journal_slices + required_inode_slices + required_data_slices + 2;
  fvm::Header header =
      fvm::Header::FromSliceCount(fvm::kMaxUsablePartitions, blobfs_slices, options.fvm_slice_size);
  options.device_block_count = header.fvm_partition_size / options.device_block_size;
  return options;
}

TEST(SmallDiskTest, SmallestValidFvmDisk) {
  EXPECT_EQ(fs_test::TestFilesystem::Create(MinimumFvmDiskSizeOptions()).status_value(), ZX_OK);
}

TEST(SmallDiskTest, FvmDiskTooSmall) {
  auto options = MinimumFvmDiskSizeOptions();
  options.device_block_count -= kBlobfsBlockSize / options.device_block_size;
  EXPECT_NE(fs_test::TestFilesystem::Create(options).status_value(), ZX_OK);
}

void QueryInfo(fs_test::TestFilesystem& fs, size_t expected_nodes, size_t expected_bytes) {
  fbl::unique_fd root_fd;
  ASSERT_TRUE(root_fd = fbl::unique_fd(open(fs.mount_path().c_str(), O_RDONLY | O_DIRECTORY)))
      << strerror(errno);
  fdio_cpp::UnownedFdioCaller root_connection(root_fd);
  auto result = fidl::WireCall(fidl::UnownedClientEnd<fuchsia_io::Directory>(
                                   zx::unowned_channel(root_connection.borrow_channel())))
                    ->QueryFilesystem();
  ASSERT_TRUE(result.ok()) << result.FormatDescription();
  ASSERT_EQ(result.value().s, ZX_OK) << zx_status_get_string(result.value().s);
  const auto& info = *result.value().info;

  constexpr std::string_view kFsName = "blobfs";
  const char* name = reinterpret_cast<const char*>(info.name.data());
  ASSERT_EQ(name, kFsName) << "Unexpected filesystem mounted";
  EXPECT_EQ(info.block_size, kBlobfsBlockSize);
  EXPECT_EQ(info.max_filename_size, 64U);
  EXPECT_EQ(info.fs_type, static_cast<uint32_t>(fuchsia_fs::VfsType::kBlobfs));
  EXPECT_NE(info.fs_id, 0ul);

  // Check that used_bytes are within a reasonable range
  EXPECT_GE(info.used_bytes, expected_bytes);
  EXPECT_LE(info.used_bytes, info.total_bytes);

  // Check that total_bytes are a multiple of slice_size
  const uint64_t slice_size = fs.options().fvm_slice_size;
  EXPECT_GE(info.total_bytes, slice_size);
  EXPECT_EQ(info.total_bytes % slice_size, 0ul);
  EXPECT_GE(info.total_nodes, fs.options().num_inodes);
  EXPECT_EQ((info.total_nodes * sizeof(Inode)) % slice_size, 0ul);
  EXPECT_EQ(info.used_nodes, expected_nodes);
}

TEST_F(BlobfsWithFvmTest, QueryInfo) {
  size_t total_bytes = 0;
  ASSERT_NO_FATAL_FAILURE(QueryInfo(fs(), 0, 0));
  for (size_t i = 10; i < 16; i++) {
    std::unique_ptr<BlobInfo> info = GenerateRandomBlob(fs().mount_path(), 1 << i);
    std::unique_ptr<MerkleTreeInfo> merkle_tree =
        CreateMerkleTree(info->data.get(), info->size_data, /*use_compact_format=*/true);

    fbl::unique_fd fd;
    ASSERT_NO_FATAL_FAILURE(MakeBlob(*info, &fd));
    total_bytes += fbl::round_up(merkle_tree->merkle_tree_size + info->size_data, kBlobfsBlockSize);
  }

  ASSERT_NO_FATAL_FAILURE(QueryInfo(fs(), 6, total_bytes));
}

TEST_P(BlobfsIntegrationTest, UseAfterUnlink) {
  for (size_t i = 0; i < 16; i++) {
    std::unique_ptr<BlobInfo> info = GenerateRandomBlob(fs().mount_path(), 1 << i);

    fbl::unique_fd fd;
    ASSERT_NO_FATAL_FAILURE(MakeBlob(*info, &fd));

    // We should be able to unlink the blob.
    ASSERT_EQ(0, unlink(info->path));

    // We should still be able to read the blob after unlinking.
    ASSERT_NO_FATAL_FAILURE(VerifyContents(fd.get(), info->data.get(), info->size_data));

    // After closing the file, however, we should not be able to re-open the blob.
    fd.reset();
    ASSERT_LT(open(info->path, O_RDONLY), 0) << "Expected blob to be deleted";
  }
}

TEST_P(BlobfsIntegrationTest, WriteAfterRead) {
  // srand(zxtest::Runner::GetInstance()->random_seed());
  for (size_t i = 0; i < 16; i++) {
    std::unique_ptr<BlobInfo> info = GenerateRandomBlob(fs().mount_path(), 1 << i);

    fbl::unique_fd fd;
    ASSERT_NO_FATAL_FAILURE(MakeBlob(*info, &fd));

    // After blob generation, writes should be rejected.
    ASSERT_LT(write(fd.get(), info->data.get(), 1), 0)
        << "After being written, the blob should refuse writes";

    off_t seek_pos = static_cast<off_t>(rand() % info->size_data);
    ASSERT_EQ(seek_pos, lseek(fd.get(), seek_pos, SEEK_SET));
    ASSERT_LT(write(fd.get(), info->data.get(), 1), 0)
        << "After being written, the blob should refuse writes";
    ASSERT_LT(ftruncate(fd.get(), rand() % info->size_data), 0)
        << "The blob should always refuse to be truncated";

    ASSERT_EQ(0, unlink(info->path));
  }
}

TEST_P(BlobfsIntegrationTest, WriteAfterUnlink) {
  size_t size = 1 << 20;
  std::unique_ptr<BlobInfo> info = GenerateRandomBlob(fs().mount_path(), size);

  // Partially write out first blob.
  fbl::unique_fd fd(open(info->path, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR));
  ASSERT_TRUE(fd) << "Failed to create blob";
  ASSERT_EQ(0, ftruncate(fd.get(), size));
  ASSERT_EQ(0, StreamAll(write, fd.get(), info->data.get(), size / 2)) << "Failed to write Data";
  ASSERT_EQ(0, unlink(info->path));
  ASSERT_EQ(0, StreamAll(write, fd.get(), info->data.get() + size / 2, size - (size / 2)))
      << "Failed to write Data";
  fd.reset();
  ASSERT_LT(open(info->path, O_RDONLY), 0);
}

TEST_P(BlobfsIntegrationTest, ReadTooLarge) {
  for (size_t i = 0; i < 16; i++) {
    std::unique_ptr<BlobInfo> info = GenerateRandomBlob(fs().mount_path(), 1 << i);

    fbl::unique_fd fd;
    ASSERT_NO_FATAL_FAILURE(MakeBlob(*info, &fd));

    std::unique_ptr<char[]> buffer(new char[info->size_data]);

    // Try read beyond end of blob.
    off_t end_off = static_cast<off_t>(info->size_data);
    ASSERT_EQ(end_off, lseek(fd.get(), end_off, SEEK_SET));
    ASSERT_EQ(0, read(fd.get(), buffer.get(), 1)) << "Expected empty read beyond end of file";

    // Try some reads which straddle the end of the blob.
    for (ssize_t j = 1; j < static_cast<ssize_t>(info->size_data); j *= 2) {
      end_off = static_cast<off_t>(info->size_data - j);
      ASSERT_EQ(end_off, lseek(fd.get(), end_off, SEEK_SET));
      ASSERT_EQ(j, read(fd.get(), buffer.get(), j * 2))
          << "Expected to only read one byte at end of file";
      ASSERT_EQ(memcmp(buffer.get(), &info->data[info->size_data - j], j), 0)
          << "Read data, but it was bad";
    }

    ASSERT_EQ(0, unlink(info->path));
  }
}

TEST_P(BlobfsIntegrationTest, BadCreation) {
  std::string name(fs().mount_path());
  name.append("/00112233445566778899AABBCCDDEEFFGGHHIIJJKKLLMMNNOOPPQQRRSSTTUUVV");
  fbl::unique_fd fd(open(name.c_str(), O_CREAT | O_RDWR, S_IRUSR | S_IWUSR));
  ASSERT_FALSE(fd) << "Only acceptable pathnames are hex";

  name.assign(fs().mount_path());
  name.append("/00112233445566778899AABBCCDDEEFF");
  fd.reset(open(name.c_str(), O_CREAT | O_RDWR, S_IRUSR | S_IWUSR));
  ASSERT_FALSE(fd) << "Only acceptable pathnames are 32 hex-encoded bytes";

  std::unique_ptr<BlobInfo> info = GenerateRandomBlob(fs().mount_path(), 1 << 15);

  fd.reset(open(info->path, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR));
  ASSERT_TRUE(fd) << "Failed to create blob";
  ASSERT_EQ(-1, ftruncate(fd.get(), 0)) << "Blob without data doesn't match null blob";

  // This is the size of the entire disk; we shouldn't fail here as setting blob size
  // has nothing to do with how much space blob will occupy.
  fd.reset();
  fd.reset(open(info->path, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR));
  ASSERT_EQ(
      0, ftruncate(fd.get(), fs().options().device_block_count * fs().options().device_block_size))
      << "Huge blob";

  // Write nothing, but close the blob. Since the write was incomplete,
  // it will be inaccessible.
  fd.reset(open(info->path, O_RDWR));
  ASSERT_FALSE(fd) << "Cannot access partial blob";
  fd.reset(open(info->path, O_RDONLY));
  ASSERT_FALSE(fd) << "Cannot access partial blob";

  // And once more -- let's write everything but the last byte of a blob's data.
  fd.reset(open(info->path, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR));
  ASSERT_TRUE(fd) << "Failed to create blob";
  ASSERT_EQ(0, ftruncate(fd.get(), info->size_data)) << "Failed to allocate blob";
  ASSERT_EQ(0, StreamAll(write, fd.get(), info->data.get(), info->size_data - 1))
      << "Failed to write data";
  fd.reset(open(info->path, O_RDWR));
  ASSERT_FALSE(fd) << "Cannot access partial blob";
}

// Attempts to read the contents of the Blob.
void VerifyCompromised(int fd, const uint8_t* data, size_t size_data) {
  std::unique_ptr<char[]> buf(new char[size_data]);

  ASSERT_EQ(0, lseek(fd, 0, SEEK_SET));
  ASSERT_EQ(-1, StreamAll(read, fd, buf.get(), size_data)) << "Expected reading to fail";
}

// Creates a blob with the provided Merkle tree + Data, and
// reads to verify the data.
void MakeBlobCompromised(BlobInfo* info) {
  fbl::unique_fd fd(open(info->path, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR));
  ASSERT_TRUE(fd) << "Failed to create blob";
  ASSERT_EQ(0, ftruncate(fd.get(), info->size_data));

  // If we're writing a blob with invalid sizes, it's possible that writing will fail.
  StreamAll(write, fd.get(), info->data.get(), info->size_data);

  ASSERT_NO_FATAL_FAILURE(VerifyCompromised(fd.get(), info->data.get(), info->size_data));
}

TEST_P(BlobfsIntegrationTest, CorruptBlob) {
  // srand(zxtest::Runner::GetInstance()->random_seed());
  for (size_t i = 1; i < 18; i++) {
    std::unique_ptr<BlobInfo> info = GenerateRandomBlob(fs().mount_path(), 1 << i);
    info->size_data -= (rand() % info->size_data) + 1;
    if (info->size_data == 0) {
      info->size_data = 1;
    }
    ASSERT_NO_FATAL_FAILURE(MakeBlobCompromised(info.get()));
  }

  for (size_t i = 0; i < 18; i++) {
    std::unique_ptr<BlobInfo> info = GenerateRandomBlob(fs().mount_path(), 1 << i);
    // Flip a random bit of the data.
    size_t rand_index = rand() % info->size_data;
    uint8_t old_val = info->data.get()[rand_index];
    while ((info->data.get()[rand_index] = static_cast<uint8_t>(rand())) == old_val) {
    }
    ASSERT_NO_FATAL_FAILURE(MakeBlobCompromised(info.get()));
  }
}

TEST_P(BlobfsIntegrationTest, CorruptDigest) {
  // srand(zxtest::Runner::GetInstance()->random_seed());
  for (size_t i = 1; i < 18; i++) {
    std::unique_ptr<BlobInfo> info = GenerateRandomBlob(fs().mount_path(), 1 << i);

    char hexdigits[17] = "0123456789abcdef";
    size_t idx = strlen(info->path) - 1 - (rand() % digest::kSha256HexLength);
    char newchar = hexdigits[rand() % 16];
    while (info->path[idx] == newchar) {
      newchar = hexdigits[rand() % 16];
    }
    info->path[idx] = newchar;
    ASSERT_NO_FATAL_FAILURE(MakeBlobCompromised(info.get()));
  }

  for (size_t i = 0; i < 18; i++) {
    std::unique_ptr<BlobInfo> info = GenerateRandomBlob(fs().mount_path(), 1 << i);
    // Flip a random bit of the data.
    size_t rand_index = rand() % info->size_data;
    uint8_t old_val = info->data.get()[rand_index];
    while ((info->data.get()[rand_index] = static_cast<uint8_t>(rand())) == old_val) {
    }
    ASSERT_NO_FATAL_FAILURE(MakeBlobCompromised(info.get()));
  }
}

TEST_P(BlobfsIntegrationTest, EdgeAllocation) {
  // Powers of two...
  for (size_t i = 1; i < 16; i++) {
    // -1, 0, +1 offsets...
    for (size_t j = -1; j < 2; j++) {
      std::unique_ptr<BlobInfo> info = GenerateRandomBlob(fs().mount_path(), (1 << i) + j);
      fbl::unique_fd fd;
      ASSERT_NO_FATAL_FAILURE(MakeBlob(*info, &fd));
      ASSERT_EQ(0, unlink(info->path));
    }
  }
}

TEST_P(BlobfsIntegrationTest, UmountWithOpenFile) {
  std::unique_ptr<BlobInfo> info = GenerateRandomBlob(fs().mount_path(), 1 << 16);
  fbl::unique_fd fd;
  ASSERT_NO_FATAL_FAILURE(MakeBlob(*info, &fd));

  // Intentionally don't close the file descriptor: Unmount anyway.
  EXPECT_EQ(fs().Unmount().status_value(), ZX_OK);
  EXPECT_EQ(fs().Mount().status_value(), ZX_OK);
  // Just closing our local handle; the connection should be disconnected.
  int close_return = close(fd.release());
  int close_error = errno;
  ASSERT_EQ(-1, close_return);
  ASSERT_EQ(EPIPE, close_error);

  fd.reset(open(info->path, O_RDONLY));
  ASSERT_TRUE(fd) << "Failed to open blob";
  ASSERT_NO_FATAL_FAILURE(VerifyContents(fd.get(), info->data.get(), info->size_data));
  fd.reset();

  ASSERT_EQ(0, unlink(info->path));
}

TEST_P(BlobfsIntegrationTest, UmountWithMappedFile) {
  std::unique_ptr<BlobInfo> info = GenerateRandomBlob(fs().mount_path(), 1 << 16);
  fbl::unique_fd fd;
  ASSERT_NO_FATAL_FAILURE(MakeBlob(*info, &fd));

  void* addr = mmap(nullptr, info->size_data, PROT_READ, MAP_SHARED, fd.get(), 0);
  ASSERT_NE(addr, nullptr);
  fd.reset();

  // Intentionally don't unmap the file descriptor: Unmount anyway.
  EXPECT_EQ(fs().Unmount().status_value(), ZX_OK);
  EXPECT_EQ(fs().Mount().status_value(), ZX_OK);
  ASSERT_EQ(munmap(addr, info->size_data), 0);

  fd.reset(open(info->path, O_RDONLY));
  ASSERT_TRUE(fd.get()) << "Failed to open blob";
  ASSERT_NO_FATAL_FAILURE(VerifyContents(fd.get(), info->data.get(), info->size_data));
  ASSERT_EQ(0, unlink(info->path));
}

TEST_P(BlobfsIntegrationTest, UmountWithOpenMappedFile) {
  std::unique_ptr<BlobInfo> info = GenerateRandomBlob(fs().mount_path(), 1 << 16);
  fbl::unique_fd fd;
  ASSERT_NO_FATAL_FAILURE(MakeBlob(*info, &fd));

  void* addr = mmap(nullptr, info->size_data, PROT_READ, MAP_SHARED, fd.get(), 0);
  ASSERT_NE(addr, nullptr);

  // Intentionally don't close the file descriptor: Unmount anyway.
  EXPECT_EQ(fs().Unmount().status_value(), ZX_OK);
  EXPECT_EQ(fs().Mount().status_value(), ZX_OK);
  // Just closing our local handle; the connection should be disconnected.
  ASSERT_EQ(0, munmap(addr, info->size_data));
  int close_return = close(fd.release());
  int close_error = errno;
  ASSERT_EQ(-1, close_return);
  ASSERT_EQ(EPIPE, close_error);

  fd.reset(open(info->path, O_RDONLY));
  ASSERT_TRUE(fd.get()) << "Failed to open blob";
  ASSERT_NO_FATAL_FAILURE(VerifyContents(fd.get(), info->data.get(), info->size_data));
  ASSERT_EQ(0, unlink(info->path));
}

TEST_P(BlobfsIntegrationTest, CreateUmountRemountSmall) {
  for (size_t i = 10; i < 16; i++) {
    std::unique_ptr<BlobInfo> info = GenerateRandomBlob(fs().mount_path(), 1 << i);

    fbl::unique_fd fd;
    ASSERT_NO_FATAL_FAILURE(MakeBlob(*info, &fd));

    fd.reset();
    EXPECT_EQ(fs().Unmount().status_value(), ZX_OK);
    EXPECT_EQ(fs().Mount().status_value(), ZX_OK);

    fd.reset(open(info->path, O_RDONLY));
    ASSERT_TRUE(fd) << "Failed to open blob";

    ASSERT_NO_FATAL_FAILURE(VerifyContents(fd.get(), info->data.get(), info->size_data));
    ASSERT_EQ(0, unlink(info->path));
  }
}

bool IsReadable(int fd) {
  char buf[1];
  return pread(fd, buf, sizeof(buf), 0) == sizeof(buf);
}

// Tests that we cannot read from the Blob until it has been fully written.
TEST_P(BlobfsIntegrationTest, EarlyRead) {
  std::unique_ptr<BlobInfo> info = GenerateRandomBlob(fs().mount_path(), 1 << 17);
  fbl::unique_fd fd(open(info->path, O_CREAT | O_EXCL | O_RDWR, S_IRUSR | S_IWUSR));
  ASSERT_TRUE(fd);

  // A second fd should also not be readable.
  fbl::unique_fd fd2(open(info->path, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR));
  ASSERT_TRUE(fd2);

  ASSERT_FALSE(IsReadable(fd.get())) << "Should not be readable after open";
  ASSERT_FALSE(IsReadable(fd2.get())) << "Should not be readable after open";

  ASSERT_EQ(0, ftruncate(fd.get(), info->size_data));
  ASSERT_FALSE(IsReadable(fd.get())) << "Should not be readable after alloc";
  ASSERT_FALSE(IsReadable(fd2.get())) << "Should not be readable after alloc";

  ASSERT_EQ(0, StreamAll(write, fd.get(), info->data.get(), info->size_data))
      << "Failed to write Data";

  // Okay, NOW we can read.
  // Double check that attempting to read early didn't cause problems...
  ASSERT_NO_FATAL_FAILURE(VerifyContents(fd.get(), info->data.get(), info->size_data));
  ASSERT_NO_FATAL_FAILURE(VerifyContents(fd2.get(), info->data.get(), info->size_data));

  ASSERT_TRUE(IsReadable(fd.get()));
}

// Waits for up to 10 seconds until the file is readable. Returns 0 on success.
void CheckReadable(fbl::unique_fd fd, std::atomic<bool>* result) {
  struct pollfd fds;
  fds.fd = fd.get();
  fds.events = POLLIN;

  if (poll(&fds, 1, 10000) != 1) {
    printf("Failed to wait for readable blob\n");
    *result = false;
  }

  if (fds.revents != POLLIN) {
    printf("Unexpected event\n");
    *result = false;
  }

  if (!IsReadable(fd.get())) {
    printf("Not readable\n");
    *result = false;
  }

  *result = true;
}

// Tests that poll() can tell, at some point, when it's ok to read.
TEST_P(BlobfsIntegrationTest, WaitForRead) {
  std::unique_ptr<BlobInfo> info = GenerateRandomBlob(fs().mount_path(), 1 << 17);
  fbl::unique_fd fd(open(info->path, O_CREAT | O_EXCL | O_RDWR, S_IRUSR | S_IWUSR));
  ASSERT_TRUE(fd);

  {
    // Launch a background thread to wait for the file to become readable.
    std::atomic<bool> result;
    std::thread waiter_thread(CheckReadable, std::move(fd), &result);

    MakeBlob(*info, &fd);

    waiter_thread.join();
    ASSERT_TRUE(result.load()) << "Background operation failed";
  }

  // Before continuing, make sure that MakeBlob was successful.
  ASSERT_NO_FATAL_FAILURE();

  // Double check that attempting to read early didn't cause problems...
  ASSERT_NO_FATAL_FAILURE(VerifyContents(fd.get(), info->data.get(), info->size_data));
}

void UnlinkAndRecreate(const char* path, fbl::unique_fd* fd) {
  ASSERT_EQ(0, unlink(path));
  fd->reset();  // Make sure the file is gone.
  fd->reset(open(path, O_CREAT | O_RDWR | O_EXCL, S_IRUSR | S_IWUSR));
  ASSERT_TRUE(*fd) << "Failed to recreate blob";
}

// Try unlinking while creating a blob.
TEST_P(BlobfsIntegrationTest, RestartCreation) {
  std::unique_ptr<BlobInfo> info = GenerateRandomBlob(fs().mount_path(), 1 << 17);

  fbl::unique_fd fd(open(info->path, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR));
  ASSERT_TRUE(fd) << "Failed to create blob";

  // Unlink after first open.
  ASSERT_NO_FATAL_FAILURE(UnlinkAndRecreate(info->path, &fd));

  // Unlink after init.
  ASSERT_EQ(0, ftruncate(fd.get(), info->size_data));
  ASSERT_NO_FATAL_FAILURE(UnlinkAndRecreate(info->path, &fd));

  // Unlink after first write.
  ASSERT_EQ(0, ftruncate(fd.get(), info->size_data));
  ASSERT_EQ(0, StreamAll(write, fd.get(), info->data.get(), info->size_data))
      << "Failed to write Data";
  ASSERT_NO_FATAL_FAILURE(UnlinkAndRecreate(info->path, &fd));
}

// Attempt using invalid operations.
TEST_P(BlobfsIntegrationTest, InvalidOperations) {
  // First off, make a valid blob.
  std::unique_ptr<BlobInfo> info = GenerateRandomBlob(fs().mount_path(), 1 << 12);
  fbl::unique_fd fd;
  ASSERT_NO_FATAL_FAILURE(MakeBlob(*info, &fd));
  ASSERT_NO_FATAL_FAILURE(VerifyContents(fd.get(), info->data.get(), info->size_data));

  // Try some unsupported operations.
  ASSERT_LT(rename(info->path, info->path), 0);
  ASSERT_LT(truncate(info->path, 0), 0);
  ASSERT_LT(utime(info->path, nullptr), 0);

  // Access the file once more, after these operations.
  ASSERT_NO_FATAL_FAILURE(VerifyContents(fd.get(), info->data.get(), info->size_data));
}

// Attempt operations on the root directory.
TEST_P(BlobfsIntegrationTest, RootDirectory) {
  std::string name(fs().mount_path());
  name.append("/.");
  fbl::unique_fd dirfd(open(name.c_str(), O_RDONLY));
  ASSERT_TRUE(dirfd) << "Cannot open root directory";

  std::unique_ptr<BlobInfo> info = GenerateRandomBlob(fs().mount_path(), 1 << 12);

  // Test operations which should ONLY operate on Blobs.
  ASSERT_LT(ftruncate(dirfd.get(), info->size_data), 0);

  char buf[8];
  ASSERT_LT(write(dirfd.get(), buf, 8), 0) << "Should not write to directory";
  ASSERT_LT(read(dirfd.get(), buf, 8), 0) << "Should not read from directory";

  // Should NOT be able to unlink root dir.
  ASSERT_LT(unlink(info->path), 0);
}

TEST_P(BlobfsIntegrationTest, PartialWrite) {
  size_t size = 1 << 20;
  std::unique_ptr<BlobInfo> info_complete = GenerateRandomBlob(fs().mount_path(), size);
  std::unique_ptr<BlobInfo> info_partial = GenerateRandomBlob(fs().mount_path(), size);

  // Partially write out first blob.
  fbl::unique_fd fd_partial(open(info_partial->path, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR));
  ASSERT_TRUE(fd_partial) << "Failed to create blob";
  ASSERT_EQ(0, ftruncate(fd_partial.get(), size));
  ASSERT_EQ(0, StreamAll(write, fd_partial.get(), info_partial->data.get(), size / 2))
      << "Failed to write Data";

  // Completely write out second blob.
  fbl::unique_fd fd_complete;
  ASSERT_NO_FATAL_FAILURE(MakeBlob(*info_complete, &fd_complete));
}

TEST_P(BlobfsIntegrationTest, PartialWriteSleepyDisk) {
  size_t size = 1 << 20;
  std::unique_ptr<BlobInfo> info_complete = GenerateRandomBlob(fs().mount_path(), size);
  std::unique_ptr<BlobInfo> info_partial = GenerateRandomBlob(fs().mount_path(), size);

  // Partially write out first blob.
  fbl::unique_fd fd_partial(open(info_partial->path, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR));
  ASSERT_TRUE(fd_partial) << "Failed to create blob";
  ASSERT_EQ(0, ftruncate(fd_partial.get(), size));
  ASSERT_EQ(0, StreamAll(write, fd_partial.get(), info_partial->data.get(), size / 2))
      << "Failed to write Data";

  // Completely write out second blob.
  fbl::unique_fd fd_complete;
  ASSERT_NO_FATAL_FAILURE(MakeBlob(*info_complete, &fd_complete));

  ASSERT_EQ(0, syncfs(fd_complete.get()));
  ASSERT_EQ(fs().GetRamDisk()->SleepAfter(0).status_value(), ZX_OK);

  fd_complete.reset(open(info_complete->path, O_RDONLY));
  ASSERT_TRUE(fd_complete) << "Failed to re-open blob";

  ASSERT_EQ(0, syncfs(fd_complete.get()));
  ASSERT_EQ(fs().GetRamDisk()->Wake().status_value(), ZX_OK);

  ASSERT_NO_FATAL_FAILURE(VerifyContents(fd_complete.get(), info_complete->data.get(), size));

  fd_partial.reset();
  fd_partial.reset(open(info_partial->path, O_RDONLY));
  ASSERT_FALSE(fd_partial) << "Should not be able to open invalid blob";
}

TEST_P(BlobfsIntegrationTest, MultipleWrites) {
  std::unique_ptr<BlobInfo> info = GenerateRandomBlob(fs().mount_path(), 1 << 16);

  fbl::unique_fd fd(open(info->path, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR));
  ASSERT_TRUE(fd);

  ASSERT_EQ(0, ftruncate(fd.get(), info->size_data));

  const int kNumWrites = 128;
  size_t write_size = info->size_data / kNumWrites;
  for (size_t written = 0; written < info->size_data; written += write_size) {
    ASSERT_EQ(0, StreamAll(write, fd.get(), info->data.get() + written, write_size))
        << "iteration " << written / write_size;
  }

  fd.reset();
  fd.reset(open(info->path, O_RDONLY));
  ASSERT_TRUE(fd);
  ASSERT_NO_FATAL_FAILURE(VerifyContents(fd.get(), info->data.get(), info->size_data));
}

TEST_P(BlobfsIntegrationTest, ReadOnly) {
  // Mount the filesystem as read-write. We can create new blobs.
  std::unique_ptr<BlobInfo> info = GenerateRandomBlob(fs().mount_path(), 1 << 10);
  fbl::unique_fd blob_fd;
  ASSERT_NO_FATAL_FAILURE(MakeBlob(*info, &blob_fd));
  ASSERT_NO_FATAL_FAILURE(VerifyContents(blob_fd.get(), info->data.get(), info->size_data));
  blob_fd.reset();

  EXPECT_EQ(fs().Unmount().status_value(), ZX_OK);
  fs_management::MountOptions options = fs().DefaultMountOptions();
  options.readonly = true;
  EXPECT_EQ(fs().Mount(options).status_value(), ZX_OK);

  // We can read old blobs
  blob_fd.reset(open(info->path, O_RDONLY));
  ASSERT_TRUE(blob_fd);
  ASSERT_NO_FATAL_FAILURE(VerifyContents(blob_fd.get(), info->data.get(), info->size_data));

  // We cannot create new blobs
  info = GenerateRandomBlob(fs().mount_path(), 1 << 10);
  blob_fd.reset(open(info->path, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR));
  ASSERT_FALSE(blob_fd);
}

void OpenBlockDevice(const std::string& path,
                     std::unique_ptr<block_client::RemoteBlockDevice>* block_device) {
  fbl::unique_fd fd(open(path.c_str(), O_RDWR));
  ASSERT_TRUE(fd) << "Unable to open block device";

  auto endpoints = fidl::CreateEndpoints<fuchsia_io::Node>();
  ASSERT_EQ(endpoints.status_value(), ZX_OK);
  auto [channel, server] = *std::move(endpoints);

  fdio_cpp::FdioCaller caller(std::move(fd));
  ASSERT_EQ(fidl::WireCall(
                fidl::UnownedClientEnd<fio::Node>(zx::unowned_channel(caller.borrow_channel())))
                ->Clone(fio::wire::OpenFlags::kCloneSameRights, std::move(server))
                .status(),
            ZX_OK);
  ASSERT_EQ(block_client::RemoteBlockDevice::Create(channel.TakeChannel(), block_device), ZX_OK);
}

using SliceRange = fuchsia_hardware_block_volume_VsliceRange;

uint64_t BlobfsBlockToFvmSlice(fs_test::TestFilesystem& fs, uint64_t block) {
  const size_t blocks_per_slice = fs.options().fvm_slice_size / kBlobfsBlockSize;
  return block / blocks_per_slice;
}

// The test creates a blob with data of size disk_size. The data is
// compressible so needs less space on disk. This will test if we can persist
// a blob whose uncompressed data is larger than available free space.
// The test is expected to fail when compression is turned off.
TEST_P(BlobfsIntegrationTest, BlobLargerThanAvailableSpaceTest) {
  std::unique_ptr<BlobInfo> info = GenerateBlob(
      [](uint8_t* data, size_t length) { memset(data, '\0', length); }, fs().mount_path(),
      fs().options().device_block_count * fs().options().device_block_size + 1);

  fbl::unique_fd fd;
  ASSERT_NO_FATAL_FAILURE(MakeBlob(*info, &fd))
      << "Test is expected to fail when compression is turned off";

  // We can re-open and verify the Blob as read-only.
  fd.reset(open(info->path, O_RDONLY));
  ASSERT_TRUE(fd) << "Failed to-reopen blob";
  ASSERT_NO_FATAL_FAILURE(VerifyContents(fd.get(), info->data.get(), info->size_data));

  // Force decompression by remounting, re-accessing blob.
  EXPECT_EQ(fs().Unmount().status_value(), ZX_OK);
  EXPECT_EQ(fs().Mount().status_value(), ZX_OK);
  fd.reset(open(info->path, O_RDONLY));
  ASSERT_TRUE(fd) << "Failed to-reopen blob";
  ASSERT_NO_FATAL_FAILURE(VerifyContents(fd.get(), info->data.get(), info->size_data));

  ASSERT_EQ(0, unlink(info->path));
}

void GetSliceRange(const BlobfsWithFvmTest& test, const std::vector<uint64_t>& slices,
                   std::vector<SliceRange>* ranges) {
  std::unique_ptr<block_client::RemoteBlockDevice> block_device;
  ASSERT_NO_FATAL_FAILURE(OpenBlockDevice(test.fs().DevicePath().value(), &block_device));

  size_t ranges_count;
  SliceRange range_array[fuchsia_hardware_block_volume_MAX_SLICE_REQUESTS];
  ASSERT_EQ(
      block_device->VolumeQuerySlices(slices.data(), slices.size(), range_array, &ranges_count),
      ZX_OK);
  ranges->clear();
  for (size_t i = 0; i < ranges_count; i++) {
    ranges->push_back(range_array[i]);
  }
}

// This tests growing both additional inodes and data blocks.
TEST_F(BlobfsWithFvmTest, ResizePartition) {
  EXPECT_EQ(fs().Unmount().status_value(), ZX_OK);
  std::vector<SliceRange> old_slices;
  std::vector<uint64_t> query = {BlobfsBlockToFvmSlice(fs(), kFVMNodeMapStart),
                                 BlobfsBlockToFvmSlice(fs(), kFVMDataStart)};
  ASSERT_NO_FATAL_FAILURE(GetSliceRange(*this, query, &old_slices));
  ASSERT_EQ(old_slices.size(), 2ul);
  EXPECT_EQ(fs().Mount().status_value(), ZX_OK);

  size_t required_nodes =
      (old_slices[0].count * fs().options().fvm_slice_size) / kBlobfsInodeSize + 2;
  for (size_t i = 0; i < required_nodes; i++) {
    std::unique_ptr<BlobInfo> info =
        GenerateBlob([&](void* data, size_t len) { ::memcpy(data, &i, std::min(sizeof(i), len)); },
                     fs().mount_path(), sizeof(i));

    fbl::unique_fd fd;
    ASSERT_NO_FATAL_FAILURE(MakeBlob(*info, &fd));
  }

  // Remount partition.
  EXPECT_EQ(fs().Unmount().status_value(), ZX_OK);
  EXPECT_EQ(fs().Mount().status_value(), ZX_OK);

  EXPECT_EQ(fs().Unmount().status_value(), ZX_OK);
  std::vector<SliceRange> slices;
  ASSERT_NO_FATAL_FAILURE(GetSliceRange(*this, query, &slices));
  ASSERT_EQ(slices.size(), 2ul);
  EXPECT_EQ(slices[0].count, old_slices[0].count + 1);
  EXPECT_GT(slices[1].count, old_slices[1].count);
}

void FvmShrink(const std::string& path, uint64_t offset, uint64_t length) {
  std::unique_ptr<block_client::RemoteBlockDevice> block_device;
  ASSERT_NO_FATAL_FAILURE(OpenBlockDevice(path, &block_device));
  ASSERT_EQ(block_device->VolumeShrink(offset, length), ZX_OK);
}

void FvmExtend(const std::string& path, uint64_t offset, uint64_t length) {
  std::unique_ptr<block_client::RemoteBlockDevice> block_device;
  ASSERT_NO_FATAL_FAILURE(OpenBlockDevice(path, &block_device));
  ASSERT_EQ(block_device->VolumeExtend(offset, length), ZX_OK);
}

TEST_F(BlobfsWithFvmTest, CorruptAtMount) {
  EXPECT_EQ(fs().Unmount().status_value(), ZX_OK);

  // Shrink slice so FVM will differ from Blobfs.
  uint64_t offset = BlobfsBlockToFvmSlice(fs(), kFVMNodeMapStart);
  std::vector<SliceRange> slices;
  std::vector<uint64_t> query = {BlobfsBlockToFvmSlice(fs(), kFVMNodeMapStart)};
  ASSERT_NO_FATAL_FAILURE(GetSliceRange(*this, query, &slices));
  ASSERT_EQ(slices.size(), 1ul);
  uint64_t len = slices[0].count;
  ASSERT_GT(len, 0ul);
  ASSERT_NO_FATAL_FAILURE(FvmShrink(fs().DevicePath().value(), offset + len - 1, 1));

  fbl::unique_fd fd(open(fs().DevicePath().value().c_str(), O_RDWR));
  ASSERT_TRUE(fd);

  ASSERT_NE(fs_management::Mount(std::move(fd), fs_management::kDiskFormatBlobfs,
                                 fs().DefaultMountOptions(), fs_management::LaunchStdioAsync)
                .status_value(),
            ZX_OK);

  // Grow slice count with one extra slice.
  ASSERT_NO_FATAL_FAILURE(FvmExtend(fs().DevicePath().value(), offset + len - 1, 2));

  EXPECT_EQ(fs().Mount().status_value(), ZX_OK);
  EXPECT_EQ(fs().Unmount().status_value(), ZX_OK);

  // Verify that mount automatically removed the extra slice.
  ASSERT_NO_FATAL_FAILURE(GetSliceRange(*this, query, &slices));
  ASSERT_EQ(slices.size(), 1ul);
  EXPECT_TRUE(slices[0].allocated);
  EXPECT_EQ(slices[0].count, len);
}

TEST_P(BlobfsIntegrationTest, FailedWrite) {
  const uint64_t pages_per_block = kBlobfsBlockSize / fs().options().device_block_size;

  std::unique_ptr<BlobInfo> info = GenerateRandomBlob(fs().mount_path(), kBlobfsBlockSize);

  fbl::unique_fd fd(open(info->path, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR));
  ASSERT_TRUE(fd) << "Failed to create blob";

  // Truncate before sleeping the ramdisk. This is so potential FVM updates
  // do not interfere with the ramdisk block count.
  ASSERT_EQ(ftruncate(fd.get(), info->size_data), 0);

  // Journal:
  // - One Superblock block
  // - One Inode table block
  // - One Bitmap block
  //
  // Non-journal:
  // - One Inode table block
  // - One Data block
  constexpr uint64_t kBlockCountToWrite = 5;

  // Sleep after |kBlockCountToWrite - 1| blocks. This is 1 less than will be needed to write out
  // the entire blob. This ensures that writing the blob will ultimately fail, but the write
  // operation will return a successful response.
  ASSERT_EQ(
      fs().GetRamDisk()->SleepAfter(pages_per_block * (kBlockCountToWrite - 1)).status_value(),
      ZX_OK);
  auto wake = fit::defer([&] { ASSERT_EQ(fs().GetRamDisk()->Wake().status_value(), ZX_OK); });

  ASSERT_EQ(write(fd.get(), info->data.get(), info->size_data),
            static_cast<ssize_t>(info->size_data));

  // Since the write operation ultimately failed when going out to disk,
  // syncfs will return a failed response.
  ASSERT_LT(syncfs(fd.get()), 0);

  info = GenerateRandomBlob(fs().mount_path(), kBlobfsBlockSize);
  fd.reset(open(info->path, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR));
  ASSERT_TRUE(fd) << "Failed to create blob";

  // On an FVM, truncate may either succeed or fail. If an FVM extend call is necessary,
  // it will fail since the ramdisk is asleep; otherwise, it will pass.
  ftruncate(fd.get(), static_cast<off_t>(info->size_data));

  // Since the ramdisk is asleep and our blobfs is aware of it due to the sync, write should fail.
  // TODO(smklein): Implement support for "failed write propagates to the client before
  // sync".
  // ASSERT_LT(write(fd.get(), info->data.get(), kBlobfsBlockSize), 0);
}

struct CloneThreadArgs {
  const BlobInfo* info = nullptr;
  std::atomic_bool done{false};
};

void CloneThread(CloneThreadArgs* args) {
  while (!args->done) {
    fbl::unique_fd fd(open(args->info->path, O_RDONLY));
    ASSERT_TRUE(fd);
    void* addr = mmap(nullptr, args->info->size_data, PROT_READ, MAP_PRIVATE, fd.get(), 0);
    ASSERT_NE(addr, MAP_FAILED) << "Could not mmap blob";
    // Explicitly close |fd| before unmapping.
    fd.reset();
    // Yielding before unmapping significantly improves the ability of this test to detect bugs
    // (e.g. fxbug.dev/53882) by increasing the length of time that the file is closed but still has
    // a VMO clone.
    zx_thread_legacy_yield(0);
    ASSERT_EQ(0, munmap(addr, args->info->size_data));
  }
}

// This test ensures that blobfs' lifecycle management correctly deals with a highly volatile
// number of VMO clones (which blobfs has special logic to handle, preventing the in-memory
// blob from being discarded while there are active clones).
// See fxbug.dev/53882 for background on this test case.
TEST_P(BlobfsIntegrationTest, VmoCloneWatchingTest) {
  std::unique_ptr<BlobInfo> info = GenerateBlob(CharFill<'A'>, fs().mount_path(), 4096);

  {
    fbl::unique_fd fd;
    ASSERT_NO_FATAL_FAILURE(MakeBlob(*info, &fd));
  }

  struct CloneThreadArgs thread_args {
    .info = info.get(),
  };
  std::thread clone_thread(CloneThread, &thread_args);

  constexpr int kIterations = 1000;
  for (int i = 0; i < kIterations; ++i) {
    fbl::unique_fd fd(open(info->path, O_RDONLY));
    ASSERT_TRUE(fd);
    void* addr = mmap(nullptr, info->size_data, PROT_READ, MAP_PRIVATE, fd.get(), 0);
    ASSERT_NE(addr, MAP_FAILED) << "Could not mmap blob";
    fd.reset();

    // Ensure that the contents read out from the VMO match expectations.
    // If the blob is destroyed while there are still active clones, and paging is enabled, future
    // reads for uncommitted sections of the VMO will be full of zeroes (this is the kernel's
    // behavior when the pager source is detached from a pager-backed VMO), which would fail this
    // assertion.
    char* ptr = static_cast<char*>(addr);
    for (size_t j = 0; j < info->size_data; ++j) {
      ASSERT_EQ(ptr[j], 'A');
    }
    ASSERT_EQ(0, munmap(addr, info->size_data));
  }

  thread_args.done = true;
  clone_thread.join();
}

TEST_P(BlobfsIntegrationTest, ReaddirAfterUnlinkingFileWithOpenHandleShouldNotReturnFile) {
  std::unique_ptr<BlobInfo> info = GenerateRandomBlob(fs().mount_path(), 1 << 5);
  fbl::unique_fd fd;
  ASSERT_NO_FATAL_FAILURE(MakeBlob(*info, &fd));

  // Make sure the blob can be listed with readdir.
  DIR* dir = opendir(fs().mount_path().c_str());
  ASSERT_NE(dir, nullptr);
  auto cleanup = fit::defer([dir]() { closedir(dir); });
  struct dirent* dir_entry = readdir(dir);
  ASSERT_EQ(strcmp(strrchr(info->path, '/') + 1, dir_entry->d_name), 0);

  // Unlink the blob while it's still open.
  ASSERT_EQ(0, unlink(info->path));

  // Check that the blob is no longer included in readdir.
  rewinddir(dir);
  dir_entry = readdir(dir);
  EXPECT_EQ(dir_entry, nullptr);

  // Verify that the blob is still open.
  constexpr ssize_t bytes_to_check = 20;
  std::array<uint8_t, bytes_to_check> buf;
  ASSERT_EQ(lseek(fd.get(), 0, SEEK_SET), 0);
  EXPECT_EQ(read(fd.get(), buf.data(), bytes_to_check), bytes_to_check);
  EXPECT_EQ(memcmp(buf.data(), info->data.get(), bytes_to_check), 0);
}

class BlobfsMetricIntegrationTest : public FdioTest {
 protected:
  void GetReadBytes(uint64_t* total_read_bytes) {
    const std::array<std::string, 2> algorithms = {"uncompressed", "chunked"};
    const std::array<std::string, 2> read_methods = {"paged_read_stats", "unpaged_read_stats"};
    fpromise::result<inspect::Hierarchy> hierarchy_or_error = TakeSnapshot();
    ASSERT_TRUE(hierarchy_or_error.is_ok());
    inspect::Hierarchy hierarchy = std::move(hierarchy_or_error.value());
    *total_read_bytes = 0;
    for (const std::string& algorithm : algorithms) {
      for (const std::string& stat : read_methods) {
        uint64_t read_bytes;
        ASSERT_NO_FATAL_FAILURE(
            GetUintMetricFromHierarchy(hierarchy, {stat, algorithm}, "read_bytes", &read_bytes));
        *total_read_bytes += read_bytes;
      }
    }
  }
};

TEST_F(BlobfsMetricIntegrationTest, CreateAndRead) {
  uint64_t blobs_created;
  ASSERT_NO_FATAL_FAILURE(GetUintMetric({"allocation_stats"}, "blobs_created", &blobs_created));
  ASSERT_EQ(blobs_created, 0ul);

  // Create a new blob with random contents on the mounted filesystem. This is
  // both random and small enough that it should not get compressed.
  std::unique_ptr<BlobInfo> info = GenerateRandomBlob(".", 1 << 10);
  {
    fbl::unique_fd fd(openat(root_fd(), info->path, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR));
    ASSERT_TRUE(fd.is_valid());
    ASSERT_EQ(ftruncate(fd.get(), info->size_data), 0);
    ASSERT_EQ(StreamAll(write, fd.get(), info->data.get(), info->size_data), 0)
        << "Failed to write Data";
  }

  ASSERT_NO_FATAL_FAILURE(GetUintMetric({"allocation_stats"}, "blobs_created", &blobs_created));
  ASSERT_EQ(blobs_created, 1ul);

  uint64_t read_bytes = 0;
  ASSERT_NO_FATAL_FAILURE(GetReadBytes(&read_bytes));
  ASSERT_EQ(read_bytes, 0ul);

  {
    fbl::unique_fd fd(openat(root_fd(), info->path, O_RDONLY));
    ASSERT_TRUE(fd.is_valid());
    ASSERT_NO_FATAL_FAILURE(VerifyContents(fd.get(), info->data.get(), info->size_data));
  }

  ASSERT_NO_FATAL_FAILURE(GetReadBytes(&read_bytes));
  ASSERT_EQ(read_bytes, fbl::round_up(info->size_data, kBlobfsBlockSize));
}

TEST_F(BlobfsMetricIntegrationTest, BlobfsInspectTree) {
  using namespace inspect::testing;
  using namespace ::testing;

  fpromise::result<inspect::Hierarchy> hierarchy_or_error = TakeSnapshot();
  ASSERT_TRUE(hierarchy_or_error.is_ok());

  const inspect::Hierarchy* blobfs_root = hierarchy_or_error.value().GetByPath({"blobfs"});
  ASSERT_NE(blobfs_root, nullptr);

  // Ensure that all nodes we expect exist.
  for (const char* name :
       {fs_inspect::kInfoNodeName, fs_inspect::kUsageNodeName, fs_inspect::kFvmNodeName}) {
    ASSERT_NE(blobfs_root->GetByPath({name}), nullptr)
        << "Could not find expected node in Blobfs inspect hierarchy: " << name;
  }

  // Test known values specific to Blobfs.
  const inspect::Hierarchy* info_node = blobfs_root->GetByPath({fs_inspect::kInfoNodeName});
  ASSERT_NE(info_node, nullptr);
  EXPECT_THAT(
      *info_node,
      NodeMatches(AllOf(
          NameMatches(fs_inspect::kInfoNodeName),
          PropertyList(IsSupersetOf({StringIs(fs_inspect::InfoData::kPropName, "blobfs"),
                                     UintIs(fs_inspect::InfoData::kPropMaxFilenameLength, 64),
                                     StringIs(fs_inspect::InfoData::kPropOldestVersion,
                                              ::testing::MatchesRegex("^[0-9]+\\/[0-9]+$"))})))));

  const inspect::Hierarchy* usage_node = blobfs_root->GetByPath({fs_inspect::kUsageNodeName});
  ASSERT_NE(usage_node, nullptr);
  EXPECT_THAT(*usage_node,
              NodeMatches(AllOf(
                  NameMatches(fs_inspect::kUsageNodeName),
                  PropertyList(IsSupersetOf({UintIs(fs_inspect::UsageData::kPropUsedNodes, 0)})))));

  // Create a file to increase the used inode count.
  {
    std::unique_ptr<BlobInfo> info = GenerateRandomBlob(".", 1 << 10);
    fbl::unique_fd fd(openat(root_fd(), info->path, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR));
    ASSERT_TRUE(fd.is_valid());
    ASSERT_EQ(ftruncate(fd.get(), info->size_data), 0);
    ASSERT_EQ(StreamAll(write, fd.get(), info->data.get(), info->size_data), 0)
        << "Failed to write Data";
  }

  // Take a new snapshot of the tree and check that the used node count went up.
  hierarchy_or_error = TakeSnapshot();
  ASSERT_TRUE(hierarchy_or_error.is_ok());
  blobfs_root = hierarchy_or_error.value().GetByPath({"blobfs"});
  ASSERT_NE(blobfs_root, nullptr);

  usage_node = blobfs_root->GetByPath({fs_inspect::kUsageNodeName});
  ASSERT_NE(usage_node, nullptr);
  EXPECT_THAT(*usage_node,
              NodeMatches(AllOf(
                  NameMatches(fs_inspect::kUsageNodeName),
                  PropertyList(IsSupersetOf({UintIs(fs_inspect::UsageData::kPropUsedNodes, 1)})))));
}

INSTANTIATE_TEST_SUITE_P(/*no prefix*/, BlobfsIntegrationTest,
                         testing::Values(BlobfsDefaultTestParam(), BlobfsWithFvmTestParam(),
                                         BlobfsWithPaddedLayoutTestParam()),
                         testing::PrintToStringParamName());

}  // namespace
}  // namespace blobfs
