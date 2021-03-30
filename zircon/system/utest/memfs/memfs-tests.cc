// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/cpp/task.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/memfs/memfs.h>
#include <limits.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <threads.h>
#include <unistd.h>
#include <zircon/processargs.h>
#include <zircon/syscalls.h>

#include <memory>

#include <fbl/unique_fd.h>
#include <fbl/vector.h>
#include <zxtest/zxtest.h>

namespace {

TEST(MemfsTests, TestMemfsOverflow) {
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  ASSERT_EQ(loop.StartThread(), ZX_OK);

  // Create a memfs filesystem, acquire a file descriptor
  memfs_filesystem_t* vfs;
  zx_handle_t root;
  ASSERT_EQ(memfs_create_filesystem(loop.dispatcher(), &vfs, &root), ZX_OK);
  int root_fd;
  ASSERT_EQ(fdio_fd_create(root, &root_fd), ZX_OK);

  // Access files within the filesystem.
  DIR* d = fdopendir(root_fd);
  ASSERT_NOT_NULL(d);

  // Issue writes to the file in an order that previously would have triggered
  // an overflow in the memfs write path.
  //
  // Values provided mimic the bug reported by syzkaller (fxbug.dev/33581).
  uint8_t buf[4096];
  memset(buf, 'a', sizeof(buf));
  fbl::unique_fd fd(openat(dirfd(d), "file", O_CREAT | O_RDWR));
  ASSERT_TRUE(fd);
  ASSERT_EQ(pwrite(fd.get(), buf, 199, 0), 199);
  ASSERT_EQ(pwrite(fd.get(), buf, 226, 0xfffffffffffff801), -1);
  ASSERT_EQ(errno, EINVAL);

  ASSERT_EQ(closedir(d), 0);
  sync_completion_t unmounted;
  memfs_free_filesystem(vfs, &unmounted);
  ASSERT_EQ(sync_completion_wait(&unmounted, zx::duration::infinite().get()), ZX_OK);
}

TEST(MemfsTests, TestMemfsDetachLinkedFilesystem) {
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  ASSERT_EQ(loop.StartThread(), ZX_OK);

  // Create a memfs filesystem, acquire a file descriptor
  memfs_filesystem_t* vfs;
  zx_handle_t root;
  ASSERT_EQ(memfs_create_filesystem(loop.dispatcher(), &vfs, &root), ZX_OK);
  int root_fd;
  ASSERT_EQ(fdio_fd_create(root, &root_fd), ZX_OK);

  // Access files within the filesystem.
  DIR* d = fdopendir(root_fd);
  ASSERT_NOT_NULL(d);

  // Leave a regular file.
  fbl::unique_fd fd(openat(dirfd(d), "file", O_CREAT | O_RDWR));
  ASSERT_TRUE(fd);

  // Leave an empty subdirectory.
  ASSERT_EQ(0, mkdirat(dirfd(d), "empty-subdirectory", 0));

  // Leave a subdirectory with children.
  ASSERT_EQ(0, mkdirat(dirfd(d), "subdirectory", 0));
  ASSERT_EQ(0, mkdirat(dirfd(d), "subdirectory/child", 0));

  ASSERT_EQ(closedir(d), 0);

  sync_completion_t unmounted;
  memfs_free_filesystem(vfs, &unmounted);
  ASSERT_EQ(sync_completion_wait(&unmounted, zx::duration::infinite().get()), ZX_OK);
}

}  // namespace
