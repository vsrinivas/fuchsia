// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/io/llcpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/inotify.h>
#include <lib/fdio/namespace.h>
#include <lib/fit/single_threaded_executor.h>
#include <lib/memfs/memfs.h>
#include <sys/stat.h>

#include <fbl/unique_fd.h>
#include <zxtest/zxtest.h>

#include "src/lib/fxl/strings/substitute.h"
#include "src/lib/storage/vfs/cpp/remote_dir.h"

namespace {
constexpr char kTmpfsPath[] = "/fshost-inotify-tmp";

class InotifyTest : public zxtest::Test {
 public:
  InotifyTest() : memfs_loop_(&kAsyncLoopConfigNoAttachToCurrentThread) {}

  void SetUp() override {
    ASSERT_EQ(memfs_loop_.StartThread(), ZX_OK);
    zx::channel memfs_root;
    ASSERT_EQ(memfs_create_filesystem(memfs_loop_.dispatcher(), &memfs_,
                                      memfs_root.reset_and_get_address()),
              ZX_OK);
    ASSERT_OK(fdio_ns_get_installed(&namespace_));
    ASSERT_OK(fdio_ns_bind(namespace_, kTmpfsPath, memfs_root.release()));
  }

  void TearDown() override {
    ASSERT_OK(fdio_ns_unbind(namespace_, kTmpfsPath));
    sync_completion_t unmounted;
    memfs_free_filesystem(memfs_, &unmounted);
    ASSERT_EQ(ZX_OK, sync_completion_wait(&unmounted, zx::duration::infinite().get()));
  }

 protected:
  fbl::RefPtr<fs::RemoteDir> GetRemoteDir() {
    auto endpoints = fidl::CreateEndpoints<fuchsia_io::Directory>();
    EXPECT_TRUE(endpoints.is_ok());
    auto [client, server] = *std::move(endpoints);
    EXPECT_EQ(ZX_OK, fdio_open(kTmpfsPath, ZX_FS_RIGHT_READABLE | ZX_FS_RIGHT_EXECUTABLE,
                               server.TakeChannel().release()));
    return fbl::MakeRefCounted<fs::RemoteDir>(std::move(client));
  }

  void AddFile(const std::string& path, size_t content_size) {
    std::string contents(content_size, 'X');
    fbl::unique_fd fd(open(fxl::Substitute("$0/$1", kTmpfsPath, path).c_str(), O_RDWR | O_CREAT));
    ASSERT_TRUE(fd.is_valid());
    ASSERT_EQ(write(fd.get(), contents.c_str(), content_size), content_size);
    close(fd.get());
  }

  void MakeDir(const std::string& path) {
    ASSERT_EQ(0, mkdir(fxl::Substitute("$0/$1", kTmpfsPath, path).c_str(), 0666));
  }

  fdio_ns_t* namespace_;
  async::Loop memfs_loop_;
  memfs_filesystem_t* memfs_;
};

// Tests basic open/close events for inotify.
TEST_F(InotifyTest, BasicOpenClose) {
  // Initialize test directory
  MakeDir("a");
  AddFile("a/a.txt", 13);

  fbl::unique_fd inotify_fd;
  ASSERT_TRUE(inotify_fd = fbl::unique_fd(inotify_init1(0)), "%s", strerror(errno));

  // Add filter on file for notifying on file open and file close.
  int wd = inotify_add_watch(inotify_fd.get(), "/fshost-inotify-tmp/a/a.txt", IN_OPEN | IN_CLOSE);
  ASSERT_GE(wd, 0);

  // Try to open the file, with the filter.
  int fd = open("/fshost-inotify-tmp/a/a.txt", O_RDWR);
  ASSERT_GE(fd, 0);

  // Read the open event.
  struct inotify_event event;
  ASSERT_EQ(read(inotify_fd.get(), &event, sizeof(event)), sizeof(event), "%s", strerror(errno));
  ASSERT_EQ(event.mask, IN_OPEN);
  ASSERT_EQ(event.wd, wd);

  // Close the file
  ASSERT_EQ(close(fd), 0);

  // Read the close event.
  ASSERT_EQ(read(inotify_fd.get(), &event, sizeof(event)), sizeof(event), "%s", strerror(errno));
  ASSERT_EQ(event.mask, IN_CLOSE, "%s", "Returned inotify event is incorrect.");
  ASSERT_EQ(event.wd, wd, "%s", "Returned inotify watch descriptor is incorrect.");

  // remove filter
  ASSERT_EQ(inotify_rm_watch(inotify_fd.get(), wd), 0, "%s", strerror(errno));
}

}  // namespace
