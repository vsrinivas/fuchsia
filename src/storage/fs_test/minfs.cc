// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Tests for MinFS-specific behavior.

#include <errno.h>
#include <fcntl.h>
#include <fuchsia/io/llcpp/fidl.h>
#include <fuchsia/minfs/c/fidl.h>
#include <lib/fdio/vfs.h>
#include <lib/fdio/cpp/caller.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
#include <zircon/device/vfs.h>

#include <utility>

#include <fbl/algorithm.h>
#include <fbl/unique_fd.h>
#include <fvm/format.h>
#include <minfs/format.h>
#include <ramdevice-client/ramdisk.h>
#include <unittest/unittest.h>

#include "filesystems.h"
#include "misc.h"

namespace {

namespace fio = ::llcpp::fuchsia::io;

// Using twice as many blocks and slices of half-size, we have just as much space, but we require
// resizing to fill our filesystem.
const test_disk_t kGrowableTestDisk = {
    .block_count = TEST_BLOCK_COUNT_DEFAULT * 2,
    .block_size = TEST_BLOCK_SIZE_DEFAULT,
    .slice_size = TEST_FVM_SLICE_SIZE_DEFAULT / 2,
};

bool QueryInfo(fio::FilesystemInfo* info) {
  BEGIN_HELPER;
  fbl::unique_fd fd(open(kMountPath, O_RDONLY | O_DIRECTORY));
  ASSERT_TRUE(fd);
  // Sync before querying fs so that we can obtain an accurate number of used bytes. Otherwise,
  // blocks which are reserved but not yet allocated won't be counted.
  fsync(fd.get());
  fdio_cpp::FdioCaller caller(std::move(fd));
  auto result = fio::DirectoryAdmin::Call::QueryFilesystem((caller.channel()));
  ASSERT_EQ(result.status(), ZX_OK);
  ASSERT_EQ(result.Unwrap()->s, ZX_OK);
  ASSERT_NOT_NULL(result.Unwrap()->info);
  *info = *(result.Unwrap()->info);
  const char* kFsName = "minfs";
  const char* name = reinterpret_cast<const char*>(info->name.data());
  ASSERT_EQ(strncmp(name, kFsName, strlen(kFsName)), 0, "Unexpected filesystem mounted");
  ASSERT_EQ(info->block_size, minfs::kMinfsBlockSize);
  ASSERT_EQ(info->max_filename_size, minfs::kMinfsMaxNameSize);
  ASSERT_EQ(info->fs_type, VFS_TYPE_MINFS);
  ASSERT_NE(info->fs_id, 0);

  ASSERT_EQ(info->used_bytes % info->block_size, 0);
  ASSERT_EQ(info->total_bytes % info->block_size, 0);
  END_HELPER;
}

// A simple structure used to validate the results of QueryInfo.
struct ExpectedQueryInfo {
  size_t total_bytes;
  size_t used_bytes;
  size_t total_nodes;
  size_t used_nodes;
  size_t free_shared_pool_bytes;
};

bool VerifyQueryInfo(const ExpectedQueryInfo& expected) {
  BEGIN_HELPER;

  fio::FilesystemInfo info;
  ASSERT_TRUE(QueryInfo(&info));
  ASSERT_EQ(info.total_bytes, expected.total_bytes);
  ASSERT_EQ(info.used_bytes, expected.used_bytes);
  ASSERT_EQ(info.total_nodes, expected.total_nodes);
  ASSERT_EQ(info.used_nodes, expected.used_nodes);
  ASSERT_EQ(info.free_shared_pool_bytes, expected.free_shared_pool_bytes);
  END_HELPER;
}

// Verify initial conditions on a filesystem, and validate that filesystem
// modifications adjust the query info accordingly.
bool TestQueryInfo() {
  BEGIN_TEST;

  // This test assumes it is running on a disk with the default slice size.
  const size_t kSliceSize = TEST_FVM_SLICE_SIZE_DEFAULT;
  const size_t kTotalDeviceSize = test_disk_info.block_count * test_disk_info.block_size;

  const size_t kTotalSlices = fvm::UsableSlicesCount(kTotalDeviceSize, kSliceSize);
  const size_t kFreeSlices = kTotalSlices - minfs::kMinfsMinimumSlices;

  ExpectedQueryInfo expected_info = {};
  expected_info.total_bytes = kSliceSize;
  // TODO(ZX-1372): Adjust this once minfs accounting on truncate is fixed.
  expected_info.used_bytes = 2 * minfs::kMinfsBlockSize;
  // The inode table's implementation is currently a flat array on disk.
  expected_info.total_nodes = kSliceSize / sizeof(minfs::Inode);
  // The "zero-th" inode is reserved, as well as the root directory.
  expected_info.used_nodes = 2;
  // The remainder of the FVM should be unused during this filesystem test.
  expected_info.free_shared_pool_bytes = kFreeSlices * kSliceSize;
  ASSERT_TRUE(VerifyQueryInfo(expected_info));

  // Allocate kExtraNodeCount new files, each using truncated (sparse) files.
  const int kExtraNodeCount = 16;
  for (int i = 0; i < kExtraNodeCount; i++) {
    char path[128];
    snprintf(path, sizeof(path) - 1, "%s/file_%d", kMountPath, i);

    fbl::unique_fd fd(open(path, O_CREAT | O_RDWR));
    ASSERT_GT(fd.get(), 0, "Failed to create file");
    ASSERT_EQ(ftruncate(fd.get(), 30 * 1024), 0);
  }

  // Adjust our query expectations: We should see 16 new nodes, but no other
  // difference.
  expected_info.used_nodes += kExtraNodeCount;
  ASSERT_TRUE(VerifyQueryInfo(expected_info));
  END_TEST;
}

bool ToggleMetrics(bool enabled) {
  BEGIN_HELPER;
  fbl::unique_fd fd(open(kMountPath, O_RDONLY | O_DIRECTORY));
  ASSERT_TRUE(fd);
  fdio_cpp::FdioCaller caller(std::move(fd));
  zx_status_t status;
  ASSERT_EQ(fuchsia_minfs_MinfsToggleMetrics(caller.borrow_channel(), enabled, &status), ZX_OK);
  ASSERT_EQ(status, ZX_OK);
  END_HELPER;
}

bool GetMetricsUnavailable() {
  BEGIN_HELPER;
  fbl::unique_fd fd(open(kMountPath, O_RDONLY | O_DIRECTORY));
  ASSERT_TRUE(fd);
  zx_status_t status;
  fdio_cpp::FdioCaller caller(std::move(fd));
  fuchsia_minfs_Metrics metrics;
  ASSERT_EQ(fuchsia_minfs_MinfsGetMetrics(caller.borrow_channel(), &status, &metrics), ZX_OK);
  ASSERT_EQ(status, ZX_ERR_UNAVAILABLE);
  END_HELPER;
}

bool GetMetrics(fuchsia_minfs_Metrics* metrics) {
  BEGIN_HELPER;
  fbl::unique_fd fd(open(kMountPath, O_RDONLY | O_DIRECTORY));
  ASSERT_TRUE(fd);
  zx_status_t status;
  fdio_cpp::FdioCaller caller(std::move(fd));
  ASSERT_EQ(fuchsia_minfs_MinfsGetMetrics(caller.borrow_channel(), &status, metrics), ZX_OK);
  ASSERT_EQ(status, ZX_OK);
  END_HELPER;
}

// Validate that Minfs metrics are functioning correctly.
bool TestMetrics() {
  BEGIN_TEST;

  ASSERT_TRUE(GetMetricsUnavailable());
  ASSERT_TRUE(ToggleMetrics(true));

  fuchsia_minfs_Metrics metrics;
  ASSERT_TRUE(GetMetrics(&metrics));

  ASSERT_EQ(metrics.fs_metrics.create.success.total_calls, 0);
  ASSERT_EQ(metrics.fs_metrics.create.failure.total_calls, 0);

  char path[128];
  snprintf(path, sizeof(path) - 1, "%s/test-file", kMountPath);
  fbl::unique_fd fd(open(path, O_CREAT | O_RDWR));
  ASSERT_TRUE(fd);
  ASSERT_TRUE(GetMetrics(&metrics));
  ASSERT_EQ(metrics.fs_metrics.create.success.total_calls, 1);
  ASSERT_EQ(metrics.fs_metrics.create.failure.total_calls, 0);
  ASSERT_NE(metrics.fs_metrics.create.success.total_time_spent, 0);
  ASSERT_EQ(metrics.fs_metrics.create.failure.total_time_spent, 0);

  fd.reset(open(path, O_CREAT | O_RDWR | O_EXCL));
  ASSERT_FALSE(fd);
  ASSERT_TRUE(GetMetrics(&metrics));
  ASSERT_EQ(metrics.fs_metrics.create.success.total_calls, 1);
  ASSERT_EQ(metrics.fs_metrics.create.failure.total_calls, 1);
  ASSERT_NE(metrics.fs_metrics.create.success.total_time_spent, 0);
  ASSERT_NE(metrics.fs_metrics.create.failure.total_time_spent, 0);

  ASSERT_TRUE(GetMetrics(&metrics));
  ASSERT_EQ(metrics.fs_metrics.unlink.success.total_calls, 0);
  ASSERT_EQ(metrics.fs_metrics.unlink.failure.total_calls, 0);
  ASSERT_EQ(metrics.fs_metrics.unlink.success.total_time_spent, 0);
  ASSERT_EQ(metrics.fs_metrics.unlink.failure.total_time_spent, 0);

  ASSERT_EQ(unlink(path), 0);
  ASSERT_TRUE(GetMetrics(&metrics));
  ASSERT_EQ(metrics.fs_metrics.unlink.success.total_calls, 1);
  ASSERT_EQ(metrics.fs_metrics.unlink.failure.total_calls, 0);
  ASSERT_NE(metrics.fs_metrics.unlink.success.total_time_spent, 0);
  ASSERT_EQ(metrics.fs_metrics.unlink.failure.total_time_spent, 0);

  ASSERT_NE(unlink(path), 0);
  ASSERT_TRUE(GetMetrics(&metrics));
  ASSERT_EQ(metrics.fs_metrics.unlink.success.total_calls, 1);
  ASSERT_EQ(metrics.fs_metrics.unlink.failure.total_calls, 1);
  ASSERT_NE(metrics.fs_metrics.unlink.success.total_time_spent, 0);
  ASSERT_NE(metrics.fs_metrics.unlink.failure.total_time_spent, 0);

  ASSERT_TRUE(ToggleMetrics(false));
  ASSERT_TRUE(GetMetricsUnavailable());

  END_TEST;
}

bool GetFreeBlocks(uint32_t* out_free_blocks) {
  BEGIN_HELPER;
  fio::FilesystemInfo info;
  ASSERT_TRUE(QueryInfo(&info));
  uint64_t total_bytes = info.total_bytes + info.free_shared_pool_bytes;
  uint64_t used_bytes = info.used_bytes;
  *out_free_blocks = static_cast<uint32_t>((total_bytes - used_bytes) / info.block_size);
  END_HELPER;
}

// Write to the file until at most |max_remaining_blocks| remain in the partition.
// Return the new remaining block count as |actual_remaining_blocks|.
bool FillPartition(int fd, uint32_t max_remaining_blocks, uint32_t* actual_remaining_blocks) {
  BEGIN_HELPER;
  char data[minfs::kMinfsBlockSize];
  memset(data, 0xaa, sizeof(data));
  uint32_t free_blocks;

  while (true) {
    ASSERT_TRUE(GetFreeBlocks(&free_blocks));
    if (free_blocks <= max_remaining_blocks) {
      break;
    }

    ASSERT_EQ(write(fd, data, sizeof(data)), sizeof(data));
  }

  ASSERT_LE(free_blocks, max_remaining_blocks);
  ASSERT_GT(free_blocks, 0);

  *actual_remaining_blocks = free_blocks;
  END_HELPER;
}

// Return number of blocks allocated by the file at |fd|.
bool GetFileBlocks(int fd, uint64_t* blocks) {
  BEGIN_HELPER;
  struct stat stats;
  ASSERT_EQ(fstat(fd, &stats), 0);
  off_t size = stats.st_blocks * VNATTR_BLKSIZE;
  ASSERT_EQ(size % minfs::kMinfsBlockSize, 0);
  *blocks = static_cast<uint64_t>(size / minfs::kMinfsBlockSize);
  END_HELPER;
}

// Fill a directory to at most |max_blocks| full of direntries.
// We assume the directory is empty to begin with, and any files we are adding do not already exist.
bool FillDirectory(int dir_fd, uint32_t max_blocks) {
  BEGIN_HELPER;

  uint32_t file_count = 0;
  while (true) {
    char path[128];
    snprintf(path, sizeof(path) - 1, "file_%u", file_count++);
    fbl::unique_fd fd(openat(dir_fd, path, O_CREAT | O_RDWR));
    ASSERT_TRUE(fd);

    uint64_t current_blocks;
    ASSERT_TRUE(GetFileBlocks(dir_fd, &current_blocks));

    if (current_blocks > max_blocks) {
      ASSERT_EQ(unlinkat(dir_fd, path, 0), 0);
      break;
    }
  }

  END_HELPER;
}

// Test various operations when the Minfs partition is near capacity.
bool TestFullOperations() {
  BEGIN_TEST;

  // Define file names we will use upfront.
  const char* big_path = "big_file";
  const char* med_path = "med_file";
  const char* sml_path = "sml_file";

  // Open the mount point and create three files.
  fbl::unique_fd mnt_fd(open(kMountPath, O_RDONLY));
  ASSERT_TRUE(mnt_fd);

  fbl::unique_fd big_fd(openat(mnt_fd.get(), big_path, O_CREAT | O_RDWR));
  ASSERT_TRUE(big_fd);

  fbl::unique_fd med_fd(openat(mnt_fd.get(), med_path, O_CREAT | O_RDWR));
  ASSERT_TRUE(med_fd);

  fbl::unique_fd sml_fd(openat(mnt_fd.get(), sml_path, O_CREAT | O_RDWR));
  ASSERT_TRUE(sml_fd);

  // Write to the "big" file, filling the partition
  // and leaving at most kMinfsDirect + 1 blocks unused.
  uint32_t free_blocks = minfs::kMinfsDirect + 1;
  uint32_t actual_blocks;
  ASSERT_TRUE(FillPartition(big_fd.get(), free_blocks, &actual_blocks));

  // Write enough data to the second file to take up all remaining blocks except for 1.
  // This should strictly be writing to the direct block section of the file.
  char data[minfs::kMinfsBlockSize];
  memset(data, 0xaa, sizeof(data));
  for (unsigned i = 0; i < actual_blocks - 1; i++) {
    ASSERT_EQ(write(med_fd.get(), data, sizeof(data)), sizeof(data));
  }

  // Make sure we now have only 1 block remaining.
  ASSERT_TRUE(GetFreeBlocks(&free_blocks));
  ASSERT_EQ(free_blocks, 1);

  // We should now have exactly 1 free block remaining. Attempt to write into the indirect
  // section of the file so we ensure that at least 2 blocks are required.
  // This is expected to fail.
  ASSERT_EQ(lseek(med_fd.get(), minfs::kMinfsBlockSize * minfs::kMinfsDirect, SEEK_SET),
            minfs::kMinfsBlockSize * minfs::kMinfsDirect);
  ASSERT_LT(write(med_fd.get(), data, sizeof(data)), 0);

  // Without block reservation, something from the failed write remains allocated. Try editing
  // nearby blocks to force a writeback of partially allocated data.
  // Note: This will fail without block reservation since the previous failed write would leave
  //       the only free block incorrectly allocated and 1 additional block is required for
  //       copy-on-write truncation.
  struct stat s;
  ASSERT_EQ(fstat(big_fd.get(), &s), 0);
  ssize_t truncate_size =
      fbl::round_up(static_cast<uint64_t>(s.st_size / 2), minfs::kMinfsBlockSize);
  ASSERT_EQ(ftruncate(big_fd.get(), truncate_size), 0);

  // We should still have 1 free block remaining. Writing to the beginning of the second file
  // should only require 1 (direct) block, and therefore pass.
  // Note: This fails without block reservation.
  ASSERT_EQ(write(sml_fd.get(), data, sizeof(data)), sizeof(data));

  // Attempt to remount. Without block reservation, an additional block from the previously
  // failed write will still be incorrectly allocated, causing fsck to fail.
  ASSERT_TRUE(check_remount());

  // Re-open files.
  mnt_fd.reset(open(kMountPath, O_RDONLY));
  ASSERT_TRUE(mnt_fd);
  big_fd.reset(openat(mnt_fd.get(), big_path, O_RDWR));
  ASSERT_TRUE(big_fd);
  sml_fd.reset(openat(mnt_fd.get(), sml_path, O_RDWR));
  ASSERT_TRUE(sml_fd);

  // Make sure we now have at least kMinfsDirect + 1 blocks remaining.
  ASSERT_TRUE(GetFreeBlocks(&free_blocks));
  ASSERT_GE(free_blocks, minfs::kMinfsDirect + 1);

  // We have some room now, so create a new directory.
  const char* dir_path = "directory";
  ASSERT_EQ(mkdirat(mnt_fd.get(), dir_path, 0666), 0);
  fbl::unique_fd dir_fd(openat(mnt_fd.get(), dir_path, O_RDONLY));
  ASSERT_TRUE(dir_fd);

  // Fill the directory up to kMinfsDirect blocks full of direntries.
  ASSERT_TRUE(FillDirectory(dir_fd.get(), minfs::kMinfsDirect));

  // Now re-fill the partition by writing as much as possible back to the original file.
  // Attempt to leave 1 block free.
  ASSERT_EQ(lseek(big_fd.get(), truncate_size, SEEK_SET), truncate_size);
  free_blocks = 1;
  ASSERT_TRUE(FillPartition(big_fd.get(), free_blocks, &actual_blocks));

  if (actual_blocks == 0) {
    // It is possible that, in our previous allocation of big_fd, we ended up leaving less than
    // |free_blocks| free. Since the file has grown potentially large, it is possible that
    // allocating a single block will also allocate additional indirect blocks.
    // For example, in a case where we have 2 free blocks remaining and expect to allocate 1,
    // we may actually end up allocating 2 instead, leaving us with 0 free blocks.
    // Since sml_fd is using less than kMinfsDirect blocks and thus is guaranteed to have a 1:1
    // block usage ratio, we can remedy this situation by removing a single block from sml_fd.
    ASSERT_EQ(ftruncate(sml_fd.get(), 0), 0);
  }

  while (actual_blocks > free_blocks) {
    // Otherwise, if too many blocks remain (if e.g. we needed to allocate 3 blocks but only 2
    // are remaining), write to sml_fd until only 1 remains.
    ASSERT_EQ(write(sml_fd.get(), data, sizeof(data)), sizeof(data));
    actual_blocks--;
  }

  // Ensure that there is now exactly one block remaining.
  ASSERT_TRUE(GetFreeBlocks(&actual_blocks));
  ASSERT_EQ(free_blocks, actual_blocks);

  // Now, attempt to add one more file to the directory we created. Since it will need to
  // allocate 2 blocks (1 indirect + 1 direct) and there is only 1 remaining, it should fail.
  uint64_t block_count;
  ASSERT_TRUE(GetFileBlocks(dir_fd.get(), &block_count));
  ASSERT_EQ(block_count, minfs::kMinfsDirect);
  fbl::unique_fd tmp_fd(openat(dir_fd.get(), "new_file", O_CREAT | O_RDWR));
  ASSERT_FALSE(tmp_fd);

  // Again, try editing nearby blocks to force bad allocation leftovers to be persisted, and
  // remount the partition. This is expected to fail without block reservation.
  ASSERT_EQ(fstat(big_fd.get(), &s), 0);
  ASSERT_EQ(s.st_size % minfs::kMinfsBlockSize, 0);
  truncate_size = s.st_size - minfs::kMinfsBlockSize;
  ASSERT_EQ(ftruncate(big_fd.get(), truncate_size), 0);
  ASSERT_TRUE(check_remount());

  // Re-open files.
  mnt_fd.reset(open(kMountPath, O_RDONLY));
  ASSERT_TRUE(mnt_fd);
  big_fd.reset(openat(mnt_fd.get(), big_path, O_RDWR));
  ASSERT_TRUE(big_fd);
  sml_fd.reset(openat(mnt_fd.get(), sml_path, O_RDWR));
  ASSERT_TRUE(sml_fd);

  // Fill the partition again, writing one block of data to sml_fd
  // in case we need an emergency truncate.
  ASSERT_EQ(write(sml_fd.get(), data, sizeof(data)), sizeof(data));
  ASSERT_EQ(lseek(big_fd.get(), truncate_size, SEEK_SET), truncate_size);
  free_blocks = 1;
  ASSERT_TRUE(FillPartition(big_fd.get(), free_blocks, &actual_blocks));

  if (actual_blocks == 0) {
    // If we ended up with fewer blocks than expected, truncate sml_fd to create more space.
    // (See note above for details.)
    ASSERT_EQ(ftruncate(sml_fd.get(), 0), 0);
  }

  while (actual_blocks > free_blocks) {
    // Otherwise, if too many blocks remain (if e.g. we needed to allocate 3 blocks but only 2
    // are remaining), write to sml_fd until only 1 remains.
    ASSERT_EQ(write(sml_fd.get(), data, sizeof(data)), sizeof(data));
    actual_blocks--;
  }

  // Ensure that there is now exactly one block remaining.
  ASSERT_TRUE(GetFreeBlocks(&actual_blocks));
  ASSERT_EQ(free_blocks, actual_blocks);

  // Now, attempt to rename one of our original files under the new directory.
  // This should also fail.
  ASSERT_NE(renameat(mnt_fd.get(), med_path, dir_fd.get(), med_path), 0);

  // Again, truncate the original file and attempt to remount.
  // Again, this should fail without block reservation.
  ASSERT_EQ(fstat(big_fd.get(), &s), 0);
  ASSERT_EQ(s.st_size % minfs::kMinfsBlockSize, 0);
  truncate_size = s.st_size - minfs::kMinfsBlockSize;
  ASSERT_EQ(ftruncate(big_fd.get(), truncate_size), 0);
  ASSERT_TRUE(check_remount());

  mnt_fd.reset(open(kMountPath, O_RDONLY));
  ASSERT_EQ(unlinkat(mnt_fd.get(), big_path, 0), 0);
  ASSERT_EQ(unlinkat(mnt_fd.get(), med_path, 0), 0);
  ASSERT_EQ(unlinkat(mnt_fd.get(), sml_path, 0), 0);
  END_TEST;
}

bool TestUnlinkFail(void) {
  BEGIN_TEST;

  if (use_real_disk) {
    fprintf(stderr, "Ramdisk required; skipping test\n");
    return true;
  }

  uint32_t original_blocks;
  ASSERT_TRUE(GetFreeBlocks(&original_blocks));

  uint32_t fd_count = 100;
  fbl::unique_fd fds[fd_count];

  char data[minfs::kMinfsBlockSize];
  memset(data, 0xaa, sizeof(data));
  const char* filename = "::file";

  // Open, write to, and unlink |fd_count| total files without closing them.
  for (unsigned i = 0; i < fd_count; i++) {
    // Since we are unlinking, we can use the same filename for all files.
    fds[i].reset(open(filename, O_CREAT | O_RDWR | O_EXCL));
    ASSERT_TRUE(fds[i]);
    ASSERT_EQ(write(fds[i].get(), data, sizeof(data)), sizeof(data));
    ASSERT_EQ(unlink(filename), 0);
  }

  // Close the first, middle, and last files to test behavior when various "links" are removed.
  uint32_t first_fd = 0;
  uint32_t mid_fd = fd_count / 2;
  uint32_t last_fd = fd_count - 1;
  ASSERT_EQ(close(fds[first_fd].release()), 0);
  ASSERT_EQ(close(fds[mid_fd].release()), 0);
  ASSERT_EQ(close(fds[last_fd].release()), 0);

  // Sync Minfs to ensure all unlink operations complete.
  fbl::unique_fd fd(open(filename, O_CREAT));
  ASSERT_TRUE(fd);
  ASSERT_EQ(syncfs(fd.get()), 0);

  // Check that the number of Minfs free blocks has decreased.
  uint32_t current_blocks;
  ASSERT_TRUE(GetFreeBlocks(&current_blocks));
  ASSERT_LT(current_blocks, original_blocks);

  // Put the ramdisk to sleep and close all the fds. This will cause file purge to fail,
  // and all unlinked files will be left intact (on disk).
  ASSERT_EQ(ramdisk_sleep_after(test_ramdisk, 0), 0);

  // The ramdisk is asleep but since no transactions have been processed, the writeback state has
  // not been updated. The first file we close will appear to succeed.
  ASSERT_EQ(close(fds[first_fd + 1].release()), 0);

  // Sync to ensure the writeback state is updated. Since the purge from the previous close will
  // fail, sync will also fail.
  ASSERT_LT(syncfs(fd.get()), 0);

  // Close all open fds. These will appear to succeed, although all pending transactions will fail.
  for (unsigned i = first_fd + 2; i < last_fd; i++) {
    if (i != mid_fd) {
      ASSERT_EQ(close(fds[i].release()), 0);
    }
  }

  // Sync Minfs to ensure all close operations complete. Since Minfs is in a read-only state and
  // some requests have not been successfully persisted to disk, the sync is expected to fail.
  ASSERT_LT(syncfs(fd.get()), 0);

  // Writeback should have failed.
  // However, the in-memory state has been updated correctly.
  ASSERT_TRUE(GetFreeBlocks(&current_blocks));
  ASSERT_EQ(current_blocks, original_blocks);

  // Remount Minfs, which should cause leftover unlinked files to be removed.
  ASSERT_EQ(ramdisk_wake(test_ramdisk), 0);
  ASSERT_TRUE(check_remount());

  // Check that the block count has been reverted to the value before any files were added.
  ASSERT_TRUE(GetFreeBlocks(&current_blocks));
  ASSERT_EQ(current_blocks, original_blocks);

  END_TEST;
}

bool GetAllocatedBlocks(uint64_t* out_allocated_blocks) {
  BEGIN_HELPER;
  fio::FilesystemInfo info;
  ASSERT_TRUE(QueryInfo(&info));
  *out_allocated_blocks = static_cast<uint64_t>(info.used_bytes) / info.block_size;
  END_HELPER;
}

bool GetAllocations(zx::vmo* out_vmo, uint64_t* out_count) {
  BEGIN_HELPER;
  fbl::unique_fd fd(open(kMountPath, O_RDONLY | O_DIRECTORY));
  ASSERT_TRUE(fd);
  zx_status_t status;
  fdio_cpp::FdioCaller caller(std::move(fd));
  zx_handle_t vmo_handle;
  ASSERT_EQ(fuchsia_minfs_MinfsGetAllocatedRegions(caller.borrow_channel(), &status, &vmo_handle,
                                                   out_count),
            ZX_OK);
  ASSERT_EQ(status, ZX_OK);
  out_vmo->reset(vmo_handle);
  END_HELPER;
}

// Verifies that the information returned by GetAllocatedRegions FIDL call is correct by
// checking it against the block devices metrics.
bool TestGetAllocatedRegions() {
  BEGIN_TEST;

  constexpr char kFirstPath[] = "some_file";
  constexpr char kSecondPath[] = "another_file";
  fbl::unique_fd mnt_fd(open(kMountPath, O_RDONLY));
  ASSERT_TRUE(mnt_fd);

  fbl::unique_fd first_fd(openat(mnt_fd.get(), kFirstPath, O_CREAT | O_RDWR));
  ASSERT_TRUE(first_fd);
  fbl::unique_fd second_fd(openat(mnt_fd.get(), kSecondPath, O_CREAT | O_RDWR));
  ASSERT_TRUE(second_fd);

  char data[minfs::kMinfsBlockSize];
  memset(data, 0xb0b, sizeof(data));
  // Interleave writes
  ASSERT_EQ(write(first_fd.get(), data, sizeof(data)), sizeof(data));
  ASSERT_EQ(fsync(first_fd.get()), 0);
  ASSERT_EQ(write(second_fd.get(), data, sizeof(data)), sizeof(data));
  ASSERT_EQ(fsync(second_fd.get()), 0);
  ASSERT_EQ(write(first_fd.get(), data, sizeof(data)), sizeof(data));
  ASSERT_EQ(fsync(first_fd.get()), 0);

  // Ensure that the number of bytes reported via GetAllocatedRegions and QueryInfo is the same
  zx::vmo vmo;
  uint64_t count;
  uint64_t actual_blocks;
  uint64_t total_blocks = 0;
  ASSERT_TRUE(GetAllocations(&vmo, &count));
  ASSERT_TRUE(GetAllocatedBlocks(&actual_blocks));
  fbl::Array<fuchsia_minfs_BlockRegion> buffer(new fuchsia_minfs_BlockRegion[count], count);
  ASSERT_EQ(vmo.read(buffer.data(), 0, sizeof(fuchsia_minfs_BlockRegion) * count), ZX_OK);
  for (size_t i = 0; i < count; i++) {
    total_blocks += buffer[i].length;
  }
  ASSERT_EQ(total_blocks, actual_blocks);

  // Delete second_fd. This allows us test that the FIDL call will still match the metrics
  // from QueryInfo after deletes and with fragmentation.
  ASSERT_EQ(unlinkat(mnt_fd.get(), kSecondPath, 0), 0);
  ASSERT_EQ(close(second_fd.release()), 0);
  ASSERT_EQ(fsync(mnt_fd.get()), 0);
  total_blocks = 0;

  ASSERT_TRUE(GetAllocations(&vmo, &count));
  ASSERT_TRUE(GetAllocatedBlocks(&actual_blocks));
  buffer.reset(new fuchsia_minfs_BlockRegion[count], count);
  ASSERT_EQ(vmo.read(buffer.data(), 0, sizeof(fuchsia_minfs_BlockRegion) * count), ZX_OK);
  for (size_t i = 0; i < count; i++) {
    total_blocks += buffer[i].length;
  }
  ASSERT_EQ(total_blocks, actual_blocks);

  END_TEST;
}

}  // namespace

#define RUN_MINFS_TESTS_NORMAL(name, CASE_TESTS) \
  FS_TEST_CASE(name, default_test_disk, CASE_TESTS, FS_TEST_NORMAL, minfs, 1)

#define RUN_MINFS_TESTS_FVM(name, CASE_TESTS) \
  FS_TEST_CASE(name##_fvm, default_test_disk, CASE_TESTS, FS_TEST_FVM, minfs, 1)

RUN_MINFS_TESTS_NORMAL(FsMinfsTests,
                       RUN_TEST_LARGE(TestFullOperations) RUN_TEST_MEDIUM(TestUnlinkFail)
                           RUN_TEST_MEDIUM(TestGetAllocatedRegions))

RUN_MINFS_TESTS_FVM(FsMinfsFvmTests, RUN_TEST_MEDIUM(TestQueryInfo) RUN_TEST_MEDIUM(TestMetrics)
                                         RUN_TEST_MEDIUM(TestUnlinkFail))

// Running with an isolated FVM to avoid interactions with the other integration tests.
FS_TEST_CASE(FsMinfsFullFvmTests, kGrowableTestDisk, RUN_TEST_LARGE(TestFullOperations),
             FS_TEST_FVM, minfs, 1)
