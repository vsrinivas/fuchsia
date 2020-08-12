// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO: 44323

#include <assert.h>
#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <zircon/syscalls.h>

#include <iterator>
#include <memory>

#include <fbl/algorithm.h>
#include <fbl/alloc_checker.h>
#include <fbl/unique_fd.h>
#include <fs/test/posix/tests.h>
#include <zxtest/zxtest.h>

namespace posix_tests {
namespace {

#define ASSERT_STREAM_ALL(op, fd, buf, len) ASSERT_EQ(op(fd, (buf), (len)), (ssize_t)(len), "");

void check_file_contains(const char* filename, const void* data, ssize_t len) {
  char buf[PATH_MAX];
  struct stat st;

  ASSERT_EQ(stat(filename, &st), 0);
  ASSERT_EQ(st.st_size, len);
  fbl::unique_fd fd(open(filename, O_RDWR, 0644));
  ASSERT_TRUE(fd);
  ASSERT_STREAM_ALL(read, fd.get(), buf, len);
  ASSERT_EQ(memcmp(buf, data, len), 0);
}

void check_file_empty(const char* filename) {
  struct stat st;
  ASSERT_EQ(stat(filename, &st), 0);
  ASSERT_EQ(st.st_size, 0);
}

void fill_file(int fd, uint8_t* u8, ssize_t new_len, ssize_t old_len) {
  fbl::AllocChecker ac;
  std::unique_ptr<uint8_t[]> readbuf(new (&ac) uint8_t[new_len]);
  ASSERT_TRUE(ac.check());
  if (new_len > old_len) {  // Expanded the file
    // Verify that the file is unchanged up to old_len
    ASSERT_EQ(lseek(fd, 0, SEEK_SET), 0);
    ASSERT_STREAM_ALL(read, fd, readbuf.get(), old_len);
    ASSERT_EQ(memcmp(readbuf.get(), u8, old_len), 0);
    // Verify that the file is filled with zeroes from old_len to new_len
    ASSERT_EQ(lseek(fd, old_len, SEEK_SET), old_len);
    ASSERT_STREAM_ALL(read, fd, readbuf.get(), new_len - old_len);
    for (ssize_t n = 0; n < (new_len - old_len); n++) {
      ASSERT_EQ(readbuf[n], 0);
    }
    // Overwrite those zeroes with the contents of u8
    ASSERT_EQ(lseek(fd, old_len, SEEK_SET), old_len);
    ASSERT_STREAM_ALL(write, fd, u8 + old_len, new_len - old_len);
  } else {  // Shrunk the file (or kept it the same length)
    // Verify that the file is unchanged up to new_len
    ASSERT_EQ(lseek(fd, 0, SEEK_SET), 0);
    ASSERT_STREAM_ALL(read, fd, readbuf.get(), new_len);
    ASSERT_EQ(memcmp(readbuf.get(), u8, new_len), 0);
  }
}

void checked_truncate(FilesystemTest* ops, const char* filename, uint8_t* u8, ssize_t new_len,
                      TestType type) {
  // Acquire the old size
  struct stat st;
  ASSERT_EQ(stat(filename, &st), 0);
  ssize_t old_len = st.st_size;

  // Truncate the file, verify the size gets updated
  fbl::unique_fd fd(open(filename, O_RDWR, 0644));
  ASSERT_TRUE(fd);
  ASSERT_EQ(ftruncate(fd.get(), new_len), 0);
  ASSERT_EQ(stat(filename, &st), 0);
  ASSERT_EQ(st.st_size, new_len);

  // Close and reopen the file; verify the inode stays updated
  ASSERT_EQ(close(fd.release()), 0);
  fd.reset(open(filename, O_RDWR, 0644));
  ASSERT_TRUE(fd);
  ASSERT_EQ(stat(filename, &st), 0);
  ASSERT_EQ(st.st_size, new_len);

  if (type == TestType::Remount) {
    ASSERT_EQ(close(fd.release()), 0);
    ops->Remount();
    ASSERT_EQ(stat(filename, &st), 0);
    ASSERT_EQ(st.st_size, new_len);
    fd.reset(open(filename, O_RDWR, 0644));
  }

  fill_file(fd.get(), u8, new_len, old_len);
}

void fchecked_truncate(int fd, uint8_t* u8, ssize_t new_len) {
  // Acquire the old size
  struct stat st;
  ASSERT_EQ(fstat(fd, &st), 0);
  ssize_t old_len = st.st_size;

  // Truncate the file, verify the size gets updated
  ASSERT_EQ(ftruncate(fd, new_len), 0);
  ASSERT_EQ(fstat(fd, &st), 0);
  ASSERT_EQ(st.st_size, new_len);

  fill_file(fd, u8, new_len, old_len);
}

}  // namespace

// Test that the really simple cases of truncate are operational
void TestTruncateSingleBlockFile(FilesystemTest* ops) {
  const char* str = "Hello, World!\n";
  char filename[PATH_MAX];
  ASSERT_LT(snprintf(filename, sizeof(filename), "%s/%s", ops->mount_path(), __func__),
            sizeof(filename));

  // Try writing a string to a file
  fbl::unique_fd fd(open(filename, O_RDWR | O_CREAT, 0644));
  ASSERT_TRUE(fd);
  ASSERT_STREAM_ALL(write, fd.get(), str, strlen(str));
  check_file_contains(filename, str, strlen(str));

  // Check that opening a file with O_TRUNC makes it empty
  fbl::unique_fd fd2(open(filename, O_RDWR | O_TRUNC, 0644));
  ASSERT_TRUE(fd2);
  check_file_empty(filename);

  // Check that we can still write to a file that has been truncated
  ASSERT_EQ(lseek(fd.get(), 0, SEEK_SET), 0);
  ASSERT_STREAM_ALL(write, fd.get(), str, strlen(str));
  check_file_contains(filename, str, strlen(str));

  // Check that we can truncate the file using the "truncate" function
  ASSERT_EQ(truncate(filename, 5), 0);
  check_file_contains(filename, str, 5);
  ASSERT_EQ(truncate(filename, 0), 0);
  check_file_empty(filename);

  // Check that truncating an already empty file does not cause problems
  ASSERT_EQ(truncate(filename, 0), 0);
  check_file_empty(filename);

  // Check that we can use truncate to extend a file
  char empty[5] = {0, 0, 0, 0, 0};
  ASSERT_EQ(truncate(filename, 5), 0);
  check_file_contains(filename, empty, 5);

  ASSERT_EQ(close(fd.release()), 0);
  ASSERT_EQ(close(fd2.release()), 0);
  ASSERT_EQ(unlink(filename), 0);
}

void TestTruncateMultiBlockFile(FilesystemTest* ops, size_t buf_size, size_t iterations,
                                TestType type) {
  if ((type == TestType::Remount) && ops->CanBeRemounted()) {
    fprintf(stderr, "Filesystem cannot be mounted; cannot test persistence\n");
  }

  // Fill a test buffer with data
  fbl::AllocChecker ac;
  std::unique_ptr<uint8_t[]> buf(new (&ac) uint8_t[buf_size]);
  ASSERT_TRUE(ac.check());

  unsigned seed = static_cast<unsigned>(zx_ticks_get());
  printf("Truncate test using seed: %u\n", seed);
  srand(seed);
  for (unsigned n = 0; n < buf_size; n++) {
    buf[n] = static_cast<uint8_t>(rand_r(&seed));
  }

  char filename[PATH_MAX];
  ASSERT_LT(snprintf(filename, sizeof(filename), "%s/%s-%lu-%lu", ops->mount_path(), __func__,
                     buf_size, iterations),
            sizeof(filename));
  // Start a file filled with a buffer
  fbl::unique_fd fd(open(filename, O_RDWR | O_CREAT, 0644));
  ASSERT_TRUE(fd);
  ASSERT_STREAM_ALL(write, fd.get(), buf.get(), buf_size);

  if (type != TestType::KeepOpen) {
    ASSERT_EQ(close(fd.release()), 0);
  }

  // Repeatedly truncate / write to the file
  for (size_t i = 0; i < iterations; i++) {
    size_t len = rand_r(&seed) % buf_size;
    if (type == KeepOpen) {
      fchecked_truncate(fd.get(), buf.get(), len);
    } else {
      checked_truncate(ops, filename, buf.get(), len, type);
    }
  }
  ASSERT_EQ(unlink(filename), 0);
  if (type == KeepOpen) {
    ASSERT_EQ(close(fd.release()), 0);
  }
}

void TestTruncatePartialBlockSparse(FilesystemTest* ops, CloseUnlinkOrder order) {
  // TODO(fxbug.dev/44323): Acquire these constants directly from MinFS's header
  constexpr size_t kBlockSize = 8192;
  constexpr size_t kDirectBlocks = 16;
  constexpr size_t kIndirectBlocks = 31;
  constexpr size_t kDirectPerIndirect = kBlockSize / 4;

  uint8_t buf[kBlockSize];
  memset(buf, 0xAB, sizeof(buf));

  off_t write_offsets[] = {
      kBlockSize * 5,
      kBlockSize * kDirectBlocks,
      kBlockSize * kDirectBlocks + kBlockSize * kDirectPerIndirect * 1,
      kBlockSize * kDirectBlocks + kBlockSize * kDirectPerIndirect * 2,
      kBlockSize * kDirectBlocks + kBlockSize * kDirectPerIndirect * kIndirectBlocks -
          2 * kBlockSize,
      kBlockSize * kDirectBlocks + kBlockSize * kDirectPerIndirect * kIndirectBlocks - kBlockSize,
      kBlockSize * kDirectBlocks + kBlockSize * kDirectPerIndirect * kIndirectBlocks,
      kBlockSize * kDirectBlocks + kBlockSize * kDirectPerIndirect * kIndirectBlocks + kBlockSize,
  };

  char path[PATH_MAX];
  ASSERT_LT(snprintf(path, sizeof(path), "%s/%s", ops->mount_path(), __func__), sizeof(path));
  for (size_t i = 0; i < std::size(write_offsets); i++) {
    off_t write_off = write_offsets[i];
    fbl::unique_fd fd(open(path, O_CREAT | O_RDWR));
    ASSERT_TRUE(fd);
    ASSERT_EQ(lseek(fd.get(), write_off, SEEK_SET), write_off);
    ASSERT_EQ(write(fd.get(), buf, sizeof(buf)), sizeof(buf));
    ASSERT_EQ(ftruncate(fd.get(), write_off + 2 * kBlockSize), 0);
    ASSERT_EQ(ftruncate(fd.get(), write_off + kBlockSize + kBlockSize / 2), 0);
    ASSERT_EQ(ftruncate(fd.get(), write_off + kBlockSize / 2), 0);
    ASSERT_EQ(ftruncate(fd.get(), write_off - kBlockSize / 2), 0);
    if (order == UnlinkThenClose) {
      ASSERT_EQ(unlink(path), 0);
      ASSERT_EQ(close(fd.release()), 0);
    } else {
      ASSERT_EQ(close(fd.release()), 0);
      ASSERT_EQ(unlink(path), 0);
    }
  }
}

void TestTruncateErrno(FilesystemTest* ops) {
  char path[PATH_MAX];
  ASSERT_LT(snprintf(path, sizeof(path), "%s/%s", ops->mount_path(), __func__), sizeof(path));
  fbl::unique_fd fd(open(path, O_RDWR | O_CREAT | O_EXCL));
  ASSERT_TRUE(fd);

  ASSERT_EQ(ftruncate(fd.get(), -1), -1);
  ASSERT_EQ(errno, EINVAL);
  errno = 0;
  ASSERT_EQ(ftruncate(fd.get(), 1UL << 60), -1);
  ASSERT_EQ(errno, EINVAL);

  ASSERT_EQ(unlink(path), 0);
  ASSERT_EQ(close(fd.release()), 0);
}

}  // namespace posix_tests
