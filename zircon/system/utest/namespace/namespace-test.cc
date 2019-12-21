// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <dirent.h>
#include <errno.h>
#include <fbl/algorithm.h>
#include <fbl/unique_fd.h>
#include <fcntl.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/namespace.h>
#include <lib/zx/channel.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <zircon/compiler.h>
#include <zircon/syscalls.h>
#include <zxtest/zxtest.h>

#include <string>
#include <vector>

namespace {

struct Mapping {
  const char* local;
  const char* remote;
};

const Mapping NS[] = {
    {"/bin", "/boot/bin"},
    {"/lib", "/boot/lib"},
    {"/fake/dev", "/tmp/fake-namespace-test/dev"},
    {"/fake/tmp", "/tmp/fake-namespace-test-tmp"},
};

void CreateNamespaceHelper(fdio_ns_t** out) {
  ASSERT_TRUE(mkdir("/tmp/fake-namespace-test", 066) == 0 || errno == EEXIST);
  ASSERT_TRUE(mkdir("/tmp/fake-namespace-test/dev", 066) == 0 || errno == EEXIST);
  ASSERT_TRUE(mkdir("/tmp/fake-namespace-test-tmp", 066) == 0 || errno == EEXIST);

  // Create new ns
  fdio_ns_t* ns;
  ASSERT_OK(fdio_ns_create(&ns));
  for (unsigned n = 0; n < fbl::count_of(NS); n++) {
    fbl::unique_fd fd(open(NS[n].remote, O_RDONLY | O_DIRECTORY));
    ASSERT_GT(fd.get(), 0);
    ASSERT_OK(fdio_ns_bind_fd(ns, NS[n].local, fd.get()));
    ASSERT_EQ(close(fd.release()), 0);
  }
  *out = ns;
}

// Tests destruction of the namespace while no clients exist.
TEST(NamespaceTest, Destroy) {
  fdio_ns_t* ns;
  ASSERT_NO_FATAL_FAILURES(CreateNamespaceHelper(&ns));
  ASSERT_OK(fdio_ns_destroy(ns));
}

// Tests destruction of the namespace while an open connection exists.
// Destruction should still occur, but after the connection is closed.
TEST(NamespaceTest, DestroyWhileInUse) {
  fdio_ns_t* ns;
  ASSERT_NO_FATAL_FAILURES(CreateNamespaceHelper(&ns));

  fbl::unique_fd fd(fdio_ns_opendir(ns));
  ASSERT_GE(fd.get(), 0, "Couldn't open root");
  ASSERT_OK(fdio_ns_destroy(ns));
  ASSERT_EQ(close(fd.release()), 0);
}

// Tests that remote connections may be bound to the root of the namespace.
TEST(NamespaceTest, BindRoot) {
  fdio_ns_t* ns;
  ASSERT_OK(fdio_ns_create(&ns));
  fbl::unique_fd fd(open("/boot/bin", O_RDONLY | O_DIRECTORY));
  ASSERT_GT(fd.get(), 0);
  ASSERT_OK(fdio_ns_bind_fd(ns, "/", fd.get()));
  ASSERT_EQ(close(fd.release()), 0);
  ASSERT_OK(fdio_ns_destroy(ns));
}

TEST(NamespaceTest, BindRootHandle) {
  fdio_ns_t* ns;
  ASSERT_OK(fdio_ns_create(&ns));
  zx::channel h1, h2;
  ASSERT_OK(zx::channel::create(0, &h1, &h2));
  ASSERT_OK(fdio_service_connect("/boot/bin", h1.release()));
  ASSERT_OK(fdio_ns_bind(ns, "/", h2.release()));
  ASSERT_OK(fdio_ns_destroy(ns));
}

// Tests that rebinding and shadowing are disallowed on the root vnode.
TEST(NamespaceTest, ShadowRoot) {
  fdio_ns_t* ns;
  ASSERT_OK(fdio_ns_create(&ns));
  fbl::unique_fd fd(open("/boot/bin", O_RDONLY | O_DIRECTORY));
  ASSERT_GT(fd.get(), 0);
  ASSERT_OK(fdio_ns_bind_fd(ns, "/", fd.get()));
  ASSERT_EQ(fdio_ns_bind_fd(ns, "/", fd.get()), ZX_ERR_ALREADY_EXISTS, "Rebind disallowed");
  ASSERT_EQ(fdio_ns_bind_fd(ns, "/a", fd.get()), ZX_ERR_NOT_SUPPORTED);
  ASSERT_EQ(fdio_ns_bind_fd(ns, "/a/b", fd.get()), ZX_ERR_NOT_SUPPORTED);
  ASSERT_EQ(close(fd.release()), 0);
  ASSERT_OK(fdio_ns_destroy(ns));
}

// Tests that rebinding and shadowing are disallowed on non-root vnodes.
TEST(NamespaceTest, ShadowNonRoot) {
  fdio_ns_t* ns;
  ASSERT_OK(fdio_ns_create(&ns));
  fbl::unique_fd fd(open("/boot/bin", O_RDONLY | O_DIRECTORY));
  ASSERT_GT(fd.get(), 0);

  ASSERT_OK(fdio_ns_bind_fd(ns, "/foo", fd.get()));
  ASSERT_EQ(fdio_ns_bind_fd(ns, "/foo", fd.get()), ZX_ERR_ALREADY_EXISTS);
  ASSERT_EQ(fdio_ns_bind_fd(ns, "/foo/b", fd.get()), ZX_ERR_NOT_SUPPORTED);
  ASSERT_EQ(fdio_ns_bind_fd(ns, "/foo/b/c", fd.get()), ZX_ERR_NOT_SUPPORTED);

  ASSERT_OK(fdio_ns_bind_fd(ns, "/bar/foo", fd.get()));
  ASSERT_EQ(fdio_ns_bind_fd(ns, "/bar", fd.get()), ZX_ERR_ALREADY_EXISTS);
  ASSERT_EQ(fdio_ns_bind_fd(ns, "/bar/foo", fd.get()), ZX_ERR_ALREADY_EXISTS);
  ASSERT_EQ(fdio_ns_bind_fd(ns, "/bar/foo/b", fd.get()), ZX_ERR_NOT_SUPPORTED);
  ASSERT_EQ(fdio_ns_bind_fd(ns, "/bar/foo/b/c", fd.get()), ZX_ERR_NOT_SUPPORTED);

  ASSERT_EQ(close(fd.release()), 0);
  ASSERT_OK(fdio_ns_destroy(ns));
}

// Tests exporting a namespace with no contents.
TEST(NamespaceTest, ExportEmpty) {
  fdio_ns_t* ns;
  ASSERT_OK(fdio_ns_create(&ns));
  fdio_flat_namespace_t* flat = nullptr;
  ASSERT_OK(fdio_ns_export(ns, &flat));
  ASSERT_EQ(flat->count, 0);

  fdio_ns_free_flat_ns(flat);
  ASSERT_OK(fdio_ns_destroy(ns));
}

// Tests exporting a namespace with a single entry: the root.
TEST(NamespaceTest, ExportRoot) {
  fdio_ns_t* ns;
  ASSERT_OK(fdio_ns_create(&ns));

  fbl::unique_fd fd(open("/boot/bin", O_RDONLY | O_DIRECTORY));
  ASSERT_GT(fd.get(), 0);
  ASSERT_OK(fdio_ns_bind_fd(ns, "/", fd.get()));
  ASSERT_EQ(close(fd.release()), 0);

  fdio_flat_namespace_t* flat = nullptr;
  ASSERT_OK(fdio_ns_export(ns, &flat));
  ASSERT_EQ(flat->count, 1);
  ASSERT_STR_EQ(flat->path[0], "/");

  fdio_ns_free_flat_ns(flat);
  ASSERT_OK(fdio_ns_destroy(ns));
}

// Tests exporting a namespace with multiple entries.
TEST(NamespaceTest, Export) {
  fdio_ns_t* ns;
  ASSERT_NO_FATAL_FAILURES(CreateNamespaceHelper(&ns));

  // Actually create the flat namespace.
  fdio_flat_namespace_t* flat = nullptr;
  ASSERT_OK(fdio_ns_export(ns, &flat));

  // Validate the contents match the initialized mapping.
  ASSERT_EQ(flat->count, fbl::count_of(NS));
  for (unsigned n = 0; n < fbl::count_of(NS); n++) {
    ASSERT_STR_EQ(flat->path[n], NS[n].local);
  }

  fdio_ns_free_flat_ns(flat);
  ASSERT_OK(fdio_ns_destroy(ns));
}

// Tests changing the current namespace.
TEST(NamespaceTest, Chdir) {
  fdio_ns_t* old_ns;
  ASSERT_OK(fdio_ns_get_installed(&old_ns));

  fdio_ns_t* ns;
  ASSERT_NO_FATAL_FAILURES(CreateNamespaceHelper(&ns));
  ASSERT_OK(fdio_ns_chdir(ns));

  DIR* dir;
  struct dirent* de;

  // should show "bin", "lib", "fake" -- our rootdir
  ASSERT_TRUE((dir = opendir(".")));
  ASSERT_NOT_NULL((de = readdir(dir)));
  EXPECT_STR_EQ(de->d_name, ".");
  ASSERT_NOT_NULL((de = readdir(dir)));
  EXPECT_STR_EQ(de->d_name, "bin");
  ASSERT_NOT_NULL((de = readdir(dir)));
  EXPECT_STR_EQ(de->d_name, "lib");
  ASSERT_NOT_NULL((de = readdir(dir)));
  EXPECT_STR_EQ(de->d_name, "fake");
  ASSERT_NULL((de = readdir(dir)));
  ASSERT_EQ(closedir(dir), 0);

  // should show "fake" directory, containing parent's pre-allocated tmp dir.
  ASSERT_TRUE((dir = opendir("fake")));
  ASSERT_TRUE((de = readdir(dir)));
  ASSERT_STR_EQ(de->d_name, ".");
  ASSERT_TRUE((de = readdir(dir)));
  ASSERT_STR_EQ(de->d_name, "dev");
  ASSERT_TRUE((de = readdir(dir)));
  ASSERT_STR_EQ(de->d_name, "tmp");
  ASSERT_EQ(closedir(dir), 0);

  // Try doing some basic file ops within the namespace
  fbl::unique_fd fd(open("fake/tmp/newfile", O_CREAT | O_RDWR | O_EXCL));
  ASSERT_GT(fd.get(), 0);
  ASSERT_GT(write(fd.get(), "hello", strlen("hello")), 0);
  ASSERT_EQ(close(fd.release()), 0);
  ASSERT_EQ(unlink("fake/tmp/newfile"), 0);
  ASSERT_EQ(mkdir("fake/tmp/newdir", 0666), 0);
  ASSERT_EQ(rename("fake/tmp/newdir", "fake/tmp/olddir"), 0);
  ASSERT_EQ(rmdir("fake/tmp/olddir"), 0);

  ASSERT_OK(fdio_ns_chdir(old_ns));
  ASSERT_OK(fdio_ns_destroy(ns));
}

// Tests that we can unbind nodes from the namespace.
TEST(NamespaceTest, UnbindNonRoot) {
  fdio_ns_t* old_ns;
  ASSERT_OK(fdio_ns_get_installed(&old_ns));

  // Create a namespace with a single entry.
  fdio_ns_t* ns;
  ASSERT_OK(fdio_ns_create(&ns));
  fbl::unique_fd fd(open("/boot/bin", O_RDONLY | O_DIRECTORY));
  ASSERT_GT(fd.get(), 0);
  ASSERT_OK(fdio_ns_bind_fd(ns, "/my/local/path", fd.get()));
  ASSERT_OK(fdio_ns_bind_fd(ns, "/top", fd.get()));
  ASSERT_OK(fdio_ns_bind_fd(ns, "/another_top", fd.get()));
  ASSERT_EQ(close(fd.release()), 0);
  ASSERT_OK(fdio_ns_chdir(ns));

  struct stat st;
  ASSERT_EQ(stat("my", &st), 0);
  ASSERT_EQ(stat("my/local", &st), 0);
  ASSERT_EQ(stat("my/local/path", &st), 0);

  ASSERT_EQ(fdio_ns_unbind(ns, "/"), ZX_ERR_NOT_SUPPORTED);
  ASSERT_EQ(fdio_ns_unbind(ns, "/my"), ZX_ERR_NOT_FOUND);
  ASSERT_EQ(fdio_ns_unbind(ns, "/my/local"), ZX_ERR_NOT_FOUND);
  ASSERT_EQ(fdio_ns_unbind(ns, "/my/local/path/okay/too/much/though"), ZX_ERR_NOT_FOUND);
  ASSERT_OK(fdio_ns_unbind(ns, "/my/local/path"));
  // Ensure unbinding a top-level node when another still exists works.
  ASSERT_OK(fdio_ns_unbind(ns, "/top"));

  // Removing the namespace entry should remove all nodes back up to the root.
  ASSERT_EQ(stat("my", &st), -1);
  ASSERT_EQ(stat("my/local", &st), -1);
  ASSERT_EQ(stat("my/local/path", &st), -1);

  ASSERT_OK(fdio_ns_chdir(old_ns));
  ASSERT_OK(fdio_ns_destroy(ns));
}

// Tests that we cannot unbind the root of the namespace.
TEST(NamespaceTest, UnbindRoot) {
  fdio_ns_t* old_ns;
  ASSERT_OK(fdio_ns_get_installed(&old_ns));

  // Create a namespace with a single entry.
  fdio_ns_t* ns;
  ASSERT_OK(fdio_ns_create(&ns));
  fbl::unique_fd fd(open("/boot/bin", O_RDONLY | O_DIRECTORY));
  ASSERT_GT(fd.get(), 0);
  ASSERT_OK(fdio_ns_bind_fd(ns, "/", fd.get()));
  ASSERT_EQ(close(fd.release()), 0);
  ASSERT_OK(fdio_ns_chdir(ns));

  struct stat st;
  ASSERT_EQ(stat("/", &st), 0);

  // We should not be able to unbind the root.
  ASSERT_EQ(fdio_ns_unbind(ns, "/"), ZX_ERR_NOT_SUPPORTED);
  ASSERT_EQ(stat("/", &st), 0);

  ASSERT_OK(fdio_ns_chdir(old_ns));
  ASSERT_OK(fdio_ns_destroy(ns));
}

// Tests that intermediate nodes are unbound up to an ancestor that
// has other children.
TEST(NamespaceTest, UnbindAncestor) {
  fdio_ns_t* old_ns;
  ASSERT_OK(fdio_ns_get_installed(&old_ns));

  // Create a namespace with a single entry.
  fdio_ns_t* ns;
  ASSERT_OK(fdio_ns_create(&ns));
  fbl::unique_fd fd(open("/boot/bin", O_RDONLY | O_DIRECTORY));
  ASSERT_GT(fd.get(), 0);
  ASSERT_OK(fdio_ns_bind_fd(ns, "/my/local/path", fd.get()));
  ASSERT_OK(fdio_ns_bind_fd(ns, "/my/other/path", fd.get()));
  ASSERT_EQ(close(fd.release()), 0);
  ASSERT_OK(fdio_ns_chdir(ns));

  struct stat st;
  ASSERT_EQ(stat("my", &st), 0);
  ASSERT_EQ(stat("my/local", &st), 0);
  ASSERT_EQ(stat("my/local/path", &st), 0);
  ASSERT_EQ(stat("my/other", &st), 0);
  ASSERT_EQ(stat("my/other/path", &st), 0);

  ASSERT_OK(fdio_ns_unbind(ns, "/my/local/path"));

  // Removing the namespace entry should remove all nodes back up to a common
  // ancestor, but not other subtrees.
  ASSERT_EQ(stat("my", &st), 0);
  ASSERT_EQ(stat("my/local", &st), -1);       // Removed
  ASSERT_EQ(stat("my/local/path", &st), -1);  // Removed
  ASSERT_EQ(stat("my/other", &st), 0);
  ASSERT_EQ(stat("my/other/path", &st), 0);

  ASSERT_OK(fdio_ns_chdir(old_ns));
  ASSERT_OK(fdio_ns_destroy(ns));
}

TEST(NamespaceTest, ExportGlobalRoot) {
  fdio_flat_namespace_t* flat = nullptr;
  ASSERT_OK(fdio_ns_export_root(&flat));
  ASSERT_LE(1, flat->count);
  fdio_ns_free_flat_ns(flat);
}

TEST(NamespaceTest, GetInstalled) {
  fdio_ns_t* ns = nullptr;
  ASSERT_OK(fdio_ns_get_installed(&ns));
  ASSERT_NE(nullptr, ns);
}

TEST(NamespaceTest, Readdir) {
  fdio_ns_t* old_ns;
  ASSERT_OK(fdio_ns_get_installed(&old_ns));

  // Create new ns
  constexpr size_t kNumChildren = 1000;
  fdio_ns_t* ns;
  ASSERT_OK(fdio_ns_create(&ns));
  std::vector<zx::channel> client_ends;
  for (size_t n = 0; n < kNumChildren; n++) {
    std::string path = std::string("/test_") + std::to_string(n);
    zx::channel fake_client_end, fake_server_end;
    zx::channel::create(0, &fake_client_end, &fake_server_end);
    ASSERT_OK(fdio_ns_bind(ns, path.c_str(), fake_server_end.release()));
    client_ends.push_back(std::move(fake_client_end));
  }
  ASSERT_OK(fdio_ns_chdir(ns));

  DIR* dir;
  struct dirent* de;
  ASSERT_TRUE((dir = opendir(".")));
  ASSERT_NOT_NULL((de = readdir(dir)));
  ASSERT_STR_EQ(de->d_name, ".");

  for (size_t i = 0; i < kNumChildren; i++) {
    std::string expected_name = std::string("test_") + std::to_string(i);
    ASSERT_NOT_NULL((de = readdir(dir)));
    EXPECT_STR_EQ(de->d_name, expected_name.c_str());
  }
  ASSERT_NULL((de = readdir(dir)));
  ASSERT_EQ(closedir(dir), 0);

  ASSERT_OK(fdio_ns_chdir(old_ns));
  ASSERT_OK(fdio_ns_destroy(ns));
}

}  // namespace
