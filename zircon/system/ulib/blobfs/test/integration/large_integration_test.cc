// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <fcntl.h>
#include <lib/fzl/fdio.h>
#include <sys/stat.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <thread>

#include <blobfs/common.h>
#include <fvm/format.h>
#include <zxtest/zxtest.h>

#include "blobfs_fixtures.h"

namespace {

using fs::FilesystemTest;
using fs::RamDisk;

void RunHugeBlobRandomTest(FilesystemTest* test) {
  std::unique_ptr<fs_test_utils::BlobInfo> info;

  // This blob is extremely large, and will remain large on disk.
  // It is not easily compressible.
  size_t kMaxSize = 1 << 25;  // 32 MB.
  size_t file_size = std::min(kMaxSize, 2 * blobfs::WriteBufferSize() * blobfs::kBlobfsBlockSize);
  ASSERT_TRUE(fs_test_utils::GenerateRandomBlob(kMountPath, file_size, &info));

  fbl::unique_fd fd;
  ASSERT_NO_FAILURES(MakeBlob(info.get(), &fd));

  // We can re-open and verify the Blob as read-only.
  fd.reset(open(info->path, O_RDONLY));
  ASSERT_TRUE(fd, "Failed to-reopen blob");
  ASSERT_TRUE(fs_test_utils::VerifyContents(fd.get(), info->data.get(), info->size_data));

  // We cannot re-open the blob as writable.
  fd.reset(open(info->path, O_RDWR | O_CREAT));
  ASSERT_FALSE(fd, "Shouldn't be able to re-create blob that exists");
  fd.reset(open(info->path, O_RDWR));
  ASSERT_FALSE(fd, "Shouldn't be able to re-open blob as writable");
  fd.reset(open(info->path, O_WRONLY));
  ASSERT_FALSE(fd, "Shouldn't be able to re-open blob as writable");

  // Force decompression by remounting, re-accessing blob.
  ASSERT_NO_FAILURES(test->Remount());
  fd.reset(open(info->path, O_RDONLY));
  ASSERT_TRUE(fd, "Failed to-reopen blob");
  ASSERT_TRUE(fs_test_utils::VerifyContents(fd.get(), info->data.get(), info->size_data));

  ASSERT_EQ(0, unlink(info->path));
}

TEST_F(BlobfsTest, HugeBlobRandom) { RunHugeBlobRandomTest(this); }

TEST_F(BlobfsTestWithFvm, HugeBlobRandom) { RunHugeBlobRandomTest(this); }

void RunHugeBlobCompressibleTest(FilesystemTest* test) {
  std::unique_ptr<fs_test_utils::BlobInfo> info;

  // This blob is extremely large, and will remain large on disk, even though
  // it is very compressible.
  size_t kMaxSize = 1 << 25;  // 32 MB.
  size_t file_size = std::min(kMaxSize, 2 * blobfs::WriteBufferSize() * blobfs::kBlobfsBlockSize);
  ASSERT_TRUE(fs_test_utils::GenerateBlob(
      [](char* data, size_t length) {
        fs_test_utils::RandomFill(data, length / 2);
        data += length / 2;
        memset(data, 'a', length / 2);
      },
      kMountPath, file_size, &info));

  fbl::unique_fd fd;
  ASSERT_NO_FAILURES(MakeBlob(info.get(), &fd));

  // We can re-open and verify the Blob as read-only.
  fd.reset(open(info->path, O_RDONLY));
  ASSERT_TRUE(fd, "Failed to-reopen blob");
  ASSERT_TRUE(fs_test_utils::VerifyContents(fd.get(), info->data.get(), info->size_data));

  // We cannot re-open the blob as writable.
  fd.reset(open(info->path, O_RDWR | O_CREAT));
  ASSERT_FALSE(fd, "Shouldn't be able to re-create blob that exists");
  fd.reset(open(info->path, O_RDWR));
  ASSERT_FALSE(fd, "Shouldn't be able to re-open blob as writable");
  fd.reset(open(info->path, O_WRONLY));
  ASSERT_FALSE(fd, "Shouldn't be able to re-open blob as writable");

  // Force decompression by remounting, re-accessing blob.
  ASSERT_NO_FAILURES(test->Remount());
  fd.reset(open(info->path, O_RDONLY));
  ASSERT_TRUE(fd, "Failed to-reopen blob");
  ASSERT_TRUE(fs_test_utils::VerifyContents(fd.get(), info->data.get(), info->size_data));
}

TEST_F(BlobfsTest, HugeBlobCompressible) { RunHugeBlobCompressibleTest(this); }

TEST_F(BlobfsTestWithFvm, HugeBlobCompressible) { RunHugeBlobCompressibleTest(this); }

/*

static bool CreateUmountRemountLarge(BlobfsTest* blobfsTest) {
    BEGIN_HELPER;
    fs_test_utils::BlobList bl(MOUNT_PATH);
    // TODO(smklein): Here, and elsewhere in this file, remove this source
    // of randomness to make the unit test deterministic -- fuzzing should
    // be the tool responsible for introducing randomness into the system.
    unsigned int seed = static_cast<unsigned int>(zx_ticks_get());
    unittest_printf("unmount_remount test using seed: %u\n", seed);

    // Do some operations...
    size_t num_ops = 5000;
    for (size_t i = 0; i < num_ops; ++i) {
        switch (rand_r(&seed) % 6) {
        case 0:
            ASSERT_TRUE(bl.CreateBlob(&seed));
            break;
        case 1:
            ASSERT_TRUE(bl.ConfigBlob());
            break;
        case 2:
            ASSERT_TRUE(bl.WriteData());
            break;
        case 3:
            ASSERT_TRUE(bl.ReadData());
            break;
        case 4:
            ASSERT_TRUE(bl.ReopenBlob());
            break;
        case 5:
            ASSERT_TRUE(bl.UnlinkBlob());
            break;
        }
    }

    // Close all currently opened nodes (REGARDLESS of their state)
    bl.CloseAll();

    // Unmount, remount
    ASSERT_TRUE(blobfsTest->Remount(), "Could not re-mount blobfs");

    // Reopen all (readable) blobs
    bl.OpenAll();

    // Verify state of all blobs
    bl.VerifyAll();

    // Close everything again
    bl.CloseAll();

    END_HELPER;
}

int unmount_remount_thread(void* arg) {
    fs_test_utils::BlobList* bl = static_cast<fs_test_utils::BlobList*>(arg);
    unsigned int seed = static_cast<unsigned int>(zx_ticks_get());
    unittest_printf("unmount_remount thread using seed: %u\n", seed);

    // Do some operations...
    size_t num_ops = 1000;
    for (size_t i = 0; i < num_ops; ++i) {
        switch (rand_r(&seed) % 6) {
        case 0:
            ASSERT_TRUE(bl->CreateBlob(&seed));
            break;
        case 1:
            ASSERT_TRUE(bl->ConfigBlob());
            break;
        case 2:
            ASSERT_TRUE(bl->WriteData());
            break;
        case 3:
            ASSERT_TRUE(bl->ReadData());
            break;
        case 4:
            ASSERT_TRUE(bl->ReopenBlob());
            break;
        case 5:
            ASSERT_TRUE(bl->UnlinkBlob());
            break;
        }
    }

    return 0;
}

static bool CreateUmountRemountLargeMultithreaded(BlobfsTest* blobfsTest) {
    BEGIN_HELPER;
    fs_test_utils::BlobList bl(MOUNT_PATH);

    size_t num_threads = 10;
    fbl::AllocChecker ac;
    fbl::Array<thrd_t> threads(new (&ac) thrd_t[num_threads](), num_threads);
    ASSERT_TRUE(ac.check());

    // Launch all threads
    for (size_t i = 0; i < num_threads; i++) {
        ASSERT_EQ(thrd_create(&threads[i], unmount_remount_thread, &bl),
                  thrd_success);
    }

    // Wait for all threads to complete.
    // Currently, threads will always return a successful status.
    for (size_t i = 0; i < num_threads; i++) {
        int res;
        ASSERT_EQ(thrd_join(threads[i], &res), thrd_success);
        ASSERT_EQ(res, 0);
    }

    // Close all currently opened nodes (REGARDLESS of their state)
    bl.CloseAll();

    // Unmount, remount
    ASSERT_TRUE(blobfsTest->Remount(), "Could not re-mount blobfs");

    // reopen all blobs
    bl.OpenAll();

    // verify all blob contents
    bl.VerifyAll();

    // close everything again
    bl.CloseAll();

    END_HELPER;
}

*/

void RunNoSpaceTest() {
  std::unique_ptr<fs_test_utils::BlobInfo> last_info = nullptr;

  // Keep generating blobs until we run out of space.
  size_t count = 0;
  while (true) {
    std::unique_ptr<fs_test_utils::BlobInfo> info;
    ASSERT_TRUE(fs_test_utils::GenerateRandomBlob(kMountPath, 1 << 17, &info));

    fbl::unique_fd fd(open(info->path, O_CREAT | O_RDWR));
    ASSERT_TRUE(fd, "Failed to create blob");
    int r = ftruncate(fd.get(), info->size_data);
    if (r < 0) {
      ASSERT_EQ(errno, ENOSPC, "Blobfs expected to run out of space");
      // We ran out of space, as expected. Can we allocate if we
      // unlink a previously allocated blob of the desired size?
      ASSERT_EQ(unlink(last_info->path), 0, "Unlinking old blob");
      ASSERT_EQ(ftruncate(fd.get(), info->size_data), 0, "Re-init after unlink");

      // Yay! allocated successfully.
      break;
    }
    ASSERT_EQ(fs_test_utils::StreamAll(write, fd.get(), info->data.get(), info->size_data), 0,
              "Failed to write Data");
    last_info = std::move(info);

    if (++count % 50 == 0) {
      printf("Allocated %lu blobs\n", count);
    }
  }
}

TEST_F(BlobfsTest, NoSpace) { RunNoSpaceTest(); }

TEST_F(BlobfsTestWithFvm, NoSpace) { RunNoSpaceTest(); }

// The following test attempts to fragment the underlying blobfs partition
// assuming a trivial linear allocator. A more intelligent allocator may require
// modifications to this test.
void RunFragmentationTest(FilesystemTest* test) {
  // Keep generating blobs until we run out of space, in a pattern of large,
  // small, large, small, large.
  //
  // At the end of  the test, we'll free the small blobs, and observe if it is
  // possible to allocate a larger blob. With a simple allocator and no
  // defragmentation, this would result in a NO_SPACE error.
  constexpr size_t kSmallSize = (1 << 16);
  constexpr size_t kLargeSize = (1 << 17);

  fbl::Vector<fbl::String> small_blobs;

  bool do_small_blob = true;
  size_t count = 0;
  while (true) {
    std::unique_ptr<fs_test_utils::BlobInfo> info;
    ASSERT_TRUE(fs_test_utils::GenerateRandomBlob(kMountPath,
                                                  do_small_blob ? kSmallSize : kLargeSize, &info));
    fbl::unique_fd fd(open(info->path, O_CREAT | O_RDWR));
    ASSERT_TRUE(fd, "Failed to create blob");
    if (ftruncate(fd.get(), info->size_data) < 0) {
      ASSERT_EQ(ENOSPC, errno, "Blobfs expected to run out of space");
      break;
    }
    ASSERT_EQ(0, fs_test_utils::StreamAll(write, fd.get(), info->data.get(), info->size_data),
              "Failed to write Data");
    if (do_small_blob) {
      small_blobs.push_back(fbl::String(info->path));
    }

    do_small_blob = !do_small_blob;

    if (++count % 50 == 0) {
      printf("Allocated %lu blobs\n", count);
    }
  }

  // We have filled up the disk with both small and large blobs.
  // Observe that we cannot add another large blob.
  std::unique_ptr<fs_test_utils::BlobInfo> info;
  ASSERT_TRUE(fs_test_utils::GenerateRandomBlob(kMountPath, kLargeSize, &info));

  // Calculate actual number of blocks required to store the blob (including the merkle tree).
  blobfs::Inode large_inode;
  large_inode.blob_size = kLargeSize;
  size_t kLargeBlocks = blobfs::MerkleTreeBlocks(large_inode) + blobfs::BlobDataBlocks(large_inode);

  // We shouldn't have space (before we try allocating) ...
  fuchsia_io_FilesystemInfo usage;
  ASSERT_NO_FAILURES(test->GetFsInfo(&usage));
  ASSERT_LT(usage.total_bytes - usage.used_bytes, kLargeBlocks * blobfs::kBlobfsBlockSize);

  // ... and we don't have space (as we try allocating).
  fbl::unique_fd fd(open(info->path, O_CREAT | O_RDWR));
  ASSERT_TRUE(fd);
  ASSERT_EQ(-1, ftruncate(fd.get(), info->size_data));
  ASSERT_EQ(ENOSPC, errno, "Blobfs expected to be out of space");

  // Unlink all small blobs -- except for the last one, since we may have free
  // trailing space at the end.
  for (size_t i = 0; i < small_blobs.size() - 1; i++) {
    ASSERT_EQ(0, unlink(small_blobs[i].c_str()), "Unlinking old blob");
  }

  // This asserts an assumption of our test: Freeing these blobs should provide
  // enough space.
  ASSERT_GT(kSmallSize * (small_blobs.size() - 1), kLargeSize);

  // Validate that we have enough space (before we try allocating)...
  ASSERT_NO_FAILURES(test->GetFsInfo(&usage));
  ASSERT_GE(usage.total_bytes - usage.used_bytes, kLargeBlocks * blobfs::kBlobfsBlockSize);

  // Now that blobfs supports extents, verify that we can still allocate a large
  // blob, even if it is fragmented.
  ASSERT_EQ(0, ftruncate(fd.get(), info->size_data));

  // Sanity check that we can write and read the fragmented blob.
  ASSERT_EQ(0, fs_test_utils::StreamAll(write, fd.get(), info->data.get(), info->size_data));
  std::unique_ptr<char[]> buf(new char[info->size_data]);
  ASSERT_EQ(0, lseek(fd.get(), 0, SEEK_SET));
  ASSERT_EQ(0, fs_test_utils::StreamAll(read, fd.get(), buf.get(), info->size_data));
  ASSERT_BYTES_EQ(info->data.get(), buf.get(), info->size_data);

  // Sanity check that we can re-open and unlink the fragmented blob.
  fd.reset(open(info->path, O_RDONLY));
  ASSERT_TRUE(fd);
  ASSERT_EQ(0, unlink(info->path));
}

TEST_F(BlobfsTest, Fragmentation) { RunFragmentationTest(this); }

TEST_F(BlobfsTestWithFvm, Fragmentation) { RunFragmentationTest(this); }

void MakeBlobUnverified(fs_test_utils::BlobInfo* info, fbl::unique_fd* out_fd) {
  fbl::unique_fd fd(open(info->path, O_CREAT | O_RDWR));
  ASSERT_TRUE(fd, "Failed to create blob");
  ASSERT_EQ(ftruncate(fd.get(), info->size_data), 0);
  ASSERT_EQ(fs_test_utils::StreamAll(write, fd.get(), info->data.get(), info->size_data), 0,
            "Failed to write Data");
  out_fd->reset(fd.release());
}

void ReopenThread(const std::string& path, std::atomic_bool* done) {
  int attempts = 0;
  while (!atomic_load(done)) {
    fbl::unique_fd fd(open(path.c_str(), O_RDONLY));
    if (!fd) {
      break;
    }
    attempts++;
  }
  printf("Reopened %d times\n", attempts);
}

// The purpose of this test is to repro the case where a blob is being retrieved from the blob hash
// at the same time it is being destructed, causing an invalid vnode to be returned. This can only
// occur when the client is opening a new fd to the blob at the same time it is being destructed
// after all writes to disk have completed.
// This test works best if a sleep is added at the beginning of fbl_recycle in VnodeBlob.
//
// TODO(rvargas): The description seems to hint that this test should be removed because it's
// not really doing anything (requires adding sleeps in the code); it's trying to protect against
// a regression for a race from too far away.
void RunCreateWriteReopenTest() {
  size_t num_ops = 10;

  std::unique_ptr<fs_test_utils::BlobInfo> anchor_info;
  ASSERT_TRUE(fs_test_utils::GenerateRandomBlob(kMountPath, 1 << 10, &anchor_info));

  std::unique_ptr<fs_test_utils::BlobInfo> info;
  ASSERT_TRUE(fs_test_utils::GenerateRandomBlob(kMountPath, 10 * (1 << 20), &info));
  std::string path(info->path);

  for (size_t i = 0; i < num_ops; i++) {
    printf("Running op %lu... ", i);
    fbl::unique_fd fd;
    fbl::unique_fd anchor_fd;

    // Write both blobs to disk (without verification, so we can start reopening the blob asap).
    ASSERT_NO_FAILURES(MakeBlobUnverified(info.get(), &fd));
    ASSERT_NO_FAILURES(MakeBlobUnverified(anchor_info.get(), &anchor_fd));
    fd.reset();

    // Launch a background thread to wait for the file to become readable.
    std::atomic_bool done;
    std::thread thread(ReopenThread, path, &done);

    {
      // Always join the thread before returning from the test.
      auto join_thread = fbl::MakeAutoCall([&]() {
        done = true;
        thread.join();
      });

      // Sleep while the thread continually opens and closes the blob.
      sleep(1);
      ASSERT_EQ(syncfs(anchor_fd.get()), 0);
    }

    ASSERT_EQ(0, unlink(info->path));
    ASSERT_EQ(0, unlink(anchor_info->path));
  }
}

TEST_F(BlobfsTest, CreateWriteReopen) { RunCreateWriteReopenTest(); }

TEST_F(BlobfsTestWithFvm, CreateWriteReopen) { RunCreateWriteReopenTest(); }

void RunCreateFailureTest(const RamDisk* disk, FilesystemTest* test) {
  std::unique_ptr<fs_test_utils::BlobInfo> info;
  ASSERT_TRUE(fs_test_utils::GenerateRandomBlob(kMountPath, blobfs::kBlobfsBlockSize, &info));

  // Attempt to create a blob, failing after each written block until the operations succeeds.
  // After each failure, check for disk consistency.
  fbl::unique_fd fd;
  for (uint32_t blocks = 0; !fd; blocks++) {
    ASSERT_OK(disk->SleepAfter(blocks));

    // Blob creation may or may not succeed - as long as fsck passes, it doesn't matter.
    MakeBlob(info.get(), &fd);
    ASSERT_NO_FAILURES();

    // Resolve all transactions before waking the ramdisk.
    syncfs(fd.get());
    ASSERT_OK(disk->WakeUp());

    // Remount to check fsck results.
    ASSERT_NO_FAILURES(test->Remount());

    // Once file creation is successful, break out of the loop.
    fd.reset(open(info->path, O_RDONLY));
  }
}

TEST_F(BlobfsTest, CreateFailure) { RunCreateFailureTest(environment_->ramdisk(), this); }

TEST_F(BlobfsTestWithFvm, CreateFailure) { RunCreateFailureTest(environment_->ramdisk(), this); }

// Creates a new blob but (mostly) without complaining about failures.
void RelaxedMakeBlob(const fs_test_utils::BlobInfo* info, fbl::unique_fd* fd) {
  fd->reset(open(info->path, O_CREAT | O_RDWR));
  ASSERT_TRUE(*fd);
  if (ftruncate(fd->get(), info->size_data) < 0) {
    return;
  }
  fs_test_utils::StreamAll(write, fd->get(), info->data.get(), info->size_data);
}

TEST_F(BlobfsTestWithFvm, ExtendFailure) {
  const RamDisk* ramdisk = environment_->ramdisk();
  if (!ramdisk) {
    return;
  }

  fuchsia_io_FilesystemInfo original_usage;
  ASSERT_NO_FAILURES(GetFsInfo(&original_usage));

  // Create a blob of the maximum size possible without causing an FVM extension.
  std::unique_ptr<fs_test_utils::BlobInfo> old_info;
  ASSERT_TRUE(fs_test_utils::GenerateRandomBlob(
      kMountPath, original_usage.total_bytes - blobfs::kBlobfsBlockSize, &old_info));

  fbl::unique_fd fd;
  ASSERT_NO_FAILURES(MakeBlob(old_info.get(), &fd));
  ASSERT_EQ(syncfs(fd.get()), 0);
  fd.reset();

  // Ensure that an FVM extension did not occur.
  fuchsia_io_FilesystemInfo current_usage;
  ASSERT_NO_FAILURES(GetFsInfo(&current_usage));
  ASSERT_EQ(current_usage.total_bytes, original_usage.total_bytes);

  // Generate another blob of the smallest size possible.
  std::unique_ptr<fs_test_utils::BlobInfo> new_info;
  ASSERT_TRUE(fs_test_utils::GenerateRandomBlob(kMountPath, blobfs::kBlobfsBlockSize, &new_info));

  // Since the FVM metadata covers a large range of blocks, it will take a while to test a
  // ramdisk failure after each individual block. Since we mostly care about what happens with
  // blobfs after the extension succeeds on the FVM side, test a maximum of |metadata_failures|
  // failures within the FVM metadata write itself.
  size_t metadata_size = fvm::MetadataSize(environment_->disk_size(), kTestFvmSliceSize);
  uint32_t metadata_blocks = static_cast<uint32_t>(metadata_size / ramdisk->page_size());
  uint32_t metadata_failures = 16;
  uint32_t increment = metadata_blocks / fbl::min(metadata_failures, metadata_blocks);

  // Round down the metadata block count so we don't miss testing the transaction immediately
  // after the metadata write succeeds.
  metadata_blocks = fbl::round_down(metadata_blocks, increment);
  uint32_t blocks = 0;

  while (true) {
    ASSERT_OK(ramdisk->SleepAfter(blocks));

    // Blob creation may or may not succeed - as long as fsck passes, it doesn't matter.
    RelaxedMakeBlob(new_info.get(), &fd);

    // Resolve all transactions before waking the ramdisk.
    syncfs(fd.get());

    ASSERT_OK(ramdisk->WakeUp());

    // Replay the journal.
    Unmount();
    ASSERT_NO_FAILURES(Mount());

    // Remount again to verify integrity.
    ASSERT_NO_FAILURES(Remount());

    // Check that the original blob still exists.
    fd.reset(open(old_info->path, O_RDONLY));
    ASSERT_TRUE(fd);

    // Once file creation is successful, break out of the loop.
    fd.reset(open(new_info->path, O_RDONLY));
    if (fd) {
      struct stat stats;
      ASSERT_EQ(fstat(fd.get(), &stats), 0);
      ASSERT_EQ(static_cast<uint64_t>(stats.st_size), old_info->size_data);
      break;
    }

    if (blocks >= metadata_blocks) {
      blocks++;
    } else {
      blocks += increment;
    }
  }

  // Ensure that an FVM extension occurred.
  ASSERT_NO_FAILURES(GetFsInfo(&current_usage));
  ASSERT_GT(current_usage.total_bytes, original_usage.total_bytes);
}

class LargeBlobTest : public BlobfsFixedDiskSizeTest {
 public:
  LargeBlobTest() : BlobfsFixedDiskSizeTest(GetDiskSize()) {}

  static uint64_t GetDataBlockCount() { return 12 * blobfs::kBlobfsBlockBits / 10; }

 private:
  static uint64_t GetDiskSize() {
    // Create blobfs with enough data blocks to ensure 2 block bitmap blocks.
    // Any number above kBlobfsBlockBits should do, and the larger the
    // number, the bigger the disk (and memory used for the test).
    blobfs::Superblock superblock;
    superblock.flags = 0;
    superblock.inode_count = blobfs::kBlobfsDefaultInodeCount;
    superblock.journal_block_count = blobfs::kDefaultJournalBlocks;
    superblock.data_block_count = GetDataBlockCount();
    return blobfs::TotalBlocks(superblock) * blobfs::kBlobfsBlockSize;
  }
};

TEST_F(LargeBlobTest, UseSecondBitmap) {
  // Create (and delete) a blob large enough to overflow into the second bitmap block.
  std::unique_ptr<fs_test_utils::BlobInfo> info;
  size_t blob_size = ((GetDataBlockCount() / 2) + 1) * blobfs::kBlobfsBlockSize;
  ASSERT_TRUE(fs_test_utils::GenerateRandomBlob(kMountPath, blob_size, &info));

  fbl::unique_fd fd;
  ASSERT_NO_FAILURES(MakeBlob(info.get(), &fd));
  ASSERT_EQ(syncfs(fd.get()), 0);
  ASSERT_EQ(close(fd.release()), 0);
  ASSERT_EQ(unlink(info->path), 0);
}

}  // namespace

/*

BEGIN_TEST_CASE(blobfs_tests)
RUN_TESTS(LARGE, CreateUmountRemountLarge)
RUN_TESTS(LARGE, CreateUmountRemountLargeMultithreaded)
END_TEST_CASE(blobfs_tests)

*/
