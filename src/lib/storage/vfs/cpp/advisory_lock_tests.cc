// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <fidl/fuchsia.io/cpp/wire.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/namespace.h>
#include <lib/file-lock/file-lock.h>
#include <lib/fpromise/single_threaded_executor.h>
#include <lib/memfs/memfs.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

#include <fbl/unique_fd.h>
#include <zxtest/zxtest.h>

#include "src/lib/fxl/strings/substitute.h"
#include "src/lib/storage/vfs/cpp/remote_dir.h"

namespace {
constexpr char kTmpfsPath[] = "/fshost-flock-tmp";
constexpr char kFlockDir[] = "flock-dir";
constexpr char kTmpfsPathFile[] = "/fshost-flock-tmp/flock_smoke";
const ssize_t FILE_SIZE = 1024;

class FlockTest : public zxtest::Test {
 public:
  FlockTest() : memfs_loop_(&kAsyncLoopConfigNoAttachToCurrentThread) {}

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

  void AddFile(size_t content_size) {
    std::string contents(content_size, 'X');
    int fd = open(kTmpfsPathFile, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR);
    use_first_fd_ = true;  // first |GetFd| gets this one
    EXPECT_LT(-1, fd);
    fds_.push_back(fd);
    ASSERT_EQ(write(fd, contents.c_str(), content_size), content_size);
  }

  void CloseFile() {
    std::for_each(fds_.begin(), fds_.end(), &close);
    unlink(kTmpfsPathFile);
  }

  void MakeDir(const std::string& path) {
    ASSERT_EQ(0, mkdir(fxl::Substitute("$0/$1", kTmpfsPath, path).c_str(), 0666));
  }

  int GetFd() {
    if (use_first_fd_) {
      EXPECT_EQ(1, fds_.size());
      use_first_fd_ = false;
      return fds_[0];
    }
    int fd = open(kTmpfsPathFile, O_RDWR);
    EXPECT_LT(-1, fd);
    return fd;
  }

 private:
  // File lock related.
  std::vector<int> fds_;
  bool use_first_fd_;

  // memfs and namespace related.
  fdio_ns_t* namespace_;
  async::Loop memfs_loop_;
  memfs_filesystem_t* memfs_;
};

TEST_F(FlockTest, FlockOnDir) {
  // Initialize test directory
  MakeDir(kFlockDir);

  int fd = open(fxl::Substitute("$0/$1", kTmpfsPath, kFlockDir).c_str(), O_RDONLY | O_DIRECTORY);
  ASSERT_NE(-1, fd);

  ASSERT_EQ(0, flock(fd, LOCK_EX));

  int fd2 = open(fxl::Substitute("$0/$1", kTmpfsPath, kFlockDir).c_str(), O_RDONLY | O_DIRECTORY);
  ASSERT_LT(-1, fd2);
  ASSERT_EQ(-1, flock(fd2, LOCK_EX | LOCK_NB));

  ASSERT_EQ(0, flock(fd, LOCK_UN));
  close(fd);
  close(fd2);
}

TEST_F(FlockTest, FlockExclusiveNoBlock) {
  AddFile(FILE_SIZE);
  int fd_a = GetFd();
  int fd_b = GetFd();

  EXPECT_EQ(0, flock(fd_a, LOCK_EX));
  EXPECT_EQ(-1, flock(fd_b, LOCK_EX | LOCK_NB));
  EXPECT_EQ(errno, EWOULDBLOCK);
  EXPECT_EQ(0, flock(fd_a, LOCK_UN));

  EXPECT_EQ(0, flock(fd_b, LOCK_EX));
  EXPECT_EQ(0, flock(fd_b, LOCK_UN));
  CloseFile();
}

TEST_F(FlockTest, FlockVsShare) {
  AddFile(FILE_SIZE);
  int fd_a = GetFd();
  int fd_b = GetFd();

  ASSERT_EQ(0, flock(fd_a, LOCK_SH));
  ASSERT_EQ(0, flock(fd_b, LOCK_SH));
  ASSERT_EQ(0, flock(fd_a, LOCK_UN));
  ASSERT_EQ(0, flock(fd_b, LOCK_UN));
  CloseFile();
}

TEST_F(FlockTest, FlockLockUnlock) {
  AddFile(FILE_SIZE);
  int fd_a = GetFd();
  ASSERT_EQ(0, flock(fd_a, LOCK_SH));
  ASSERT_EQ(0, flock(fd_a, LOCK_UN));
  ASSERT_EQ(0, flock(fd_a, LOCK_SH));
  ASSERT_EQ(0, flock(fd_a, LOCK_UN));
  ASSERT_EQ(0, flock(fd_a, LOCK_EX));
  ASSERT_EQ(0, flock(fd_a, LOCK_UN));
  ASSERT_EQ(0, flock(fd_a, LOCK_EX));
  ASSERT_EQ(0, flock(fd_a, LOCK_UN));
  ASSERT_EQ(0, flock(fd_a, LOCK_SH));
  ASSERT_EQ(0, flock(fd_a, LOCK_UN));
  CloseFile();
}

TEST_F(FlockTest, FlockTwoShared) {
  AddFile(FILE_SIZE);
  int fd_a = GetFd();
  int fd_b = GetFd();
  ASSERT_EQ(0, flock(fd_a, LOCK_SH));
  ASSERT_EQ(0, flock(fd_b, LOCK_SH));
  ASSERT_EQ(0, flock(fd_a, LOCK_UN));
  ASSERT_EQ(0, flock(fd_b, LOCK_UN));
  CloseFile();
}

TEST_F(FlockTest, FlockSharedNoBlockExclusive) {
  AddFile(FILE_SIZE);
  int fd_a = GetFd();
  int fd_b = GetFd();

  ASSERT_EQ(0, flock(fd_a, LOCK_SH));
  ASSERT_EQ(-1, flock(fd_b, LOCK_EX | LOCK_NB));
  ASSERT_EQ(EWOULDBLOCK, errno);
  ASSERT_EQ(0, flock(fd_a, LOCK_UN));
  CloseFile();
}

TEST_F(FlockTest, FlockExclusiveNoBlockShared) {
  AddFile(FILE_SIZE);
  int fd_a = GetFd();
  int fd_b = GetFd();

  ASSERT_EQ(0, flock(fd_a, LOCK_EX));
  ASSERT_EQ(-1, flock(fd_b, LOCK_SH | LOCK_NB));
  ASSERT_EQ(EWOULDBLOCK, errno);
  ASSERT_EQ(0, flock(fd_a, LOCK_UN));
  CloseFile();
}

TEST_F(FlockTest, FlockExclusiveNoBlockExclusive) {
  AddFile(FILE_SIZE);
  int fd_a = GetFd();
  int fd_b = GetFd();

  ASSERT_EQ(0, flock(fd_a, LOCK_EX));
  ASSERT_EQ(-1, flock(fd_b, LOCK_EX | LOCK_NB));
  ASSERT_EQ(EWOULDBLOCK, errno);
  ASSERT_EQ(0, flock(fd_a, LOCK_UN));
  CloseFile();
}

TEST_F(FlockTest, FlockExclusiveToShared) {
  AddFile(FILE_SIZE);
  int fd_a = GetFd();
  int fd_b = GetFd();

  ASSERT_EQ(0, flock(fd_a, LOCK_EX));
  ASSERT_EQ(-1, flock(fd_b, LOCK_SH | LOCK_NB));
  ASSERT_EQ(EWOULDBLOCK, errno);
  ASSERT_EQ(0, flock(fd_a, LOCK_SH));
  ASSERT_EQ(0, flock(fd_b, LOCK_SH));
  ASSERT_EQ(0, flock(fd_a, LOCK_UN));
  ASSERT_EQ(0, flock(fd_b, LOCK_UN));
  CloseFile();
}

TEST_F(FlockTest, FlockSharedToExclusive) {
  AddFile(FILE_SIZE);
  int fd_a = GetFd();
  int fd_b = GetFd();

  ASSERT_EQ(0, flock(fd_a, LOCK_SH));
  ASSERT_EQ(0, flock(fd_b, LOCK_SH));
  ASSERT_EQ(0, flock(fd_b, LOCK_UN));
  ASSERT_EQ(0, flock(fd_a, LOCK_EX));
  ASSERT_EQ(-1, flock(fd_b, LOCK_SH | LOCK_NB));
  ASSERT_EQ(EWOULDBLOCK, errno);
  CloseFile();
}
}  // namespace
