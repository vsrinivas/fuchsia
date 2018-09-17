// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <threads.h>
#include <unistd.h>

#include <fbl/unique_fd.h>
#include <fuchsia/io/c/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/fdio/util.h>
#include <lib/memfs/memfs.h>
#include <unittest/unittest.h>
#include <zircon/processargs.h>
#include <zircon/syscalls.h>

namespace {

bool test_fidl_basic() {
    BEGIN_TEST;

    async::Loop loop(&kAsyncLoopConfigNoAttachToThread);
    ASSERT_EQ(loop.StartThread(), ZX_OK);

    ASSERT_EQ(memfs_install_at(loop.dispatcher(), "/fidltmp"), ZX_OK);
    fbl::unique_fd fd(open("/fidltmp", O_DIRECTORY | O_RDONLY));
    ASSERT_GE(fd.get(), 0);

    // Access files within the filesystem.
    DIR* d = fdopendir(fd.release());

    // Create a file
    const char* filename = "file-a";
    fd.reset(openat(dirfd(d), filename, O_CREAT | O_RDWR));
    ASSERT_GE(fd.get(), 0);
    const char* data = "hello";
    ssize_t datalen = strlen(data);
    ASSERT_EQ(write(fd.get(), data, datalen), datalen);
    fd.reset();

    zx_handle_t h, request = ZX_HANDLE_INVALID;
    ASSERT_EQ(zx_channel_create(0, &h, &request), ZX_OK);
    ASSERT_EQ(fdio_service_connect("/fidltmp/file-a", request), ZX_OK);

    fuchsia_io_NodeInfo info = {};
    ASSERT_EQ(fuchsia_io_FileDescribe(h, &info), ZX_OK);
    ASSERT_EQ(info.tag, fuchsia_io_NodeInfoTag_file);
    ASSERT_EQ(info.file.event, ZX_HANDLE_INVALID);
    zx_handle_close(h);
    closedir(d);

    loop.Shutdown();

    // No way to clean up the namespace entry. See ZX-2013 for more details.

    END_TEST;
}

} // namespace

BEGIN_TEST_CASE(fidl_tests)
RUN_TEST(test_fidl_basic)
END_TEST_CASE(fidl_tests)
