// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fbl/unique_fd.h>
#include <fcntl.h>
#include <fuchsia/io/c/fidl.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/unsafe.h>
#include <lib/zx/channel.h>
#include <unistd.h>

#include <unittest/unittest.h>

static bool unsafe_borrow_channel_test() {
    BEGIN_TEST;

    fbl::unique_fd fd(open("/svc", O_DIRECTORY | O_RDONLY));
    ASSERT_LE(0, fd.get());

    fdio_t* io = fdio_unsafe_fd_to_io(fd.get());
    ASSERT_NE(nullptr, io);

    zx::unowned_channel dir(fdio_unsafe_borrow_channel(io));
    ASSERT_TRUE(dir->is_valid());

    zx::channel h1, h2;
    ASSERT_EQ(ZX_OK, zx::channel::create(0, &h1, &h2));
    ASSERT_EQ(ZX_OK, fuchsia_io_NodeClone(dir->get(),
                                          fuchsia_io_CLONE_FLAG_SAME_RIGHTS,
                                          h1.release()));

    fdio_unsafe_release(io);
    fd.reset();

    END_TEST;
}

BEGIN_TEST_CASE(fdio_unsafe_test)
RUN_TEST(unsafe_borrow_channel_test)
END_TEST_CASE(fdio_unsafe_test)
