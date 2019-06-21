// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/io/c/fidl.h>
#include <fuchsia/process/c/fidl.h>
#include <lib/fdio/directory.h>
#include <lib/zx/channel.h>

#include <unittest/unittest.h>

static bool service_connect_test() {
    BEGIN_TEST;

    ASSERT_EQ(ZX_ERR_INVALID_ARGS, fdio_service_connect(nullptr, ZX_HANDLE_INVALID));

    zx::channel h1, h2;
    ASSERT_EQ(ZX_OK, zx::channel::create(0, &h1, &h2));
    ASSERT_EQ(ZX_ERR_NOT_FOUND, fdio_service_connect("/x/y/z", h1.release()));
    ASSERT_EQ(ZX_ERR_NOT_SUPPORTED, fdio_service_connect("/", h2.release()));

    ASSERT_EQ(ZX_OK, zx::channel::create(0, &h1, &h2));
    ASSERT_EQ(ZX_OK, fdio_service_connect("/svc/" fuchsia_process_Launcher_Name, h1.release()));

    END_TEST;
}

static bool open_test() {
    BEGIN_TEST;

    ASSERT_EQ(ZX_ERR_INVALID_ARGS, fdio_open(nullptr, 0, ZX_HANDLE_INVALID));

    zx::channel h1, h2;
    ASSERT_EQ(ZX_OK, zx::channel::create(0, &h1, &h2));
    ASSERT_EQ(ZX_ERR_NOT_FOUND, fdio_open("/x/y/z", fuchsia_io_OPEN_RIGHT_READABLE, h1.release()));
    ASSERT_EQ(ZX_ERR_NOT_SUPPORTED, fdio_open("/", fuchsia_io_OPEN_RIGHT_READABLE, h2.release()));

    ASSERT_EQ(ZX_OK, zx::channel::create(0, &h1, &h2));
    ASSERT_EQ(ZX_OK, fdio_open("/svc", fuchsia_io_OPEN_RIGHT_READABLE, h1.release()));

    zx::channel h3, h4;
    ASSERT_EQ(ZX_OK, zx::channel::create(0, &h3, &h4));
    ASSERT_EQ(ZX_OK, fdio_service_connect_at(h2.get(), fuchsia_process_Launcher_Name,
                                             h3.release()));
    ASSERT_EQ(ZX_OK, fdio_open_at(h2.get(), fuchsia_process_Launcher_Name,
                                  fuchsia_io_OPEN_RIGHT_READABLE, h4.release()));

    h3.reset(fdio_service_clone(h2.get()));
    ASSERT_TRUE(h3.is_valid());

    ASSERT_EQ(ZX_OK, zx::channel::create(0, &h3, &h4));
    ASSERT_EQ(ZX_ERR_INVALID_ARGS, fdio_service_clone_to(h2.get(), ZX_HANDLE_INVALID));
    ASSERT_EQ(ZX_OK, fdio_service_clone_to(h2.get(), h3.release()));

    END_TEST;
}

BEGIN_TEST_CASE(fdio_directory_test)
RUN_TEST(service_connect_test)
RUN_TEST(open_test)
END_TEST_CASE(fdio_directory_test)
