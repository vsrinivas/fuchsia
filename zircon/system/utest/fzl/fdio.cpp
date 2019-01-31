// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <unistd.h>

#include <fbl/unique_fd.h>
#include <fuchsia/io/c/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/memfs/memfs.h>
#include <lib/fzl/fdio.h>
#include <unittest/unittest.h>

#include <utility>

namespace {

bool fdio_call_io() {
    BEGIN_TEST;

    // Create a Memfs filesystem.
    async::Loop loop(&kAsyncLoopConfigNoAttachToThread);
    ASSERT_EQ(loop.StartThread(), ZX_OK);
    ASSERT_EQ(memfs_install_at(loop.dispatcher(), "/my-tmp"), ZX_OK);
    fbl::unique_fd dir(open("/my-tmp", O_DIRECTORY | O_RDONLY));
    ASSERT_TRUE(dir);

    // Open a file within the filesystem.
    fbl::unique_fd fd(openat(dir.get(), "my-file", O_CREAT | O_RDWR));
    ASSERT_TRUE(fd);

    // Try some filesystem operations natively:
    fzl::FdioCaller caller(std::move(fd));
    ASSERT_TRUE(caller);

    const char* golden = "foobar";
    zx_status_t status;
    uint64_t actual;
    ASSERT_EQ(fuchsia_io_FileWrite(caller.borrow_channel(),
                                   reinterpret_cast<const uint8_t*>(golden),
                                   strlen(golden), &status, &actual),
              ZX_OK);
    ASSERT_EQ(status, ZX_OK);
    ASSERT_EQ(actual, strlen(golden));

    ASSERT_EQ(fuchsia_io_FileSeek(caller.borrow_channel(), 0L, fuchsia_io_SeekOrigin_START,
                                  &status, &actual),
              ZX_OK);
    ASSERT_EQ(status, ZX_OK);
    ASSERT_EQ(actual, 0);

    char buf[256];
    ASSERT_EQ(fuchsia_io_FileRead(caller.borrow_channel(), static_cast<uint64_t>(sizeof(buf)),
                                  &status, reinterpret_cast<uint8_t*>(buf), sizeof(buf), &actual),
              ZX_OK);
    ASSERT_EQ(status, ZX_OK);
    ASSERT_EQ(actual, strlen(golden));
    ASSERT_EQ(memcmp(buf, golden, strlen(golden)), 0);

    // Re-acquire the underlying fd.
    fd = caller.release();
    ASSERT_EQ(close(fd.release()), 0);

    END_TEST;
}

}  // namespace

BEGIN_TEST_CASE(fdio_call_tests)
RUN_TEST(fdio_call_io)
END_TEST_CASE(fdio_call_tests)

