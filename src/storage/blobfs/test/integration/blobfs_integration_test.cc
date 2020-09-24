// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <fuchsia/blobfs/c/fidl.h>
#include <fuchsia/io/llcpp/fidl.h>
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
#include <fbl/auto_call.h>
#include <fs/test_support/test_support.h>
#include <fs/trace.h>
#include <fvm/format.h>
#include <zxtest/zxtest.h>

#include "blobfs_fixtures.h"
#include "fdio_test.h"

namespace {

using blobfs::BlobInfo;
using blobfs::CharFill;
using blobfs::FdioTest;
using blobfs::GenerateBlob;
using blobfs::GenerateRandomBlob;
using blobfs::StreamAll;
using blobfs::VerifyContents;
using fs::FilesystemTest;
using fs::GetTopologicalPath;
using fs::RamDisk;

namespace fio = ::llcpp::fuchsia::io;

void VerifyCorruptedBlob(int fd, const char* data, size_t size_data) {
  // Verify the contents of the Blob
  fbl::Array<char> buf(new char[size_data], size_data);

  ASSERT_EQ(lseek(fd, 0, SEEK_SET), 0);
  ASSERT_EQ(StreamAll(read, fd, &buf[0], size_data), -1, "Expected reading to fail");
}

// Creates a corrupted blob with the provided Merkle tree + Data, and
// reads to verify the data.
void ReadBlobCorrupted(BlobInfo* info) {
  fbl::unique_fd fd(open(info->path, O_CREAT | O_RDWR));
  ASSERT_TRUE(fd, "Failed to create blob");
  ASSERT_EQ(ftruncate(fd.get(), info->size_data), 0);

  // Writing corrupted blob to disk.
  StreamAll(write, fd.get(), info->data.get(), info->size_data);

  ASSERT_NO_FAILURES(VerifyCorruptedBlob(fd.get(), info->data.get(), info->size_data));
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

void RunBasicsTest() {
  for (unsigned int i = 10; i < 16; i++) {
    std::unique_ptr<BlobInfo> info;
    ASSERT_NO_FAILURES(GenerateRandomBlob(kMountPath, 1 << i, &info));

    fbl::unique_fd fd;
    ASSERT_NO_FAILURES(MakeBlob(info.get(), &fd));
    ASSERT_EQ(close(fd.release()), 0);

    // We can re-open and verify the Blob as read-only
    fd.reset(open(info->path, O_RDONLY));
    ASSERT_TRUE(fd, "Failed to-reopen blob");
    ASSERT_NO_FAILURES(VerifyContents(fd.get(), info->data.get(), info->size_data));
    ASSERT_EQ(close(fd.release()), 0);

    // We cannot re-open the blob as writable
    fd.reset(open(info->path, O_RDWR | O_CREAT));
    ASSERT_FALSE(fd, "Shouldn't be able to re-create blob that exists");
    fd.reset(open(info->path, O_RDWR));
    ASSERT_FALSE(fd, "Shouldn't be able to re-open blob as writable");
    fd.reset(open(info->path, O_WRONLY));
    ASSERT_FALSE(fd, "Shouldn't be able to re-open blob as writable");

    ASSERT_EQ(unlink(info->path), 0);
  }
}

TEST_F(BlobfsTest, Basics) { RunBasicsTest(); }

TEST_F(BlobfsTestWithFvm, Basics) { RunBasicsTest(); }

void StartMockCorruptionHandlerService(async_dispatcher_t* dispatcher,
                                       std::unique_ptr<CorruptBlobHandler>* out) {
  zx::channel client, server;
  zx_status_t status = zx::channel::create(0, &client, &server);
  ASSERT_EQ(ZX_OK, status, "");
  auto handler = std::unique_ptr<CorruptBlobHandler>(new CorruptBlobHandler());
  ASSERT_EQ(ZX_OK, handler->Bind(dispatcher, std::move(server)));

  handler->UpdateClientHandle(std::move(client));
  *out = std::move(handler);
}

void RunBlobCorruptionTest() {
  // Start the corruption handler server.
  std::unique_ptr<CorruptBlobHandler> corruption_server;
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  ASSERT_EQ(ZX_OK, loop.StartThread("corruption-dispatcher"));

  ASSERT_NO_FAILURES(StartMockCorruptionHandlerService(loop.dispatcher(), &corruption_server));
  zx_handle_t blobfs_client = corruption_server->GetClientHandle();

  // Pass the client end to blobfs.
  fbl::unique_fd fd(open(kMountPath, O_RDONLY | O_DIRECTORY));
  ASSERT_TRUE(fd);
  zx_status_t status;
  fdio_cpp::FdioCaller caller(std::move(fd));

  ASSERT_OK(fuchsia_blobfs_BlobfsSetCorruptBlobHandler(caller.borrow_channel(),
                                                       std::move(blobfs_client), &status));
  ASSERT_OK(status);

  // Create a blob, corrupt it and then attempt to read it.
  std::unique_ptr<BlobInfo> info;
  GenerateRandomBlob(kMountPath, 1 << 5, &info);
  // Flip a random bit of the data
  size_t rand_index = rand() % info->size_data;
  char old_val = info->data.get()[rand_index];
  while ((info->data.get()[rand_index] = static_cast<char>(rand())) == old_val) {
  }

  ASSERT_NO_FAILURES(ReadBlobCorrupted(info.get()));
  // Shutdown explicitly calls "join" on the "corruption-dispatcher" thread and waits for it
  // to increment num_calls_.
  loop.Shutdown();
  ASSERT_TRUE(corruption_server->IsCalled());
}
// TODO Enable these fxbug.dev/56432
TEST_F(BlobfsTest, DISABLED_CorruptBlobNotify) { RunBlobCorruptionTest(); }

TEST_F(BlobfsTestWithFvm, DISABLED_CorruptBlobNotify) { RunBlobCorruptionTest(); }

void RunUnallocatedBlobTest() {
  std::unique_ptr<BlobInfo> info;
  ASSERT_NO_FAILURES(GenerateRandomBlob(kMountPath, 1 << 10, &info));

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

TEST_F(BlobfsTest, UnallocatedBlob) { RunUnallocatedBlobTest(); }

TEST_F(BlobfsTestWithFvm, UnallocatedBlob) { RunUnallocatedBlobTest(); }

void RunNullBlobCreateUnlinkTest() {
  std::unique_ptr<BlobInfo> info;
  ASSERT_NO_FAILURES(GenerateRandomBlob(kMountPath, 0, &info));

  fbl::unique_fd fd(open(info->path, O_CREAT | O_EXCL | O_RDWR));
  ASSERT_TRUE(fd);
  ASSERT_EQ(ftruncate(fd.get(), 0), 0);
  std::array<char, 1> buf;
  ASSERT_EQ(read(fd.get(), &buf[0], 1), 0, "Null Blob should reach EOF immediately");
  ASSERT_EQ(close(fd.release()), 0);

  fd.reset(open(info->path, O_CREAT | O_EXCL | O_RDWR));
  EXPECT_FALSE(fd, "Null Blob should already exist");
  fd.reset(open(info->path, O_CREAT | O_RDWR));
  EXPECT_FALSE(fd, "Null Blob should not be openable as writable");

  fd.reset(open(info->path, O_RDONLY));
  ASSERT_TRUE(fd, "Null blob should be re-openable");

  DIR* dir = opendir(kMountPath);
  ASSERT_NOT_NULL(dir);
  auto cleanup = fbl::MakeAutoCall([dir]() { closedir(dir); });
  struct dirent* entry = readdir(dir);
  ASSERT_NOT_NULL(entry);
  const char* kEmptyBlobName = "15ec7bf0b50732b49f8228e07d24365338f9e3ab994b00af08e5a3bffe55fd8b";
  EXPECT_STR_EQ(kEmptyBlobName, entry->d_name, "Unexpected name from readdir");
  EXPECT_NULL(readdir(dir));

  ASSERT_EQ(close(fd.release()), 0);
  ASSERT_EQ(unlink(info->path), 0, "Null Blob should be unlinkable");
}

TEST_F(BlobfsTest, NullBlobCreateUnlink) { RunNullBlobCreateUnlinkTest(); }

TEST_F(BlobfsTestWithFvm, NullBlobCreateUnlink) { RunNullBlobCreateUnlinkTest(); }

void RunNullBlobCreateRemountTest(FilesystemTest* test) {
  std::unique_ptr<BlobInfo> info;
  ASSERT_NO_FAILURES(GenerateRandomBlob(kMountPath, 0, &info));

  // Create the null blob.
  fbl::unique_fd fd(open(info->path, O_CREAT | O_EXCL | O_RDWR));
  ASSERT_TRUE(fd);
  ASSERT_EQ(ftruncate(fd.get(), 0), 0);
  ASSERT_EQ(close(fd.release()), 0);

  ASSERT_NO_FAILURES(test->Remount());
  fd.reset(open(info->path, O_RDONLY));
  ASSERT_TRUE(fd, "Null blob lost after reboot");
  ASSERT_EQ(close(fd.release()), 0);
  ASSERT_EQ(unlink(info->path), 0, "Null Blob should be unlinkable");
}

TEST_F(BlobfsTest, NullBlobCreateRemount) { RunNullBlobCreateRemountTest(this); }

TEST_F(BlobfsTestWithFvm, NullBlobCreateRemount) { RunNullBlobCreateRemountTest(this); }

void RunExclusiveCreateTest() {
  std::unique_ptr<BlobInfo> info;

  ASSERT_NO_FAILURES(GenerateRandomBlob(kMountPath, 1 << 17, &info));
  fbl::unique_fd fd(open(info->path, O_CREAT | O_EXCL | O_RDWR));
  ASSERT_TRUE(fd);

  fbl::unique_fd fd2(open(info->path, O_CREAT | O_EXCL | O_RDWR));
  EXPECT_FALSE(fd2, "Should not be able to exclusively create twice");

  // But a second open should work.
  fd2.reset(open(info->path, O_CREAT | O_RDWR));
  ASSERT_TRUE(fd2);
}

TEST_F(BlobfsTest, ExclusiveCreate) { RunExclusiveCreateTest(); }

TEST_F(BlobfsTestWithFvm, ExclusiveCreate) { RunExclusiveCreateTest(); }

void RunCompressibleBlobTest(FilesystemTest* test) {
  for (size_t i = 10; i < 22; i++) {
    std::unique_ptr<BlobInfo> info;

    // Create blobs which are trivially compressible.
    ASSERT_NO_FAILURES(GenerateBlob(
        [](char* data, size_t length) {
          size_t i = 0;
          while (i < length) {
            size_t j = (rand() % (length - i)) + 1;
            memset(data, (char)j, j);
            data += j;
            i += j;
          }
        },
        kMountPath, 1 << i, &info));

    fbl::unique_fd fd;
    ASSERT_NO_FAILURES(MakeBlob(info.get(), &fd));

    // We can re-open and verify the Blob as read-only.
    fd.reset(open(info->path, O_RDONLY));
    ASSERT_TRUE(fd, "Failed to-reopen blob");
    ASSERT_NO_FAILURES(VerifyContents(fd.get(), info->data.get(), info->size_data));

    // Force decompression by remounting, re-accessing blob.
    ASSERT_NO_FAILURES(test->Remount());
    fd.reset(open(info->path, O_RDONLY));
    ASSERT_TRUE(fd, "Failed to-reopen blob");
    ASSERT_NO_FAILURES(VerifyContents(fd.get(), info->data.get(), info->size_data));

    ASSERT_EQ(0, unlink(info->path));
  }
}

TEST_F(BlobfsTest, CompressibleBlob) { RunCompressibleBlobTest(this); }

TEST_F(BlobfsTestWithFvm, CompressibleBlob) { RunCompressibleBlobTest(this); }

void RunMmapTest() {
  for (size_t i = 10; i < 16; i++) {
    std::unique_ptr<BlobInfo> info;
    ASSERT_NO_FAILURES(GenerateRandomBlob(kMountPath, 1 << i, &info));

    fbl::unique_fd fd;
    ASSERT_NO_FAILURES(MakeBlob(info.get(), &fd));
    fd.reset(open(info->path, O_RDONLY));
    ASSERT_TRUE(fd, "Failed to-reopen blob");

    void* addr = mmap(NULL, info->size_data, PROT_READ, MAP_PRIVATE, fd.get(), 0);
    ASSERT_NE(addr, MAP_FAILED, "Could not mmap blob");
    ASSERT_BYTES_EQ(addr, info->data.get(), info->size_data);
    ASSERT_EQ(0, munmap(addr, info->size_data));
    ASSERT_EQ(0, unlink(info->path));
  }
}

TEST_F(BlobfsTest, Mmap) { RunMmapTest(); }

TEST_F(BlobfsTestWithFvm, Mmap) { RunMmapTest(); }

void RunMmapUseAfterCloseTest() {
  for (size_t i = 10; i < 16; i++) {
    std::unique_ptr<BlobInfo> info;
    ASSERT_NO_FAILURES(GenerateRandomBlob(kMountPath, 1 << i, &info));

    fbl::unique_fd fd;
    ASSERT_NO_FAILURES(MakeBlob(info.get(), &fd));
    fd.reset(open(info->path, O_RDONLY));
    ASSERT_TRUE(fd, "Failed to-reopen blob");

    void* addr = mmap(NULL, info->size_data, PROT_READ, MAP_PRIVATE, fd.get(), 0);
    ASSERT_NE(addr, MAP_FAILED, "Could not mmap blob");
    fd.reset();

    // We should be able to access the mapped data after the file is closed.
    ASSERT_BYTES_EQ(addr, info->data.get(), info->size_data);

    // We should be able to re-open and remap the file.
    //
    // Although this isn't being tested explicitly (we lack a mechanism to
    // check that the second mapping uses the same underlying pages as the
    // first) the memory usage should avoid duplication in the second
    // mapping.
    fd.reset(open(info->path, O_RDONLY));
    void* addr2 = mmap(NULL, info->size_data, PROT_READ, MAP_PRIVATE, fd.get(), 0);
    ASSERT_NE(addr2, MAP_FAILED, "Could not mmap blob");
    fd.reset();
    ASSERT_BYTES_EQ(addr2, info->data.get(), info->size_data);

    ASSERT_EQ(munmap(addr, info->size_data), 0, "Could not unmap blob");
    ASSERT_EQ(munmap(addr2, info->size_data), 0, "Could not unmap blob");

    ASSERT_EQ(0, unlink(info->path));
  }
}

TEST_F(BlobfsTest, MmapUseAfterClose) { RunMmapUseAfterCloseTest(); }

TEST_F(BlobfsTestWithFvm, MmapUseAfterClose) { RunMmapUseAfterCloseTest(); }

void RunReadDirectoryTest() {
  constexpr size_t kMaxEntries = 50;
  constexpr size_t kBlobSize = 1 << 10;

  std::unique_ptr<BlobInfo> info[kMaxEntries];

  // Try to readdir on an empty directory.
  DIR* dir = opendir(kMountPath);
  ASSERT_NOT_NULL(dir);
  auto cleanup = fbl::MakeAutoCall([dir]() { closedir(dir); });
  ASSERT_NULL(readdir(dir), "Expected blobfs to start empty");

  // Fill a directory with entries.
  for (size_t i = 0; i < kMaxEntries; i++) {
    ASSERT_NO_FAILURES(GenerateRandomBlob(kMountPath, kBlobSize, &info[i]));
    fbl::unique_fd fd;
    ASSERT_NO_FAILURES(MakeBlob(info[i].get(), &fd));
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
    ASSERT_TRUE(found, "Unknown directory entry");
    entries_seen++;
  }
  ASSERT_EQ(kMaxEntries, entries_seen);

  ASSERT_NULL(readdir(dir), "Directory should be empty");
  cleanup.cancel();
  ASSERT_EQ(0, closedir(dir));
}

TEST_F(BlobfsTest, ReadDirectory) { RunReadDirectoryTest(); }

TEST_F(BlobfsTestWithFvm, ReadDirectory) { RunReadDirectoryTest(); }

class SmallDiskTest : public BlobfsFixedDiskSizeTest {
 public:
  SmallDiskTest() : BlobfsFixedDiskSizeTest(MinimumDiskSize()) {}
  explicit SmallDiskTest(uint64_t disk_size) : BlobfsFixedDiskSizeTest(disk_size) {}

 protected:
  static uint64_t MinimumDiskSize() {
    blobfs::Superblock info;
    info.inode_count = blobfs::kBlobfsDefaultInodeCount;
    info.data_block_count = blobfs::kMinimumDataBlocks;
    info.journal_block_count = blobfs::kMinimumJournalBlocks;
    info.flags = 0;

    return blobfs::TotalBlocks(info) * blobfs::kBlobfsBlockSize;
  }
};

TEST_F(SmallDiskTest, SmallestValidDisk) {}

class TooSmallDiskTest : public SmallDiskTest {
 public:
  TooSmallDiskTest() : SmallDiskTest(MinimumDiskSize() - 1024) {}

  void SetUp() override {}
  void TearDown() override {}
};

TEST_F(TooSmallDiskTest, DiskTooSmall) {
  ASSERT_NOT_OK(
      mkfs(device_path().c_str(), format_type(), launch_stdio_sync, &default_mkfs_options));
}

class SmallDiskTestWithFvm : public BlobfsFixedDiskSizeTestWithFvm {
 public:
  SmallDiskTestWithFvm() : BlobfsFixedDiskSizeTestWithFvm(MinimumDiskSize()) {}
  explicit SmallDiskTestWithFvm(uint64_t disk_size) : BlobfsFixedDiskSizeTestWithFvm(disk_size) {}

 protected:
  static uint64_t MinimumDiskSize() {
    size_t blocks_per_slice = kTestFvmSliceSize / blobfs::kBlobfsBlockSize;

    // Calculate slices required for data blocks based on minimum requirement and slice size.
    uint64_t required_data_slices =
        fbl::round_up(blobfs::kMinimumDataBlocks, blocks_per_slice) / blocks_per_slice;
    uint64_t required_journal_slices =
        fbl::round_up(blobfs::kDefaultJournalBlocks, blocks_per_slice) / blocks_per_slice;

    // Require an additional 1 slice each for super, inode, and block bitmaps.
    uint64_t blobfs_size = (required_journal_slices + required_data_slices + 3) * kTestFvmSliceSize;
    uint64_t minimum_size = blobfs_size;
    uint64_t metadata_size = fvm::MetadataSize(blobfs_size, kTestFvmSliceSize);

    // Re-calculate minimum size until the metadata size stops growing.
    while (minimum_size - blobfs_size != metadata_size * 2) {
      minimum_size = blobfs_size + metadata_size * 2;
      metadata_size = fvm::MetadataSize(minimum_size, kTestFvmSliceSize);
    }

    return minimum_size;
  }
};

TEST_F(SmallDiskTestWithFvm, SmallestValidDisk) {}

class TooSmallDiskTestWithFvm : public SmallDiskTestWithFvm {
 public:
  TooSmallDiskTestWithFvm() : SmallDiskTestWithFvm(MinimumDiskSize() - 1024) {}

  void SetUp() override { ASSERT_NO_FAILURES(FvmSetUp()); }
  void TearDown() override {}
};

TEST_F(TooSmallDiskTestWithFvm, DiskTooSmall) {
  ASSERT_NOT_OK(
      mkfs(device_path().c_str(), format_type(), launch_stdio_sync, &default_mkfs_options));
}

void QueryInfo(size_t expected_nodes, size_t expected_bytes) {
  fbl::unique_fd fd(open(kMountPath, O_RDONLY | O_DIRECTORY));
  ASSERT_TRUE(fd);

  fdio_cpp::FdioCaller caller(std::move(fd));
  auto query_result =
      fio::DirectoryAdmin::Call::QueryFilesystem(zx::unowned_channel(caller.borrow_channel()));
  ASSERT_OK(query_result.status());
  ASSERT_OK(query_result.value().s);
  ASSERT_NOT_NULL(query_result.value().info);
  const fio::FilesystemInfo& info = *query_result.value().info;

  const char kFsName[] = "blobfs";
  const char* name = reinterpret_cast<const char*>(info.name.data());
  ASSERT_STR_EQ(kFsName, name, "Unexpected filesystem mounted");
  EXPECT_EQ(info.block_size, blobfs::kBlobfsBlockSize);
  EXPECT_EQ(info.max_filename_size, 64U);
  EXPECT_EQ(info.fs_type, VFS_TYPE_BLOBFS);
  EXPECT_NE(info.fs_id, 0);

  // Check that used_bytes are within a reasonable range
  EXPECT_GE(info.used_bytes, expected_bytes);
  EXPECT_LE(info.used_bytes, info.total_bytes);

  // Check that total_bytes are a multiple of slice_size
  EXPECT_GE(info.total_bytes, kTestFvmSliceSize);
  EXPECT_EQ(info.total_bytes % kTestFvmSliceSize, 0);
  EXPECT_EQ(info.total_nodes, kTestFvmSliceSize / blobfs::kBlobfsInodeSize);
  EXPECT_EQ(info.used_nodes, expected_nodes);
}

TEST_F(BlobfsTestWithFvm, QueryInfo) {
  size_t total_bytes = 0;
  ASSERT_NO_FAILURES(QueryInfo(0, 0));
  for (size_t i = 10; i < 16; i++) {
    std::unique_ptr<BlobInfo> info;
    ASSERT_NO_FAILURES(GenerateRandomBlob(kMountPath, 1 << i, &info));

    fbl::unique_fd fd;
    ASSERT_NO_FAILURES(MakeBlob(info.get(), &fd));
    total_bytes += fbl::round_up(info->size_merkle + info->size_data, blobfs::kBlobfsBlockSize);
  }

  ASSERT_NO_FAILURES(QueryInfo(6, total_bytes));
}

void GetAllocations(zx::vmo* out_vmo, uint64_t* out_count) {
  fbl::unique_fd fd(open(kMountPath, O_RDONLY | O_DIRECTORY));
  ASSERT_TRUE(fd);
  zx_status_t status;
  zx_handle_t vmo_handle;
  fdio_cpp::FdioCaller caller(std::move(fd));
  ASSERT_OK(fuchsia_blobfs_BlobfsGetAllocatedRegions(caller.borrow_channel(), &status, &vmo_handle,
                                                     out_count));
  ASSERT_OK(status);
  out_vmo->reset(vmo_handle);
}

void RunGetAllocatedRegionsTest() {
  zx::vmo vmo;
  uint64_t count;
  size_t total_bytes = 0;
  size_t fidl_bytes = 0;

  // Although we expect this partition to be empty, we check the results of GetAllocations
  // in case blobfs chooses to store any metadata of pre-initialized data with the
  // allocated regions.
  ASSERT_NO_FAILURES(GetAllocations(&vmo, &count));

  std::vector<fuchsia_blobfs_BlockRegion> buffer(count);
  ASSERT_OK(vmo.read(buffer.data(), 0, sizeof(fuchsia_blobfs_BlockRegion) * count));
  for (size_t i = 0; i < count; i++) {
    total_bytes += buffer[i].length * blobfs::kBlobfsBlockSize;
  }

  for (size_t i = 10; i < 16; i++) {
    std::unique_ptr<BlobInfo> info;
    ASSERT_NO_FAILURES(GenerateRandomBlob(kMountPath, 1 << i, &info));

    fbl::unique_fd fd;
    ASSERT_NO_FAILURES(MakeBlob(info.get(), &fd));
    total_bytes += fbl::round_up(info->size_merkle + info->size_data, blobfs::kBlobfsBlockSize);
  }
  ASSERT_NO_FAILURES(GetAllocations(&vmo, &count));

  buffer.resize(count);
  ASSERT_OK(vmo.read(buffer.data(), 0, sizeof(fuchsia_blobfs_BlockRegion) * count));
  for (size_t i = 0; i < count; i++) {
    fidl_bytes += buffer[i].length * blobfs::kBlobfsBlockSize;
  }
  ASSERT_EQ(fidl_bytes, total_bytes);
}

TEST_F(BlobfsTest, GetAllocatedRegions) { RunGetAllocatedRegionsTest(); }

TEST_F(BlobfsTestWithFvm, GetAllocatedRegions) { RunGetAllocatedRegionsTest(); }

void RunUseAfterUnlinkTest() {
  for (size_t i = 0; i < 16; i++) {
    std::unique_ptr<BlobInfo> info;
    ASSERT_NO_FAILURES(GenerateRandomBlob(kMountPath, 1 << i, &info));

    fbl::unique_fd fd;
    ASSERT_NO_FAILURES(MakeBlob(info.get(), &fd));

    // We should be able to unlink the blob.
    ASSERT_EQ(0, unlink(info->path));

    // We should still be able to read the blob after unlinking.
    ASSERT_NO_FAILURES(VerifyContents(fd.get(), info->data.get(), info->size_data));

    // After closing the file, however, we should not be able to re-open the blob.
    fd.reset();
    ASSERT_LT(open(info->path, O_RDONLY), 0, "Expected blob to be deleted");
  }
}

TEST_F(BlobfsTest, UseAfterUnlink) { RunUseAfterUnlinkTest(); }

TEST_F(BlobfsTestWithFvm, UseAfterUnlink) { RunUseAfterUnlinkTest(); }

void RunWriteAfterReadtest() {
  srand(zxtest::Runner::GetInstance()->random_seed());
  for (size_t i = 0; i < 16; i++) {
    std::unique_ptr<BlobInfo> info;
    ASSERT_NO_FAILURES(GenerateRandomBlob(kMountPath, 1 << i, &info));

    fbl::unique_fd fd;
    ASSERT_NO_FAILURES(MakeBlob(info.get(), &fd));

    // After blob generation, writes should be rejected.
    ASSERT_LT(write(fd.get(), info->data.get(), 1), 0,
              "After being written, the blob should refuse writes");

    off_t seek_pos = (rand() % info->size_data);
    ASSERT_EQ(seek_pos, lseek(fd.get(), seek_pos, SEEK_SET));
    ASSERT_LT(write(fd.get(), info->data.get(), 1), 0,
              "After being written, the blob should refuse writes");
    ASSERT_LT(ftruncate(fd.get(), rand() % info->size_data), 0,
              "The blob should always refuse to be truncated");

    ASSERT_EQ(0, unlink(info->path));
  }
}

TEST_F(BlobfsTest, WriteAfterRead) { RunWriteAfterReadtest(); }

TEST_F(BlobfsTestWithFvm, WriteAfterRead) { RunWriteAfterReadtest(); }

void RunWriteAfterUnlinkTest() {
  std::unique_ptr<BlobInfo> info;
  size_t size = 1 << 20;
  ASSERT_NO_FAILURES(GenerateRandomBlob(kMountPath, size, &info));

  // Partially write out first blob.
  fbl::unique_fd fd(open(info->path, O_CREAT | O_RDWR));
  ASSERT_TRUE(fd, "Failed to create blob");
  ASSERT_EQ(0, ftruncate(fd.get(), size));
  ASSERT_EQ(0, StreamAll(write, fd.get(), info->data.get(), size / 2), "Failed to write Data");
  ASSERT_EQ(0, unlink(info->path));
  ASSERT_EQ(0, StreamAll(write, fd.get(), info->data.get() + size / 2, size - (size / 2)),
            "Failed to write Data");
  fd.reset();
  ASSERT_LT(open(info->path, O_RDONLY), 0);
}

TEST_F(BlobfsTest, WriteAfterUnlink) { RunWriteAfterUnlinkTest(); }

TEST_F(BlobfsTestWithFvm, WriteAfterUnlink) { RunWriteAfterUnlinkTest(); }

void RunReadTooLargeTest() {
  for (size_t i = 0; i < 16; i++) {
    std::unique_ptr<BlobInfo> info;
    ASSERT_NO_FAILURES(GenerateRandomBlob(kMountPath, 1 << i, &info));

    fbl::unique_fd fd;
    ASSERT_NO_FAILURES(MakeBlob(info.get(), &fd));

    std::unique_ptr<char[]> buffer(new char[info->size_data]);

    // Try read beyond end of blob.
    off_t end_off = info->size_data;
    ASSERT_EQ(end_off, lseek(fd.get(), end_off, SEEK_SET));
    ASSERT_EQ(0, read(fd.get(), &buffer[0], 1), "Expected empty read beyond end of file");

    // Try some reads which straddle the end of the blob.
    for (ssize_t j = 1; j < static_cast<ssize_t>(info->size_data); j *= 2) {
      end_off = info->size_data - j;
      ASSERT_EQ(end_off, lseek(fd.get(), end_off, SEEK_SET));
      ASSERT_EQ(j, read(fd.get(), &buffer[0], j * 2),
                "Expected to only read one byte at end of file");
      ASSERT_BYTES_EQ(buffer.get(), &info->data[info->size_data - j], j,
                      "Read data, but it was bad");
    }

    ASSERT_EQ(0, unlink(info->path));
  }
}

TEST_F(BlobfsTest, ReadTooLarge) { RunReadTooLargeTest(); }

TEST_F(BlobfsTestWithFvm, ReadTooLarge) { RunReadTooLargeTest(); }

void RunBadCreationTest(uint64_t disk_size) {
  std::string name(kMountPath);
  name.append("/00112233445566778899AABBCCDDEEFFGGHHIIJJKKLLMMNNOOPPQQRRSSTTUUVV");
  fbl::unique_fd fd(open(name.c_str(), O_CREAT | O_RDWR));
  ASSERT_FALSE(fd, "Only acceptable pathnames are hex");

  name.assign(kMountPath);
  name.append("/00112233445566778899AABBCCDDEEFF");
  fd.reset(open(name.c_str(), O_CREAT | O_RDWR));
  ASSERT_FALSE(fd, "Only acceptable pathnames are 32 hex-encoded bytes");

  std::unique_ptr<BlobInfo> info;
  ASSERT_NO_FAILURES(GenerateRandomBlob(kMountPath, 1 << 15, &info));

  fd.reset(open(info->path, O_CREAT | O_RDWR));
  ASSERT_TRUE(fd, "Failed to create blob");
  ASSERT_EQ(-1, ftruncate(fd.get(), 0), "Blob without data doesn't match null blob");

  // This is the size of the entire disk; we shouldn't fail here as setting blob size
  // has nothing to do with how much space blob will occupy.
  fd.reset();
  fd.reset(open(info->path, O_CREAT | O_RDWR));
  ASSERT_EQ(0, ftruncate(fd.get(), disk_size), "Huge blob");

  // Write nothing, but close the blob. Since the write was incomplete,
  // it will be inaccessible.
  fd.reset(open(info->path, O_RDWR));
  ASSERT_FALSE(fd, "Cannot access partial blob");
  fd.reset(open(info->path, O_RDONLY));
  ASSERT_FALSE(fd, "Cannot access partial blob");

  // And once more -- let's write everything but the last byte of a blob's data.
  fd.reset(open(info->path, O_CREAT | O_RDWR));
  ASSERT_TRUE(fd, "Failed to create blob");
  ASSERT_EQ(0, ftruncate(fd.get(), info->size_data), "Failed to allocate blob");
  ASSERT_EQ(0, StreamAll(write, fd.get(), info->data.get(), info->size_data - 1),
            "Failed to write data");
  fd.reset(open(info->path, O_RDWR));
  ASSERT_FALSE(fd, "Cannot access partial blob");
}

TEST_F(BlobfsTest, BadCreation) {
  RunBadCreationTest(environment_->config().ramdisk_block_count * blobfs::kBlobfsBlockSize);
}

TEST_F(BlobfsTestWithFvm, BadCreation) {
  RunBadCreationTest(environment_->config().ramdisk_block_count * blobfs::kBlobfsBlockSize);
}

// Attempts to read the contents of the Blob.
void VerifyCompromised(int fd, const char* data, size_t size_data) {
  std::unique_ptr<char[]> buf(new char[size_data]);

  ASSERT_EQ(0, lseek(fd, 0, SEEK_SET));
  ASSERT_EQ(-1, StreamAll(read, fd, &buf[0], size_data), "Expected reading to fail");
}

// Creates a blob with the provided Merkle tree + Data, and
// reads to verify the data.
void MakeBlobCompromised(BlobInfo* info) {
  fbl::unique_fd fd(open(info->path, O_CREAT | O_RDWR));
  ASSERT_TRUE(fd, "Failed to create blob");
  ASSERT_EQ(0, ftruncate(fd.get(), info->size_data));

  // If we're writing a blob with invalid sizes, it's possible that writing will fail.
  StreamAll(write, fd.get(), info->data.get(), info->size_data);

  ASSERT_NO_FAILURES(VerifyCompromised(fd.get(), info->data.get(), info->size_data));
}

void RunCorruptBlobTest() {
  srand(zxtest::Runner::GetInstance()->random_seed());
  std::unique_ptr<BlobInfo> info;
  for (size_t i = 1; i < 18; i++) {
    ASSERT_NO_FAILURES(GenerateRandomBlob(kMountPath, 1 << i, &info));
    info->size_data -= (rand() % info->size_data) + 1;
    if (info->size_data == 0) {
      info->size_data = 1;
    }
    ASSERT_NO_FAILURES(MakeBlobCompromised(info.get()));
  }

  for (size_t i = 0; i < 18; i++) {
    ASSERT_NO_FAILURES(GenerateRandomBlob(kMountPath, 1 << i, &info));
    // Flip a random bit of the data.
    size_t rand_index = rand() % info->size_data;
    char old_val = info->data.get()[rand_index];
    while ((info->data.get()[rand_index] = static_cast<char>(rand())) == old_val) {
    }
    ASSERT_NO_FAILURES(MakeBlobCompromised(info.get()));
  }
}

TEST_F(BlobfsTest, CorruptBlob) { RunCorruptBlobTest(); }

TEST_F(BlobfsTestWithFvm, CorruptBlob) { RunCorruptBlobTest(); }

void RunCorruptDigestTest() {
  srand(zxtest::Runner::GetInstance()->random_seed());
  std::unique_ptr<BlobInfo> info;
  for (size_t i = 1; i < 18; i++) {
    ASSERT_NO_FAILURES(GenerateRandomBlob(kMountPath, 1 << i, &info));

    char hexdigits[17] = "0123456789abcdef";
    size_t idx = strlen(info->path) - 1 - (rand() % digest::kSha256HexLength);
    char newchar = hexdigits[rand() % 16];
    while (info->path[idx] == newchar) {
      newchar = hexdigits[rand() % 16];
    }
    info->path[idx] = newchar;
    ASSERT_NO_FAILURES(MakeBlobCompromised(info.get()));
  }

  for (size_t i = 0; i < 18; i++) {
    ASSERT_NO_FAILURES(GenerateRandomBlob(kMountPath, 1 << i, &info));
    // Flip a random bit of the data.
    size_t rand_index = rand() % info->size_data;
    char old_val = info->data.get()[rand_index];
    while ((info->data.get()[rand_index] = static_cast<char>(rand())) == old_val) {
    }
    ASSERT_NO_FAILURES(MakeBlobCompromised(info.get()));
  }
}

TEST_F(BlobfsTest, CorruptDigest) { RunCorruptDigestTest(); }

TEST_F(BlobfsTestWithFvm, CorruptDigest) { RunCorruptDigestTest(); }

void RunEdgeAllocationTest() {
  // Powers of two...
  for (size_t i = 1; i < 16; i++) {
    // -1, 0, +1 offsets...
    for (size_t j = -1; j < 2; j++) {
      std::unique_ptr<BlobInfo> info;
      ASSERT_NO_FAILURES(GenerateRandomBlob(kMountPath, (1 << i) + j, &info));
      fbl::unique_fd fd;
      ASSERT_NO_FAILURES(MakeBlob(info.get(), &fd));
      ASSERT_EQ(0, unlink(info->path));
    }
  }
}

TEST_F(BlobfsTest, EdgeAllocation) { RunEdgeAllocationTest(); }

TEST_F(BlobfsTestWithFvm, EdgeAllocation) { RunEdgeAllocationTest(); }

void RunUmountWithOpenFileTest(FilesystemTest* test) {
  std::unique_ptr<BlobInfo> info;
  ASSERT_NO_FAILURES(GenerateRandomBlob(kMountPath, 1 << 16, &info));
  fbl::unique_fd fd;
  ASSERT_NO_FAILURES(MakeBlob(info.get(), &fd));

  // Intentionally don't close the file descriptor: Unmount anyway.
  ASSERT_NO_FAILURES(test->Remount());
  // Just closing our local handle; the connection should be disconnected.
  int close_return = close(fd.release());
  int close_error = errno;
  ASSERT_EQ(-1, close_return);
  ASSERT_EQ(EPIPE, close_error);

  fd.reset(open(info->path, O_RDONLY));
  ASSERT_TRUE(fd, "Failed to open blob");
  ASSERT_NO_FAILURES(VerifyContents(fd.get(), info->data.get(), info->size_data));
  fd.reset();

  ASSERT_EQ(0, unlink(info->path));
}

TEST_F(BlobfsTest, UmountWithOpenFile) { RunUmountWithOpenFileTest(this); }

TEST_F(BlobfsTestWithFvm, UmountWithOpenFile) { RunUmountWithOpenFileTest(this); }

void RunUmountWithMappedFileTest(FilesystemTest* test) {
  std::unique_ptr<BlobInfo> info;
  ASSERT_NO_FAILURES(GenerateRandomBlob(kMountPath, 1 << 16, &info));
  fbl::unique_fd fd;
  ASSERT_NO_FAILURES(MakeBlob(info.get(), &fd));

  void* addr = mmap(nullptr, info->size_data, PROT_READ, MAP_SHARED, fd.get(), 0);
  ASSERT_NOT_NULL(addr);
  fd.reset();

  // Intentionally don't unmap the file descriptor: Unmount anyway.
  ASSERT_NO_FAILURES(test->Remount());
  ASSERT_EQ(munmap(addr, info->size_data), 0);

  fd.reset(open(info->path, O_RDONLY));
  ASSERT_TRUE(fd.get(), "Failed to open blob");
  ASSERT_NO_FAILURES(VerifyContents(fd.get(), info->data.get(), info->size_data));
  ASSERT_EQ(0, unlink(info->path));
}

TEST_F(BlobfsTest, UmountWithMappedFile) { RunUmountWithMappedFileTest(this); }

TEST_F(BlobfsTestWithFvm, UmountWithMappedFile) { RunUmountWithMappedFileTest(this); }

void RunUmountWithOpenMappedFileTest(FilesystemTest* test) {
  std::unique_ptr<BlobInfo> info;
  ASSERT_NO_FAILURES(GenerateRandomBlob(kMountPath, 1 << 16, &info));
  fbl::unique_fd fd;
  ASSERT_NO_FAILURES(MakeBlob(info.get(), &fd));

  void* addr = mmap(nullptr, info->size_data, PROT_READ, MAP_SHARED, fd.get(), 0);
  ASSERT_NOT_NULL(addr);

  // Intentionally don't close the file descriptor: Unmount anyway.
  ASSERT_NO_FAILURES(test->Remount());
  // Just closing our local handle; the connection should be disconnected.
  ASSERT_EQ(0, munmap(addr, info->size_data));
  int close_return = close(fd.release());
  int close_error = errno;
  ASSERT_EQ(-1, close_return);
  ASSERT_EQ(EPIPE, close_error);

  fd.reset(open(info->path, O_RDONLY));
  ASSERT_TRUE(fd.get(), "Failed to open blob");
  ASSERT_NO_FAILURES(VerifyContents(fd.get(), info->data.get(), info->size_data));
  ASSERT_EQ(0, unlink(info->path));
}

TEST_F(BlobfsTest, UmountWithOpenMappedFile) { RunUmountWithOpenMappedFileTest(this); }

TEST_F(BlobfsTestWithFvm, UmountWithOpenMappedFile) { RunUmountWithOpenMappedFileTest(this); }

void RunCreateUmountRemountSmallTest(FilesystemTest* test) {
  for (size_t i = 10; i < 16; i++) {
    std::unique_ptr<BlobInfo> info;
    ASSERT_NO_FAILURES(GenerateRandomBlob(kMountPath, 1 << i, &info));

    fbl::unique_fd fd;
    ASSERT_NO_FAILURES(MakeBlob(info.get(), &fd));

    fd.reset();
    ASSERT_NO_FAILURES(test->Remount(), "Could not re-mount blobfs");

    fd.reset(open(info->path, O_RDONLY));
    ASSERT_TRUE(fd, "Failed to open blob");

    ASSERT_NO_FAILURES(VerifyContents(fd.get(), info->data.get(), info->size_data));
    ASSERT_EQ(0, unlink(info->path));
  }
}

TEST_F(BlobfsTest, CreateUmountRemountSmall) { RunCreateUmountRemountSmallTest(this); }

TEST_F(BlobfsTestWithFvm, CreateUmountRemountSmall) { RunCreateUmountRemountSmallTest(this); }

bool IsReadable(int fd) {
  char buf[1];
  return pread(fd, buf, sizeof(buf), 0) == sizeof(buf);
}

// Tests that we cannot read from the Blob until it has been fully written.
void RunEarlyReadTest() {
  std::unique_ptr<BlobInfo> info;

  ASSERT_NO_FAILURES(GenerateRandomBlob(kMountPath, 1 << 17, &info));
  fbl::unique_fd fd(open(info->path, O_CREAT | O_EXCL | O_RDWR));
  ASSERT_TRUE(fd);

  // A second fd should also not be readable.
  fbl::unique_fd fd2(open(info->path, O_CREAT | O_RDWR));
  ASSERT_TRUE(fd2);

  ASSERT_FALSE(IsReadable(fd.get()), "Should not be readable after open");
  ASSERT_FALSE(IsReadable(fd2.get()), "Should not be readable after open");

  ASSERT_EQ(0, ftruncate(fd.get(), info->size_data));
  ASSERT_FALSE(IsReadable(fd.get()), "Should not be readable after alloc");
  ASSERT_FALSE(IsReadable(fd2.get()), "Should not be readable after alloc");

  ASSERT_EQ(0, StreamAll(write, fd.get(), info->data.get(), info->size_data),
            "Failed to write Data");

  // Okay, NOW we can read.
  // Double check that attempting to read early didn't cause problems...
  ASSERT_NO_FAILURES(VerifyContents(fd.get(), info->data.get(), info->size_data));
  ASSERT_NO_FAILURES(VerifyContents(fd2.get(), info->data.get(), info->size_data));

  ASSERT_TRUE(IsReadable(fd.get()));
}

TEST_F(BlobfsTest, EarlyRead) { RunEarlyReadTest(); }

TEST_F(BlobfsTestWithFvm, EarlyRead) { RunEarlyReadTest(); }

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
void RunWaitForReadTest() {
  std::unique_ptr<BlobInfo> info;

  ASSERT_NO_FAILURES(GenerateRandomBlob(kMountPath, 1 << 17, &info));
  fbl::unique_fd fd(open(info->path, O_CREAT | O_EXCL | O_RDWR));
  ASSERT_TRUE(fd);

  {
    // Launch a background thread to wait for the file to become readable.
    std::atomic<bool> result;
    std::thread waiter_thread(CheckReadable, std::move(fd), &result);

    MakeBlob(info.get(), &fd);

    waiter_thread.join();
    ASSERT_TRUE(result.load(), "Background operation failed");
  }

  // Before continuing, make sure that MakeBlob was successful.
  ASSERT_NO_FAILURES();

  // Double check that attempting to read early didn't cause problems...
  ASSERT_NO_FAILURES(VerifyContents(fd.get(), info->data.get(), info->size_data));
}

TEST_F(BlobfsTest, WaitForRead) { RunWaitForReadTest(); }

TEST_F(BlobfsTestWithFvm, WaitForRead) { RunWaitForReadTest(); }

// Tests that seeks during writing are ignored.
void RunWriteSeekIgnoredTest() {
  srand(zxtest::Runner::GetInstance()->random_seed());
  std::unique_ptr<BlobInfo> info;
  ASSERT_NO_FAILURES(GenerateRandomBlob(kMountPath, 1 << 17, &info));
  fbl::unique_fd fd(open(info->path, O_CREAT | O_RDWR));
  ASSERT_TRUE(fd, "Failed to create blob");
  ASSERT_EQ(0, ftruncate(fd.get(), info->size_data));

  off_t seek_pos = (rand() % info->size_data);
  ASSERT_EQ(seek_pos, lseek(fd.get(), seek_pos, SEEK_SET));
  ASSERT_EQ(info->size_data, write(fd.get(), info->data.get(), info->size_data));

  // Double check that attempting to seek early didn't cause problems...
  ASSERT_NO_FAILURES(VerifyContents(fd.get(), info->data.get(), info->size_data));
}

TEST_F(BlobfsTest, WriteSeekIgnored) { RunWriteSeekIgnoredTest(); }

TEST_F(BlobfsTestWithFvm, WriteSeekIgnored) { RunWriteSeekIgnoredTest(); }

void UnlinkAndRecreate(const char* path, fbl::unique_fd* fd) {
  ASSERT_EQ(0, unlink(path));
  fd->reset();  // Make sure the file is gone.
  fd->reset(open(path, O_CREAT | O_RDWR | O_EXCL));
  ASSERT_TRUE(*fd, "Failed to recreate blob");
}

// Try unlinking while creating a blob.
void RunRestartCreationTest() {
  std::unique_ptr<BlobInfo> info;
  ASSERT_NO_FAILURES(GenerateRandomBlob(kMountPath, 1 << 17, &info));

  fbl::unique_fd fd(open(info->path, O_CREAT | O_RDWR));
  ASSERT_TRUE(fd, "Failed to create blob");

  // Unlink after first open.
  ASSERT_NO_FAILURES(UnlinkAndRecreate(info->path, &fd));

  // Unlink after init.
  ASSERT_EQ(0, ftruncate(fd.get(), info->size_data));
  ASSERT_NO_FAILURES(UnlinkAndRecreate(info->path, &fd));

  // Unlink after first write.
  ASSERT_EQ(0, ftruncate(fd.get(), info->size_data));
  ASSERT_EQ(0, StreamAll(write, fd.get(), info->data.get(), info->size_data),
            "Failed to write Data");
  ASSERT_NO_FAILURES(UnlinkAndRecreate(info->path, &fd));
}

TEST_F(BlobfsTest, RestartCreation) { RunRestartCreationTest(); }

TEST_F(BlobfsTestWithFvm, RestartCreation) { RunRestartCreationTest(); }

// Attempt using invalid operations.
void RunInvalidOperationsTest() {
  // First off, make a valid blob.
  std::unique_ptr<BlobInfo> info;
  ASSERT_NO_FAILURES(GenerateRandomBlob(kMountPath, 1 << 12, &info));
  fbl::unique_fd fd;
  ASSERT_NO_FAILURES(MakeBlob(info.get(), &fd));
  ASSERT_NO_FAILURES(VerifyContents(fd.get(), info->data.get(), info->size_data));

  // Try some unsupported operations.
  ASSERT_LT(rename(info->path, info->path), 0);
  ASSERT_LT(truncate(info->path, 0), 0);
  ASSERT_LT(utime(info->path, nullptr), 0);

  // Test that a file cannot unmount the entire blobfs.
  // Instead, the file channel will be forcibly closed as we attempt to call an unknown FIDL method.
  // Hence we clone the fd into a |canary_channel| which we know will have its peer closed.
  zx::channel canary_channel;
  ASSERT_OK(fdio_fd_clone(fd.get(), canary_channel.reset_and_get_address()));
  ASSERT_EQ(ZX_ERR_PEER_CLOSED,
            fio::DirectoryAdmin::Call::Unmount(zx::unowned_channel(canary_channel)).status());
  zx_signals_t pending;
  EXPECT_OK(canary_channel.wait_one(ZX_CHANNEL_PEER_CLOSED, zx::time::infinite_past(), &pending));

  // Access the file once more, after these operations.
  ASSERT_NO_FAILURES(VerifyContents(fd.get(), info->data.get(), info->size_data));
}

TEST_F(BlobfsTest, InvalidOperations) { RunInvalidOperationsTest(); }

TEST_F(BlobfsTestWithFvm, InvalidOperations) { RunInvalidOperationsTest(); }

// Attempt operations on the root directory.
void RunRootDirectoryTest() {
  std::string name(kMountPath);
  name.append("/.");
  fbl::unique_fd dirfd(open(name.c_str(), O_RDONLY));
  ASSERT_TRUE(dirfd, "Cannot open root directory");

  std::unique_ptr<BlobInfo> info;
  ASSERT_NO_FAILURES(GenerateRandomBlob(kMountPath, 1 << 12, &info));

  // Test operations which should ONLY operate on Blobs.
  ASSERT_LT(ftruncate(dirfd.get(), info->size_data), 0);

  char buf[8];
  ASSERT_LT(write(dirfd.get(), buf, 8), 0, "Should not write to directory");
  ASSERT_LT(read(dirfd.get(), buf, 8), 0, "Should not read from directory");

  // Should NOT be able to unlink root dir.
  ASSERT_LT(unlink(info->path), 0);
}

TEST_F(BlobfsTest, RootDirectory) { RunRootDirectoryTest(); }

TEST_F(BlobfsTestWithFvm, RootDirectory) { RunRootDirectoryTest(); }

void RunPartialWriteTest() {
  std::unique_ptr<BlobInfo> info_complete;
  std::unique_ptr<BlobInfo> info_partial;
  size_t size = 1 << 20;
  ASSERT_NO_FAILURES(GenerateRandomBlob(kMountPath, size, &info_complete));
  ASSERT_NO_FAILURES(GenerateRandomBlob(kMountPath, size, &info_partial));

  // Partially write out first blob.
  fbl::unique_fd fd_partial(open(info_partial->path, O_CREAT | O_RDWR));
  ASSERT_TRUE(fd_partial, "Failed to create blob");
  ASSERT_EQ(0, ftruncate(fd_partial.get(), size));
  ASSERT_EQ(0, StreamAll(write, fd_partial.get(), info_partial->data.get(), size / 2),
            "Failed to write Data");

  // Completely write out second blob.
  fbl::unique_fd fd_complete;
  ASSERT_NO_FAILURES(MakeBlob(info_complete.get(), &fd_complete));
}

TEST_F(BlobfsTest, PartialWrite) { RunPartialWriteTest(); }

TEST_F(BlobfsTestWithFvm, PartialWrite) { RunPartialWriteTest(); }

void RunPartialWriteSleepyDiskTest(const RamDisk* disk) {
  if (!disk) {
    return;
  }

  std::unique_ptr<BlobInfo> info_complete;
  std::unique_ptr<BlobInfo> info_partial;
  size_t size = 1 << 20;
  ASSERT_NO_FAILURES(GenerateRandomBlob(kMountPath, size, &info_complete));
  ASSERT_NO_FAILURES(GenerateRandomBlob(kMountPath, size, &info_partial));

  // Partially write out first blob.
  fbl::unique_fd fd_partial(open(info_partial->path, O_CREAT | O_RDWR));
  ASSERT_TRUE(fd_partial, "Failed to create blob");
  ASSERT_EQ(0, ftruncate(fd_partial.get(), size));
  ASSERT_EQ(0, StreamAll(write, fd_partial.get(), info_partial->data.get(), size / 2),
            "Failed to write Data");

  // Completely write out second blob.
  fbl::unique_fd fd_complete;
  ASSERT_NO_FAILURES(MakeBlob(info_complete.get(), &fd_complete));

  ASSERT_EQ(0, syncfs(fd_complete.get()));
  ASSERT_OK(disk->SleepAfter(0));

  fd_complete.reset(open(info_complete->path, O_RDONLY));
  ASSERT_TRUE(fd_complete, "Failed to re-open blob");

  ASSERT_EQ(0, syncfs(fd_complete.get()));
  ASSERT_OK(disk->WakeUp());

  ASSERT_NO_FAILURES(VerifyContents(fd_complete.get(), info_complete->data.get(), size));

  fd_partial.reset();
  fd_partial.reset(open(info_partial->path, O_RDONLY));
  ASSERT_FALSE(fd_partial, "Should not be able to open invalid blob");
}

TEST_F(BlobfsTest, PartialWriteSleepyDisk) {
  RunPartialWriteSleepyDiskTest(environment_->ramdisk());
}

TEST_F(BlobfsTestWithFvm, PartialWriteSleepyDisk) {
  RunPartialWriteSleepyDiskTest(environment_->ramdisk());
}

void RunMultipleWritesTest() {
  std::unique_ptr<BlobInfo> info;
  ASSERT_NO_FAILURES(GenerateRandomBlob(kMountPath, 1 << 16, &info));

  fbl::unique_fd fd(open(info->path, O_CREAT | O_RDWR));
  ASSERT_TRUE(fd);

  ASSERT_EQ(0, ftruncate(fd.get(), info->size_data));

  const int kNumWrites = 128;
  size_t write_size = info->size_data / kNumWrites;
  for (size_t written = 0; written < info->size_data; written += write_size) {
    ASSERT_EQ(0, StreamAll(write, fd.get(), info->data.get() + written, write_size),
              "iteration %lu", written / write_size);
  }

  fd.reset();
  fd.reset(open(info->path, O_RDONLY));
  ASSERT_TRUE(fd);
  ASSERT_NO_FAILURES(VerifyContents(fd.get(), info->data.get(), info->size_data));
}

TEST_F(BlobfsTest, MultipleWrites) { RunMultipleWritesTest(); }

TEST_F(BlobfsTestWithFvm, MultipleWrites) { RunMultipleWritesTest(); }

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

void RunQueryDevicePathTest(const std::string& device_path) {
  fbl::unique_fd root_fd(open(kMountPath, O_RDONLY | O_ADMIN));
  ASSERT_TRUE(root_fd, "Cannot open root directory");

  std::string path;
  ASSERT_OK(DirectoryAdminGetDevicePath(std::move(root_fd), &path));
  ASSERT_FALSE(path.empty());

  ASSERT_STR_EQ(device_path.c_str(), path.c_str());
  printf("device_path %s\n", device_path.c_str());

  root_fd.reset(open(kMountPath, O_RDONLY));
  ASSERT_TRUE(root_fd, "Cannot open root directory");

  ASSERT_STATUS(ZX_ERR_ACCESS_DENIED, DirectoryAdminGetDevicePath(std::move(root_fd), &path));
}

TEST_F(BlobfsTest, QueryDevicePath) {
  RunQueryDevicePathTest(environment_->GetRelativeDevicePath());
}

TEST_F(BlobfsTestWithFvm, QueryDevicePath) {
  // Make sure the two paths to compare are in the same form.
  RunQueryDevicePathTest(GetTopologicalPath(device_path()));
}

void RunReadOnlyTest(FilesystemTest* test) {
  // Mount the filesystem as read-write. We can create new blobs.
  std::unique_ptr<BlobInfo> info;
  ASSERT_NO_FAILURES(GenerateRandomBlob(kMountPath, 1 << 10, &info));
  fbl::unique_fd blob_fd;
  ASSERT_NO_FAILURES(MakeBlob(info.get(), &blob_fd));
  ASSERT_NO_FAILURES(VerifyContents(blob_fd.get(), info->data.get(), info->size_data));
  blob_fd.reset();

  test->set_read_only(true);
  ASSERT_NO_FAILURES(test->Remount());

  // We can read old blobs
  blob_fd.reset(open(info->path, O_RDONLY));
  ASSERT_TRUE(blob_fd);
  ASSERT_NO_FAILURES(VerifyContents(blob_fd.get(), info->data.get(), info->size_data));

  // We cannot create new blobs
  ASSERT_NO_FAILURES(GenerateRandomBlob(kMountPath, 1 << 10, &info));
  blob_fd.reset(open(info->path, O_CREAT | O_RDWR));
  ASSERT_FALSE(blob_fd);
}

TEST_F(BlobfsTest, ReadOnly) { RunReadOnlyTest(this); }

TEST_F(BlobfsTestWithFvm, ReadOnly) { RunReadOnlyTest(this); }

void OpenBlockDevice(const std::string& path,
                     std::unique_ptr<block_client::RemoteBlockDevice>* block_device) {
  fbl::unique_fd fd(open(path.c_str(), O_RDWR));
  ASSERT_TRUE(fd, "Unable to open block device");

  zx::channel channel, server;
  ASSERT_OK(zx::channel::create(0, &channel, &server));
  fdio_cpp::FdioCaller caller(std::move(fd));
  ASSERT_OK(fio::Node::Call::Clone(zx::unowned_channel(caller.borrow_channel()),
                                   fio::CLONE_FLAG_SAME_RIGHTS, std::move(server))
                .status());
  ASSERT_OK(block_client::RemoteBlockDevice::Create(std::move(channel), block_device));
}

using SliceRange = fuchsia_hardware_block_volume_VsliceRange;

uint64_t BlobfsBlockToFvmSlice(uint64_t block) {
  constexpr size_t kBlocksPerSlice = kTestFvmSliceSize / blobfs::kBlobfsBlockSize;
  return block / kBlocksPerSlice;
}

void GetSliceRange(const BlobfsTestWithFvm& test, const std::vector<uint64_t>& slices,
                   std::vector<SliceRange>* ranges) {
  std::unique_ptr<block_client::RemoteBlockDevice> block_device;
  ASSERT_NO_FAILURES(OpenBlockDevice(test.device_path(), &block_device));

  size_t ranges_count;
  SliceRange range_array[fuchsia_hardware_block_volume_MAX_SLICE_REQUESTS];
  ASSERT_OK(
      block_device->VolumeQuerySlices(slices.data(), slices.size(), range_array, &ranges_count));
  ranges->clear();
  for (size_t i = 0; i < ranges_count; i++) {
    ranges->push_back(range_array[i]);
  }
}

// The helper creates a blob with data of size disk_size. The data is
// compressible so needs less space on disk. This will test if we can persist
// a blob whose uncompressed data is larger than available free space.
// The test is expected to fail when compression is turned off.
void RunUncompressedBlobDataLargerThanAvailableSpaceTest(FilesystemTest* test, uint64_t disk_size) {
  std::unique_ptr<BlobInfo> info;
  ASSERT_NO_FAILURES(GenerateBlob([](char* data, size_t length) { memset(data, '\0', length); },
                                  kMountPath, disk_size + 1, &info));

  fbl::unique_fd fd;
  ASSERT_NO_FAILURES(MakeBlob(info.get(), &fd),
                     "Test is expected to fail when compression is turned off");

  // We can re-open and verify the Blob as read-only.
  fd.reset(open(info->path, O_RDONLY));
  ASSERT_TRUE(fd, "Failed to-reopen blob");
  ASSERT_NO_FAILURES(VerifyContents(fd.get(), info->data.get(), info->size_data));

  // Force decompression by remounting, re-accessing blob.
  ASSERT_NO_FAILURES(test->Remount());
  fd.reset(open(info->path, O_RDONLY));
  ASSERT_TRUE(fd, "Failed to-reopen blob");
  ASSERT_NO_FAILURES(VerifyContents(fd.get(), info->data.get(), info->size_data));

  ASSERT_EQ(0, unlink(info->path));
}

TEST_F(BlobfsTest, BlobLargerThanAvailableSpaceTest) {
  RunUncompressedBlobDataLargerThanAvailableSpaceTest(this, environment_->disk_size());
}

TEST_F(BlobfsTestWithFvm, BlobLargerThanAvailableSpaceTest) {
  RunUncompressedBlobDataLargerThanAvailableSpaceTest(this, environment_->disk_size());
}

// This tests growing both additional inodes and data blocks.
TEST_F(BlobfsTestWithFvm, ResizePartition) {
  ASSERT_NO_FAILURES(Unmount());
  std::vector<SliceRange> slices;
  std::vector<uint64_t> query = {BlobfsBlockToFvmSlice(blobfs::kFVMNodeMapStart),
                                 BlobfsBlockToFvmSlice(blobfs::kFVMDataStart)};
  ASSERT_NO_FAILURES(GetSliceRange(*this, query, &slices));
  ASSERT_EQ(2, slices.size());
  EXPECT_TRUE(slices[0].allocated);
  EXPECT_EQ(1, slices[0].count);
  EXPECT_TRUE(slices[1].allocated);
  EXPECT_EQ(2, slices[1].count);
  ASSERT_NO_FAILURES(Mount());

  size_t required = kTestFvmSliceSize / blobfs::kBlobfsInodeSize + 2;
  for (size_t i = 0; i < required; i++) {
    std::unique_ptr<BlobInfo> info;
    ASSERT_NO_FAILURES(GenerateRandomBlob(kMountPath, blobfs::kBlobfsInodeSize, &info));

    fbl::unique_fd fd;
    ASSERT_NO_FAILURES(MakeBlob(info.get(), &fd));
  }

  // Remount partition.
  ASSERT_NO_FAILURES(Remount(), "Could not re-mount blobfs");

  ASSERT_NO_FAILURES(Unmount());
  ASSERT_NO_FAILURES(GetSliceRange(*this, query, &slices));
  ASSERT_EQ(2, slices.size());
  EXPECT_TRUE(slices[0].allocated);
  EXPECT_LT(1, slices[0].count);
  EXPECT_TRUE(slices[1].allocated);
  EXPECT_LT(2, slices[1].count);
}

void FvmShrink(const std::string& path, uint64_t offset, uint64_t length) {
  std::unique_ptr<block_client::RemoteBlockDevice> block_device;
  ASSERT_NO_FAILURES(OpenBlockDevice(path, &block_device));
  ASSERT_OK(block_device->VolumeShrink(offset, length));
}

void FvmExtend(const std::string& path, uint64_t offset, uint64_t length) {
  std::unique_ptr<block_client::RemoteBlockDevice> block_device;
  ASSERT_NO_FAILURES(OpenBlockDevice(path, &block_device));
  ASSERT_OK(block_device->VolumeExtend(offset, length));
}

TEST_F(BlobfsTestWithFvm, CorruptAtMount) {
  ASSERT_NO_FAILURES(Unmount());

  // Shrink slice so FVM will differ from Blobfs.
  uint64_t offset = BlobfsBlockToFvmSlice(blobfs::kFVMNodeMapStart);
  ASSERT_NO_FAILURES(FvmShrink(device_path(), offset, 1));

  fbl::unique_fd fd(open(device_path().c_str(), O_RDWR));
  ASSERT_TRUE(fd);

  mount_options_t options = default_mount_options;
  ASSERT_NOT_OK(mount(fd.release(), mount_path(), format_type(), &options, launch_stdio_async));

  // Grow slice count to twice what it should be.
  ASSERT_NO_FAILURES(FvmExtend(device_path(), offset, 2));

  ASSERT_NO_FAILURES(Mount());
  ASSERT_NO_FAILURES(Unmount());

  // Verify that mount automatically removed the extra slice.
  std::vector<SliceRange> slices;
  std::vector<uint64_t> query = {BlobfsBlockToFvmSlice(blobfs::kFVMNodeMapStart)};
  ASSERT_NO_FAILURES(GetSliceRange(*this, query, &slices));
  ASSERT_EQ(1, slices.size());
  EXPECT_TRUE(slices[0].allocated);
  EXPECT_EQ(1, slices[0].count);
}

void RunFailedWriteTest(const RamDisk* disk) {
  if (!disk) {
    return;
  }

  uint32_t page_size = disk->page_size();
  const uint32_t pages_per_block = blobfs::kBlobfsBlockSize / page_size;

  std::unique_ptr<BlobInfo> info;
  ASSERT_NO_FAILURES(GenerateRandomBlob(kMountPath, blobfs::kBlobfsBlockSize, &info));

  fbl::unique_fd fd(open(info->path, O_CREAT | O_RDWR));
  ASSERT_TRUE(fd, "Failed to create blob");

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
  ASSERT_OK(disk->SleepAfter(pages_per_block * (kBlockCountToWrite - 1)));
  ASSERT_EQ(write(fd.get(), info->data.get(), info->size_data),
            static_cast<ssize_t>(info->size_data));

  // Since the write operation ultimately failed when going out to disk,
  // syncfs will return a failed response.
  ASSERT_LT(syncfs(fd.get()), 0);

  ASSERT_NO_FAILURES(GenerateRandomBlob(kMountPath, blobfs::kBlobfsBlockSize, &info));
  fd.reset(open(info->path, O_CREAT | O_RDWR));
  ASSERT_TRUE(fd, "Failed to create blob");

  // On an FVM, truncate may either succeed or fail. If an FVM extend call is necessary,
  // it will fail since the ramdisk is asleep; otherwise, it will pass.
  ftruncate(fd.get(), info->size_data);

  // Since the ramdisk is asleep and our blobfs is aware of it due to the sync, write should fail.
  // TODO(smklein): Implement support for "failed write propagates to the client before
  // sync".
  // ASSERT_LT(write(fd.get(), info->data.get(), blobfs::kBlobfsBlockSize), 0);

  ASSERT_OK(disk->WakeUp());
}

TEST_F(BlobfsTest, FailedWrite) {
  ASSERT_NO_FAILURES(RunFailedWriteTest(environment_->ramdisk()));

  // Force journal replay.
  Remount();
}

TEST_F(BlobfsTestWithFvm, FailedWrite) {
  ASSERT_NO_FAILURES(RunFailedWriteTest(environment_->ramdisk()));

  // Force journal replay.
  Remount();
}

struct CloneThreadArgs {
  const BlobInfo* info = nullptr;
  std::atomic_bool done{false};
};

void CloneThread(CloneThreadArgs* args) {
  while (!args->done) {
    fbl::unique_fd fd(open(args->info->path, O_RDONLY));
    ASSERT_TRUE(fd);
    void* addr = mmap(NULL, args->info->size_data, PROT_READ, MAP_PRIVATE, fd.get(), 0);
    ASSERT_NE(addr, MAP_FAILED, "Could not mmap blob");
    // Explicitly close |fd| before unmapping.
    fd.reset();
    // Yielding before unmapping significantly improves the ability of this test to detect bugs
    // (e.g. fxbug.dev/53882) by increasing the length of time that the file is closed but still has a
    // VMO clone.
    zx_nanosleep(0);
    ASSERT_EQ(0, munmap(addr, args->info->size_data));
  }
}

// This test ensures that blobfs' lifecycle management correctly deals with a highly volatile
// number of VMO clones (which blobfs has special logic to handle, preventing the in-memory
// blob from being discarded while there are active clones).
// See fxbug.dev/53882 for background on this test case.
void RunVmoCloneWatchingTest(const RamDisk* disk) {
  if (!disk) {
    return;
  }

  std::unique_ptr<BlobInfo> info;
  ASSERT_NO_FAILURES(GenerateBlob(CharFill<'A'>, kMountPath, 4096, &info));

  {
    fbl::unique_fd fd;
    ASSERT_NO_FAILURES(MakeBlob(info.get(), &fd));
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
    ASSERT_NE(addr, MAP_FAILED, "Could not mmap blob");
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

TEST_F(BlobfsTest, VmoCloneWatchingTest) {
  ASSERT_NO_FAILURES(RunVmoCloneWatchingTest(environment_->ramdisk()));
}

TEST_F(BlobfsTestWithFvm, VmoCloneWatchingTest) {
  ASSERT_NO_FAILURES(RunVmoCloneWatchingTest(environment_->ramdisk()));
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
  ASSERT_NOT_NULL(executor);
  ASSERT_NOT_NULL(blobs_created);

  fuchsia::inspect::TreePtr tree;
  async_dispatcher_t* dispatcher = executor->dispatcher();
  ASSERT_OK(fdio_service_connect_at(diagnostics_dir, "fuchsia.inspect.Tree",
                                    tree.NewRequest(dispatcher).TakeChannel().release()));

  fit::result<inspect::Hierarchy> hierarchy_or_error = TakeSnapshot(std::move(tree), executor);
  ASSERT_TRUE(hierarchy_or_error.is_ok());
  inspect::Hierarchy hierarchy = std::move(hierarchy_or_error.value());

  const inspect::Hierarchy* allocation_stats = hierarchy.GetByPath({"allocation_stats"});
  ASSERT_NOT_NULL(allocation_stats);

  const inspect::UintPropertyValue* blobs_created_value =
      allocation_stats->node().get_property<inspect::UintPropertyValue>("blobs_created");
  ASSERT_NOT_NULL(blobs_created_value);

  *blobs_created = blobs_created_value->value();
}

TEST_F(FdioTest, AllocateIncrementsMetricTest) {
  async::Loop loop = async::Loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  loop.StartThread("allocate-increments-metric-thread");
  async::Executor executor(loop.dispatcher());

  uint64_t blobs_created;
  ASSERT_NO_FAILURES(GetBlobsCreated(&executor, diagnostics_dir(), &blobs_created));
  ASSERT_EQ(blobs_created, 0);

  // Create a new blob with random contents on the mounted filesystem.
  std::unique_ptr<blobfs::BlobInfo> info;
  ASSERT_NO_FAILURES(GenerateRandomBlob(".", 1 << 8, &info));
  fbl::unique_fd fd(openat(root_fd(), info->path, O_CREAT | O_RDWR));
  ASSERT_TRUE(fd.is_valid());
  ASSERT_EQ(ftruncate(fd.get(), info->size_data), 0);
  ASSERT_EQ(blobfs::StreamAll(write, fd.get(), info->data.get(), info->size_data), 0,
            "Failed to write Data");

  ASSERT_NO_FAILURES(GetBlobsCreated(&executor, diagnostics_dir(), &blobs_created));
  ASSERT_EQ(blobs_created, 1);

  loop.Quit();
  loop.JoinThreads();
}

}  // namespace
