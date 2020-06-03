// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <unistd.h>

#include <fbl/unique_fd.h>
#include <fuchsia/io/c/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fdio/cpp/caller.h>
#include <lib/fdio/fd.h>
#include <lib/memfs/memfs.h>
#include <lib/sync/completion.h>
#include <zxtest/zxtest.h>

#include <utility>

namespace {

void TryFilesystemOperations(const fdio_cpp::FdioCaller& caller) {
  const char* golden = "foobar";
  zx_status_t status;
  uint64_t actual;
  ASSERT_EQ(
      fuchsia_io_FileWriteAt(caller.borrow_channel(), reinterpret_cast<const uint8_t*>(golden),
                             strlen(golden), 0, &status, &actual),
      ZX_OK);
  ASSERT_EQ(status, ZX_OK);
  ASSERT_EQ(actual, strlen(golden));

  char buf[256];
  ASSERT_EQ(fuchsia_io_FileReadAt(caller.borrow_channel(), static_cast<uint64_t>(sizeof(buf)), 0,
                                  &status, reinterpret_cast<uint8_t*>(buf), sizeof(buf), &actual),
            ZX_OK);
  ASSERT_EQ(status, ZX_OK);
  ASSERT_EQ(actual, strlen(golden));
  ASSERT_EQ(memcmp(buf, golden, strlen(golden)), 0);
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

}  // namespace
