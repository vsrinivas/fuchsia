// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
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
#include <unittest/unittest.h>

namespace {

bool TestMemfsNull() {
  BEGIN_TEST;

  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  ASSERT_EQ(loop.StartThread(), ZX_OK);
  memfs_filesystem_t* vfs;
  zx_handle_t root;

  ASSERT_EQ(memfs_create_filesystem(loop.dispatcher(), &vfs, &root), ZX_OK);
  ASSERT_EQ(zx_handle_close(root), ZX_OK);
  sync_completion_t unmounted;
  memfs_free_filesystem(vfs, &unmounted);
  ASSERT_EQ(sync_completion_wait(&unmounted, zx::duration::infinite().get()), ZX_OK);

  END_TEST;
}

bool TestMemfsBasic() {
  BEGIN_TEST;

  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  ASSERT_EQ(loop.StartThread(), ZX_OK);

  // Create a memfs filesystem, acquire a file descriptor
  memfs_filesystem_t* vfs;
  zx_handle_t root;
  ASSERT_EQ(memfs_create_filesystem(loop.dispatcher(), &vfs, &root), ZX_OK);
  int fd;
  ASSERT_EQ(fdio_fd_create(root, &fd), ZX_OK);

  // Access files within the filesystem.
  DIR* d = fdopendir(fd);

  // Create a file
  const char* filename = "file-a";
  fd = openat(dirfd(d), filename, O_CREAT | O_RDWR);
  ASSERT_GE(fd, 0);
  const char* data = "hello";
  ssize_t datalen = strlen(data);
  ASSERT_EQ(write(fd, data, datalen), datalen);
  ASSERT_EQ(lseek(fd, 0, SEEK_SET), 0);
  char buf[32];
  ASSERT_EQ(read(fd, buf, sizeof(buf)), datalen);
  ASSERT_EQ(memcmp(buf, data, datalen), 0);

  // Readdir the file
  struct dirent* de;
  ASSERT_NONNULL((de = readdir(d)));
  ASSERT_EQ(strcmp(de->d_name, "."), 0);
  ASSERT_NONNULL((de = readdir(d)));
  ASSERT_EQ(strcmp(de->d_name, filename), 0);
  ASSERT_NULL(readdir(d));

  ASSERT_EQ(closedir(d), 0);
  sync_completion_t unmounted;
  memfs_free_filesystem(vfs, &unmounted);
  ASSERT_EQ(sync_completion_wait(&unmounted, zx::duration::infinite().get()), ZX_OK);

  END_TEST;
}

bool TestMemfsInstall() {
  BEGIN_TEST;

  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  ASSERT_EQ(loop.StartThread(), ZX_OK);

  ASSERT_EQ(memfs_install_at(loop.dispatcher(), "/mytmp"), ZX_OK);
  int fd = open("/mytmp", O_DIRECTORY | O_RDONLY);
  ASSERT_GE(fd, 0);

  // Access files within the filesystem.
  DIR* d = fdopendir(fd);

  // Create a file
  const char* filename = "file-a";
  fd = openat(dirfd(d), filename, O_CREAT | O_RDWR);
  ASSERT_GE(fd, 0);
  const char* data = "hello";
  ssize_t datalen = strlen(data);
  ASSERT_EQ(write(fd, data, datalen), datalen);
  ASSERT_EQ(lseek(fd, 0, SEEK_SET), 0);
  char buf[32];
  ASSERT_EQ(read(fd, buf, sizeof(buf)), datalen);
  ASSERT_EQ(memcmp(buf, data, datalen), 0);

  // Readdir the file
  struct dirent* de;
  ASSERT_NONNULL((de = readdir(d)));
  ASSERT_EQ(strcmp(de->d_name, "."), 0);
  ASSERT_NONNULL((de = readdir(d)));
  ASSERT_EQ(strcmp(de->d_name, filename), 0);
  ASSERT_NULL(readdir(d));

  ASSERT_EQ(closedir(d), 0);

  ASSERT_EQ(memfs_install_at(loop.dispatcher(), "/mytmp"), ZX_ERR_ALREADY_EXISTS);

  loop.Shutdown();

  // No way to clean up the namespace entry. See ZX-2013 for more details.

  END_TEST;
}

bool TestMemfsCloseDuringAccess() {
  BEGIN_TEST;

  for (int i = 0; i < 100; i++) {
    async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
    ASSERT_EQ(loop.StartThread(), ZX_OK);

    // Create a memfs filesystem, acquire a file descriptor
    memfs_filesystem_t* vfs;
    zx_handle_t root;
    ASSERT_EQ(memfs_create_filesystem(loop.dispatcher(), &vfs, &root), ZX_OK);
    int fd = -1;
    ASSERT_EQ(fdio_fd_create(root, &fd), ZX_OK);

    // Access files within the filesystem.
    DIR* d = fdopendir(fd);
    ASSERT_NONNULL(d);
    thrd_t worker;

    struct thread_args {
      DIR* d;
      sync_completion_t spinning{};
    } args{
        .d = d,
    };

    ASSERT_EQ(thrd_create(
                  &worker,
                  [](void* arg) {
                    thread_args* args = reinterpret_cast<thread_args*>(arg);
                    DIR* d = args->d;
                    int fd = openat(dirfd(d), "foo", O_CREAT | O_RDWR);
                    while (true) {
                      if (close(fd)) {
                        return errno == EPIPE ? 0 : -1;
                      }

                      if ((fd = openat(dirfd(d), "foo", O_RDWR)) < 0) {
                        return errno == EPIPE ? 0 : -1;
                      }
                      sync_completion_signal(&args->spinning);
                    }
                  },
                  &args),
              thrd_success);

    ASSERT_EQ(sync_completion_wait(&args.spinning, zx::duration::infinite().get()), ZX_OK);

    sync_completion_t unmounted;
    memfs_free_filesystem(vfs, &unmounted);
    ASSERT_EQ(sync_completion_wait(&unmounted, zx::duration::infinite().get()), ZX_OK);

    int result;
    ASSERT_EQ(thrd_join(worker, &result), thrd_success);
    ASSERT_EQ(result, 0);

    // Now that the filesystem has terminated, we should be
    // unable to access it.
    ASSERT_LT(openat(dirfd(d), "foo", O_CREAT | O_RDWR), 0);
    ASSERT_EQ(errno, EPIPE, "Expected connection to remote server to be closed");

    // Since the filesystem has terminated, this will
    // only close the client side of the connection.
    ASSERT_EQ(closedir(d), 0);
  }

  END_TEST;
}

bool TestMemfsOverflow() {
  BEGIN_TEST;

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
  ASSERT_NONNULL(d);

  // Issue writes to the file in an order that previously would have triggered
  // an overflow in the memfs write path.
  //
  // Values provided mimic the bug reported by syzkaller (ZX-3791).
  uint8_t buf[4096];
  memset(buf, 'a', sizeof(buf));
  fbl::unique_fd fd(openat(dirfd(d), "file", O_CREAT | O_RDWR));
  ASSERT_TRUE(fd);
  ASSERT_EQ(pwrite(fd.get(), buf, 199, 0), 199);
  ASSERT_EQ(pwrite(fd.get(), buf, 226, 0xfffffffffffff801), -1);
  ASSERT_EQ(errno, EFBIG);

  ASSERT_EQ(closedir(d), 0);
  sync_completion_t unmounted;
  memfs_free_filesystem(vfs, &unmounted);
  ASSERT_EQ(sync_completion_wait(&unmounted, zx::duration::infinite().get()), ZX_OK);
  END_TEST;
}

bool TestMemfsDetachLinkedFilesystem() {
  BEGIN_TEST;

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
  ASSERT_NONNULL(d);

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
  END_TEST;
}

}  // namespace

BEGIN_TEST_CASE(memfs_tests)
RUN_TEST(TestMemfsNull)
RUN_TEST(TestMemfsBasic)
RUN_TEST(TestMemfsInstall)
RUN_TEST(TestMemfsCloseDuringAccess)
RUN_TEST(TestMemfsOverflow)
RUN_TEST(TestMemfsDetachLinkedFilesystem)
END_TEST_CASE(memfs_tests)
