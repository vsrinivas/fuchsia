// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <string>

#include "src/storage/fs_test/fs_test_fixture.h"

namespace fs_test {
namespace {

using SyncTest = FilesystemTest;

// TODO(smklein): Create a more complex test, capable of mocking a block device
// and ensuring that data is actually being flushed to a block device.
// For now, test that 'fsync' and 'fdatasync' don't throw errors for file and
// directories.
TEST_P(SyncTest, VerifyNoErrorsForFsync) {
  const std::string alpha = GetPath("alpha");
  int fd = open(alpha.c_str(), O_RDWR | O_CREAT | O_EXCL, 0644);
  ASSERT_GT(fd, 0);
  ASSERT_EQ(write(fd, "Hello, World!\n", 14), 14);
  ASSERT_EQ(fsync(fd), 0);
  ASSERT_EQ(lseek(fd, 0, SEEK_SET), 0);
  ASSERT_EQ(write(fd, "Adios, World!\n", 14), 14);
  ASSERT_EQ(fdatasync(fd), 0);
  ASSERT_EQ(close(fd), 0);
  ASSERT_EQ(unlink(alpha.c_str()), 0);
}

TEST_P(SyncTest, VerifyNoErrorsForFdatasync) {
  const std::string dirname = GetPath("dirname");
  ASSERT_EQ(mkdir(dirname.c_str(), 0755), 0);
  int fd = open(dirname.c_str(), O_RDONLY | O_DIRECTORY, 0644);
  ASSERT_GT(fd, 0);
  ASSERT_EQ(fsync(fd), 0);
  ASSERT_EQ(fdatasync(fd), 0);
  ASSERT_EQ(close(fd), 0);
  ASSERT_EQ(unlink(dirname.c_str()), 0);
}

INSTANTIATE_TEST_SUITE_P(/*no prefix*/, SyncTest, testing::ValuesIn(AllTestFilesystems()),
                         testing::PrintToStringParamName());

}  // namespace
}  // namespace fs_test
