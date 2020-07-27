// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <string>

#include <fbl/unique_fd.h>

#include "src/storage/fs_test/fs_test_fixture.h"

namespace fs_test {
namespace {

using SyncTest = FilesystemTest;

// TODO(smklein): Create a more complex test, capable of mocking a block device
// and ensuring that data is actually being flushed to a block device.
// For now, test that 'fsync' and 'fdatasync' don't throw errors for file and
// directories.
TEST_P(SyncTest, VerifyNoFsyncErrorsForFiles) {
  const std::string alpha = GetPath("alpha");
  auto fd = fbl::unique_fd(open(alpha.c_str(), O_RDWR | O_CREAT | O_EXCL, 0644));
  ASSERT_GT(fd.get(), 0);
  ASSERT_EQ(write(fd.get(), "Hello, World!\n", 14), 14);
  EXPECT_EQ(fsync(fd.get()), 0);
  EXPECT_EQ(lseek(fd.get(), 0, SEEK_SET), 0);
  EXPECT_EQ(write(fd.get(), "Adios, World!\n", 14), 14);
  EXPECT_EQ(fdatasync(fd.get()), 0);
  fd.reset();
  EXPECT_EQ(unlink(alpha.c_str()), 0);
}

TEST_P(SyncTest, VerifyNoFsyncErrorsForDirectories) {
  const std::string dirname = GetPath("dirname");
  ASSERT_EQ(mkdir(dirname.c_str(), 0755), 0);
  auto fd = fbl::unique_fd(open(dirname.c_str(), O_RDONLY | O_DIRECTORY, 0644));
  ASSERT_GT(fd.get(), 0);
  EXPECT_EQ(fsync(fd.get()), 0) << strerror(errno);
  EXPECT_EQ(fdatasync(fd.get()), 0) << strerror(errno);
  fd.reset();
  EXPECT_EQ(unlink(dirname.c_str()), 0);
}

INSTANTIATE_TEST_SUITE_P(/*no prefix*/, SyncTest, testing::ValuesIn(AllTestFilesystems()),
                         testing::PrintToStringParamName());

}  // namespace
}  // namespace fs_test
