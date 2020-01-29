// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <fuchsia/io/llcpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/fdio/cpp/caller.h>
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
#include <unittest/unittest.h>

namespace {

namespace fio = ::llcpp::fuchsia::io;

bool TestFidlBasic() {
  BEGIN_TEST;

  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  ASSERT_EQ(loop.StartThread(), ZX_OK);

  ASSERT_EQ(memfs_install_at(loop.dispatcher(), "/fidltmp"), ZX_OK);
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

  // No way to clean up the namespace entry. See ZX-2013 for more details.

  END_TEST;
}

bool TestFidlOpenReadOnly() {
  BEGIN_TEST;

  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  ASSERT_EQ(loop.StartThread(), ZX_OK);

  ASSERT_EQ(memfs_install_at(loop.dispatcher(), "/fidltmp-ro"), ZX_OK);
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

  // No way to clean up the namespace entry. See ZX-2013 for more details.

  END_TEST;
}

bool QueryInfo(const char* path, fio::FilesystemInfo* info) {
  BEGIN_HELPER;
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
  END_HELPER;
}

bool TestFidlQueryFilesystem() {
  BEGIN_TEST;

  {
    async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
    ASSERT_EQ(loop.StartThread(), ZX_OK);

    ASSERT_EQ(memfs_install_at(loop.dispatcher(), "/fidltmp-basic"), ZX_OK);
    fbl::unique_fd fd(open("/fidltmp-basic", O_DIRECTORY | O_RDONLY));
    ASSERT_GE(fd.get(), 0);

    // Sanity checks
    fio::FilesystemInfo info;
    ASSERT_TRUE(QueryInfo("/fidltmp-basic", &info));

    // Query number of blocks
    uint64_t physmem_size = zx_system_get_physmem();
    ASSERT_EQ(info.total_bytes, physmem_size);
    ASSERT_EQ(info.used_bytes, 0);

    loop.Shutdown();
  }

  // Query disk pressure in a page-limited scenario
  {
    async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
    ASSERT_EQ(loop.StartThread(), ZX_OK);

    size_t max_num_pages = 3;
    ASSERT_EQ(
        memfs_install_at_with_page_limit(loop.dispatcher(), max_num_pages, "/fidltmp-limited"),
        ZX_OK);
    fbl::unique_fd fd(open("/fidltmp-limited", O_DIRECTORY | O_RDONLY));
    ASSERT_GE(fd.get(), 0);

    fio::FilesystemInfo info;
    ASSERT_TRUE(QueryInfo("/fidltmp-limited", &info));

    // When space is limited, total_bytes must be a multiple of block_size
    ASSERT_EQ(info.total_bytes % info.block_size, 0);

    // Query number of blocks
    ASSERT_EQ(info.total_bytes, 3 * info.block_size);
    ASSERT_EQ(info.used_bytes, 0);

    // Create a file with a size smaller than ZX_PAGE_SIZE
    DIR* d = fdopendir(fd.release());
    const char* filename = "file-a";
    fd.reset(openat(dirfd(d), filename, O_CREAT | O_RDWR));
    ASSERT_GE(fd.get(), 0);
    const char* data = "hello";
    ssize_t datalen = strlen(data);
    ASSERT_EQ(write(fd.get(), data, datalen), datalen);

    // Query should now indicate an entire page is used
    ASSERT_TRUE(QueryInfo("/fidltmp-limited", &info));
    ASSERT_EQ(info.used_bytes, 1 * info.block_size);

    // Unlink and close the file
    ASSERT_EQ(unlinkat(dirfd(d), filename, 0), 0);
    ASSERT_EQ(close(fd.get()), 0);
    fd.reset();

    // Query should now indicate 0 bytes used
    ASSERT_TRUE(QueryInfo("/fidltmp-limited", &info));
    ASSERT_EQ(info.used_bytes, 0);

    closedir(d);
    loop.Shutdown();
  }

  // No way to clean up the namespace entry. See ZX-2013 for more details.

  END_TEST;
}

}  // namespace

BEGIN_TEST_CASE(fidl_tests)
RUN_TEST(TestFidlBasic)
RUN_TEST(TestFidlOpenReadOnly)
RUN_TEST(TestFidlQueryFilesystem)
END_TEST_CASE(fidl_tests)
