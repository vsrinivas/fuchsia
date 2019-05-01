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
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/fdio/directory.h>
#include <lib/memfs/cpp/vnode.h>
#include <lib/memfs/memfs.h>
#include <lib/zx/channel.h>
#include <lib/zx/vmo.h>
#include <unittest/unittest.h>
#include <zircon/processargs.h>
#include <zircon/syscalls.h>

namespace {

bool test_vmofile_basic() {
    BEGIN_TEST;

    async::Loop loop(&kAsyncLoopConfigNoAttachToThread);
    ASSERT_EQ(loop.StartThread(), ZX_OK);
    async_dispatcher_t* dispatcher = loop.dispatcher();

    zx::channel client, server;
    ASSERT_EQ(zx::channel::create(0, &client, &server), ZX_OK);

    memfs::Vfs vfs;
    vfs.SetDispatcher(dispatcher);

    fbl::RefPtr<memfs::VnodeDir> root;
    ASSERT_EQ(memfs::CreateFilesystem("<tmp>", &vfs, &root), ZX_OK);

    zx::vmo backing_vmo;
    ASSERT_EQ(zx::vmo::create(64, 0, &backing_vmo), ZX_OK);
    ASSERT_EQ(backing_vmo.write("hello, world!", 0, 13), ZX_OK);
    ASSERT_EQ(vfs.CreateFromVmo(root.get(), "greeting", backing_vmo.get(), 0, 13),
              ZX_OK);
    ASSERT_EQ(vfs.ServeDirectory(std::move(root), std::move(server)), ZX_OK);

    zx::channel h, request;
    ASSERT_EQ(zx::channel::create(0, &h, &request), ZX_OK);
    ASSERT_EQ(fuchsia_io_DirectoryOpen(client.get(), ZX_FS_RIGHT_READABLE, 0,
                                       "greeting", 8, request.release()),
              ZX_OK);

    zx_status_t status = ZX_OK;
    fuchsia_mem_Buffer buffer = {};
    ASSERT_EQ(fuchsia_io_FileGetBuffer(h.get(), fuchsia_io_VMO_FLAG_READ, &status, &buffer), ZX_OK);
    ASSERT_EQ(status, ZX_OK);
    ASSERT_NE(buffer.vmo, ZX_HANDLE_INVALID);
    ASSERT_EQ(buffer.size, 13);
    zx_handle_close(buffer.vmo);

    fuchsia_io_NodeInfo info = {};
    ASSERT_EQ(fuchsia_io_FileDescribe(h.get(), &info), ZX_OK);
    ASSERT_EQ(info.tag, fuchsia_io_NodeInfoTag_vmofile);
    ASSERT_EQ(info.vmofile.offset, 0u);
    ASSERT_EQ(info.vmofile.length, 13u);
    zx_handle_close(info.vmofile.vmo);

    uint64_t seek = 0u;
    ASSERT_EQ(fuchsia_io_FileSeek(h.get(), 7u, fuchsia_io_SeekOrigin_START,
                                  &status, &seek),
              ZX_OK);
    ASSERT_EQ(status, ZX_OK);
    ASSERT_EQ(seek, 7u);
    memset(&info, 0, sizeof(info));
    ASSERT_EQ(fuchsia_io_FileDescribe(h.get(), &info), ZX_OK);
    ASSERT_EQ(info.tag, fuchsia_io_NodeInfoTag_vmofile);
    ASSERT_EQ(info.vmofile.offset, 0u);
    ASSERT_EQ(info.vmofile.length, 13u);
    zx_handle_close(info.vmofile.vmo);

    h.reset();

    vfs.Shutdown([](zx_status_t status) {
        EXPECT_EQ(status, ZX_OK);
    });

    loop.Shutdown();

    END_TEST;
}

} // namespace

BEGIN_TEST_CASE(vmofile_tests)
RUN_TEST(test_vmofile_basic)
END_TEST_CASE(vmofile_tests)
