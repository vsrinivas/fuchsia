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

TEST(MemfsTests, TestMemfsNull) {
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  ASSERT_EQ(loop.StartThread(), ZX_OK);
  memfs_filesystem_t* vfs;
  zx_handle_t root;

  ASSERT_EQ(memfs_create_filesystem(loop.dispatcher(), &vfs, &root), ZX_OK);
  ASSERT_EQ(zx_handle_close(root), ZX_OK);
  sync_completion_t unmounted;
  memfs_free_filesystem(vfs, &unmounted);
  ASSERT_EQ(sync_completion_wait(&unmounted, zx::duration::infinite().get()), ZX_OK);
}

TEST(MemfsTests, TestMemfsBasic) {
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
  close(fd);

  // Readdir the file
  struct dirent* de;
  ASSERT_NOT_NULL((de = readdir(d)));
  ASSERT_EQ(strcmp(de->d_name, "."), 0);
  ASSERT_NOT_NULL((de = readdir(d)));
  ASSERT_EQ(strcmp(de->d_name, filename), 0);
  ASSERT_NULL(readdir(d));

  ASSERT_EQ(closedir(d), 0);
  sync_completion_t unmounted;
  memfs_free_filesystem(vfs, &unmounted);
  ASSERT_EQ(sync_completion_wait(&unmounted, zx::duration::infinite().get()), ZX_OK);
}

TEST(MemfsTests, TestMemfsAppend) {
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
  fd = openat(dirfd(d), filename, O_CREAT | O_RDWR | O_APPEND);
  ASSERT_GE(fd, 0);
  const char* data = "hello";
  ssize_t datalen = strlen(data);
  ASSERT_EQ(write(fd, data, datalen), datalen);
  ASSERT_EQ(lseek(fd, 0, SEEK_SET), 0);
  data = ", world";
  datalen = strlen(data);
  ASSERT_EQ(write(fd, data, datalen), datalen);
  ASSERT_EQ(lseek(fd, 0, SEEK_CUR), 12);
  ASSERT_EQ(lseek(fd, 0, SEEK_SET), 0);
  data = "hello, world";
  datalen = strlen(data);
  char buf[32];
  ASSERT_EQ(read(fd, buf, sizeof(buf)), datalen);
  ASSERT_EQ(memcmp(buf, data, datalen), 0);
  close(fd);

  ASSERT_EQ(closedir(d), 0);
  sync_completion_t unmounted;
  memfs_free_filesystem(vfs, &unmounted);
  ASSERT_EQ(sync_completion_wait(&unmounted, zx::duration::infinite().get()), ZX_OK);
}

TEST(MemfsTests, TestMemfsInstall) {
  memfs_filesystem_t* fs;
  memfs_filesystem_t* fs_2;
  {
    async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
    ASSERT_EQ(loop.StartThread(), ZX_OK);

    ASSERT_EQ(memfs_install_at(loop.dispatcher(), "/mytmp", &fs), ZX_OK);
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
    close(fd);

    // Readdir the file
    struct dirent* de;
    ASSERT_NOT_NULL((de = readdir(d)));
    ASSERT_EQ(strcmp(de->d_name, "."), 0);
    ASSERT_NOT_NULL((de = readdir(d)));
    ASSERT_EQ(strcmp(de->d_name, filename), 0);
    ASSERT_NULL(readdir(d));

    ASSERT_EQ(closedir(d), 0);
    ASSERT_EQ(memfs_install_at(loop.dispatcher(), "/mytmp", &fs_2), ZX_ERR_ALREADY_EXISTS);

    // Wait for cleanup of failed memfs install.
    sync_completion_t unmounted;
    async::PostTask(loop.dispatcher(), [&unmounted]() { sync_completion_signal(&unmounted); });
    ASSERT_EQ(sync_completion_wait(&unmounted, zx::duration::infinite().get()), ZX_OK);

    loop.Shutdown();
  }
  memfs_uninstall_unsafe(fs, "/mytmp");

  // No way to clean up the namespace entry. See fxbug.dev/31875 for more details.
}

TEST(MemfsTests, TestMemfsCloseDuringAccess) {
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
    ASSERT_NOT_NULL(d);
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
}

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
