// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <fidl/fuchsia.io/cpp/wire.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fdio/cpp/caller.h>
#include <lib/fdio/fd.h>
#include <lib/memfs/memfs.h>
#include <lib/sync/completion.h>
#include <unistd.h>

#include <utility>

#include <fbl/unique_fd.h>
#include <zxtest/zxtest.h>

namespace fio = fuchsia_io;

namespace {

void TryFilesystemOperations(zx::unowned_channel channel) {
  const char* golden = "foobar";
  auto write_result = fidl::WireCall<fio::File>(channel)->WriteAt(
      fidl::VectorView<uint8_t>::FromExternal(
          const_cast<uint8_t*>(reinterpret_cast<const uint8_t*>(golden)), strlen(golden)),
      0);
  ASSERT_EQ(write_result.status(), ZX_OK);
  ASSERT_EQ(write_result->s, ZX_OK);
  ASSERT_EQ(write_result->actual, strlen(golden));

  auto read_result = fidl::WireCall<fio::File>(channel)->ReadAt(256, 0);
  ASSERT_EQ(read_result.status(), ZX_OK);
  ASSERT_EQ(read_result->s, ZX_OK);
  ASSERT_EQ(read_result->data.count(), strlen(golden));
  ASSERT_EQ(memcmp(read_result->data.data(), golden, strlen(golden)), 0);
}

void TryFilesystemOperations(const zx::channel& channel) {
  TryFilesystemOperations(channel.borrow());
}

void TryFilesystemOperations(const fdio_cpp::FdioCaller& caller) {
  TryFilesystemOperations(caller.channel());
}

void TryFilesystemOperations(const fdio_cpp::UnownedFdioCaller& caller) {
  TryFilesystemOperations(caller.channel());
}

class Harness {
 public:
  Harness() {}

  ~Harness() {
    if (memfs_) {
      sync_completion_t unmounted;
      memfs_free_filesystem(memfs_, &unmounted);
      sync_completion_wait(&unmounted, ZX_SEC(3));
    }
  }

  void Setup() {
    ASSERT_EQ(loop_.StartThread(), ZX_OK);
    zx_handle_t root;
    ASSERT_EQ(memfs_create_filesystem(loop_.dispatcher(), &memfs_, &root), ZX_OK);
    int fd;
    ASSERT_EQ(fdio_fd_create(root, &fd), ZX_OK);
    fbl::unique_fd dir(fd);
    ASSERT_TRUE(dir);
    fd_.reset(openat(dir.get(), "my-file", O_CREAT | O_RDWR));
    ASSERT_TRUE(fd_);
  }

  fbl::unique_fd fd() { return std::move(fd_); }

 private:
  async::Loop loop_ = async::Loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  memfs_filesystem_t* memfs_ = nullptr;
  fbl::unique_fd fd_;
};

TEST(FdioCallTests, FdioCallerFile) {
  Harness harness;
  ASSERT_NO_FATAL_FAILURES(harness.Setup());
  auto fd = harness.fd();

  // Try some filesystem operations.
  fdio_cpp::FdioCaller caller(std::move(fd));
  ASSERT_TRUE(caller);
  ASSERT_NO_FATAL_FAILURES(TryFilesystemOperations(caller));

  // Re-acquire the underlying fd.
  fd = caller.release();
  ASSERT_EQ(close(fd.release()), 0);
}

TEST(FdioCallTests, FdioCallerMoveAssignment) {
  Harness harness;
  ASSERT_NO_FATAL_FAILURES(harness.Setup());
  auto fd = harness.fd();

  fdio_cpp::FdioCaller caller(std::move(fd));
  fdio_cpp::FdioCaller move_assignment_caller = std::move(caller);
  ASSERT_TRUE(move_assignment_caller);
  ASSERT_FALSE(caller);
  ASSERT_NO_FATAL_FAILURES(TryFilesystemOperations(move_assignment_caller));
}

TEST(FdioCallTests, FdioCallerMoveConstructor) {
  Harness harness;
  ASSERT_NO_FATAL_FAILURES(harness.Setup());
  auto fd = harness.fd();

  fdio_cpp::FdioCaller caller(std::move(fd));
  fdio_cpp::FdioCaller move_ctor_caller(std::move(caller));
  ASSERT_TRUE(move_ctor_caller);
  ASSERT_FALSE(caller);
  ASSERT_NO_FATAL_FAILURES(TryFilesystemOperations(move_ctor_caller));
}

TEST(FdioCallTests, FdioCallerBorrow) {
  Harness harness;
  ASSERT_NO_FATAL_FAILURES(harness.Setup());
  auto fd = harness.fd();

  fdio_cpp::FdioCaller caller(std::move(fd));
  zx::unowned_channel channel = caller.channel();
  ASSERT_TRUE(channel->is_valid());
  ASSERT_TRUE(caller);
  ASSERT_NO_FATAL_FAILURES(TryFilesystemOperations(caller));
  ASSERT_NO_FATAL_FAILURES(TryFilesystemOperations(std::move(channel)));
  ASSERT_NO_FATAL_FAILURES(TryFilesystemOperations(caller.node().channel()));
  ASSERT_NO_FATAL_FAILURES(TryFilesystemOperations(caller.file().channel()));
  ASSERT_NO_FATAL_FAILURES(TryFilesystemOperations(caller.directory().channel()));
}

TEST(FdioCallTests, FdioCallerClone) {
  Harness harness;
  ASSERT_NO_FATAL_FAILURES(harness.Setup());
  auto fd = harness.fd();

  fdio_cpp::FdioCaller caller(std::move(fd));
  auto channel = caller.clone_channel();
  ASSERT_OK(channel.status_value());
  ASSERT_TRUE(channel->is_valid());
  ASSERT_TRUE(caller);
  ASSERT_NE(caller.channel()->get(), channel->get());
  ASSERT_NO_FATAL_FAILURES(TryFilesystemOperations(caller));
  ASSERT_NO_FATAL_FAILURES(TryFilesystemOperations(channel->borrow()));
  ASSERT_NO_FATAL_FAILURES(TryFilesystemOperations(caller.clone_node()->channel()));
  ASSERT_NO_FATAL_FAILURES(TryFilesystemOperations(caller.clone_file()->channel()));
  ASSERT_NO_FATAL_FAILURES(TryFilesystemOperations(caller.clone_directory()->channel()));
}

TEST(FdioCallTests, FdioCallerTake) {
  Harness harness;
  ASSERT_NO_FATAL_FAILURES(harness.Setup());
  auto fd = harness.fd();

  fdio_cpp::FdioCaller caller(std::move(fd));
  auto channel = caller.take_channel();
  ASSERT_OK(channel.status_value());
  ASSERT_TRUE(channel->is_valid());
  ASSERT_FALSE(caller);
  ASSERT_NO_FATAL_FAILURES(TryFilesystemOperations(channel->borrow()));
}

TEST(FdioCallTests, UnownedFdioCaller) {
  Harness harness;
  ASSERT_NO_FATAL_FAILURES(harness.Setup());
  auto fd = harness.fd();
  fdio_cpp::UnownedFdioCaller caller(fd);
  ASSERT_TRUE(caller);
  ASSERT_TRUE(fd);
  ASSERT_NO_FATAL_FAILURES(TryFilesystemOperations(caller));
}

TEST(FdioCallTests, UnownedFdioCallerBorrow) {
  Harness harness;
  ASSERT_NO_FATAL_FAILURES(harness.Setup());
  auto fd = harness.fd();

  fdio_cpp::UnownedFdioCaller caller(fd);
  zx::unowned_channel channel = caller.channel();
  ASSERT_TRUE(channel->is_valid());
  ASSERT_TRUE(caller);
  ASSERT_NO_FATAL_FAILURES(TryFilesystemOperations(caller));
  ASSERT_NO_FATAL_FAILURES(TryFilesystemOperations(std::move(channel)));
  ASSERT_NO_FATAL_FAILURES(TryFilesystemOperations(caller.node().channel()));
  ASSERT_NO_FATAL_FAILURES(TryFilesystemOperations(caller.file().channel()));
  ASSERT_NO_FATAL_FAILURES(TryFilesystemOperations(caller.directory().channel()));
}

}  // namespace
