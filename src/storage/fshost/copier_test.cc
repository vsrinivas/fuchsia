// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/fshost/copier.h"

#include <unistd.h>

#include <gtest/gtest.h>

namespace fshost {
namespace {

TEST(Copier, Copy) {
  ASSERT_EQ(mkdir("/tmp/copier_test", 0777), 0);
  ASSERT_EQ(mkdir("/tmp/copier_test/dir", 0777), 0);
  fbl::unique_fd fd(open("/tmp/copier_test/file1", O_RDWR | O_CREAT, 0666));
  ASSERT_TRUE(fd);
  ASSERT_EQ(write(fd.get(), "hello", 5), 5);
  fd = fbl::unique_fd(open("/tmp/copier_test/dir/file2", O_RDWR | O_CREAT, 0666));
  ASSERT_TRUE(fd);
  ASSERT_EQ(write(fd.get(), "hello", 5), 5);

  fd = fbl::unique_fd(open("/tmp/copier_test", O_RDONLY));
  ASSERT_TRUE(fd);

  auto data_or = Copier::Read(std::move(fd));
  ASSERT_TRUE(data_or.is_ok()) << data_or.status_string();

  ASSERT_EQ(mkdir("/tmp/copier_test/copied", 0777), 0);
  fd = fbl::unique_fd(open("/tmp/copier_test/copied", O_RDONLY));
  ASSERT_TRUE(fd);

  EXPECT_EQ(data_or->Write(std::move(fd)), ZX_OK);

  fd = fbl::unique_fd(open("/tmp/copier_test/copied/file1", O_RDONLY));
  ASSERT_TRUE(fd);
  char buf[5];
  EXPECT_EQ(read(fd.get(), buf, 5), 5);
  EXPECT_EQ(memcmp("hello", buf, 5), 0);

  fd = fbl::unique_fd(open("/tmp/copier_test/copied/dir/file2", O_RDONLY));
  ASSERT_TRUE(fd);
  EXPECT_EQ(read(fd.get(), buf, 5), 5);
  EXPECT_EQ(memcmp("hello", buf, 5), 0);
}

}  // namespace
}  // namespace fshost
