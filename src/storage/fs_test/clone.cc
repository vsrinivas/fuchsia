// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <lib/fdio/fd.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <zircon/errors.h>
#include <zircon/types.h>

#include <fbl/unique_fd.h>

#include "src/storage/fs_test/fs_test_fixture.h"

namespace fs_test {
namespace {

using CloneTest = FilesystemTest;

TEST_P(CloneTest, SimpleClone) {
  std::string file = GetPath("file");
  fbl::unique_fd fd(open(file.c_str(), O_RDWR | O_CREAT, 0644));
  ASSERT_TRUE(fd);

  zx_handle_t handle = ZX_HANDLE_INVALID;
  ASSERT_EQ(fdio_fd_clone(fd.get(), &handle), ZX_OK);

  fbl::unique_fd fd2(-1);
  int fd_out;
  ASSERT_EQ(fdio_fd_create(handle, &fd_out), ZX_OK);
  fd2.reset(fd_out);

  // Output from one fd...
  char output[5];
  memset(output, 'a', sizeof(output));
  ASSERT_EQ(write(fd.get(), output, sizeof(output)), static_cast<ssize_t>(sizeof(output)));

  ASSERT_EQ(lseek(fd.get(), 0, SEEK_SET), 0);

  // ... Should be visible to the other fd.
  char input[5];
  ASSERT_EQ(read(fd2.get(), input, sizeof(input)), static_cast<ssize_t>(sizeof(input)));
  ASSERT_EQ(memcmp(input, output, sizeof(input)), 0);

  // Clean up
  ASSERT_EQ(close(fd.release()), 0);
  ASSERT_EQ(close(fd2.release()), 0);
  ASSERT_EQ(unlink(file.c_str()), 0);
}

INSTANTIATE_TEST_SUITE_P(/*no prefix*/, CloneTest, testing::ValuesIn(AllTestFilesystems()),
                         testing::PrintToStringParamName());

}  // namespace
}  // namespace fs_test
