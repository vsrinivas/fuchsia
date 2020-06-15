// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <thread>

#include <blobfs/common.h>
#include <fbl/auto_call.h>
#include <fvm/format.h>
#include <zxtest/zxtest.h>

#include "blobfs_fixtures.h"
#include "load_generator.h"

namespace {

namespace fio = ::llcpp::fuchsia::io;

using blobfs::BlobInfo;
using blobfs::GenerateBlob;
using blobfs::GenerateRandomBlob;
using blobfs::RandomFill;
using blobfs::StreamAll;
using blobfs::VerifyContents;
using fs::FilesystemTest;
using fs::RamDisk;

void RunHugeBlobRandomTest(FilesystemTest* test) {
  std::unique_ptr<BlobInfo> info;

  // This blob is extremely large, and will remain large on disk.
  // It is not easily compressible.
  size_t kMaxSize = 1 << 25;  // 32 MB.
  size_t file_size = std::min(kMaxSize, 2 * blobfs::WriteBufferSize() * blobfs::kBlobfsBlockSize);
  ASSERT_NO_FAILURES(GenerateRandomBlob(kMountPath, file_size, &info));

  fbl::unique_fd fd;
  ASSERT_NO_FAILURES(MakeBlob(info.get(), &fd));

  // We can re-open and verify the Blob as read-only.
  fd.reset(open(info->path, O_RDONLY));
  ASSERT_TRUE(fd, "Failed to-reopen blob");
  ASSERT_NO_FAILURES(VerifyContents(fd.get(), info->data.get(), info->size_data));

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
  ASSERT_NO_FAILURES(VerifyContents(fd.get(), info->data.get(), info->size_data));

  ASSERT_EQ(0, unlink(info->path));
}

TEST_F(BlobfsTest, HugeBlobRandom) { RunHugeBlobRandomTest(this); }

TEST_F(BlobfsTestWithFvm, HugeBlobRandom) { RunHugeBlobRandomTest(this); }

void RunHugeBlobCompressibleTest(FilesystemTest* test) {
  std::unique_ptr<BlobInfo> info;

  // This blob is extremely large, and will remain large on disk, even though
  // it is very compressible.
  size_t kMaxSize = 1 << 25;  // 32 MB.
  size_t file_size = std::min(kMaxSize, 2 * blobfs::WriteBufferSize() * blobfs::kBlobfsBlockSize);
  ASSERT_NO_FAILURES(GenerateBlob(
      [](char* data, size_t length) {
        RandomFill(data, length / 2);
        data += length / 2;
        memset(data, 'a', length / 2);
      },
      kMountPath, file_size, &info));

  fbl::unique_fd fd;
  ASSERT_NO_FAILURES(MakeBlob(info.get(), &fd));

  // We can re-open and verify the Blob as read-only.
  fd.reset(open(info->path, O_RDONLY));
  ASSERT_TRUE(fd, "Failed to-reopen blob");
  ASSERT_NO_FAILURES(VerifyContents(fd.get(), info->data.get(), info->size_data));

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
  ASSERT_NO_FAILURES(VerifyContents(fd.get(), info->data.get(), info->size_data));
}

TEST_F(BlobfsTest, HugeBlobCompressible) { RunHugeBlobCompressibleTest(this); }

TEST_F(BlobfsTestWithFvm, HugeBlobCompressible) { RunHugeBlobCompressibleTest(this); }

void RunSingleThreadStressTest(FilesystemTest* test) {
  BlobList blob_list(kMountPath);
  unsigned int seed = zxtest::Runner::GetInstance()->random_seed();
  blob_list.GenerateLoad(5000, &seed);

  blob_list.CloseFiles();
  ASSERT_NO_FAILURES(test->Remount());

  blob_list.VerifyFiles();
}

TEST_F(BlobfsTest, SingleThreadStress) { RunSingleThreadStressTest(this); }

TEST_F(BlobfsTestWithFvm, SingleThreadStress) { RunSingleThreadStressTest(this); }

void StressThread(BlobList* blob_list, unsigned int seed) {
  unsigned int rand_state = seed;
  blob_list->GenerateLoad(1000, &rand_state);
}

void RunMultiThreadStressTest(FilesystemTest* test) {
  BlobList blob_list(kMountPath);
  unsigned int seed = zxtest::Runner::GetInstance()->random_seed();

  std::array<std::thread, 10> threads;
  for (std::thread& thread : threads) {
    thread = std::thread(StressThread, &blob_list, rand_r(&seed));
  }

  for (std::thread& thread : threads) {
    thread.join();
  }

  blob_list.CloseFiles();
  ASSERT_NO_FAILURES(test->Remount());

  blob_list.VerifyFiles();
}

TEST_F(BlobfsTest, MultiThreadStress) { RunMultiThreadStressTest(this); }

TEST_F(BlobfsTestWithFvm, MultiThreadStress) { RunMultiThreadStressTest(this); }

void MakeBlobUnverified(BlobInfo* info, fbl::unique_fd* out_fd) {
  fbl::unique_fd fd(open(info->path, O_CREAT | O_RDWR));
  ASSERT_TRUE(fd, "Failed to create blob");
  ASSERT_EQ(ftruncate(fd.get(), info->size_data), 0);
  ASSERT_EQ(StreamAll(write, fd.get(), info->data.get(), info->size_data), 0,
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
  fprintf(stderr, "Reopened %d times\n", attempts);
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

  std::unique_ptr<BlobInfo> anchor_info;
  ASSERT_NO_FAILURES(GenerateRandomBlob(kMountPath, 1 << 10, &anchor_info));

  std::unique_ptr<BlobInfo> info;
  ASSERT_NO_FAILURES(GenerateRandomBlob(kMountPath, 10 * (1 << 20), &info));
  std::string path(info->path);

  for (size_t i = 0; i < num_ops; i++) {
    fprintf(stderr, "Running op %lu...\n", i);
    fbl::unique_fd fd;
    fbl::unique_fd anchor_fd;

    // Write both blobs to disk (without verification, so we can start reopening the blob asap).
    ASSERT_NO_FAILURES(MakeBlobUnverified(info.get(), &fd));
    ASSERT_NO_FAILURES(MakeBlobUnverified(anchor_info.get(), &anchor_fd));
    fd.reset();

    // Launch a background thread to wait for the file to become readable.
    std::atomic_bool done = false;
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
  std::unique_ptr<BlobInfo> info;
  ASSERT_NO_FAILURES(GenerateRandomBlob(kMountPath, blobfs::kBlobfsBlockSize, &info));

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
void RelaxedMakeBlob(const BlobInfo* info, fbl::unique_fd* fd) {
  fd->reset(open(info->path, O_CREAT | O_RDWR));
  ASSERT_TRUE(*fd);
  if (ftruncate(fd->get(), info->size_data) < 0) {
    return;
  }
  StreamAll(write, fd->get(), info->data.get(), info->size_data);
}

TEST_F(BlobfsTestWithFvm, ExtendFailure) {
  const RamDisk* ramdisk = environment_->ramdisk();
  if (!ramdisk) {
    return;
  }

  fio::FilesystemInfo original_usage;
  ASSERT_NO_FAILURES(GetFsInfo(&original_usage));

  // Create a blob of the maximum size possible without causing an FVM extension.
  std::unique_ptr<BlobInfo> old_info;
  ASSERT_NO_FAILURES(GenerateRandomBlob(
      kMountPath, original_usage.total_bytes - blobfs::kBlobfsBlockSize, &old_info));

  fbl::unique_fd fd;
  ASSERT_NO_FAILURES(MakeBlob(old_info.get(), &fd));
  ASSERT_EQ(syncfs(fd.get()), 0);
  fd.reset();

  // Ensure that an FVM extension did not occur.
  fio::FilesystemInfo current_usage;
  ASSERT_NO_FAILURES(GetFsInfo(&current_usage));
  ASSERT_EQ(current_usage.total_bytes, original_usage.total_bytes);

  // Generate another blob of the smallest size possible.
  std::unique_ptr<BlobInfo> new_info;
  ASSERT_NO_FAILURES(GenerateRandomBlob(kMountPath, blobfs::kBlobfsBlockSize, &new_info));

  // Since the FVM metadata covers a large range of blocks, it will take a while to test a
  // ramdisk failure after each individual block. Since we mostly care about what happens with
  // blobfs after the extension succeeds on the FVM side, test a maximum of |metadata_failures|
  // failures within the FVM metadata write itself.
  size_t metadata_size = fvm::MetadataSize(environment_->disk_size(), kTestFvmSliceSize);
  uint32_t metadata_blocks = static_cast<uint32_t>(metadata_size / ramdisk->page_size());
  uint32_t metadata_failures = 16;
  uint32_t increment = metadata_blocks / std::min(metadata_failures, metadata_blocks);

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

}  // namespace
