// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <fcntl.h>
#include <lib/fdio/fd.h>
#include <lib/zx/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <zircon/syscalls.h>

#include <unittest/unittest.h>

bool chmod_ignored_permissions_test(void) {
    BEGIN_TEST;

    const char * path = "/";
    int fd = open(path, O_DIRECTORY | O_RDONLY);
    ASSERT_NE(-1, fd);

    mode_t mode = S_IFREG | S_IRWXU | S_IRWXG | S_IRWXO;
    ASSERT_EQ(0, chmod(path, mode));
    ASSERT_EQ(0, fchmod(fd, mode));

    close(fd);

    END_TEST;
}

bool chmod_not_implemented_test(void) {
    BEGIN_TEST;

    const char * path = "/";
    int fd = open(path, O_DIRECTORY | O_RDONLY);
    ASSERT_NE(-1, fd);

    mode_t mode = S_ISUID;
    ASSERT_EQ(-1, chmod(path, mode));
    ASSERT_EQ(ENOSYS, errno);
    ASSERT_EQ(-1, fchmod(fd, mode));
    ASSERT_EQ(ENOSYS, errno);

    close(fd);

    END_TEST;
}

BEGIN_TEST_CASE(fdio_stubs_test)
RUN_TEST(chmod_ignored_permissions_test)
RUN_TEST(chmod_not_implemented_test)
END_TEST_CASE(fdio_stubs_test)
