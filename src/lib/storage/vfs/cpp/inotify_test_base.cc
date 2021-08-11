// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "inotify_test_base.h"

namespace fs {

void InotifyTest::SetUp() {
  ASSERT_EQ(memfs_loop_.StartThread(), ZX_OK);
  zx::channel memfs_root;
  ASSERT_EQ(memfs_create_filesystem(memfs_loop_.dispatcher(), &memfs_,
                                    memfs_root.reset_and_get_address()),
            ZX_OK);
  ASSERT_OK(fdio_ns_get_installed(&namespace_));
  ASSERT_OK(fdio_ns_bind(namespace_, kTmpfsPath, memfs_root.release()));
}

void InotifyTest::TearDown() {
  ASSERT_OK(fdio_ns_unbind(namespace_, kTmpfsPath));
  sync_completion_t unmounted;
  memfs_free_filesystem(memfs_, &unmounted);
  ASSERT_EQ(ZX_OK, sync_completion_wait(&unmounted, zx::duration::infinite().get()));
}

fbl::RefPtr<fs::RemoteDir> InotifyTest::GetRemoteDir() {
  auto endpoints = fidl::CreateEndpoints<fuchsia_io::Directory>();
  EXPECT_TRUE(endpoints.is_ok());
  auto [client, server] = *std::move(endpoints);
  EXPECT_EQ(ZX_OK, fdio_open(kTmpfsPath, ZX_FS_RIGHT_READABLE | ZX_FS_RIGHT_EXECUTABLE,
                             server.TakeChannel().release()));
  return fbl::MakeRefCounted<fs::RemoteDir>(std::move(client));
}

void InotifyTest::AddFile(const std::string& path, size_t content_size) {
  std::string contents(content_size, 'X');
  fbl::unique_fd fd(open(fxl::Substitute("$0/$1", kTmpfsPath, path).c_str(), O_RDWR | O_CREAT));
  ASSERT_TRUE(fd.is_valid());
  ASSERT_EQ(write(fd.get(), contents.c_str(), content_size), content_size);
  close(fd.get());
}

void InotifyTest::WriteToFile(const std::string& path, size_t content_size) {
  std::string contents(content_size, 'X');
  fbl::unique_fd fd(open(fxl::Substitute("$0/$1", kTmpfsPath, path).c_str(), O_WRONLY));
  ASSERT_TRUE(fd.is_valid());
  ASSERT_EQ(write(fd.get(), contents.c_str(), content_size), content_size);
  close(fd.get());
}

void InotifyTest::TruncateFile(const std::string& path, size_t new_file_size) {
  fbl::unique_fd fd(open(fxl::Substitute("$0/$1", kTmpfsPath, path).c_str(), O_WRONLY));
  ASSERT_TRUE(fd.is_valid());
  ASSERT_EQ(ftruncate(fd.get(), new_file_size), 0);
  close(fd.get());
}

void InotifyTest::MakeDir(const std::string& path) {
  ASSERT_EQ(0, mkdir(fxl::Substitute("$0/$1", kTmpfsPath, path).c_str(), 0666));
}

}  // namespace fs
