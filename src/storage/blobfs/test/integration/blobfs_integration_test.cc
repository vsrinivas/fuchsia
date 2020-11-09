// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <fuchsia/blobfs/c/fidl.h>
#include <fuchsia/io/llcpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async-loop/loop.h>
#include <lib/async/cpp/executor.h>
#include <lib/async/default.h>
#include <lib/fdio/cpp/caller.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fidl-utils/bind.h>
#include <lib/inspect/cpp/hierarchy.h>
#include <lib/inspect/service/cpp/reader.h>
#include <lib/zx/vmo.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <utime.h>
#include <zircon/device/vfs.h>
#include <zircon/fidl.h>

#include <array>
#include <atomic>
#include <thread>
#include <vector>

#include <block-client/cpp/remote-block-device.h>
#include <digest/digest.h>
#include <fbl/array.h>
#include <fbl/auto_call.h>
#include <fs-management/launch.h>
#include <fs-management/mount.h>
#include <fs/test_support/test_support.h>
#include <fs/trace.h>
#include <fvm/format.h>
#include <gtest/gtest.h>

#include "src/storage/blobfs/test/integration/blobfs_fixtures.h"
#include "src/storage/blobfs/test/integration/fdio_test.h"

namespace blobfs {
namespace {

namespace fio = ::llcpp::fuchsia::io;

void VerifyCorruptedBlob(int fd, const char* data, size_t size_data) {
  // Verify the contents of the Blob
  fbl::Array<char> buf(new char[size_data], size_data);

  ASSERT_EQ(lseek(fd, 0, SEEK_SET), 0);
  ASSERT_EQ(StreamAll(read, fd, &buf[0], size_data), -1) << "Expected reading to fail";
}

// Creates a corrupted blob with the provided Merkle tree + Data, and
// reads to verify the data.
void ReadBlobCorrupted(BlobInfo* info) {
  fbl::unique_fd fd(open(info->path, O_CREAT | O_RDWR));
  ASSERT_TRUE(fd) << "Failed to create blob";
  ASSERT_EQ(ftruncate(fd.get(), info->size_data), 0);

  // Writing corrupted blob to disk.
  StreamAll(write, fd.get(), info->data.get(), info->size_data);

  ASSERT_NO_FATAL_FAILURE(VerifyCorruptedBlob(fd.get(), info->data.get(), info->size_data));
  ASSERT_EQ(close(fd.release()), 0);
}

// Class emulating the corruption handler service
class CorruptBlobHandler final {
 public:
  using CorruptBlobHandlerBinder = fidl::Binder<CorruptBlobHandler>;

  ~CorruptBlobHandler() {}

  zx_status_t CorruptBlob(const uint8_t* merkleroot_hash, size_t count) {
    num_calls_++;
    return ZX_OK;
  }

  zx_status_t Bind(async_dispatcher_t* dispatcher, zx::channel channel) {
    static constexpr fuchsia_blobfs_CorruptBlobHandler_ops_t kOps = {
        .CorruptBlob = CorruptBlobHandlerBinder::BindMember<&CorruptBlobHandler::CorruptBlob>,
    };

    return CorruptBlobHandlerBinder::BindOps<fuchsia_blobfs_CorruptBlobHandler_dispatch>(
        dispatcher, std::move(channel), this, &kOps);
  }

  void UpdateClientHandle(zx::channel client) { client_ = std::move(client); }

  zx_handle_t GetClientHandle() { return client_.get(); }

  // Checks if the corruption handler is called.
  bool IsCalled(void) { return num_calls_ > 0; }

 private:
  zx::channel client_, server_;
  size_t num_calls_;
};

// Go over the parent device logic and test fixture.
TEST_F(BlobfsTest, Trivial) {}

TEST_F(BlobfsTestWithFvm, Trivial) {}

void RunBasicsTest(fs_test::TestFilesystem& fs) {
  for (unsigned int i = 10; i < 16; i++) {
    std::unique_ptr<BlobInfo> info;
    ASSERT_NO_FATAL_FAILURE(GenerateRandomBlob(fs.mount_path(), 1 << i, &info));

    fbl::unique_fd fd;
    ASSERT_NO_FATAL_FAILURE(MakeBlob(info.get(), &fd));
    ASSERT_EQ(close(fd.release()), 0);

    // We can re-open and verify the Blob as read-only
    fd.reset(open(info->path, O_RDONLY));
    ASSERT_TRUE(fd) << "Failed to-reopen blob";
    ASSERT_NO_FATAL_FAILURE(VerifyContents(fd.get(), info->data.get(), info->size_data));
    ASSERT_EQ(close(fd.release()), 0);

    // We cannot re-open the blob as writable
    fd.reset(open(info->path, O_RDWR | O_CREAT));
    ASSERT_FALSE(fd) << "Shouldn't be able to re-create blob that exists";
    fd.reset(open(info->path, O_RDWR));
    ASSERT_FALSE(fd) << "Shouldn't be able to re-open blob as writable";
    fd.reset(open(info->path, O_WRONLY));
    ASSERT_FALSE(fd) << "Shouldn't be able to re-open blob as writable";

    ASSERT_EQ(unlink(info->path), 0);
  }
}

TEST_F(BlobfsTest, Basics) { RunBasicsTest(fs()); }

TEST_F(BlobfsTestWithFvm, Basics) { RunBasicsTest(fs()); }

void StartMockCorruptionHandlerService(async_dispatcher_t* dispatcher,
                                       std::unique_ptr<CorruptBlobHandler>* out) {
  zx::channel client, server;
  zx_status_t status = zx::channel::create(0, &client, &server);
  ASSERT_EQ(ZX_OK, status);
  auto handler = std::unique_ptr<CorruptBlobHandler>(new CorruptBlobHandler());
  ASSERT_EQ(ZX_OK, handler->Bind(dispatcher, std::move(server)));

  handler->UpdateClientHandle(std::move(client));
  *out = std::move(handler);
}

void RunBlobCorruptionTest(fs_test::TestFilesystem& fs) {
  // Start the corruption handler server.
  std::unique_ptr<CorruptBlobHandler> corruption_server;
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  ASSERT_EQ(ZX_OK, loop.StartThread("corruption-dispatcher"));

  ASSERT_NO_FATAL_FAILURE(StartMockCorruptionHandlerService(loop.dispatcher(), &corruption_server));
  zx_handle_t blobfs_client = corruption_server->GetClientHandle();

  // Pass the client end to blobfs.
  fbl::unique_fd fd(open(fs.mount_path().c_str(), O_RDONLY | O_DIRECTORY));
  ASSERT_TRUE(fd);
  zx_status_t status;
  fdio_cpp::FdioCaller caller(std::move(fd));

  ASSERT_EQ(fuchsia_blobfs_BlobfsSetCorruptBlobHandler(caller.borrow_channel(),
                                                       std::move(blobfs_client), &status),
            ZX_OK);
  ASSERT_EQ(status, ZX_OK);

  // Create a blob, corrupt it and then attempt to read it.
  std::unique_ptr<BlobInfo> info;
  GenerateRandomBlob(fs.mount_path().c_str(), 1 << 5, &info);
  // Flip a random bit of the data
  size_t rand_index = rand() % info->size_data;
  char old_val = info->data.get()[rand_index];
  while ((info->data.get()[rand_index] = static_cast<char>(rand())) == old_val) {
  }

  ASSERT_NO_FATAL_FAILURE(ReadBlobCorrupted(info.get()));
  // Shutdown explicitly calls "join" on the "corruption-dispatcher" thread and waits for it
  // to increment num_calls_.
  loop.Shutdown();
  ASSERT_TRUE(corruption_server->IsCalled());
}
// TODO Enable these fxbug.dev/56432
TEST_F(BlobfsTest, DISABLED_CorruptBlobNotify) { RunBlobCorruptionTest(fs()); }

TEST_F(BlobfsTestWithFvm, DISABLED_CorruptBlobNotify) { RunBlobCorruptionTest(fs()); }

void RunUnallocatedBlobTest(fs_test::TestFilesystem& fs) {
  std::unique_ptr<BlobInfo> info;
  ASSERT_NO_FATAL_FAILURE(GenerateRandomBlob(fs.mount_path(), 1 << 10, &info));

  // We can create a blob with a name.
  ASSERT_TRUE(fbl::unique_fd(open(info->path, O_CREAT | O_EXCL | O_RDWR)));
  // It won't exist if we close it before allocating space.
  ASSERT_FALSE(fbl::unique_fd(open(info->path, O_RDWR)));
  ASSERT_FALSE(fbl::unique_fd(open(info->path, O_RDONLY)));
  // We can "re-use" the name.
  {
    fbl::unique_fd fd(open(info->path, O_CREAT | O_EXCL | O_RDWR));
    ASSERT_TRUE(fd);
    ASSERT_EQ(ftruncate(fd.get(), info->size_data), 0);
  }
}

TEST_F(BlobfsTest, UnallocatedBlob) { RunUnallocatedBlobTest(fs()); }

TEST_F(BlobfsTestWithFvm, UnallocatedBlob) { RunUnallocatedBlobTest(fs()); }

void RunNullBlobCreateUnlinkTest(fs_test::TestFilesystem& fs) {
  std::unique_ptr<BlobInfo> info;
  ASSERT_NO_FATAL_FAILURE(GenerateRandomBlob(fs.mount_path(), 0, &info));

  fbl::unique_fd fd(open(info->path, O_CREAT | O_EXCL | O_RDWR));
  ASSERT_TRUE(fd);
  ASSERT_EQ(ftruncate(fd.get(), 0), 0);
  std::array<char, 1> buf;
  ASSERT_EQ(read(fd.get(), &buf[0], 1), 0) << "Null Blob should reach EOF immediately";
  ASSERT_EQ(close(fd.release()), 0);

  fd.reset(open(info->path, O_CREAT | O_EXCL | O_RDWR));
  EXPECT_FALSE(fd) << "Null Blob should already exist";
  fd.reset(open(info->path, O_CREAT | O_RDWR));
  EXPECT_FALSE(fd) << "Null Blob should not be openable as writable";

  fd.reset(open(info->path, O_RDONLY));
  ASSERT_TRUE(fd) << "Null blob should be re-openable";

  DIR* dir = opendir(fs.mount_path().c_str());
  ASSERT_NE(dir, nullptr);
  auto cleanup = fbl::MakeAutoCall([dir]() { closedir(dir); });
  struct dirent* entry = readdir(dir);
  ASSERT_NE(entry, nullptr);
  constexpr std::string_view kEmptyBlobName =
      "15ec7bf0b50732b49f8228e07d24365338f9e3ab994b00af08e5a3bffe55fd8b";
  EXPECT_EQ(entry->d_name, kEmptyBlobName) << "Unexpected name from readdir";
  EXPECT_EQ(readdir(dir), nullptr);

  ASSERT_EQ(close(fd.release()), 0);
  ASSERT_EQ(unlink(info->path), 0) << "Null Blob should be unlinkable";
}

TEST_F(BlobfsTest, NullBlobCreateUnlink) { RunNullBlobCreateUnlinkTest(fs()); }

TEST_F(BlobfsTestWithFvm, NullBlobCreateUnlink) { RunNullBlobCreateUnlinkTest(fs()); }

void RunNullBlobCreateRemountTest(fs_test::TestFilesystem& fs) {
  std::unique_ptr<BlobInfo> info;
  ASSERT_NO_FATAL_FAILURE(GenerateRandomBlob(fs.mount_path(), 0, &info));

  // Create the null blob.
  fbl::unique_fd fd(open(info->path, O_CREAT | O_EXCL | O_RDWR));
  ASSERT_TRUE(fd);
  ASSERT_EQ(ftruncate(fd.get(), 0), 0);
  ASSERT_EQ(close(fd.release()), 0);
  EXPECT_EQ(fs.Unmount().status_value(), ZX_OK);
  EXPECT_EQ(fs.Mount().status_value(), ZX_OK);
  fd.reset(open(info->path, O_RDONLY));
  ASSERT_TRUE(fd) << "Null blob lost after reboot";
  ASSERT_EQ(close(fd.release()), 0);
  ASSERT_EQ(unlink(info->path), 0) << "Null Blob should be unlinkable";
}

TEST_F(BlobfsTest, NullBlobCreateRemount) { RunNullBlobCreateRemountTest(fs()); }

TEST_F(BlobfsTestWithFvm, NullBlobCreateRemount) { RunNullBlobCreateRemountTest(fs()); }

void RunExclusiveCreateTest(fs_test::TestFilesystem& fs) {
  std::unique_ptr<BlobInfo> info;

  ASSERT_NO_FATAL_FAILURE(GenerateRandomBlob(fs.mount_path(), 1 << 17, &info));
  fbl::unique_fd fd(open(info->path, O_CREAT | O_EXCL | O_RDWR));
  ASSERT_TRUE(fd);

  fbl::unique_fd fd2(open(info->path, O_CREAT | O_EXCL | O_RDWR));
  EXPECT_FALSE(fd2) << "Should not be able to exclusively create twice";

  // But a second open should work.
  fd2.reset(open(info->path, O_CREAT | O_RDWR));
  ASSERT_TRUE(fd2);
}

TEST_F(BlobfsTest, ExclusiveCreate) { RunExclusiveCreateTest(fs()); }

TEST_F(BlobfsTestWithFvm, ExclusiveCreate) { RunExclusiveCreateTest(fs()); }

void RunCompressibleBlobTest(fs_test::TestFilesystem& fs) {
  for (size_t i = 10; i < 22; i++) {
    std::unique_ptr<BlobInfo> info;

    // Create blobs which are trivially compressible.
    ASSERT_NO_FATAL_FAILURE(GenerateBlob(
        [](char* data, size_t length) {
          size_t i = 0;
          while (i < length) {
            size_t j = (rand() % (length - i)) + 1;
            memset(data, (char)j, j);
            data += j;
            i += j;
          }
        },
        fs.mount_path().c_str(), 1 << i, &info));

    fbl::unique_fd fd;
    ASSERT_NO_FATAL_FAILURE(MakeBlob(info.get(), &fd));

    // We can re-open and verify the Blob as read-only.
    fd.reset(open(info->path, O_RDONLY));
    ASSERT_TRUE(fd) << "Failed to-reopen blob";
    ASSERT_NO_FATAL_FAILURE(VerifyContents(fd.get(), info->data.get(), info->size_data));

    // Force decompression by remounting, re-accessing blob.
    EXPECT_EQ(fs.Unmount().status_value(), ZX_OK);
    EXPECT_EQ(fs.Mount().status_value(), ZX_OK);
    fd.reset(open(info->path, O_RDONLY));
    ASSERT_TRUE(fd) << "Failed to-reopen blob";
    ASSERT_NO_FATAL_FAILURE(VerifyContents(fd.get(), info->data.get(), info->size_data));

    ASSERT_EQ(0, unlink(info->path));
  }
}

TEST_F(BlobfsTest, CompressibleBlob) { RunCompressibleBlobTest(fs()); }

TEST_F(BlobfsTestWithFvm, CompressibleBlob) { RunCompressibleBlobTest(fs()); }

void RunMmapTest(fs_test::TestFilesystem& fs) {
  for (size_t i = 10; i < 16; i++) {
    std::unique_ptr<BlobInfo> info;
    ASSERT_NO_FATAL_FAILURE(GenerateRandomBlob(fs.mount_path(), 1 << i, &info));

    fbl::unique_fd fd;
    ASSERT_NO_FATAL_FAILURE(MakeBlob(info.get(), &fd));
    fd.reset(open(info->path, O_RDONLY));
    ASSERT_TRUE(fd) << "Failed to-reopen blob";

    void* addr = mmap(NULL, info->size_data, PROT_READ, MAP_PRIVATE, fd.get(), 0);
    ASSERT_NE(addr, MAP_FAILED) << "Could not mmap blob";
    ASSERT_EQ(memcmp(addr, info->data.get(), info->size_data), 0);
    ASSERT_EQ(0, munmap(addr, info->size_data));
    ASSERT_EQ(0, unlink(info->path));
  }
}

TEST_F(BlobfsTest, Mmap) { RunMmapTest(fs()); }

TEST_F(BlobfsTestWithFvm, Mmap) { RunMmapTest(fs()); }

void RunMmapUseAfterCloseTest(fs_test::TestFilesystem& fs) {
  for (size_t i = 10; i < 16; i++) {
    std::unique_ptr<BlobInfo> info;
    ASSERT_NO_FATAL_FAILURE(GenerateRandomBlob(fs.mount_path(), 1 << i, &info));

    fbl::unique_fd fd;
    ASSERT_NO_FATAL_FAILURE(MakeBlob(info.get(), &fd));
    fd.reset(open(info->path, O_RDONLY));
    ASSERT_TRUE(fd) << "Failed to-reopen blob";

    void* addr = mmap(NULL, info->size_data, PROT_READ, MAP_PRIVATE, fd.get(), 0);
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
    void* addr2 = mmap(NULL, info->size_data, PROT_READ, MAP_PRIVATE, fd.get(), 0);
    ASSERT_NE(addr2, MAP_FAILED) << "Could not mmap blob";
    fd.reset();
    ASSERT_EQ(memcmp(addr2, info->data.get(), info->size_data), 0);

    ASSERT_EQ(munmap(addr, info->size_data), 0) << "Could not unmap blob";
    ASSERT_EQ(munmap(addr2, info->size_data), 0) << "Could not unmap blob";

    ASSERT_EQ(0, unlink(info->path));
  }
}

TEST_F(BlobfsTest, MmapUseAfterClose) { RunMmapUseAfterCloseTest(fs()); }

TEST_F(BlobfsTestWithFvm, MmapUseAfterClose) { RunMmapUseAfterCloseTest(fs()); }

void RunReadDirectoryTest(fs_test::TestFilesystem& fs) {
  constexpr size_t kMaxEntries = 50;
  constexpr size_t kBlobSize = 1 << 10;

  std::unique_ptr<BlobInfo> info[kMaxEntries];

  // Try to readdir on an empty directory.
  DIR* dir = opendir(fs.mount_path().c_str());
  ASSERT_NE(dir, nullptr);
  auto cleanup = fbl::MakeAutoCall([dir]() { closedir(dir); });
  ASSERT_EQ(readdir(dir), nullptr) << "Expected blobfs to start empty";

  // Fill a directory with entries.
  for (size_t i = 0; i < kMaxEntries; i++) {
    ASSERT_NO_FATAL_FAILURE(GenerateRandomBlob(fs.mount_path(), kBlobSize, &info[i]));
    fbl::unique_fd fd;
    ASSERT_NO_FATAL_FAILURE(MakeBlob(info[i].get(), &fd));
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
    for (size_t i = 0; i < kMaxEntries; i++) {
      if ((info[i]->size_data != 0) &&
          strcmp(strrchr(info[i]->path, '/') + 1, dir_entry->d_name) == 0) {
        ASSERT_EQ(0, unlink(info[i]->path));
        // It's a bit hacky, but we set 'size_data' to zero
        // to identify the entry has been unlinked.
        info[i]->size_data = 0;
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

TEST_F(BlobfsTest, ReadDirectory) { RunReadDirectoryTest(fs()); }

TEST_F(BlobfsTestWithFvm, ReadDirectory) { RunReadDirectoryTest(fs()); }

fs_test::TestFilesystemOptions MinimumDiskSizeOptions() {
  Superblock info;
  info.inode_count = kBlobfsDefaultInodeCount;
  info.data_block_count = kMinimumDataBlocks;
  info.journal_block_count = kMinimumJournalBlocks;
  info.flags = 0;
  auto options = fs_test::TestFilesystemOptions::BlobfsWithoutFvm();
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
      fbl::round_up(kDefaultJournalBlocks, blocks_per_slice) / blocks_per_slice;

  // Require an additional 1 slice each for super, inode, and block bitmaps.
  uint64_t blobfs_slices = required_journal_slices + required_data_slices + 3;
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
  fbl::unique_fd fd(open(fs.mount_path().c_str(), O_RDONLY | O_DIRECTORY));
  ASSERT_TRUE(fd);

  fdio_cpp::FdioCaller caller(std::move(fd));
  auto query_result =
      fio::DirectoryAdmin::Call::QueryFilesystem(zx::unowned_channel(caller.borrow_channel()));
  ASSERT_EQ(query_result.status(), ZX_OK);
  ASSERT_EQ(query_result.value().s, ZX_OK);
  ASSERT_NE(query_result.value().info, nullptr);
  const fio::FilesystemInfo& info = *query_result.value().info;

  constexpr std::string_view kFsName = "blobfs";
  const char* name = reinterpret_cast<const char*>(info.name.data());
  ASSERT_EQ(name, kFsName) << "Unexpected filesystem mounted";
  EXPECT_EQ(info.block_size, kBlobfsBlockSize);
  EXPECT_EQ(info.max_filename_size, 64U);
  EXPECT_EQ(info.fs_type, VFS_TYPE_BLOBFS);
  EXPECT_NE(info.fs_id, 0ul);

  // Check that used_bytes are within a reasonable range
  EXPECT_GE(info.used_bytes, expected_bytes);
  EXPECT_LE(info.used_bytes, info.total_bytes);

  // Check that total_bytes are a multiple of slice_size
  const uint64_t slice_size = fs.options().fvm_slice_size;
  EXPECT_GE(info.total_bytes, slice_size);
  EXPECT_EQ(info.total_bytes % slice_size, 0ul);
  EXPECT_EQ(info.total_nodes, slice_size / kBlobfsInodeSize);
  EXPECT_EQ(info.used_nodes, expected_nodes);
}

TEST_F(BlobfsTestWithFvm, QueryInfo) {
  size_t total_bytes = 0;
  ASSERT_NO_FATAL_FAILURE(QueryInfo(fs(), 0, 0));
  for (size_t i = 10; i < 16; i++) {
    std::unique_ptr<BlobInfo> info;
    ASSERT_NO_FATAL_FAILURE(GenerateRandomBlob(fs().mount_path(), 1 << i, &info));

    fbl::unique_fd fd;
    ASSERT_NO_FATAL_FAILURE(MakeBlob(info.get(), &fd));
    total_bytes += fbl::round_up(info->size_merkle + info->size_data, kBlobfsBlockSize);
  }

  ASSERT_NO_FATAL_FAILURE(QueryInfo(fs(), 6, total_bytes));
}

void GetAllocations(fs_test::TestFilesystem& fs, zx::vmo* out_vmo, uint64_t* out_count) {
  fbl::unique_fd fd(open(fs.mount_path().c_str(), O_RDONLY | O_DIRECTORY));
  ASSERT_TRUE(fd);
  zx_status_t status;
  zx_handle_t vmo_handle;
  fdio_cpp::FdioCaller caller(std::move(fd));
  ASSERT_EQ(fuchsia_blobfs_BlobfsGetAllocatedRegions(caller.borrow_channel(), &status, &vmo_handle,
                                                     out_count),
            ZX_OK);
  ASSERT_EQ(status, ZX_OK);
  out_vmo->reset(vmo_handle);
}

void RunGetAllocatedRegionsTest(fs_test::TestFilesystem& fs) {
  zx::vmo vmo;
  uint64_t count;
  size_t total_bytes = 0;
  size_t fidl_bytes = 0;

  // Although we expect this partition to be empty, we check the results of GetAllocations
  // in case blobfs chooses to store any metadata of pre-initialized data with the
  // allocated regions.
  ASSERT_NO_FATAL_FAILURE(GetAllocations(fs, &vmo, &count));

  std::vector<fuchsia_blobfs_BlockRegion> buffer(count);
  ASSERT_EQ(vmo.read(buffer.data(), 0, sizeof(fuchsia_blobfs_BlockRegion) * count), ZX_OK);
  for (size_t i = 0; i < count; i++) {
    total_bytes += buffer[i].length * kBlobfsBlockSize;
  }

  for (size_t i = 10; i < 16; i++) {
    std::unique_ptr<BlobInfo> info;
    ASSERT_NO_FATAL_FAILURE(GenerateRandomBlob(fs.mount_path(), 1 << i, &info));

    fbl::unique_fd fd;
    ASSERT_NO_FATAL_FAILURE(MakeBlob(info.get(), &fd));
    total_bytes += fbl::round_up(info->size_merkle + info->size_data, kBlobfsBlockSize);
  }
  ASSERT_NO_FATAL_FAILURE(GetAllocations(fs, &vmo, &count));

  buffer.resize(count);
  ASSERT_EQ(vmo.read(buffer.data(), 0, sizeof(fuchsia_blobfs_BlockRegion) * count), ZX_OK);
  for (size_t i = 0; i < count; i++) {
    fidl_bytes += buffer[i].length * kBlobfsBlockSize;
  }
  ASSERT_EQ(fidl_bytes, total_bytes);
}

TEST_F(BlobfsTest, GetAllocatedRegions) { RunGetAllocatedRegionsTest(fs()); }

TEST_F(BlobfsTestWithFvm, GetAllocatedRegions) { RunGetAllocatedRegionsTest(fs()); }

void RunUseAfterUnlinkTest(fs_test::TestFilesystem& fs) {
  for (size_t i = 0; i < 16; i++) {
    std::unique_ptr<BlobInfo> info;
    ASSERT_NO_FATAL_FAILURE(GenerateRandomBlob(fs.mount_path(), 1 << i, &info));

    fbl::unique_fd fd;
    ASSERT_NO_FATAL_FAILURE(MakeBlob(info.get(), &fd));

    // We should be able to unlink the blob.
    ASSERT_EQ(0, unlink(info->path));

    // We should still be able to read the blob after unlinking.
    ASSERT_NO_FATAL_FAILURE(VerifyContents(fd.get(), info->data.get(), info->size_data));

    // After closing the file, however, we should not be able to re-open the blob.
    fd.reset();
    ASSERT_LT(open(info->path, O_RDONLY), 0) << "Expected blob to be deleted";
  }
}

TEST_F(BlobfsTest, UseAfterUnlink) { RunUseAfterUnlinkTest(fs()); }

TEST_F(BlobfsTestWithFvm, UseAfterUnlink) { RunUseAfterUnlinkTest(fs()); }

void RunWriteAfterReadtest(fs_test::TestFilesystem& fs) {
  // srand(zxtest::Runner::GetInstance()->random_seed());
  for (size_t i = 0; i < 16; i++) {
    std::unique_ptr<BlobInfo> info;
    ASSERT_NO_FATAL_FAILURE(GenerateRandomBlob(fs.mount_path(), 1 << i, &info));

    fbl::unique_fd fd;
    ASSERT_NO_FATAL_FAILURE(MakeBlob(info.get(), &fd));

    // After blob generation, writes should be rejected.
    ASSERT_LT(write(fd.get(), info->data.get(), 1), 0)
        << "After being written, the blob should refuse writes";

    off_t seek_pos = (rand() % info->size_data);
    ASSERT_EQ(seek_pos, lseek(fd.get(), seek_pos, SEEK_SET));
    ASSERT_LT(write(fd.get(), info->data.get(), 1), 0)
        << "After being written, the blob should refuse writes";
    ASSERT_LT(ftruncate(fd.get(), rand() % info->size_data), 0)
        << "The blob should always refuse to be truncated";

    ASSERT_EQ(0, unlink(info->path));
  }
}

TEST_F(BlobfsTest, WriteAfterRead) { RunWriteAfterReadtest(fs()); }

TEST_F(BlobfsTestWithFvm, WriteAfterRead) { RunWriteAfterReadtest(fs()); }

void RunWriteAfterUnlinkTest(fs_test::TestFilesystem& fs) {
  std::unique_ptr<BlobInfo> info;
  size_t size = 1 << 20;
  ASSERT_NO_FATAL_FAILURE(GenerateRandomBlob(fs.mount_path(), size, &info));

  // Partially write out first blob.
  fbl::unique_fd fd(open(info->path, O_CREAT | O_RDWR));
  ASSERT_TRUE(fd) << "Failed to create blob";
  ASSERT_EQ(0, ftruncate(fd.get(), size));
  ASSERT_EQ(0, StreamAll(write, fd.get(), info->data.get(), size / 2)) << "Failed to write Data";
  ASSERT_EQ(0, unlink(info->path));
  ASSERT_EQ(0, StreamAll(write, fd.get(), info->data.get() + size / 2, size - (size / 2)))
      << "Failed to write Data";
  fd.reset();
  ASSERT_LT(open(info->path, O_RDONLY), 0);
}

TEST_F(BlobfsTest, WriteAfterUnlink) { RunWriteAfterUnlinkTest(fs()); }

TEST_F(BlobfsTestWithFvm, WriteAfterUnlink) { RunWriteAfterUnlinkTest(fs()); }

void RunReadTooLargeTest(fs_test::TestFilesystem& fs) {
  for (size_t i = 0; i < 16; i++) {
    std::unique_ptr<BlobInfo> info;
    ASSERT_NO_FATAL_FAILURE(GenerateRandomBlob(fs.mount_path(), 1 << i, &info));

    fbl::unique_fd fd;
    ASSERT_NO_FATAL_FAILURE(MakeBlob(info.get(), &fd));

    std::unique_ptr<char[]> buffer(new char[info->size_data]);

    // Try read beyond end of blob.
    off_t end_off = info->size_data;
    ASSERT_EQ(end_off, lseek(fd.get(), end_off, SEEK_SET));
    ASSERT_EQ(0, read(fd.get(), &buffer[0], 1)) << "Expected empty read beyond end of file";

    // Try some reads which straddle the end of the blob.
    for (ssize_t j = 1; j < static_cast<ssize_t>(info->size_data); j *= 2) {
      end_off = info->size_data - j;
      ASSERT_EQ(end_off, lseek(fd.get(), end_off, SEEK_SET));
      ASSERT_EQ(j, read(fd.get(), &buffer[0], j * 2))
          << "Expected to only read one byte at end of file";
      ASSERT_EQ(memcmp(buffer.get(), &info->data[info->size_data - j], j), 0)
          << "Read data, but it was bad";
    }

    ASSERT_EQ(0, unlink(info->path));
  }
}

TEST_F(BlobfsTest, ReadTooLarge) { RunReadTooLargeTest(fs()); }

TEST_F(BlobfsTestWithFvm, ReadTooLarge) { RunReadTooLargeTest(fs()); }

void RunBadCreationTest(fs_test::TestFilesystem& fs) {
  std::string name(fs.mount_path());
  name.append("/00112233445566778899AABBCCDDEEFFGGHHIIJJKKLLMMNNOOPPQQRRSSTTUUVV");
  fbl::unique_fd fd(open(name.c_str(), O_CREAT | O_RDWR));
  ASSERT_FALSE(fd) << "Only acceptable pathnames are hex";

  name.assign(fs.mount_path());
  name.append("/00112233445566778899AABBCCDDEEFF");
  fd.reset(open(name.c_str(), O_CREAT | O_RDWR));
  ASSERT_FALSE(fd) << "Only acceptable pathnames are 32 hex-encoded bytes";

  std::unique_ptr<BlobInfo> info;
  ASSERT_NO_FATAL_FAILURE(GenerateRandomBlob(fs.mount_path(), 1 << 15, &info));

  fd.reset(open(info->path, O_CREAT | O_RDWR));
  ASSERT_TRUE(fd) << "Failed to create blob";
  ASSERT_EQ(-1, ftruncate(fd.get(), 0)) << "Blob without data doesn't match null blob";

  // This is the size of the entire disk; we shouldn't fail here as setting blob size
  // has nothing to do with how much space blob will occupy.
  fd.reset();
  fd.reset(open(info->path, O_CREAT | O_RDWR));
  ASSERT_EQ(0,
            ftruncate(fd.get(), fs.options().device_block_count * fs.options().device_block_size))
      << "Huge blob";

  // Write nothing, but close the blob. Since the write was incomplete,
  // it will be inaccessible.
  fd.reset(open(info->path, O_RDWR));
  ASSERT_FALSE(fd) << "Cannot access partial blob";
  fd.reset(open(info->path, O_RDONLY));
  ASSERT_FALSE(fd) << "Cannot access partial blob";

  // And once more -- let's write everything but the last byte of a blob's data.
  fd.reset(open(info->path, O_CREAT | O_RDWR));
  ASSERT_TRUE(fd) << "Failed to create blob";
  ASSERT_EQ(0, ftruncate(fd.get(), info->size_data)) << "Failed to allocate blob";
  ASSERT_EQ(0, StreamAll(write, fd.get(), info->data.get(), info->size_data - 1))
      << "Failed to write data";
  fd.reset(open(info->path, O_RDWR));
  ASSERT_FALSE(fd) << "Cannot access partial blob";
}

TEST_F(BlobfsTest, BadCreation) { RunBadCreationTest(fs()); }

TEST_F(BlobfsTestWithFvm, BadCreation) { RunBadCreationTest(fs()); }

// Attempts to read the contents of the Blob.
void VerifyCompromised(int fd, const char* data, size_t size_data) {
  std::unique_ptr<char[]> buf(new char[size_data]);

  ASSERT_EQ(0, lseek(fd, 0, SEEK_SET));
  ASSERT_EQ(-1, StreamAll(read, fd, &buf[0], size_data)) << "Expected reading to fail";
}

// Creates a blob with the provided Merkle tree + Data, and
// reads to verify the data.
void MakeBlobCompromised(BlobInfo* info) {
  fbl::unique_fd fd(open(info->path, O_CREAT | O_RDWR));
  ASSERT_TRUE(fd) << "Failed to create blob";
  ASSERT_EQ(0, ftruncate(fd.get(), info->size_data));

  // If we're writing a blob with invalid sizes, it's possible that writing will fail.
  StreamAll(write, fd.get(), info->data.get(), info->size_data);

  ASSERT_NO_FATAL_FAILURE(VerifyCompromised(fd.get(), info->data.get(), info->size_data));
}

void RunCorruptBlobTest(fs_test::TestFilesystem& fs) {
  // srand(zxtest::Runner::GetInstance()->random_seed());
  std::unique_ptr<BlobInfo> info;
  for (size_t i = 1; i < 18; i++) {
    ASSERT_NO_FATAL_FAILURE(GenerateRandomBlob(fs.mount_path(), 1 << i, &info));
    info->size_data -= (rand() % info->size_data) + 1;
    if (info->size_data == 0) {
      info->size_data = 1;
    }
    ASSERT_NO_FATAL_FAILURE(MakeBlobCompromised(info.get()));
  }

  for (size_t i = 0; i < 18; i++) {
    ASSERT_NO_FATAL_FAILURE(GenerateRandomBlob(fs.mount_path(), 1 << i, &info));
    // Flip a random bit of the data.
    size_t rand_index = rand() % info->size_data;
    char old_val = info->data.get()[rand_index];
    while ((info->data.get()[rand_index] = static_cast<char>(rand())) == old_val) {
    }
    ASSERT_NO_FATAL_FAILURE(MakeBlobCompromised(info.get()));
  }
}

TEST_F(BlobfsTest, CorruptBlob) { RunCorruptBlobTest(fs()); }

TEST_F(BlobfsTestWithFvm, CorruptBlob) { RunCorruptBlobTest(fs()); }

void RunCorruptDigestTest(fs_test::TestFilesystem& fs) {
  // srand(zxtest::Runner::GetInstance()->random_seed());
  std::unique_ptr<BlobInfo> info;
  for (size_t i = 1; i < 18; i++) {
    ASSERT_NO_FATAL_FAILURE(GenerateRandomBlob(fs.mount_path(), 1 << i, &info));

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
    ASSERT_NO_FATAL_FAILURE(GenerateRandomBlob(fs.mount_path(), 1 << i, &info));
    // Flip a random bit of the data.
    size_t rand_index = rand() % info->size_data;
    char old_val = info->data.get()[rand_index];
    while ((info->data.get()[rand_index] = static_cast<char>(rand())) == old_val) {
    }
    ASSERT_NO_FATAL_FAILURE(MakeBlobCompromised(info.get()));
  }
}

TEST_F(BlobfsTest, CorruptDigest) { RunCorruptDigestTest(fs()); }

TEST_F(BlobfsTestWithFvm, CorruptDigest) { RunCorruptDigestTest(fs()); }

void RunEdgeAllocationTest(fs_test::TestFilesystem& fs) {
  // Powers of two...
  for (size_t i = 1; i < 16; i++) {
    // -1, 0, +1 offsets...
    for (size_t j = -1; j < 2; j++) {
      std::unique_ptr<BlobInfo> info;
      ASSERT_NO_FATAL_FAILURE(GenerateRandomBlob(fs.mount_path(), (1 << i) + j, &info));
      fbl::unique_fd fd;
      ASSERT_NO_FATAL_FAILURE(MakeBlob(info.get(), &fd));
      ASSERT_EQ(0, unlink(info->path));
    }
  }
}

TEST_F(BlobfsTest, EdgeAllocation) { RunEdgeAllocationTest(fs()); }

TEST_F(BlobfsTestWithFvm, EdgeAllocation) { RunEdgeAllocationTest(fs()); }

void RunUmountWithOpenFileTest(fs_test::TestFilesystem& fs) {
  std::unique_ptr<BlobInfo> info;
  ASSERT_NO_FATAL_FAILURE(GenerateRandomBlob(fs.mount_path(), 1 << 16, &info));
  fbl::unique_fd fd;
  ASSERT_NO_FATAL_FAILURE(MakeBlob(info.get(), &fd));

  // Intentionally don't close the file descriptor: Unmount anyway.
  EXPECT_EQ(fs.Unmount().status_value(), ZX_OK);
  EXPECT_EQ(fs.Mount().status_value(), ZX_OK);
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

TEST_F(BlobfsTest, UmountWithOpenFile) { RunUmountWithOpenFileTest(fs()); }

TEST_F(BlobfsTestWithFvm, UmountWithOpenFile) { RunUmountWithOpenFileTest(fs()); }

void RunUmountWithMappedFileTest(fs_test::TestFilesystem& fs) {
  std::unique_ptr<BlobInfo> info;
  ASSERT_NO_FATAL_FAILURE(GenerateRandomBlob(fs.mount_path(), 1 << 16, &info));
  fbl::unique_fd fd;
  ASSERT_NO_FATAL_FAILURE(MakeBlob(info.get(), &fd));

  void* addr = mmap(nullptr, info->size_data, PROT_READ, MAP_SHARED, fd.get(), 0);
  ASSERT_NE(addr, nullptr);
  fd.reset();

  // Intentionally don't unmap the file descriptor: Unmount anyway.
  EXPECT_EQ(fs.Unmount().status_value(), ZX_OK);
  EXPECT_EQ(fs.Mount().status_value(), ZX_OK);
  ASSERT_EQ(munmap(addr, info->size_data), 0);

  fd.reset(open(info->path, O_RDONLY));
  ASSERT_TRUE(fd.get()) << "Failed to open blob";
  ASSERT_NO_FATAL_FAILURE(VerifyContents(fd.get(), info->data.get(), info->size_data));
  ASSERT_EQ(0, unlink(info->path));
}

TEST_F(BlobfsTest, UmountWithMappedFile) { RunUmountWithMappedFileTest(fs()); }

TEST_F(BlobfsTestWithFvm, UmountWithMappedFile) { RunUmountWithMappedFileTest(fs()); }

void RunUmountWithOpenMappedFileTest(fs_test::TestFilesystem& fs) {
  std::unique_ptr<BlobInfo> info;
  ASSERT_NO_FATAL_FAILURE(GenerateRandomBlob(fs.mount_path(), 1 << 16, &info));
  fbl::unique_fd fd;
  ASSERT_NO_FATAL_FAILURE(MakeBlob(info.get(), &fd));

  void* addr = mmap(nullptr, info->size_data, PROT_READ, MAP_SHARED, fd.get(), 0);
  ASSERT_NE(addr, nullptr);

  // Intentionally don't close the file descriptor: Unmount anyway.
  EXPECT_EQ(fs.Unmount().status_value(), ZX_OK);
  EXPECT_EQ(fs.Mount().status_value(), ZX_OK);
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

TEST_F(BlobfsTest, UmountWithOpenMappedFile) { RunUmountWithOpenMappedFileTest(fs()); }

TEST_F(BlobfsTestWithFvm, UmountWithOpenMappedFile) { RunUmountWithOpenMappedFileTest(fs()); }

void RunCreateUmountRemountSmallTest(fs_test::TestFilesystem& fs) {
  for (size_t i = 10; i < 16; i++) {
    std::unique_ptr<BlobInfo> info;
    ASSERT_NO_FATAL_FAILURE(GenerateRandomBlob(fs.mount_path(), 1 << i, &info));

    fbl::unique_fd fd;
    ASSERT_NO_FATAL_FAILURE(MakeBlob(info.get(), &fd));

    fd.reset();
    EXPECT_EQ(fs.Unmount().status_value(), ZX_OK);
    EXPECT_EQ(fs.Mount().status_value(), ZX_OK);

    fd.reset(open(info->path, O_RDONLY));
    ASSERT_TRUE(fd) << "Failed to open blob";

    ASSERT_NO_FATAL_FAILURE(VerifyContents(fd.get(), info->data.get(), info->size_data));
    ASSERT_EQ(0, unlink(info->path));
  }
}

TEST_F(BlobfsTest, CreateUmountRemountSmall) { RunCreateUmountRemountSmallTest(fs()); }

TEST_F(BlobfsTestWithFvm, CreateUmountRemountSmall) { RunCreateUmountRemountSmallTest(fs()); }

bool IsReadable(int fd) {
  char buf[1];
  return pread(fd, buf, sizeof(buf), 0) == sizeof(buf);
}

// Tests that we cannot read from the Blob until it has been fully written.
void RunEarlyReadTest(fs_test::TestFilesystem& fs) {
  std::unique_ptr<BlobInfo> info;

  ASSERT_NO_FATAL_FAILURE(GenerateRandomBlob(fs.mount_path(), 1 << 17, &info));
  fbl::unique_fd fd(open(info->path, O_CREAT | O_EXCL | O_RDWR));
  ASSERT_TRUE(fd);

  // A second fd should also not be readable.
  fbl::unique_fd fd2(open(info->path, O_CREAT | O_RDWR));
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

TEST_F(BlobfsTest, EarlyRead) { RunEarlyReadTest(fs()); }

TEST_F(BlobfsTestWithFvm, EarlyRead) { RunEarlyReadTest(fs()); }

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
void RunWaitForReadTest(fs_test::TestFilesystem& fs) {
  std::unique_ptr<BlobInfo> info;

  ASSERT_NO_FATAL_FAILURE(GenerateRandomBlob(fs.mount_path(), 1 << 17, &info));
  fbl::unique_fd fd(open(info->path, O_CREAT | O_EXCL | O_RDWR));
  ASSERT_TRUE(fd);

  {
    // Launch a background thread to wait for the file to become readable.
    std::atomic<bool> result;
    std::thread waiter_thread(CheckReadable, std::move(fd), &result);

    MakeBlob(info.get(), &fd);

    waiter_thread.join();
    ASSERT_TRUE(result.load()) << "Background operation failed";
  }

  // Before continuing, make sure that MakeBlob was successful.
  ASSERT_NO_FATAL_FAILURE();

  // Double check that attempting to read early didn't cause problems...
  ASSERT_NO_FATAL_FAILURE(VerifyContents(fd.get(), info->data.get(), info->size_data));
}

TEST_F(BlobfsTest, WaitForRead) { RunWaitForReadTest(fs()); }

TEST_F(BlobfsTestWithFvm, WaitForRead) { RunWaitForReadTest(fs()); }

// Tests that seeks during writing are ignored.
void RunWriteSeekIgnoredTest(fs_test::TestFilesystem& fs) {
  // srand(zxtest::Runner::GetInstance()->random_seed());
  std::unique_ptr<BlobInfo> info;
  ASSERT_NO_FATAL_FAILURE(GenerateRandomBlob(fs.mount_path(), 1 << 17, &info));
  fbl::unique_fd fd(open(info->path, O_CREAT | O_RDWR));
  ASSERT_TRUE(fd) << "Failed to create blob";
  ASSERT_EQ(0, ftruncate(fd.get(), info->size_data));

  off_t seek_pos = (rand() % info->size_data);
  ASSERT_EQ(seek_pos, lseek(fd.get(), seek_pos, SEEK_SET));
  ASSERT_EQ(write(fd.get(), info->data.get(), info->size_data),
            static_cast<ssize_t>(info->size_data));

  // Double check that attempting to seek early didn't cause problems...
  ASSERT_NO_FATAL_FAILURE(VerifyContents(fd.get(), info->data.get(), info->size_data));
}

TEST_F(BlobfsTest, WriteSeekIgnored) { RunWriteSeekIgnoredTest(fs()); }

TEST_F(BlobfsTestWithFvm, WriteSeekIgnored) { RunWriteSeekIgnoredTest(fs()); }

void UnlinkAndRecreate(const char* path, fbl::unique_fd* fd) {
  ASSERT_EQ(0, unlink(path));
  fd->reset();  // Make sure the file is gone.
  fd->reset(open(path, O_CREAT | O_RDWR | O_EXCL));
  ASSERT_TRUE(*fd) << "Failed to recreate blob";
}

// Try unlinking while creating a blob.
void RunRestartCreationTest(fs_test::TestFilesystem& fs) {
  std::unique_ptr<BlobInfo> info;
  ASSERT_NO_FATAL_FAILURE(GenerateRandomBlob(fs.mount_path(), 1 << 17, &info));

  fbl::unique_fd fd(open(info->path, O_CREAT | O_RDWR));
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

TEST_F(BlobfsTest, RestartCreation) { RunRestartCreationTest(fs()); }

TEST_F(BlobfsTestWithFvm, RestartCreation) { RunRestartCreationTest(fs()); }

// Attempt using invalid operations.
void RunInvalidOperationsTest(fs_test::TestFilesystem& fs) {
  // First off, make a valid blob.
  std::unique_ptr<BlobInfo> info;
  ASSERT_NO_FATAL_FAILURE(GenerateRandomBlob(fs.mount_path(), 1 << 12, &info));
  fbl::unique_fd fd;
  ASSERT_NO_FATAL_FAILURE(MakeBlob(info.get(), &fd));
  ASSERT_NO_FATAL_FAILURE(VerifyContents(fd.get(), info->data.get(), info->size_data));

  // Try some unsupported operations.
  ASSERT_LT(rename(info->path, info->path), 0);
  ASSERT_LT(truncate(info->path, 0), 0);
  ASSERT_LT(utime(info->path, nullptr), 0);

  // Test that a file cannot unmount the entire blobfs.
  // Instead, the file channel will be forcibly closed as we attempt to call an unknown FIDL method.
  // Hence we clone the fd into a |canary_channel| which we know will have its peer closed.
  zx::channel canary_channel;
  ASSERT_EQ(fdio_fd_clone(fd.get(), canary_channel.reset_and_get_address()), ZX_OK);
  ASSERT_EQ(ZX_ERR_PEER_CLOSED,
            fio::DirectoryAdmin::Call::Unmount(zx::unowned_channel(canary_channel)).status());
  zx_signals_t pending;
  EXPECT_EQ(canary_channel.wait_one(ZX_CHANNEL_PEER_CLOSED, zx::time::infinite_past(), &pending),
            ZX_OK);

  // Access the file once more, after these operations.
  ASSERT_NO_FATAL_FAILURE(VerifyContents(fd.get(), info->data.get(), info->size_data));
}

TEST_F(BlobfsTest, InvalidOperations) { RunInvalidOperationsTest(fs()); }

TEST_F(BlobfsTestWithFvm, InvalidOperations) { RunInvalidOperationsTest(fs()); }

// Attempt operations on the root directory.
void RunRootDirectoryTest(fs_test::TestFilesystem& fs) {
  std::string name(fs.mount_path());
  name.append("/.");
  fbl::unique_fd dirfd(open(name.c_str(), O_RDONLY));
  ASSERT_TRUE(dirfd) << "Cannot open root directory";

  std::unique_ptr<BlobInfo> info;
  ASSERT_NO_FATAL_FAILURE(GenerateRandomBlob(fs.mount_path(), 1 << 12, &info));

  // Test operations which should ONLY operate on Blobs.
  ASSERT_LT(ftruncate(dirfd.get(), info->size_data), 0);

  char buf[8];
  ASSERT_LT(write(dirfd.get(), buf, 8), 0) << "Should not write to directory";
  ASSERT_LT(read(dirfd.get(), buf, 8), 0) << "Should not read from directory";

  // Should NOT be able to unlink root dir.
  ASSERT_LT(unlink(info->path), 0);
}

TEST_F(BlobfsTest, RootDirectory) { RunRootDirectoryTest(fs()); }

TEST_F(BlobfsTestWithFvm, RootDirectory) { RunRootDirectoryTest(fs()); }

void RunPartialWriteTest(fs_test::TestFilesystem& fs) {
  std::unique_ptr<BlobInfo> info_complete;
  std::unique_ptr<BlobInfo> info_partial;
  size_t size = 1 << 20;
  ASSERT_NO_FATAL_FAILURE(GenerateRandomBlob(fs.mount_path(), size, &info_complete));
  ASSERT_NO_FATAL_FAILURE(GenerateRandomBlob(fs.mount_path(), size, &info_partial));

  // Partially write out first blob.
  fbl::unique_fd fd_partial(open(info_partial->path, O_CREAT | O_RDWR));
  ASSERT_TRUE(fd_partial) << "Failed to create blob";
  ASSERT_EQ(0, ftruncate(fd_partial.get(), size));
  ASSERT_EQ(0, StreamAll(write, fd_partial.get(), info_partial->data.get(), size / 2))
      << "Failed to write Data";

  // Completely write out second blob.
  fbl::unique_fd fd_complete;
  ASSERT_NO_FATAL_FAILURE(MakeBlob(info_complete.get(), &fd_complete));
}

TEST_F(BlobfsTest, PartialWrite) { RunPartialWriteTest(fs()); }

TEST_F(BlobfsTestWithFvm, PartialWrite) { RunPartialWriteTest(fs()); }

void RunPartialWriteSleepyDiskTest(fs_test::TestFilesystem& fs) {
  std::unique_ptr<BlobInfo> info_complete;
  std::unique_ptr<BlobInfo> info_partial;
  size_t size = 1 << 20;
  ASSERT_NO_FATAL_FAILURE(GenerateRandomBlob(fs.mount_path(), size, &info_complete));
  ASSERT_NO_FATAL_FAILURE(GenerateRandomBlob(fs.mount_path(), size, &info_partial));

  // Partially write out first blob.
  fbl::unique_fd fd_partial(open(info_partial->path, O_CREAT | O_RDWR));
  ASSERT_TRUE(fd_partial) << "Failed to create blob";
  ASSERT_EQ(0, ftruncate(fd_partial.get(), size));
  ASSERT_EQ(0, StreamAll(write, fd_partial.get(), info_partial->data.get(), size / 2))
      << "Failed to write Data";

  // Completely write out second blob.
  fbl::unique_fd fd_complete;
  ASSERT_NO_FATAL_FAILURE(MakeBlob(info_complete.get(), &fd_complete));

  ASSERT_EQ(0, syncfs(fd_complete.get()));
  ASSERT_EQ(fs.GetRamDisk()->SleepAfter(0).status_value(), ZX_OK);

  fd_complete.reset(open(info_complete->path, O_RDONLY));
  ASSERT_TRUE(fd_complete) << "Failed to re-open blob";

  ASSERT_EQ(0, syncfs(fd_complete.get()));
  ASSERT_EQ(fs.GetRamDisk()->Wake().status_value(), ZX_OK);

  ASSERT_NO_FATAL_FAILURE(VerifyContents(fd_complete.get(), info_complete->data.get(), size));

  fd_partial.reset();
  fd_partial.reset(open(info_partial->path, O_RDONLY));
  ASSERT_FALSE(fd_partial) << "Should not be able to open invalid blob";
}

TEST_F(BlobfsTest, PartialWriteSleepyDisk) { RunPartialWriteSleepyDiskTest(fs()); }

TEST_F(BlobfsTestWithFvm, PartialWriteSleepyDisk) { RunPartialWriteSleepyDiskTest(fs()); }

void RunMultipleWritesTest(fs_test::TestFilesystem& fs) {
  std::unique_ptr<BlobInfo> info;
  ASSERT_NO_FATAL_FAILURE(GenerateRandomBlob(fs.mount_path(), 1 << 16, &info));

  fbl::unique_fd fd(open(info->path, O_CREAT | O_RDWR));
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

TEST_F(BlobfsTest, MultipleWrites) { RunMultipleWritesTest(fs()); }

TEST_F(BlobfsTestWithFvm, MultipleWrites) { RunMultipleWritesTest(fs()); }

zx_status_t DirectoryAdminGetDevicePath(fbl::unique_fd directory, std::string* path) {
  fdio_cpp::FdioCaller caller(std::move(directory));
  auto result =
      fio::DirectoryAdmin::Call::GetDevicePath(zx::unowned_channel(caller.borrow_channel()));
  if (result.status() != ZX_OK) {
    return result.status();
  }
  if (result->s != ZX_OK) {
    return result->s;
  }
  path->assign(std::string(result->path.begin(), result->path.size()));
  return ZX_OK;
}

void RunQueryDevicePathTest(fs_test::TestFilesystem& fs) {
  fbl::unique_fd root_fd(open(fs.mount_path().c_str(), O_RDONLY | O_ADMIN));
  ASSERT_TRUE(root_fd) << "Cannot open root directory";

  std::string path;
  ASSERT_EQ(DirectoryAdminGetDevicePath(std::move(root_fd), &path), ZX_OK);
  ASSERT_FALSE(path.empty());

  // TODO(fxbug.dev/63405): The NULL terminator probably shouldn't be here.
  ASSERT_EQ(fs::GetTopologicalPath(fs.DevicePath().value()) + std::string(1, '\0'), path);
  printf("device_path %s\n", fs.DevicePath()->c_str());

  root_fd.reset(open(fs.mount_path().c_str(), O_RDONLY));
  ASSERT_TRUE(root_fd) << "Cannot open root directory";

  ASSERT_EQ(DirectoryAdminGetDevicePath(std::move(root_fd), &path), ZX_ERR_ACCESS_DENIED);
}

TEST_F(BlobfsTest, QueryDevicePath) { RunQueryDevicePathTest(fs()); }

TEST_F(BlobfsTestWithFvm, QueryDevicePath) {
  // Make sure the two paths to compare are in the same form.
  RunQueryDevicePathTest(fs());
}

void RunReadOnlyTest(fs_test::TestFilesystem& fs) {
  // Mount the filesystem as read-write. We can create new blobs.
  std::unique_ptr<BlobInfo> info;
  ASSERT_NO_FATAL_FAILURE(GenerateRandomBlob(fs.mount_path(), 1 << 10, &info));
  fbl::unique_fd blob_fd;
  ASSERT_NO_FATAL_FAILURE(MakeBlob(info.get(), &blob_fd));
  ASSERT_NO_FATAL_FAILURE(VerifyContents(blob_fd.get(), info->data.get(), info->size_data));
  blob_fd.reset();

  EXPECT_EQ(fs.Unmount().status_value(), ZX_OK);
  mount_options_t options = default_mount_options;
  options.readonly = true;
  EXPECT_EQ(fs.MountWithOptions(options).status_value(), ZX_OK);

  // We can read old blobs
  blob_fd.reset(open(info->path, O_RDONLY));
  ASSERT_TRUE(blob_fd);
  ASSERT_NO_FATAL_FAILURE(VerifyContents(blob_fd.get(), info->data.get(), info->size_data));

  // We cannot create new blobs
  ASSERT_NO_FATAL_FAILURE(GenerateRandomBlob(fs.mount_path(), 1 << 10, &info));
  blob_fd.reset(open(info->path, O_CREAT | O_RDWR));
  ASSERT_FALSE(blob_fd);
}

TEST_F(BlobfsTest, ReadOnly) { RunReadOnlyTest(fs()); }

TEST_F(BlobfsTestWithFvm, ReadOnly) { RunReadOnlyTest(fs()); }

void OpenBlockDevice(const std::string& path,
                     std::unique_ptr<block_client::RemoteBlockDevice>* block_device) {
  fbl::unique_fd fd(open(path.c_str(), O_RDWR));
  ASSERT_TRUE(fd) << "Unable to open block device";

  zx::channel channel, server;
  ASSERT_EQ(zx::channel::create(0, &channel, &server), ZX_OK);
  fdio_cpp::FdioCaller caller(std::move(fd));
  ASSERT_EQ(fio::Node::Call::Clone(zx::unowned_channel(caller.borrow_channel()),
                                   fio::CLONE_FLAG_SAME_RIGHTS, std::move(server))
                .status(),
            ZX_OK);
  ASSERT_EQ(block_client::RemoteBlockDevice::Create(std::move(channel), block_device), ZX_OK);
}

using SliceRange = fuchsia_hardware_block_volume_VsliceRange;

uint64_t BlobfsBlockToFvmSlice(fs_test::TestFilesystem& fs, uint64_t block) {
  const size_t blocks_per_slice = fs.options().fvm_slice_size / kBlobfsBlockSize;
  return block / blocks_per_slice;
}

void GetSliceRange(const BlobfsTestWithFvm& test, const std::vector<uint64_t>& slices,
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

// The helper creates a blob with data of size disk_size. The data is
// compressible so needs less space on disk. This will test if we can persist
// a blob whose uncompressed data is larger than available free space.
// The test is expected to fail when compression is turned off.
void RunUncompressedBlobDataLargerThanAvailableSpaceTest(fs_test::TestFilesystem& fs) {
  std::unique_ptr<BlobInfo> info;
  ASSERT_NO_FATAL_FAILURE(
      GenerateBlob([](char* data, size_t length) { memset(data, '\0', length); }, fs.mount_path(),
                   fs.options().device_block_count * fs.options().device_block_size + 1, &info));

  fbl::unique_fd fd;
  ASSERT_NO_FATAL_FAILURE(MakeBlob(info.get(), &fd))
      << "Test is expected to fail when compression is turned off";

  // We can re-open and verify the Blob as read-only.
  fd.reset(open(info->path, O_RDONLY));
  ASSERT_TRUE(fd) << "Failed to-reopen blob";
  ASSERT_NO_FATAL_FAILURE(VerifyContents(fd.get(), info->data.get(), info->size_data));

  // Force decompression by remounting, re-accessing blob.
  EXPECT_EQ(fs.Unmount().status_value(), ZX_OK);
  EXPECT_EQ(fs.Mount().status_value(), ZX_OK);
  fd.reset(open(info->path, O_RDONLY));
  ASSERT_TRUE(fd) << "Failed to-reopen blob";
  ASSERT_NO_FATAL_FAILURE(VerifyContents(fd.get(), info->data.get(), info->size_data));

  ASSERT_EQ(0, unlink(info->path));
}

TEST_F(BlobfsTest, BlobLargerThanAvailableSpaceTest) {
  RunUncompressedBlobDataLargerThanAvailableSpaceTest(fs());
}

TEST_F(BlobfsTestWithFvm, BlobLargerThanAvailableSpaceTest) {
  RunUncompressedBlobDataLargerThanAvailableSpaceTest(fs());
}

// This tests growing both additional inodes and data blocks.
TEST_F(BlobfsTestWithFvm, ResizePartition) {
  EXPECT_EQ(fs().Unmount().status_value(), ZX_OK);
  std::vector<SliceRange> slices;
  std::vector<uint64_t> query = {BlobfsBlockToFvmSlice(fs(), kFVMNodeMapStart),
                                 BlobfsBlockToFvmSlice(fs(), kFVMDataStart)};
  ASSERT_NO_FATAL_FAILURE(GetSliceRange(*this, query, &slices));
  ASSERT_EQ(slices.size(), 2ul);
  EXPECT_TRUE(slices[0].allocated);
  EXPECT_EQ(slices[0].count, 1ul);
  EXPECT_TRUE(slices[1].allocated);
  EXPECT_EQ(slices[1].count, 1ul);
  EXPECT_EQ(fs().Mount().status_value(), ZX_OK);

  size_t required = fs().options().fvm_slice_size / kBlobfsInodeSize + 2;
  for (size_t i = 0; i < required; i++) {
    std::unique_ptr<BlobInfo> info;
    ASSERT_NO_FATAL_FAILURE(GenerateRandomBlob(fs().mount_path(), kBlobfsInodeSize, &info));

    fbl::unique_fd fd;
    ASSERT_NO_FATAL_FAILURE(MakeBlob(info.get(), &fd));
  }

  // Remount partition.
  EXPECT_EQ(fs().Unmount().status_value(), ZX_OK);
  EXPECT_EQ(fs().Mount().status_value(), ZX_OK);

  EXPECT_EQ(fs().Unmount().status_value(), ZX_OK);
  ASSERT_NO_FATAL_FAILURE(GetSliceRange(*this, query, &slices));
  ASSERT_EQ(slices.size(), 2ul);
  EXPECT_TRUE(slices[0].allocated);
  EXPECT_GT(slices[0].count, 1ul);
  EXPECT_TRUE(slices[1].allocated);
  EXPECT_GT(slices[1].count, 1ul);
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

TEST_F(BlobfsTestWithFvm, CorruptAtMount) {
  EXPECT_EQ(fs().Unmount().status_value(), ZX_OK);

  // Shrink slice so FVM will differ from Blobfs.
  uint64_t offset = BlobfsBlockToFvmSlice(fs(), kFVMNodeMapStart);
  ASSERT_NO_FATAL_FAILURE(FvmShrink(fs().DevicePath().value(), offset, 1));

  fbl::unique_fd fd(open(fs().DevicePath().value().c_str(), O_RDWR));
  ASSERT_TRUE(fd);

  mount_options_t options = default_mount_options;
  ASSERT_NE(mount(fd.release(), fs().mount_path().c_str(), DISK_FORMAT_BLOBFS, &options,
                  launch_stdio_async),
            ZX_OK);

  // Grow slice count to twice what it should be.
  ASSERT_NO_FATAL_FAILURE(FvmExtend(fs().DevicePath().value(), offset, 2));

  EXPECT_EQ(fs().Mount().status_value(), ZX_OK);
  EXPECT_EQ(fs().Unmount().status_value(), ZX_OK);

  // Verify that mount automatically removed the extra slice.
  std::vector<SliceRange> slices;
  std::vector<uint64_t> query = {BlobfsBlockToFvmSlice(fs(), kFVMNodeMapStart)};
  ASSERT_NO_FATAL_FAILURE(GetSliceRange(*this, query, &slices));
  ASSERT_EQ(slices.size(), 1ul);
  EXPECT_TRUE(slices[0].allocated);
  EXPECT_EQ(slices[0].count, 1ul);
}

void RunFailedWriteTest(fs_test::TestFilesystem& fs) {
  uint32_t page_size = 8192;  // disk->page_size();
  const uint32_t pages_per_block = kBlobfsBlockSize / page_size;

  std::unique_ptr<BlobInfo> info;
  ASSERT_NO_FATAL_FAILURE(GenerateRandomBlob(fs.mount_path(), kBlobfsBlockSize, &info));

  fbl::unique_fd fd(open(info->path, O_CREAT | O_RDWR));
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
  constexpr int kBlockCountToWrite = 5;

  // Sleep after |kBlockCountToWrite - 1| blocks. This is 1 less than will be
  // needed to write out the entire blob. This ensures that writing the blob
  // will ultimately fail, but the write operation will return a successful
  // response.
  ASSERT_EQ(fs.GetRamDisk()->SleepAfter(pages_per_block * (kBlockCountToWrite - 1)).status_value(),
            ZX_OK);
  ASSERT_EQ(write(fd.get(), info->data.get(), info->size_data),
            static_cast<ssize_t>(info->size_data));

  // Since the write operation ultimately failed when going out to disk,
  // syncfs will return a failed response.
  ASSERT_LT(syncfs(fd.get()), 0);

  ASSERT_NO_FATAL_FAILURE(GenerateRandomBlob(fs.mount_path(), kBlobfsBlockSize, &info));
  fd.reset(open(info->path, O_CREAT | O_RDWR));
  ASSERT_TRUE(fd) << "Failed to create blob";

  // On an FVM, truncate may either succeed or fail. If an FVM extend call is necessary,
  // it will fail since the ramdisk is asleep; otherwise, it will pass.
  ftruncate(fd.get(), info->size_data);

  // Since the ramdisk is asleep and our blobfs is aware of it due to the sync, write should fail.
  // TODO(smklein): Implement support for "failed write propagates to the client before
  // sync".
  // ASSERT_LT(write(fd.get(), info->data.get(), kBlobfsBlockSize), 0);

  ASSERT_EQ(fs.GetRamDisk()->Wake().status_value(), ZX_OK);
}

TEST_F(BlobfsTest, FailedWrite) { ASSERT_NO_FATAL_FAILURE(RunFailedWriteTest(fs())); }

TEST_F(BlobfsTestWithFvm, FailedWrite) { ASSERT_NO_FATAL_FAILURE(RunFailedWriteTest(fs())); }

struct CloneThreadArgs {
  const BlobInfo* info = nullptr;
  std::atomic_bool done{false};
};

void CloneThread(CloneThreadArgs* args) {
  while (!args->done) {
    fbl::unique_fd fd(open(args->info->path, O_RDONLY));
    ASSERT_TRUE(fd);
    void* addr = mmap(NULL, args->info->size_data, PROT_READ, MAP_PRIVATE, fd.get(), 0);
    ASSERT_NE(addr, MAP_FAILED) << "Could not mmap blob";
    // Explicitly close |fd| before unmapping.
    fd.reset();
    // Yielding before unmapping significantly improves the ability of this test to detect bugs
    // (e.g. fxbug.dev/53882) by increasing the length of time that the file is closed but still has
    // a VMO clone.
    zx_nanosleep(0);
    ASSERT_EQ(0, munmap(addr, args->info->size_data));
  }
}

// This test ensures that blobfs' lifecycle management correctly deals with a highly volatile
// number of VMO clones (which blobfs has special logic to handle, preventing the in-memory
// blob from being discarded while there are active clones).
// See fxbug.dev/53882 for background on this test case.
void RunVmoCloneWatchingTest(fs_test::TestFilesystem& fs) {
  std::unique_ptr<BlobInfo> info;
  ASSERT_NO_FATAL_FAILURE(GenerateBlob(CharFill<'A'>, fs.mount_path(), 4096, &info));

  {
    fbl::unique_fd fd;
    ASSERT_NO_FATAL_FAILURE(MakeBlob(info.get(), &fd));
  }

  struct CloneThreadArgs thread_args {
    .info = info.get(),
  };
  std::thread clone_thread(CloneThread, &thread_args);

  constexpr int kIterations = 1000;
  for (int i = 0; i < kIterations; ++i) {
    fbl::unique_fd fd(open(info->path, O_RDONLY));
    ASSERT_TRUE(fd);
    void* addr = mmap(NULL, info->size_data, PROT_READ, MAP_PRIVATE, fd.get(), 0);
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

TEST_F(BlobfsTest, VmoCloneWatchingTest) { ASSERT_NO_FATAL_FAILURE(RunVmoCloneWatchingTest(fs())); }

TEST_F(BlobfsTestWithFvm, VmoCloneWatchingTest) {
  ASSERT_NO_FATAL_FAILURE(RunVmoCloneWatchingTest(fs()));
}

fit::result<inspect::Hierarchy> TakeSnapshot(fuchsia::inspect::TreePtr tree,
                                             async::Executor* executor) {
  std::condition_variable cv;
  std::mutex m;
  bool done = false;
  fit::result<inspect::Hierarchy> hierarchy_or_error;

  auto promise =
      inspect::ReadFromTree(std::move(tree)).then([&](fit::result<inspect::Hierarchy>& result) {
        {
          std::unique_lock<std::mutex> lock(m);
          hierarchy_or_error = std::move(result);
          done = true;
        }
        cv.notify_all();
      });

  executor->schedule_task(std::move(promise));

  std::unique_lock<std::mutex> lock(m);
  cv.wait(lock, [&done]() { return done; });

  return hierarchy_or_error;
}

void GetBlobsCreated(async::Executor* executor, zx_handle_t diagnostics_dir,
                     uint64_t* blobs_created) {
  ASSERT_NE(executor, nullptr);
  ASSERT_NE(blobs_created, nullptr);

  fuchsia::inspect::TreePtr tree;
  async_dispatcher_t* dispatcher = executor->dispatcher();
  ASSERT_EQ(fdio_service_connect_at(diagnostics_dir, "fuchsia.inspect.Tree",
                                    tree.NewRequest(dispatcher).TakeChannel().release()),
            ZX_OK);

  fit::result<inspect::Hierarchy> hierarchy_or_error = TakeSnapshot(std::move(tree), executor);
  ASSERT_TRUE(hierarchy_or_error.is_ok());
  inspect::Hierarchy hierarchy = std::move(hierarchy_or_error.value());

  const inspect::Hierarchy* allocation_stats = hierarchy.GetByPath({"allocation_stats"});
  ASSERT_NE(allocation_stats, nullptr);

  const inspect::UintPropertyValue* blobs_created_value =
      allocation_stats->node().get_property<inspect::UintPropertyValue>("blobs_created");
  ASSERT_NE(blobs_created_value, nullptr);

  *blobs_created = blobs_created_value->value();
}

TEST_F(FdioTest, AllocateIncrementsMetricTest) {
  async::Loop loop = async::Loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  loop.StartThread("allocate-increments-metric-thread");
  async::Executor executor(loop.dispatcher());

  uint64_t blobs_created;
  ASSERT_NO_FATAL_FAILURE(GetBlobsCreated(&executor, diagnostics_dir(), &blobs_created));
  ASSERT_EQ(blobs_created, 0ul);

  // Create a new blob with random contents on the mounted filesystem.
  std::unique_ptr<BlobInfo> info;
  ASSERT_NO_FATAL_FAILURE(GenerateRandomBlob(".", 1 << 8, &info));
  fbl::unique_fd fd(openat(root_fd(), info->path, O_CREAT | O_RDWR));
  ASSERT_TRUE(fd.is_valid());
  ASSERT_EQ(ftruncate(fd.get(), info->size_data), 0);
  ASSERT_EQ(StreamAll(write, fd.get(), info->data.get(), info->size_data), 0)
      << "Failed to write Data";

  ASSERT_NO_FATAL_FAILURE(GetBlobsCreated(&executor, diagnostics_dir(), &blobs_created));
  ASSERT_EQ(blobs_created, 1ul);

  loop.Quit();
  loop.JoinThreads();
}

}  // namespace
}  // namespace blobfs
