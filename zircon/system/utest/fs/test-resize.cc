// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <fcntl.h>
#include <fuchsia/io/llcpp/fidl.h>
#include <lib/fdio/cpp/caller.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
#include <zircon/compiler.h>
#include <zircon/syscalls.h>

#include <memory>
#include <new>

#include <fbl/algorithm.h>
#include <fvm/format.h>
#include <minfs/format.h>
#include <unittest/unittest.h>

#include "filesystems.h"
#include "misc.h"

namespace {

namespace fio = ::llcpp::fuchsia::io;

bool QueryInfo(uint64_t* out_free_pool_size) {
  BEGIN_HELPER;
  fbl::unique_fd fd(open(kMountPath, O_RDONLY | O_DIRECTORY));
  ASSERT_TRUE(fd);
  fdio_cpp::FdioCaller caller(std::move(fd));
  auto query_result = fio::DirectoryAdmin::Call::QueryFilesystem(caller.channel());
  ASSERT_EQ(query_result.status(), ZX_OK);
  ASSERT_NOT_NULL(query_result.Unwrap()->info);
  fio::FilesystemInfo* info = query_result.Unwrap()->info;
  // This should always be true, for all filesystems.
  ASSERT_GT(info->total_bytes, info->used_bytes);
  *out_free_pool_size = info->free_shared_pool_bytes;
  END_HELPER;
}

bool EnsureCanGrow() {
  BEGIN_HELPER;
  uint64_t free_pool_size;
  ASSERT_TRUE(QueryInfo(&free_pool_size));
  // This tests expects to run with free FVM space.
  ASSERT_GT(free_pool_size, 0);
  END_HELPER;
}

bool EnsureCannotGrow() {
  BEGIN_HELPER;
  uint64_t free_pool_size;
  ASSERT_TRUE(QueryInfo(&free_pool_size));
  ASSERT_EQ(free_pool_size, 0);
  END_HELPER;
}

const test_disk_t max_inode_disk = {
    .block_count = 1LLU << 15,
    .block_size = 1LLU << 9,
    .slice_size = 1LLU << 20,
};

template <bool Remount>
bool TestUseAllInodes() {
  BEGIN_TEST;
  if (use_real_disk) {
    fprintf(stderr, "Ramdisk required; skipping test\n");
    return true;
  }
  ASSERT_TRUE(test_info->supports_resize);
  ASSERT_TRUE(EnsureCanGrow());

  // Create 100,000 inodes.
  // We expect that this will force enough inodes to cause the
  // filesystem structures to resize partway through.
  constexpr size_t kFilesPerDirectory = 100;
  size_t d = 0;
  while (true) {
    if (d % 100 == 0) {
      printf("Creating directory (containing 100 files): %lu\n", d);
    }
    char dname[128];
    snprintf(dname, sizeof(dname), "::%lu", d);
    if (mkdir(dname, 0666) < 0) {
      ASSERT_EQ(errno, ENOSPC);
      break;
    }
    bool stop = false;
    for (size_t f = 0; f < kFilesPerDirectory; f++) {
      char fname[128];
      snprintf(fname, sizeof(fname), "::%lu/%lu", d, f);
      fbl::unique_fd fd(open(fname, O_CREAT | O_RDWR | O_EXCL));
      if (!fd) {
        ASSERT_EQ(errno, ENOSPC);
        stop = true;
        break;
      }
    }
    if (stop) {
      break;
    }
    d++;
  }

  ASSERT_TRUE(EnsureCannotGrow());

  if (Remount) {
    printf("Unmounting, Re-mounting, verifying...\n");
    ASSERT_TRUE(check_remount(), "Could not remount filesystem");
  }

  size_t directory_count = d;
  for (size_t d = 0; d < directory_count; d++) {
    if (d % 100 == 0) {
      printf("Deleting directory (containing 100 files): %lu\n", d);
    }
    for (size_t f = 0; f < kFilesPerDirectory; f++) {
      char fname[128];
      snprintf(fname, sizeof(fname), "::%lu/%lu", d, f);
      ASSERT_EQ(unlink(fname), 0);
    }
    char dname[128];
    snprintf(dname, sizeof(dname), "::%lu", d);
    ASSERT_EQ(rmdir(dname), 0);
  }

  END_TEST;
}

const test_disk_t max_data_disk = {
    .block_count = 1LLU << 17,
    .block_size = 1LLU << 9,
    .slice_size = 1LLU << 20,
};

template <bool Remount>
bool TestUseAllData() {
  BEGIN_TEST;
  if (use_real_disk) {
    fprintf(stderr, "Ramdisk required; skipping test\n");
    return true;
  }
  constexpr size_t kBufSize = (1 << 20);
  constexpr size_t kFileBufCount = 20;
  ASSERT_TRUE(test_info->supports_resize);
  ASSERT_TRUE(EnsureCanGrow());

  uint64_t disk_size = test_disk_info.block_count * test_disk_info.block_size;
  size_t metadata_size = fvm::MetadataSize(disk_size, max_data_disk.slice_size);

  ASSERT_GT(disk_size, metadata_size * 2);
  disk_size -= 2 * metadata_size;

  ASSERT_GT(disk_size, minfs::kMinfsMinimumSlices * max_data_disk.slice_size);
  disk_size -= minfs::kMinfsMinimumSlices * max_data_disk.slice_size;

  std::unique_ptr<uint8_t[]> buf(new uint8_t[kBufSize]);
  memset(buf.get(), 0, kBufSize);

  size_t f = 0;
  while (true) {
    printf("Creating 20 MB file #%lu\n", f);
    char fname[128];
    snprintf(fname, sizeof(fname), "::%lu", f);
    fbl::unique_fd fd(open(fname, O_CREAT | O_RDWR | O_EXCL));
    if (!fd) {
      ASSERT_EQ(errno, ENOSPC);
      break;
    }
    f++;
    bool stop = false;
    for (size_t i = 0; i < kFileBufCount; i++) {
      ASSERT_EQ(ftruncate(fd.get(), kBufSize * kFileBufCount), 0);
      ssize_t r = write(fd.get(), buf.get(), kBufSize);
      if (r != kBufSize) {
        ASSERT_EQ(errno, ENOSPC);
        stop = true;
        break;
      }
    }
    if (stop) {
      break;
    }
  }

  ASSERT_TRUE(EnsureCannotGrow());

  if (Remount) {
    printf("Unmounting, Re-mounting, verifying...\n");
    ASSERT_TRUE(check_remount(), "Could not remount filesystem");
  }

  size_t file_count = f;
  for (size_t f = 0; f < file_count; f++) {
    char fname[128];
    snprintf(fname, sizeof(fname), "::%lu", f);
    ASSERT_EQ(unlink(fname), 0);
  }

  END_TEST;
}

}  // namespace

// Reformat the disk between tests to restore original size.
RUN_FOR_ALL_FILESYSTEMS_TYPE(fs_resize_tests_inodes_remount, max_inode_disk, FS_TEST_FVM,
                             RUN_TEST_LARGE((TestUseAllInodes<true>)))

RUN_FOR_ALL_FILESYSTEMS_TYPE(fs_resize_tests_inodes, max_inode_disk, FS_TEST_FVM,
                             RUN_TEST_LARGE((TestUseAllInodes<false>)))

RUN_FOR_ALL_FILESYSTEMS_TYPE(fs_resize_tests_data_remount, max_data_disk, FS_TEST_FVM,
                             RUN_TEST_LARGE((TestUseAllData<true>)))

RUN_FOR_ALL_FILESYSTEMS_TYPE(fs_resize_tests_data, max_data_disk, FS_TEST_FVM,
                             RUN_TEST_LARGE((TestUseAllData<false>)))
