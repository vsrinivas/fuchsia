// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <time.h>

#include <iostream>
#include <vector>

#include "src/storage/host_fs_test/fixture.h"

namespace fs_test {
namespace {

void CheckFileContains(const char* filename, const void* data, ssize_t len) {
  char buf[4096];
  struct stat st;

  ASSERT_EQ(emu_stat(filename, &st), 0);
  ASSERT_EQ(st.st_size, len);
  int fd = emu_open(filename, O_RDWR, 0644);
  ASSERT_GT(fd, 0);
  ASSERT_TRUE(CheckStreamAll(emu_read, fd, buf, len));
  ASSERT_EQ(memcmp(buf, data, len), 0);
  ASSERT_EQ(emu_close(fd), 0);
}

void CheckFileEmpty(const char* filename) {
  struct stat st;
  ASSERT_EQ(emu_stat(filename, &st), 0);
  ASSERT_EQ(st.st_size, 0);
}

// Test that the really simple cases of truncate are operational
TEST_F(HostFilesystemTest, TruncateSmall) {
  const char* str = "Hello, World!\n";
  const char* filename = "::alpha";

  // Try writing a string to a file
  int fd = emu_open(filename, O_RDWR | O_CREAT, 0644);
  ASSERT_GT(fd, 0);
  ASSERT_TRUE(CheckStreamAll(emu_write, fd, str, strlen(str)));
  ASSERT_NO_FATAL_FAILURE(CheckFileContains(filename, str, strlen(str)));

  // Check that opening a file with O_TRUNC makes it empty
  int fd2 = emu_open(filename, O_RDWR | O_TRUNC, 0644);
  ASSERT_GT(fd2, 0);
  ASSERT_NO_FATAL_FAILURE(CheckFileEmpty(filename));

  // Check that we can still write to a file that has been truncated
  ASSERT_EQ(emu_lseek(fd, 0, SEEK_SET), 0);
  ASSERT_TRUE(CheckStreamAll(emu_write, fd, str, strlen(str)));
  ASSERT_NO_FATAL_FAILURE(CheckFileContains(filename, str, strlen(str)));

  // Check that we can truncate the file using the "truncate" function
  ASSERT_EQ(emu_ftruncate(fd, 5), 0);
  ASSERT_NO_FATAL_FAILURE(CheckFileContains(filename, str, 5));
  ASSERT_EQ(emu_ftruncate(fd, 0), 0);
  ASSERT_NO_FATAL_FAILURE(CheckFileEmpty(filename));

  // Check that truncating an already empty file does not cause problems
  ASSERT_EQ(emu_ftruncate(fd, 0), 0);
  ASSERT_NO_FATAL_FAILURE(CheckFileEmpty(filename));

  // Check that we can use truncate to extend a file
  char empty[5] = {0, 0, 0, 0, 0};
  ASSERT_EQ(emu_ftruncate(fd, 5), 0);
  ASSERT_NO_FATAL_FAILURE(CheckFileContains(filename, empty, 5));

  ASSERT_EQ(emu_close(fd), 0);
  ASSERT_EQ(emu_close(fd2), 0);
}

void CheckedTruncate(const char* filename, uint8_t* u8, ssize_t new_len) {
  // Acquire the old size
  struct stat st;
  ASSERT_EQ(emu_stat(filename, &st), 0);
  ssize_t old_len = st.st_size;

  // Truncate the file, verify the size gets updated
  int fd = emu_open(filename, O_RDWR, 0644);
  ASSERT_GT(fd, 0);
  ASSERT_EQ(emu_ftruncate(fd, new_len), 0);
  ASSERT_EQ(emu_stat(filename, &st), 0);
  ASSERT_EQ(st.st_size, new_len);

  // close and reopen the file; verify the inode stays updated
  ASSERT_EQ(emu_close(fd), 0);
  fd = emu_open(filename, O_RDWR, 0644);
  ASSERT_GT(fd, 0);
  ASSERT_EQ(emu_stat(filename, &st), 0);
  ASSERT_EQ(st.st_size, new_len);

  std::vector<uint8_t> readbuf(new_len);
  if (new_len > old_len) {  // Expanded the file
    // Verify that the file is unchanged up to old_len
    ASSERT_EQ(emu_lseek(fd, 0, SEEK_SET), 0);
    ASSERT_TRUE(CheckStreamAll(emu_read, fd, readbuf.data(), old_len));
    if (old_len > 0) {
      // It's undefined behavior to call memcmp on a nullptr, so only do this
      // check if old_len is nonzero and thus readbuf.data() is expected to
      // contain a real pointer.
      ASSERT_EQ(memcmp(readbuf.data(), u8, old_len), 0);
    }
    // Verify that the file is filled with zeroes from old_len to new_len
    ASSERT_EQ(emu_lseek(fd, old_len, SEEK_SET), old_len);
    ASSERT_TRUE(CheckStreamAll(emu_read, fd, readbuf.data(), new_len - old_len));
    for (ssize_t n = 0; n < (new_len - old_len); ++n) {
      ASSERT_EQ(readbuf[n], 0);
    }
    // Overwrite those zeroes with the contents of u8
    ASSERT_EQ(emu_lseek(fd, old_len, SEEK_SET), old_len);
    ASSERT_TRUE(CheckStreamAll(emu_write, fd, u8 + old_len, new_len - old_len));
  } else {  // Shrunk the file (or kept it the same length)
    // Verify that the file is unchanged up to new_len
    ASSERT_EQ(emu_lseek(fd, 0, SEEK_SET), 0);
    ASSERT_TRUE(CheckStreamAll(emu_read, fd, readbuf.data(), new_len));
    if (new_len > 0) {
      // It's undefined behavior to call memcmp on a nullptr, so only do this
      // check if old_len is nonzero and thus readbuf.data() is expected to
      // contain a real pointer.
      ASSERT_EQ(memcmp(readbuf.data(), u8, new_len), 0);
    }
  }

  ASSERT_EQ(emu_close(fd), 0);
}

struct TestParam {
  size_t buf_size;
  size_t iterations;
};

class TruncateLargeHostFilesystemTest : public HostFilesystemTest,
                                        public testing::WithParamInterface<TestParam> {};

// Test that truncate doesn't have issues dealing with larger files
// Repeatedly write to / truncate a file.
TEST_P(TruncateLargeHostFilesystemTest, Large) {
  // Fill a test buffer with data
  std::vector<uint8_t> buf(GetParam().buf_size);

  unsigned seed = static_cast<unsigned>(time(0));
  std::cerr << "Truncate test using seed: " << seed << std::endl;
  srand(seed);
  for (unsigned n = 0; n < GetParam().buf_size; ++n) {
    buf[n] = static_cast<uint8_t>(rand_r(&seed));
  }

  // Start a file filled with the u8 buffer
  const char* filename = "::alpha";
  int fd = emu_open(filename, O_RDWR | O_CREAT, 0644);
  ASSERT_GT(fd, 0);
  ASSERT_TRUE(CheckStreamAll(emu_write, fd, buf.data(), GetParam().buf_size));
  ASSERT_EQ(emu_close(fd), 0);

  // Repeatedly truncate / write to the file
  for (size_t i = 0; i < GetParam().iterations; ++i) {
    size_t len = rand_r(&seed) % GetParam().buf_size;
    ASSERT_NO_FATAL_FAILURE(CheckedTruncate(filename, buf.data(), len));
    ASSERT_EQ(RunFsck(), 0);
  }
}

std::string GetParamDescription(const testing::TestParamInfo<TestParam>& param) {
  std::stringstream s;
  s << "BufSize" << param.param.buf_size << "Iterations" << param.param.iterations;
  return s.str();
}

INSTANTIATE_TEST_SUITE_P(/*no prefix*/, TruncateLargeHostFilesystemTest,
                         testing::Values(TestParam{1 << 10, 100}, TestParam{1 << 15, 100},
                                         TestParam{1 << 20, 100}, TestParam{1 << 25, 10}),
                         GetParamDescription);

}  // namespace
}  // namespace fs_test
