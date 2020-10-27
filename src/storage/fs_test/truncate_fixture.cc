// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/fs_test/truncate_fixture.h"

#include <fcntl.h>
#include <sched.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
#include <zircon/syscalls.h>

#include <iostream>
#include <memory>

#include <fbl/alloc_checker.h>
#include <fbl/unique_fd.h>

namespace fs_test {
namespace {

void FillFile(int fd, uint8_t* u8, ssize_t new_len, ssize_t old_len) {
  fbl::AllocChecker ac;
  std::unique_ptr<uint8_t[]> readbuf(new (&ac) uint8_t[new_len]);
  ASSERT_TRUE(ac.check());
  if (new_len > old_len) {  // Expanded the file
    // Verify that the file is unchanged up to old_len
    ASSERT_EQ(lseek(fd, 0, SEEK_SET), 0);
    ASSERT_EQ(read(fd, readbuf.get(), old_len), old_len);
    ASSERT_EQ(memcmp(readbuf.get(), u8, old_len), 0);
    // Verify that the file is filled with zeroes from old_len to new_len
    ASSERT_EQ(lseek(fd, old_len, SEEK_SET), old_len);
    ASSERT_EQ(read(fd, readbuf.get(), new_len - old_len), new_len - old_len);
    for (ssize_t n = 0; n < (new_len - old_len); n++) {
      ASSERT_EQ(readbuf[n], 0);
    }
    // Overwrite those zeroes with the contents of u8
    ASSERT_EQ(lseek(fd, old_len, SEEK_SET), old_len);
    ASSERT_EQ(write(fd, u8 + old_len, new_len - old_len), new_len - old_len);
  } else {  // Shrunk the file (or kept it the same length)
    // Verify that the file is unchanged up to new_len
    ASSERT_EQ(lseek(fd, 0, SEEK_SET), 0);
    ASSERT_EQ(read(fd, readbuf.get(), new_len), new_len);
    ASSERT_EQ(memcmp(readbuf.get(), u8, new_len), 0);
  }
}

void CheckedTruncate(TestFilesystem& fs, bool remount, const char* filename, uint8_t* u8,
                     ssize_t new_len) {
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

  if (remount) {
    ASSERT_EQ(close(fd.release()), 0);
    EXPECT_EQ(fs.Unmount().status_value(), ZX_OK);
    EXPECT_EQ(fs.Fsck().status_value(), ZX_OK);
    EXPECT_EQ(fs.Mount().status_value(), ZX_OK);
    ASSERT_EQ(stat(filename, &st), 0);
    ASSERT_EQ(st.st_size, new_len);
    fd.reset(open(filename, O_RDWR, 0644));
  }

  ASSERT_NO_FATAL_FAILURE(FillFile(fd.get(), u8, new_len, old_len));
}

void CheckedFtruncate(int fd, uint8_t* u8, ssize_t new_len) {
  // Acquire the old size
  struct stat st;
  ASSERT_EQ(fstat(fd, &st), 0);
  ssize_t old_len = st.st_size;

  // Truncate the file, verify the size gets updated
  ASSERT_EQ(ftruncate(fd, new_len), 0);
  ASSERT_EQ(fstat(fd, &st), 0);
  ASSERT_EQ(st.st_size, new_len);

  ASSERT_NO_FATAL_FAILURE(FillFile(fd, u8, new_len, old_len));
}

// Test that truncate doesn't have issues dealing with larger files
// Repeatedly write to / truncate a file.
TEST_P(LargeTruncateTest, RepeatedlyWritingAndTruncatingLargeFileSucceeds) {
  // Fill a test buffer with data
  fbl::AllocChecker ac;
  std::unique_ptr<uint8_t[]> buf(new (&ac) uint8_t[buffer_size()]);
  ASSERT_TRUE(ac.check());

  unsigned seed = static_cast<unsigned>(zx_ticks_get());
  std::cout << "Truncate test using seed: " << seed;
  srand(seed);
  for (unsigned n = 0; n < buffer_size(); n++) {
    buf[n] = static_cast<uint8_t>(rand_r(&seed));
  }

  // Start a file filled with a buffer
  const std::string filename = GetPath("alpha");
  fbl::unique_fd fd(open(filename.c_str(), O_RDWR | O_CREAT, 0644));
  ASSERT_TRUE(fd);
  ASSERT_EQ(write(fd.get(), buf.get(), buffer_size()), static_cast<ssize_t>(buffer_size()));

  if (test_type() != LargeTruncateTestType::KeepOpen) {
    ASSERT_EQ(close(fd.release()), 0);
  }

  // Repeatedly truncate / write to the file
  for (size_t i = 0; i < iterations(); ++i) {
    size_t len = rand_r(&seed) % buffer_size();
    if (test_type() == LargeTruncateTestType::KeepOpen) {
      ASSERT_NO_FATAL_FAILURE(CheckedFtruncate(fd.get(), buf.get(), len));
    } else {
      ASSERT_NO_FATAL_FAILURE(CheckedTruncate(fs(), test_type() == LargeTruncateTestType::Remount,
                                              filename.c_str(), buf.get(), len));
    }
  }
  ASSERT_EQ(unlink(filename.c_str()), 0);
  if (test_type() == LargeTruncateTestType::KeepOpen) {
    ASSERT_EQ(close(fd.release()), 0);
  }
}

}  // namespace

std::string GetDescriptionForLargeTruncateTestParamType(
    const testing::TestParamInfo<LargeTruncateTestParamType> param) {
  std::stringstream s;
  s << std::get<0>(param.param) << "WithBufferSize"
    << std::to_string(std::get<0>(std::get<1>(param.param))) << "Iterations"
    << std::to_string(std::get<1>(std::get<1>(param.param))) << "Type";
  switch (std::get<2>(std::get<1>(param.param))) {
    case LargeTruncateTestType::KeepOpen:
      s << "KeepOpen";
      break;
    case LargeTruncateTestType::Reopen:
      s << "Reopen";
      break;
    case LargeTruncateTestType::Remount:
      s << "Remount";
      break;
  }
  return s.str();
}

}  // namespace fs_test
