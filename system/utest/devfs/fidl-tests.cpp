// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/io/c/fidl.h>
#include <lib/fdio/util.h>
#include <unittest/unittest.h>
#include <zircon/syscalls.h>

namespace {

bool TestFidlBasic() {
    BEGIN_TEST;

    zx_handle_t h = ZX_HANDLE_INVALID;
    zx_handle_t request = ZX_HANDLE_INVALID;
    fuchsia_io_NodeInfo info = {};

    ASSERT_EQ(zx_channel_create(0, &h, &request), ZX_OK);
    ASSERT_EQ(fdio_service_connect("/dev/class", request), ZX_OK);
    memset(&info, 0, sizeof(info));
    ASSERT_EQ(fuchsia_io_FileDescribe(h, &info), ZX_OK);
    ASSERT_EQ(info.tag, fuchsia_io_NodeInfoTag_directory);
    zx_handle_close(h);

    ASSERT_EQ(zx_channel_create(0, &h, &request), ZX_OK);
    ASSERT_EQ(fdio_service_connect("/dev/zero", request), ZX_OK);
    memset(&info, 0, sizeof(info));
    ASSERT_EQ(fuchsia_io_FileDescribe(h, &info), ZX_OK);
    ASSERT_EQ(info.tag, fuchsia_io_NodeInfoTag_device);
    ASSERT_NE(info.device.event, ZX_HANDLE_INVALID);
    zx_handle_close(info.device.event);
    zx_handle_close(h);

    END_TEST;
}

} // namespace

BEGIN_TEST_CASE(fidl_tests)
RUN_TEST(TestFidlBasic)
END_TEST_CASE(fidl_tests)
