// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fdio/watcher.h>

#include <unittest/unittest.h>

static bool watch_invalid_dirfd_test() {
    BEGIN_TEST;

    zx_status_t status = fdio_watch_directory(-1, nullptr, ZX_TIME_INFINITE, nullptr);
    ASSERT_EQ(ZX_ERR_INVALID_ARGS, status);

    END_TEST;
}

BEGIN_TEST_CASE(fdio_watcher_test)
RUN_TEST(watch_invalid_dirfd_test)
END_TEST_CASE(fdio_watcher_test)
