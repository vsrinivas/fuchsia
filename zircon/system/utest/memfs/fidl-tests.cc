// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <fuchsia/io/llcpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fdio/cpp/caller.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/memfs/memfs.h>
#include <limits.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <threads.h>
#include <unistd.h>
#include <zircon/device/vfs.h>
#include <zircon/processargs.h>
#include <zircon/syscalls.h>

#include <utility>

#include <fbl/unique_fd.h>
#include <zxtest/zxtest.h>

namespace {

namespace fio = ::llcpp::fuchsia::io;

TEST(FidlTests, TestFidlBasic) {
  memfs_filesystem_t* fs;
  {
    async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
    ASSERT_EQ(loop.StartThread(), ZX_OK);

    ASSERT_EQ(memfs_install_at(loop.dispatcher(), "/fidltmp", &fs), ZX_OK);
    fbl::unique_fd fd(open("/fidltmp", O_DIRECTORY | O_RDONLY));
    ASSERT_GE(fd.get(), 0);

    // Create a file
    const char* filename = "file-a";
    fd.reset(openat(fd.get(), filename, O_CREAT | O_RDWR));
    ASSERT_GE(fd.get(), 0);
    const char* data = "hello";
    ssize_t datalen = strlen(data);
    ASSERT_EQ(write(fd.get(), data, datalen), datalen);
    fd.reset();

    zx_handle_t h, request;
    ASSERT_EQ(zx_channel_create(0, &h, &request), ZX_OK);
    ASSERT_EQ(fdio_service_connect("/fidltmp/file-a", request), ZX_OK);

    auto describe_result = fio::File::Call::Describe(zx::unowned_channel(h));
    ASSERT_EQ(describe_result.status(), ZX_OK);
    ASSERT_TRUE(describe_result.Unwrap()->info.is_file());
    ASSERT_EQ(describe_result.Unwrap()->info.file().event.get(), ZX_HANDLE_INVALID);
    zx_handle_close(h);

    loop.Shutdown();
  }
  ASSERT_EQ(memfs_uninstall_unsafe(fs, "/fidltmp"), ZX_OK);

  // No way to clean up the namespace entry. See fxbug.dev/31875 for more details.
}

TEST(FidlTests, TestFidlOpenReadOnly) {
  memfs_filesystem_t* fs;
  {
    async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
    ASSERT_EQ(loop.StartThread(), ZX_OK);

    ASSERT_EQ(memfs_install_at(loop.dispatcher(), "/fidltmp-ro", &fs), ZX_OK);
    fbl::unique_fd fd(open("/fidltmp-ro", O_DIRECTORY | O_RDONLY));
    ASSERT_GE(fd.get(), 0);

    // Create a file
    const char* filename = "file-ro";
    fd.reset(openat(fd.get(), filename, O_CREAT | O_RDWR));
    ASSERT_GE(fd.get(), 0);
    fd.reset();

    zx_handle_t h, request;
    ASSERT_EQ(zx_channel_create(0, &h, &request), ZX_OK);
    ASSERT_EQ(fdio_open("/fidltmp-ro/file-ro", ZX_FS_RIGHT_READABLE, request), ZX_OK);

    auto result = fio::File::Call::GetFlags(zx::unowned_channel(h));
    ASSERT_EQ(result.status(), ZX_OK);
    ASSERT_EQ(result.Unwrap()->s, ZX_OK);
    ASSERT_EQ(result.Unwrap()->flags, ZX_FS_RIGHT_READABLE);
    zx_handle_close(h);

    loop.Shutdown();
  }
  memfs_uninstall_unsafe(fs, "/fidltmp-ro");

  // No way to clean up the namespace entry. See fxbug.dev/31875 for more details.
}

void QueryInfo(const char* path, fio::FilesystemInfo* info) {
  fbl::unique_fd fd(open(path, O_RDONLY | O_DIRECTORY));
  ASSERT_TRUE(fd);
  fdio_cpp::FdioCaller caller(std::move(fd));
  auto result = fio::DirectoryAdmin::Call::QueryFilesystem((caller.channel()));
  ASSERT_EQ(result.status(), ZX_OK);
  ASSERT_EQ(result.Unwrap()->s, ZX_OK);
  ASSERT_NOT_NULL(result.Unwrap()->info);
  *info = *(result.Unwrap()->info);
  const char* kFsName = "memfs";
  const char* name = reinterpret_cast<const char*>(info->name.data());
  ASSERT_EQ(strncmp(name, kFsName, strlen(kFsName)), 0, "Unexpected filesystem mounted");
  ASSERT_EQ(info->block_size, ZX_PAGE_SIZE);
  ASSERT_EQ(info->max_filename_size, NAME_MAX);
  ASSERT_EQ(info->fs_type, VFS_TYPE_MEMFS);
  ASSERT_NE(info->fs_id, 0);
  ASSERT_EQ(info->used_bytes % info->block_size, 0);
}

TEST(FidlTests, TestFidlQueryFilesystem) {
  {
    async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
    ASSERT_EQ(loop.StartThread(), ZX_OK);
    memfs_filesystem_t* fs;
    ASSERT_EQ(memfs_install_at(loop.dispatcher(), "/fidltmp-basic", &fs), ZX_OK);
    fbl::unique_fd fd(open("/fidltmp-basic", O_DIRECTORY | O_RDONLY));
    ASSERT_GE(fd.get(), 0);

    // Sanity checks
    fio::FilesystemInfo info;
    ASSERT_NO_FATAL_FAILURES(QueryInfo("/fidltmp-basic", &info));

    // These values are nonsense, but they're the nonsense we expect memfs to generate.
    ASSERT_EQ(info.total_bytes, UINT64_MAX);
    ASSERT_EQ(info.used_bytes, 0);

    loop.Shutdown();
    ASSERT_EQ(memfs_uninstall_unsafe(fs, "/fidltmp-basic"), ZX_OK);
  }

  // No way to clean up the namespace entry. See fxbug.dev/31875 for more details.
}

}  // namespace
