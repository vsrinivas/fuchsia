// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/storage/vfs/cpp/inotify_test_base.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>

namespace fs {

void InotifyTest::SetUp() {
  ASSERT_EQ(memfs_loop_.StartThread(), ZX_OK);

  zx::result<ScopedMemfs> memfs =
      ScopedMemfs::CreateMountedAt(memfs_loop_.dispatcher(), kTmpfsPath);
  ASSERT_TRUE(memfs.is_ok());
  memfs_ = std::make_unique<ScopedMemfs>(std::move(*memfs));
}

void InotifyTest::TearDown() { memfs_.reset(); }

fbl::RefPtr<fs::RemoteDir> InotifyTest::GetRemoteDir() {
  auto endpoints = fidl::CreateEndpoints<fuchsia_io::Directory>();
  EXPECT_TRUE(endpoints.is_ok());
  auto [client, server] = *std::move(endpoints);
  EXPECT_EQ(ZX_OK, fdio_open(kTmpfsPath,
                             static_cast<uint32_t>(fuchsia_io::wire::OpenFlags::kRightReadable |
                                                   fuchsia_io::wire::OpenFlags::kRightExecutable),
                             server.TakeChannel().release()));
  return fbl::MakeRefCounted<fs::RemoteDir>(std::move(client));
}

void InotifyTest::AddFile(const std::string& path, size_t content_size) {
  std::string contents(content_size, 'X');
  fbl::unique_fd fd(open(fxl::Substitute("$0/$1", kTmpfsPath, path).c_str(), O_RDWR | O_CREAT,
                         S_IRUSR | S_IWUSR));
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
