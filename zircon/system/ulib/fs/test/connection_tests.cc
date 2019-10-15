// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/io/llcpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <stdio.h>

#include <utility>

#include <fs/pseudo_dir.h>
#include <fs/pseudo_file.h>
#include <fs/synchronous_vfs.h>
#include <zxtest/zxtest.h>

namespace {

namespace fio = ::llcpp::fuchsia::io;

zx_status_t DummyReader(fbl::String* output) { return ZX_OK; }

zx_status_t DummyWriter(fbl::StringPiece input) { return ZX_OK; }

class VfsTestSetup : public zxtest::Test {
 public:
  // Setup file structure with one directory and one file. Note: On creation
  // directories and files have no flags and rights.
  VfsTestSetup() : loop_(&kAsyncLoopConfigNoAttachToCurrentThread) {
    vfs_.SetDispatcher(loop_.dispatcher());
    root_ = fbl::AdoptRef<fs::PseudoDir>(new fs::PseudoDir());
    dir_ = fbl::AdoptRef<fs::PseudoDir>(new fs::PseudoDir());
    file_ = fbl::AdoptRef<fs::Vnode>(new fs::BufferedPseudoFile(&DummyReader, &DummyWriter));
    root_->AddEntry("dir", dir_);
    root_->AddEntry("file", file_);
  }

  zx_status_t ConnectClient(zx::channel server_end) {
    // Serve root directory with maximum rights
    return vfs_.ServeDirectory(root_, std::move(server_end));
  }

 protected:
  void SetUp() override { loop_.StartThread(); }

  void TearDown() override { loop_.Shutdown(); }

 private:
  async::Loop loop_;
  fs::SynchronousVfs vfs_;
  fbl::RefPtr<fs::PseudoDir> root_;
  fbl::RefPtr<fs::PseudoDir> dir_;
  fbl::RefPtr<fs::Vnode> file_;
};

using ConnectionTest = VfsTestSetup;

TEST_F(ConnectionTest, NodeGetSetFlagsOnFile) {
  // Create connection to vfs
  zx::channel client_end, server_end;
  ASSERT_OK(zx::channel::create(0u, &client_end, &server_end));
  ASSERT_OK(ConnectClient(std::move(server_end)));

  // Connect to File
  zx::channel fc1, fc2;
  ASSERT_OK(zx::channel::create(0u, &fc1, &fc2));
  ASSERT_OK(fdio_open_at(client_end.get(), "file", fio::OPEN_RIGHT_READABLE, fc2.release()));

  // Use NodeGetFlags to get current flags and rights
  auto file_get_result = fio::Node::Call::NodeGetFlags(zx::unowned_channel(fc1));
  EXPECT_OK(file_get_result.status());
  EXPECT_EQ(fio::OPEN_RIGHT_READABLE, file_get_result.Unwrap()->flags);
  // Make modifications to flags with NodeSetFlags: Note this only works for OPEN_FLAG_APPEND based
  // on posix standard
  auto file_set_result =
      fio::Node::Call::NodeSetFlags(zx::unowned_channel(fc1), fio::OPEN_FLAG_APPEND);
  EXPECT_OK(file_set_result.Unwrap()->s);
  // Check that the new flag is saved
  file_get_result = fio::Node::Call::NodeGetFlags(zx::unowned_channel(fc1));
  EXPECT_OK(file_get_result.Unwrap()->s);
  EXPECT_EQ(fio::OPEN_RIGHT_READABLE | fio::OPEN_FLAG_APPEND, file_get_result.Unwrap()->flags);
}

TEST_F(ConnectionTest, NodeGetSetFlagsOnDirectory) {
  // Create connection to vfs
  zx::channel client_end, server_end;
  ASSERT_OK(zx::channel::create(0u, &client_end, &server_end));
  ASSERT_OK(ConnectClient(std::move(server_end)));

  // Connect to Directory
  zx::channel dc1, dc2;
  ASSERT_OK(zx::channel::create(0u, &dc1, &dc2));
  ASSERT_OK(fdio_open_at(client_end.get(), "dir",
                         fio::OPEN_RIGHT_READABLE | fio::OPEN_RIGHT_WRITABLE, dc2.release()));

  // Read/write/read directory flags; same as for file
  auto dir_get_result = fio::Node::Call::NodeGetFlags(zx::unowned_channel(dc1));
  EXPECT_OK(dir_get_result.Unwrap()->s);
  EXPECT_EQ(fio::OPEN_RIGHT_READABLE | fio::OPEN_RIGHT_WRITABLE, dir_get_result.Unwrap()->flags);

  auto dir_set_result =
      fio::Node::Call::NodeSetFlags(zx::unowned_channel(dc1), fio::OPEN_FLAG_APPEND);
  EXPECT_OK(dir_set_result.Unwrap()->s);

  dir_get_result = fio::Node::Call::NodeGetFlags(zx::unowned_channel(dc1));
  EXPECT_OK(dir_get_result.Unwrap()->s);
  EXPECT_EQ(fio::OPEN_RIGHT_READABLE | fio::OPEN_RIGHT_WRITABLE | fio::OPEN_FLAG_APPEND,
            dir_get_result.Unwrap()->flags);
}

TEST_F(ConnectionTest, FileGetSetFlagsOnFile) {
  // Create connection to vfs
  zx::channel client_end, server_end;
  ASSERT_OK(zx::channel::create(0u, &client_end, &server_end));
  ASSERT_OK(ConnectClient(std::move(server_end)));

  // Connect to File
  zx::channel fc1, fc2;
  ASSERT_OK(zx::channel::create(0u, &fc1, &fc2));
  ASSERT_OK(fdio_open_at(client_end.get(), "file", fio::OPEN_RIGHT_READABLE, fc2.release()));

  // Use NodeGetFlags to get current flags and rights
  auto file_get_result = fio::File::Call::GetFlags(zx::unowned_channel(fc1));
  EXPECT_OK(file_get_result.status());
  EXPECT_EQ(fio::OPEN_RIGHT_READABLE, file_get_result.Unwrap()->flags);
  // Make modifications to flags with NodeSetFlags: Note this only works for OPEN_FLAG_APPEND based
  // on posix standard
  auto file_set_result = fio::File::Call::SetFlags(zx::unowned_channel(fc1), fio::OPEN_FLAG_APPEND);
  EXPECT_OK(file_set_result.Unwrap()->s);
  // Check that the new flag is saved
  file_get_result = fio::File::Call::GetFlags(zx::unowned_channel(fc1));
  EXPECT_OK(file_get_result.Unwrap()->s);
  EXPECT_EQ(fio::OPEN_RIGHT_READABLE | fio::OPEN_FLAG_APPEND, file_get_result.Unwrap()->flags);
}

TEST_F(ConnectionTest, FileGetSetFlagsDirectory) {
  // Create connection to vfs
  zx::channel client_end, server_end;
  ASSERT_OK(zx::channel::create(0u, &client_end, &server_end));
  ASSERT_OK(ConnectClient(std::move(server_end)));

  // Connect to Directory
  zx::channel dc1, dc2;
  ASSERT_OK(zx::channel::create(0u, &dc1, &dc2));
  ASSERT_OK(fdio_open_at(client_end.get(), "dir",
                         fio::OPEN_RIGHT_READABLE | fio::OPEN_RIGHT_WRITABLE, dc2.release()));

  // Read/write/read directory flags using File protocol Get/SetFlags
  // TODO(fxb/34613): Currently, connection.h handles multiplexing Node/File/Directory
  // calls to the same underlying object, and thus the following tests pass.
  // However, this is incorrect behavior that only works due to the current
  // connection implementation. File operations should not be able to be called
  // on Directory objects. Change these tests once connection.h functionality
  // is split in ulib/fs rework.
  auto dir_get_result = fio::File::Call::GetFlags(zx::unowned_channel(dc1));
  EXPECT_OK(dir_get_result.Unwrap()->s);
  EXPECT_EQ(fio::OPEN_RIGHT_READABLE | fio::OPEN_RIGHT_WRITABLE, dir_get_result.Unwrap()->flags);

  auto dir_set_result = fio::File::Call::SetFlags(zx::unowned_channel(dc1), fio::OPEN_FLAG_APPEND);
  EXPECT_OK(dir_set_result.Unwrap()->s);

  dir_get_result = fio::File::Call::GetFlags(zx::unowned_channel(dc1));
  EXPECT_OK(dir_get_result.Unwrap()->s);
  EXPECT_EQ(fio::OPEN_RIGHT_READABLE | fio::OPEN_RIGHT_WRITABLE | fio::OPEN_FLAG_APPEND,
            dir_get_result.Unwrap()->flags);
}

}  // namespace
