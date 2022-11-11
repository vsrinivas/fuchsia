// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>

#include <gtest/gtest.h>

#include "src/proc/tests/chromiumos/syscalls/test_helper.h"

namespace {

bool CheckLock(int fd, short type, off_t start, off_t length, pid_t pid) {
  ForkHelper helper;
  // Fork a process to be able to check the state of locks in fd.
  helper.RunInForkedProcess([&] {
    struct flock fl;
    fl.l_type = F_WRLCK;
    fl.l_whence = SEEK_SET;
    fl.l_start = start;
    fl.l_len = length;
    SAFE_SYSCALL(fcntl(fd, F_GETLK, &fl));

    ASSERT_EQ(fl.l_type, type);
    if (type != F_UNLCK) {
      ASSERT_EQ(fl.l_whence, SEEK_SET);
      ASSERT_EQ(fl.l_start, start);
      ASSERT_EQ(fl.l_len, length);
      ASSERT_EQ(fl.l_pid, pid);
    }
  });
  return helper.WaitForChildren();
}

// Open a file to test. It will be of size 3000, and the position will be at
// 2000.
int OpenTestFile() {
  char *tmp = getenv("TEST_TMPDIR");
  std::string path = tmp == nullptr ? "/tmp/fcntltest" : std::string(tmp) + "/fcntltest";
  int fd = open(path.c_str(), O_RDWR | O_CREAT | O_TRUNC, 0777);
  SAFE_SYSCALL(lseek(fd, 2999, SEEK_SET));
  // Make the file 3000 bytes longs
  SAFE_SYSCALL(write(fd, &fd, 1));
  // Move to 2000
  SAFE_SYSCALL(lseek(fd, 2000, SEEK_SET));
  return fd;
}

TEST(FcntlLockTest, ReleaseLockInMiddleOfAnotherLock) {
  ForkHelper helper;
  helper.RunInForkedProcess([&] {
    int fd = OpenTestFile();

    struct flock fl;
    fl.l_type = F_WRLCK;
    fl.l_whence = SEEK_CUR;
    fl.l_start = -2000;
    fl.l_len = 3000;
    SAFE_SYSCALL(fcntl(fd, F_SETLK, &fl));

    fl.l_type = F_UNLCK;
    fl.l_whence = SEEK_END;
    fl.l_start = -2000;
    fl.l_len = 1000;
    SAFE_SYSCALL(fcntl(fd, F_SETLK, &fl));

    // Check that we have a lock between [0, 1000[ and [2000, 3000[.
    ASSERT_TRUE(CheckLock(fd, F_WRLCK, 0, 1000, getpid()));
    ASSERT_TRUE(CheckLock(fd, F_UNLCK, 1000, 1000, 0));
    ASSERT_TRUE(CheckLock(fd, F_WRLCK, 2000, 1000, getpid()));
  });
}

TEST(FcntlLockTest, ChangeLockTypeInMiddleOfAnotherLock) {
  ForkHelper helper;
  helper.RunInForkedProcess([&] {
    int fd = OpenTestFile();

    struct flock fl;
    fl.l_type = F_WRLCK;
    fl.l_whence = SEEK_SET;
    fl.l_start = 0;
    fl.l_len = 3000;
    SAFE_SYSCALL(fcntl(fd, F_SETLK, &fl));

    fl.l_type = F_RDLCK;
    fl.l_whence = SEEK_END;
    fl.l_start = -2000;
    fl.l_len = 1000;
    SAFE_SYSCALL(fcntl(fd, F_SETLK, &fl));

    // Check that we have a write lock between [0, 1000[ and [2000, 3000[ and a
    // read lock between [1000, 2000[.
    ASSERT_TRUE(CheckLock(fd, F_WRLCK, 0, 1000, getpid()));
    ASSERT_TRUE(CheckLock(fd, F_RDLCK, 1000, 1000, getpid()));
    ASSERT_TRUE(CheckLock(fd, F_WRLCK, 2000, 1000, getpid()));
  });
}

TEST(FcntlLockTest, CloneFiles) {
  // Do all the test in another process, as it will requires closing the parent
  // process before the child one.
  ForkHelper helper;
  helper.RunInForkedProcess([&] {
    int fd = OpenTestFile();
    pid_t pid = getpid();

    // Lock the file.
    struct flock fl;
    fl.l_type = F_WRLCK;
    fl.l_whence = SEEK_SET;
    fl.l_start = 0;
    fl.l_len = 0;
    SAFE_SYSCALL(fcntl(fd, F_SETLK, &fl));

    // Clone the process, with CLONE_FILES
    int flags = CLONE_FILES | SIGCHLD;
    if (SAFE_SYSCALL(syscall(SYS_clone, flags, nullptr, nullptr, nullptr, nullptr)) > 0) {
      // Parent immediately exit.
      _exit(testing::Test::HasFailure());
    }

    // The child is a new process but with the exact same file table as its
    // parent.
    ASSERT_NE(getpid(), pid);
    // Wait for our parent to finish.
    while (getppid() == pid) {
      usleep(1000);
    }

    // Fork a process to be able to check the state of locks in fd. The returned
    // pid is expected to be the one of the now dead process.
    ASSERT_TRUE(CheckLock(fd, F_WRLCK, 0, 0, pid));

    int new_fd = dup(fd);
    // Closing fd should release the lock.
    SAFE_SYSCALL(close(fd));
    ASSERT_TRUE(CheckLock(new_fd, F_UNLCK, 0, 0, 0));
  });
}

TEST(FcntlLockTest, CheckErrors) {
  int fd = OpenTestFile();

  struct flock fl;
  fl.l_type = 42;
  fl.l_whence = SEEK_SET;
  fl.l_start = 0;
  fl.l_len = 0;

  ASSERT_EQ(fcntl(fd, F_SETLK, &fl), -1);
  ASSERT_EQ(errno, EINVAL);

  fl.l_type = F_WRLCK;
  fl.l_whence = 42;

  ASSERT_EQ(fcntl(fd, F_SETLK, &fl), -1);
  ASSERT_EQ(errno, EINVAL);

  fl.l_type = F_WRLCK;
  fl.l_whence = SEEK_END;
  fl.l_start = std::numeric_limits<decltype(fl.l_start)>::max();
  fl.l_len = 0;

  ASSERT_EQ(fcntl(fd, F_SETLK, &fl), -1);
  ASSERT_EQ(errno, EOVERFLOW);

  fl.l_type = F_WRLCK;
  fl.l_whence = SEEK_SET;
  fl.l_start = 0;
  fl.l_len = -1;

  ASSERT_EQ(fcntl(fd, F_SETLK, &fl), -1);
  ASSERT_EQ(errno, EINVAL);
}

TEST(FcntlTest, FdDup) {
  int fd = OpenTestFile();

  int new_fd = SAFE_SYSCALL(fcntl(fd, F_DUPFD, 1000));
  ASSERT_GE(new_fd, 1000);
  new_fd = SAFE_SYSCALL(fcntl(fd, F_DUPFD, 0));
  ASSERT_LT(new_fd, 1000);
}

}  // namespace
