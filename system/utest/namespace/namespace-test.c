// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <launchpad/launchpad.h>
#include <mxio/namespace.h>
#include <magenta/compiler.h>
#include <magenta/syscalls.h>
#include <unittest/unittest.h>

typedef struct {
    const char* local;
    const char* remote;
} nstab_t;

static nstab_t NS[] = {
    { "/bin", "/boot/bin" },
    { "/lib", "/boot/lib" },
    { "/fake", "/tmp/fake-namespace-test" },
    { "/fake/dev", "/tmp/fake-namespace-test/dev" },
    { "/fake/tmp", "/tmp/fake-namespace-test-tmp" },
};

static bool namespace_create_test(void) {
    BEGIN_TEST;

    ASSERT_TRUE(mkdir("/tmp/fake-namespace-test", 066) == 0 || errno == EEXIST, "");
    ASSERT_TRUE(mkdir("/tmp/fake-namespace-test/dev", 066) == 0 || errno == EEXIST, "");
    ASSERT_TRUE(mkdir("/tmp/fake-namespace-test-tmp", 066) == 0 || errno == EEXIST, "");

    // Create new ns
    mxio_ns_t* ns;
    ASSERT_EQ(mxio_ns_create(&ns), MX_OK, "");
    for (unsigned n = 0; n < countof(NS); n++) {
        int fd = open(NS[n].remote, O_RDONLY | O_DIRECTORY);
        ASSERT_GT(fd, 0, "");
        ASSERT_EQ(mxio_ns_bind_fd(ns, NS[n].local, fd), MX_OK, "");
        ASSERT_EQ(close(fd), 0, "");
    }
    ASSERT_EQ(mxio_ns_chdir(ns), MX_OK, "");

    DIR* dir;
    struct dirent* de;

    // should show "bin", "lib", "fake" -- our rootdir
    ASSERT_NONNULL((dir = opendir(".")), "");
    ASSERT_NONNULL((de = readdir(dir)), "");
    ASSERT_EQ(strcmp(de->d_name, "fake"), 0, "");
    ASSERT_NONNULL((de = readdir(dir)), "");
    ASSERT_EQ(strcmp(de->d_name, "lib"), 0, "");
    ASSERT_NONNULL((de = readdir(dir)), "");
    ASSERT_EQ(strcmp(de->d_name, "bin"), 0, "");
    ASSERT_EQ(closedir(dir), 0, "");

    // should show "fake" directory, containing parent's pre-allocated tmp dir.
    ASSERT_NONNULL((dir = opendir("fake")), "");
    ASSERT_NONNULL((de = readdir(dir)), "");
    ASSERT_EQ(strcmp(de->d_name, "tmp"), 0, "");
    ASSERT_NONNULL((de = readdir(dir)), "");
    ASSERT_EQ(strcmp(de->d_name, "dev"), 0, "");
    ASSERT_NONNULL((de = readdir(dir)), "");
    ASSERT_EQ(strcmp(de->d_name, "."), 0, "");
    ASSERT_EQ(closedir(dir), 0, "");

    // Try doing some basic file ops within the namespace
    int fd = open("fake/newfile", O_CREAT | O_RDWR | O_EXCL);
    ASSERT_GT(fd, 0, "");
    ASSERT_GT(write(fd, "hello", strlen("hello")), 0, "");
    ASSERT_EQ(close(fd), 0, "");
    ASSERT_EQ(unlink("fake/newfile"), 0, "");
    ASSERT_EQ(mkdir("fake/newdir", 0666), 0, "");
    ASSERT_EQ(rename("fake/newdir", "fake/olddir"), 0, "");
    ASSERT_EQ(rmdir("fake/olddir"), 0, "");

    END_TEST;
}

BEGIN_TEST_CASE(namespace_tests)
RUN_TEST_MEDIUM(namespace_create_test)
END_TEST_CASE(namespace_tests)

int main(int argc, char** argv) {
    return unittest_run_all_tests(argc, argv) ? 0 : -1;
}
