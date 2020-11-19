// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fbl/unique_fd.h>
#include <limits.h>
#include <sys/sendfile.h>
#include <sys/types.h>
#include <unistd.h>

#include <zxtest/zxtest.h>

TEST(SendfileTest, Smoke) {
  char src_path[] = "/tmp/sendfile_smoke.XXXXXX";

  fbl::unique_fd src_file(mkstemp(src_path));
  ASSERT_TRUE(src_file.is_valid());
  unlink(src_path);

  int dst_pipe[2];
  ASSERT_NE(-1, pipe(dst_pipe));

  const char* message = "hello, world";
  size_t message_length = strlen(message);
  ASSERT_EQ(message_length, pwrite(src_file.get(), message, message_length, 0));

  ASSERT_EQ(5, sendfile(dst_pipe[1], src_file.get(), nullptr, 5));
  close(dst_pipe[1]);

  ASSERT_EQ(5, lseek(src_file.get(), 0, SEEK_CUR));

  char buffer[1024] = {};
  ASSERT_EQ(5, read(dst_pipe[0], buffer, sizeof(buffer)));
  close(dst_pipe[0]);

  EXPECT_EQ(0, strcmp(buffer, "hello"));
}

TEST(SendfileTest, BadFileDescriptor) {
  ASSERT_EQ(-1, sendfile(-1, -1, nullptr, 1));
  ASSERT_EQ(EBADF, errno);
}

TEST(SendfileTest, WithOffset) {
  char src_path[] = "/tmp/sendfile_offset.XXXXXX";

  fbl::unique_fd src_file(mkstemp(src_path));
  ASSERT_TRUE(src_file.is_valid());
  unlink(src_path);

  int dst_pipe[2];
  ASSERT_NE(-1, pipe(dst_pipe));

  const char* message = "hello, world";
  size_t message_length = strlen(message);
  ASSERT_EQ(message_length, pwrite(src_file.get(), message, message_length, 0));

  off_t offset = 3;

  ASSERT_EQ(5, sendfile(dst_pipe[1], src_file.get(), &offset, 5));
  close(dst_pipe[1]);

  ASSERT_EQ(8, offset);
  ASSERT_EQ(0, lseek(src_file.get(), 0, SEEK_CUR));

  char buffer[1024] = {};
  ASSERT_EQ(5, read(dst_pipe[0], buffer, sizeof(buffer)));
  close(dst_pipe[0]);
  EXPECT_EQ(0, strcmp(buffer, "lo, w"));
}
