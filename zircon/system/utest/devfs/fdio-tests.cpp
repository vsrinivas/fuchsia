// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <unistd.h>

#include <fbl/unique_fd.h>
#include <fuchsia/io/c/fidl.h>
#include <lib/fdio/limits.h>
#include <lib/fdio/util.h>
#include <unittest/unittest.h>
#include <zircon/processargs.h>
#include <zircon/syscalls.h>

namespace {

bool TestDeviceClone() {
    BEGIN_TEST;

    fbl::unique_fd fd(open("/dev/zero", O_RDONLY));

    zx_handle_t handle = ZX_HANDLE_INVALID;
    zx_status_t status = fdio_fd_clone(fd.get(), &handle);
    ASSERT_EQ(status, ZX_OK);
    ASSERT_NE(handle, ZX_HANDLE_INVALID);
    zx_handle_close(handle);

    END_TEST;
}

bool TestDeviceTransfer() {
    BEGIN_TEST;

    fbl::unique_fd fd(open("/dev/zero", O_RDONLY));

    zx_handle_t handle = ZX_HANDLE_INVALID;
    zx_status_t status = fdio_fd_transfer(fd.release(), &handle);
    ASSERT_EQ(status, ZX_OK);
    ASSERT_NE(handle, ZX_HANDLE_INVALID);
    zx_handle_close(handle);

    END_TEST;
}

} // namespace

BEGIN_TEST_CASE(fdio_tests)
RUN_TEST(TestDeviceClone)
RUN_TEST(TestDeviceTransfer)
END_TEST_CASE(fdio_tests)
