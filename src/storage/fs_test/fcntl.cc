// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <fbl/unique_fd.h>

#include "src/storage/fs_test/fs_test_fixture.h"

namespace fs_test {
namespace {

using FcntlTest = FilesystemTest;

TEST_P(FcntlTest, FcntlAppend) {
  fbl::unique_fd fd(open(GetPath("file").c_str(), O_APPEND | O_RDWR | O_CREAT, 0644));
  ASSERT_TRUE(fd);

  // Do a quick check that O_APPEND is appending
  char buf[5];
  memset(buf, 'a', sizeof(buf));
  ASSERT_EQ(lseek(fd.get(), 0, SEEK_SET), 0);
  ASSERT_EQ(write(fd.get(), buf, sizeof(buf)), static_cast<ssize_t>(sizeof(buf)));
  ASSERT_EQ(lseek(fd.get(), 0, SEEK_SET), 0);
  ASSERT_EQ(write(fd.get(), buf, sizeof(buf)), static_cast<ssize_t>(sizeof(buf)));
  struct stat sb;
  ASSERT_EQ(fstat(fd.get(), &sb), 0);
  ASSERT_EQ(sb.st_size, static_cast<off_t>(sizeof(buf) * 2));

  // Use F_GETFL; observe O_APPEND
  int flags = fcntl(fd.get(), F_GETFL);
  ASSERT_GT(flags, -1);
  ASSERT_EQ(flags & O_ACCMODE, O_RDWR) << "Access mode flags did not match";
  ASSERT_EQ(flags & ~O_ACCMODE, O_APPEND) << "Status flags did not match";

  // Use F_SETFL; turn off O_APPEND
  ASSERT_EQ(fcntl(fd.get(), F_SETFL, flags & ~O_APPEND), 0);

  // Use F_GETFL; observe O_APPEND has been turned off
  flags = fcntl(fd.get(), F_GETFL);
  ASSERT_GT(flags, -1);
  ASSERT_EQ(flags & O_ACCMODE, O_RDWR) << "Access mode flags did not match";
  ASSERT_EQ(flags & ~O_ACCMODE, 0) << "Status flags did not match";

  // Write to the file, verify it is no longer appending.
  ASSERT_EQ(lseek(fd.get(), 0, SEEK_SET), 0);
  ASSERT_EQ(write(fd.get(), buf, sizeof(buf)), static_cast<ssize_t>(sizeof(buf)));
  ASSERT_EQ(fstat(fd.get(), &sb), 0);
  ASSERT_EQ(sb.st_size, static_cast<off_t>(sizeof(buf) * 2));

  // Clean up
  ASSERT_EQ(close(fd.release()), 0);
  ASSERT_EQ(unlink(GetPath("file").c_str()), 0);
}

TEST_P(FcntlTest, FcntlAccessBits) {
  fbl::unique_fd fd(open(GetPath("file").c_str(), O_APPEND | O_RDWR | O_CREAT, 0644));
  ASSERT_TRUE(fd);

  // Do a quick check that we can write
  char buf[5];
  memset(buf, 'a', sizeof(buf));
  ASSERT_EQ(lseek(fd.get(), 0, SEEK_SET), 0);
  ASSERT_EQ(write(fd.get(), buf, sizeof(buf)), static_cast<ssize_t>(sizeof(buf)));
  struct stat sb;
  ASSERT_EQ(fstat(fd.get(), &sb), 0);
  ASSERT_EQ(sb.st_size, static_cast<off_t>(sizeof(buf)));

  // Use F_GETFL; observe O_APPEND
  int flags = fcntl(fd.get(), F_GETFL);
  ASSERT_GT(flags, -1);
  ASSERT_EQ(flags & O_ACCMODE, O_RDWR) << "Access mode flags did not match";
  ASSERT_EQ(flags & ~O_ACCMODE, O_APPEND) << "Status flags did not match";

  // Use F_SETFL; try to turn off everything except O_APPEND
  // (if fcntl paid attention to access bits, this would make the file
  // read-only).
  ASSERT_EQ(fcntl(fd.get(), F_SETFL, O_APPEND), 0);

  // We're still appending -- AND writable, because the access bits haven't
  // changed.
  ASSERT_EQ(lseek(fd.get(), 0, SEEK_SET), 0);
  ASSERT_EQ(write(fd.get(), buf, sizeof(buf)), static_cast<ssize_t>(sizeof(buf)));
  ASSERT_EQ(fstat(fd.get(), &sb), 0);
  ASSERT_EQ(sb.st_size, static_cast<off_t>(sizeof(buf) * 2));

  // Clean up
  ASSERT_EQ(close(fd.release()), 0);
  ASSERT_EQ(unlink(GetPath("file").c_str()), 0);
}

INSTANTIATE_TEST_SUITE_P(/*no prefix*/, FcntlTest, testing::ValuesIn(AllTestFilesystems()),
                         testing::PrintToStringParamName());

}  // namespace
}  // namespace fs_test
