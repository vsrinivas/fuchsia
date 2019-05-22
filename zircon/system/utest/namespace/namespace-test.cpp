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

#include <fbl/algorithm.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/namespace.h>
#include <lib/zx/channel.h>
#include <unittest/unittest.h>
#include <zircon/compiler.h>
#include <zircon/syscalls.h>

namespace {

struct Mapping {
    const char* local;
    const char* remote;
};

const Mapping NS[] = {
    { "/bin", "/boot/bin" },
    { "/lib", "/boot/lib" },
    { "/fake/dev", "/tmp/fake-namespace-test/dev" },
    { "/fake/tmp", "/tmp/fake-namespace-test-tmp" },
};

bool CreateNamespaceHelper(fdio_ns_t** out) {
    BEGIN_HELPER;

    ASSERT_TRUE(mkdir("/tmp/fake-namespace-test", 066) == 0 || errno == EEXIST);
    ASSERT_TRUE(mkdir("/tmp/fake-namespace-test/dev", 066) == 0 || errno == EEXIST);
    ASSERT_TRUE(mkdir("/tmp/fake-namespace-test-tmp", 066) == 0 || errno == EEXIST);

    // Create new ns
    fdio_ns_t* ns;
    ASSERT_EQ(fdio_ns_create(&ns), ZX_OK);
    for (unsigned n = 0; n < fbl::count_of(NS); n++) {
        int fd = open(NS[n].remote, O_RDONLY | O_DIRECTORY);
        ASSERT_GT(fd, 0);
        ASSERT_EQ(fdio_ns_bind_fd(ns, NS[n].local, fd), ZX_OK);
        ASSERT_EQ(close(fd), 0);
    }
    *out = ns;

    END_HELPER;
}

// Tests destruction of the namespace while no clients exist.
bool DestroyTest() {
    BEGIN_TEST;

    fdio_ns_t* ns;
    ASSERT_TRUE(CreateNamespaceHelper(&ns));
    fdio_ns_destroy(ns);

    END_TEST;
}

// Tests destruction of the namespace while an open connection exists.
// Destruction should still occur, but after the connection is closed.
bool DestroyWhileInUseTest() {
    BEGIN_TEST;

    fdio_ns_t* ns;
    ASSERT_TRUE(CreateNamespaceHelper(&ns));

    int fd = fdio_ns_opendir(ns);
    ASSERT_GE(fd, 0, "Couldn't open root");
    fdio_ns_destroy(ns);
    ASSERT_EQ(close(fd), 0);

    END_TEST;
}

// Tests that remote connections may be bound to the root of the namespace.
bool BindRootTest() {
    BEGIN_TEST;

    fdio_ns_t* ns;
    ASSERT_EQ(fdio_ns_create(&ns), ZX_OK);
    int fd = open("/boot/bin", O_RDONLY | O_DIRECTORY);
    ASSERT_GT(fd, 0);
    ASSERT_EQ(fdio_ns_bind_fd(ns, "/", fd), ZX_OK);
    ASSERT_EQ(close(fd), 0);
    fdio_ns_destroy(ns);

    END_TEST;
}

bool BindRootHandleTest() {
    BEGIN_TEST;

    fdio_ns_t* ns;
    ASSERT_EQ(fdio_ns_create(&ns), ZX_OK);
    zx::channel h1, h2;
    ASSERT_EQ(ZX_OK, zx::channel::create(0, &h1, &h2));
    ASSERT_EQ(ZX_OK, fdio_service_connect("/boot/bin", h1.release()));
    ASSERT_EQ(fdio_ns_bind(ns, "/", h2.release()), ZX_OK);
    fdio_ns_destroy(ns);

    END_TEST;
}

// Tests that rebinding and shadowing are disallowed on the root vnode.
bool ShadowRootTest() {
    BEGIN_TEST;

    fdio_ns_t* ns;
    ASSERT_EQ(fdio_ns_create(&ns), ZX_OK);
    int fd = open("/boot/bin", O_RDONLY | O_DIRECTORY);
    ASSERT_GT(fd, 0);
    ASSERT_EQ(fdio_ns_bind_fd(ns, "/", fd), ZX_OK);
    ASSERT_EQ(fdio_ns_bind_fd(ns, "/", fd), ZX_ERR_ALREADY_EXISTS, "Rebind disallowed");
    ASSERT_EQ(fdio_ns_bind_fd(ns, "/a", fd), ZX_ERR_NOT_SUPPORTED);
    ASSERT_EQ(fdio_ns_bind_fd(ns, "/a/b", fd), ZX_ERR_NOT_SUPPORTED);
    ASSERT_EQ(close(fd), 0);
    fdio_ns_destroy(ns);

    END_TEST;
}

// Tests that rebinding and shadowing are disallowed on non-root vnodes.
bool ShadowNonRootTest() {
    BEGIN_TEST;

    fdio_ns_t* ns;
    ASSERT_EQ(fdio_ns_create(&ns), ZX_OK);
    int fd = open("/boot/bin", O_RDONLY | O_DIRECTORY);
    ASSERT_GT(fd, 0);

    ASSERT_EQ(fdio_ns_bind_fd(ns, "/foo", fd), ZX_OK);
    ASSERT_EQ(fdio_ns_bind_fd(ns, "/foo", fd), ZX_ERR_ALREADY_EXISTS);
    ASSERT_EQ(fdio_ns_bind_fd(ns, "/foo/b", fd), ZX_ERR_NOT_SUPPORTED);
    ASSERT_EQ(fdio_ns_bind_fd(ns, "/foo/b/c", fd), ZX_ERR_NOT_SUPPORTED);

    ASSERT_EQ(fdio_ns_bind_fd(ns, "/bar/foo", fd), ZX_OK);
    ASSERT_EQ(fdio_ns_bind_fd(ns, "/bar", fd), ZX_ERR_ALREADY_EXISTS);
    ASSERT_EQ(fdio_ns_bind_fd(ns, "/bar/foo", fd), ZX_ERR_ALREADY_EXISTS);
    ASSERT_EQ(fdio_ns_bind_fd(ns, "/bar/foo/b", fd), ZX_ERR_NOT_SUPPORTED);
    ASSERT_EQ(fdio_ns_bind_fd(ns, "/bar/foo/b/c", fd), ZX_ERR_NOT_SUPPORTED);

    ASSERT_EQ(close(fd), 0);
    fdio_ns_destroy(ns);

    END_TEST;
}

// Tests exporting a namespace with no contents.
bool ExportEmptyTest() {
    BEGIN_TEST;

    fdio_ns_t* ns;
    ASSERT_EQ(fdio_ns_create(&ns), ZX_OK);
    fdio_flat_namespace_t* flat = nullptr;
    ASSERT_EQ(fdio_ns_export(ns, &flat), ZX_OK);
    ASSERT_EQ(flat->count, 0);

    fdio_ns_free_flat_ns(flat);
    fdio_ns_destroy(ns);

    END_TEST;
}

// Tests exporting a namespace with a single entry: the root.
bool ExportRootTest() {
    BEGIN_TEST;

    fdio_ns_t* ns;
    ASSERT_EQ(fdio_ns_create(&ns), ZX_OK);

    int fd = open("/boot/bin", O_RDONLY | O_DIRECTORY);
    ASSERT_GT(fd, 0);
    ASSERT_EQ(fdio_ns_bind_fd(ns, "/", fd), ZX_OK);
    ASSERT_EQ(close(fd), 0);

    fdio_flat_namespace_t* flat = nullptr;
    ASSERT_EQ(fdio_ns_export(ns, &flat), ZX_OK);
    ASSERT_EQ(flat->count, 1);
    ASSERT_EQ(strcmp(flat->path[0], "/"), 0);

    fdio_ns_free_flat_ns(flat);
    fdio_ns_destroy(ns);

    END_TEST;
}

// Tests exporting a namespace with multiple entries.
bool ExportTest() {
    BEGIN_TEST;

    fdio_ns_t* ns;
    ASSERT_TRUE(CreateNamespaceHelper(&ns));

    // Actually create the flat namespace.
    fdio_flat_namespace_t* flat = nullptr;
    ASSERT_EQ(fdio_ns_export(ns, &flat), ZX_OK);

    // Validate the contents match the initialized mapping.
    ASSERT_EQ(flat->count, fbl::count_of(NS));
    for (unsigned n = 0; n < fbl::count_of(NS); n++) {
        ASSERT_EQ(strcmp(flat->path[n], NS[n].local), 0);
    }

    fdio_ns_free_flat_ns(flat);
    fdio_ns_destroy(ns);

    END_TEST;
}

// Tests changing the current namespace.
bool ChdirTest() {
    BEGIN_TEST;

    fdio_ns_t* old_ns;
    ASSERT_EQ(fdio_ns_get_installed(&old_ns), ZX_OK);

    fdio_ns_t* ns;
    ASSERT_TRUE(CreateNamespaceHelper(&ns));
    ASSERT_EQ(fdio_ns_chdir(ns), ZX_OK);

    DIR* dir;
    struct dirent* de;

    // should show "bin", "lib", "fake" -- our rootdir
    ASSERT_NONNULL((dir = opendir(".")));
    ASSERT_NONNULL((de = readdir(dir)));
    ASSERT_EQ(strcmp(de->d_name, "."), 0);
    ASSERT_NONNULL((de = readdir(dir)));
    ASSERT_EQ(strcmp(de->d_name, "bin"), 0);
    ASSERT_NONNULL((de = readdir(dir)));
    ASSERT_EQ(strcmp(de->d_name, "lib"), 0);
    ASSERT_NONNULL((de = readdir(dir)));
    ASSERT_EQ(strcmp(de->d_name, "fake"), 0);
    ASSERT_EQ(closedir(dir), 0);

    // should show "fake" directory, containing parent's pre-allocated tmp dir.
    ASSERT_NONNULL((dir = opendir("fake")));
    ASSERT_NONNULL((de = readdir(dir)));
    ASSERT_EQ(strcmp(de->d_name, "."), 0);
    ASSERT_NONNULL((de = readdir(dir)));
    ASSERT_EQ(strcmp(de->d_name, "dev"), 0);
    ASSERT_NONNULL((de = readdir(dir)));
    ASSERT_EQ(strcmp(de->d_name, "tmp"), 0);
    ASSERT_EQ(closedir(dir), 0);

    // Try doing some basic file ops within the namespace
    int fd = open("fake/tmp/newfile", O_CREAT | O_RDWR | O_EXCL);
    ASSERT_GT(fd, 0);
    ASSERT_GT(write(fd, "hello", strlen("hello")), 0);
    ASSERT_EQ(close(fd), 0);
    ASSERT_EQ(unlink("fake/tmp/newfile"), 0);
    ASSERT_EQ(mkdir("fake/tmp/newdir", 0666), 0);
    ASSERT_EQ(rename("fake/tmp/newdir", "fake/tmp/olddir"), 0);
    ASSERT_EQ(rmdir("fake/tmp/olddir"), 0);

    ASSERT_EQ(fdio_ns_chdir(old_ns), ZX_OK);
    fdio_ns_destroy(ns);

    END_TEST;
}

// Tests that we can unbind nodes from the namespace.
bool UnbindNonRootTest() {
    BEGIN_TEST;

    fdio_ns_t* old_ns;
    ASSERT_EQ(fdio_ns_get_installed(&old_ns), ZX_OK);


    // Create a namespace with a single entry.
    fdio_ns_t* ns;
    ASSERT_EQ(fdio_ns_create(&ns), ZX_OK);
    int fd = open("/boot/bin", O_RDONLY | O_DIRECTORY);
    ASSERT_GT(fd, 0);
    ASSERT_EQ(fdio_ns_bind_fd(ns, "/my/local/path", fd), ZX_OK);
    ASSERT_EQ(fdio_ns_bind_fd(ns, "/top", fd), ZX_OK);
    ASSERT_EQ(fdio_ns_bind_fd(ns, "/another_top", fd), ZX_OK);
    ASSERT_EQ(close(fd), 0);
    ASSERT_EQ(fdio_ns_chdir(ns), ZX_OK);

    struct stat st;
    ASSERT_EQ(stat("my", &st), 0);
    ASSERT_EQ(stat("my/local", &st), 0);
    ASSERT_EQ(stat("my/local/path", &st), 0);

    ASSERT_EQ(fdio_ns_unbind(ns, "/"), ZX_ERR_NOT_SUPPORTED);
    ASSERT_EQ(fdio_ns_unbind(ns, "/my"), ZX_ERR_NOT_FOUND);
    ASSERT_EQ(fdio_ns_unbind(ns, "/my/local"), ZX_ERR_NOT_FOUND);
    ASSERT_EQ(fdio_ns_unbind(ns, "/my/local/path/okay/too/much/though"), ZX_ERR_NOT_FOUND);
    ASSERT_EQ(fdio_ns_unbind(ns, "/my/local/path"), ZX_OK);
    // Ensure unbinding a top-level node when another still exists works.
    ASSERT_EQ(fdio_ns_unbind(ns, "/top"), ZX_OK);

    // Removing the namespace entry should remove all nodes back up to the root.
    ASSERT_EQ(stat("my", &st), -1);
    ASSERT_EQ(stat("my/local", &st), -1);
    ASSERT_EQ(stat("my/local/path", &st), -1);

    ASSERT_EQ(fdio_ns_chdir(old_ns), ZX_OK);
    fdio_ns_destroy(ns);

    END_TEST;
}

// Tests that we cannot unbind the root of the namespace.
bool UnbindRootTest() {
    BEGIN_TEST;

    fdio_ns_t* old_ns;
    ASSERT_EQ(fdio_ns_get_installed(&old_ns), ZX_OK);


    // Create a namespace with a single entry.
    fdio_ns_t* ns;
    ASSERT_EQ(fdio_ns_create(&ns), ZX_OK);
    int fd = open("/boot/bin", O_RDONLY | O_DIRECTORY);
    ASSERT_GT(fd, 0);
    ASSERT_EQ(fdio_ns_bind_fd(ns, "/", fd), ZX_OK);
    ASSERT_EQ(close(fd), 0);
    ASSERT_EQ(fdio_ns_chdir(ns), ZX_OK);

    struct stat st;
    ASSERT_EQ(stat("/", &st), 0);

    // We should not be able to unbind the root.
    ASSERT_EQ(fdio_ns_unbind(ns, "/"), ZX_ERR_NOT_SUPPORTED);
    ASSERT_EQ(stat("/", &st), 0);

    ASSERT_EQ(fdio_ns_chdir(old_ns), ZX_OK);
    fdio_ns_destroy(ns);

    END_TEST;
}

// Tests that intermediate nodes are unbound up to an ancestor that
// has other children.
bool UnbindAncestorTest() {
    BEGIN_TEST;

    fdio_ns_t* old_ns;
    ASSERT_EQ(fdio_ns_get_installed(&old_ns), ZX_OK);


    // Create a namespace with a single entry.
    fdio_ns_t* ns;
    ASSERT_EQ(fdio_ns_create(&ns), ZX_OK);
    int fd = open("/boot/bin", O_RDONLY | O_DIRECTORY);
    ASSERT_GT(fd, 0);
    ASSERT_EQ(fdio_ns_bind_fd(ns, "/my/local/path", fd), ZX_OK);
    ASSERT_EQ(fdio_ns_bind_fd(ns, "/my/other/path", fd), ZX_OK);
    ASSERT_EQ(close(fd), 0);
    ASSERT_EQ(fdio_ns_chdir(ns), ZX_OK);

    struct stat st;
    ASSERT_EQ(stat("my", &st), 0);
    ASSERT_EQ(stat("my/local", &st), 0);
    ASSERT_EQ(stat("my/local/path", &st), 0);
    ASSERT_EQ(stat("my/other", &st), 0);
    ASSERT_EQ(stat("my/other/path", &st), 0);

    ASSERT_EQ(fdio_ns_unbind(ns, "/my/local/path"), ZX_OK);

    // Removing the namespace entry should remove all nodes back up to a common
    // ancestor, but not other subtrees.
    ASSERT_EQ(stat("my", &st), 0);
    ASSERT_EQ(stat("my/local", &st), -1); // Removed
    ASSERT_EQ(stat("my/local/path", &st), -1); // Removed
    ASSERT_EQ(stat("my/other", &st), 0);
    ASSERT_EQ(stat("my/other/path", &st), 0);

    ASSERT_EQ(fdio_ns_chdir(old_ns), ZX_OK);
    fdio_ns_destroy(ns);

    END_TEST;
}

BEGIN_TEST_CASE(namespace_tests)
RUN_TEST_MEDIUM(DestroyTest)
RUN_TEST_MEDIUM(DestroyWhileInUseTest)
RUN_TEST_MEDIUM(BindRootTest)
RUN_TEST_MEDIUM(BindRootHandleTest)
RUN_TEST_MEDIUM(ShadowRootTest)
RUN_TEST_MEDIUM(ShadowNonRootTest)
RUN_TEST_MEDIUM(ExportEmptyTest)
RUN_TEST_MEDIUM(ExportRootTest)
RUN_TEST_MEDIUM(ExportTest)
RUN_TEST_MEDIUM(ChdirTest)
RUN_TEST_MEDIUM(UnbindNonRootTest)
RUN_TEST_MEDIUM(UnbindRootTest)
RUN_TEST_MEDIUM(UnbindAncestorTest)
END_TEST_CASE(namespace_tests)

} // namespace
