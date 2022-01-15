// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef __Fuchsia__
// In some Linux systems we can determine if
// flock is blocking a thread by examining the
// contents of /proc/<process_id>/task/<thread_id>/wchan.
// This pseudo-file contains the name of the kernel channel
// on which the thread is waiting (and "0" if not blocked).
// On other linux systems, this file contains "0"
// regardless of the state of the thread and tests that
// use this will hang or not work reliably.
//
// Uncomment if /proc/<process_id>/task/<thread_id>/wchan works.
//#define LINUX_HAS_WCHAN (1)
#ifdef LINUX_HAS_WCHAN

#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include <fstream>
#include <string>

#endif
#endif

#include <fcntl.h>
#include <pthread.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <algorithm>
#include <functional>
#include <memory>
#include <semaphore>
#include <thread>
#include <vector>

#include <zxtest/zxtest.h>

// These tests are deterministic, but rely
// on polling. We need to wait durings polls
// so as not to thrash the CPU.
constexpr long POLL_TIME_NS = 5000000L;

#ifdef __Fuchsia__

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/cpp/task.h>
#include <lib/fdio/fdio.h>
#include <lib/fdio/unsafe.h>
#include <lib/memfs/memfs.h>
#include <lib/zxio/types.h>
#include <lib/zxio/zxio.h>
#include <zircon/process.h>

const std::string flock_root("/flock_test");
#else

const std::string flock_root("/tmp");
#endif

const ssize_t FILE_SIZE = 1024;

//
// An important part of this test is the destructor.
// If a test fails by a lock not being released,
// the std::thread::join call will block, failing
// the test.
//
class LockThread {
 public:
  LockThread(int fd, bool exclusive) : fd_(fd), exclusive_(exclusive) {}

  ~LockThread() { lock_thread_.join(); }

  void start() {
    std::function<void()> exec_fn([this]() {
      registerThreadInfo();
      int mode = (this->exclusive_ ? LOCK_EX : LOCK_SH);
      EXPECT_EQ(0, flock(this->fd_, mode));
      EXPECT_EQ(0, flock(this->fd_, LOCK_UN));
    });
    std::thread exec_thread(exec_fn);
    lock_thread_.swap(exec_thread);
  }

#ifdef __Fuchsia__
  void registerThreadInfo() { exec_thread_ = zx_thread_self(); }

  static constexpr zx_thread_state_t ZX_THREAD_STATE_BOGUS = ((zx_thread_state_t)0x0006u);
  void pollForBlock() {
    bool blocked = false;
    while (!blocked) {
      zx_info_thread thd_info;
      thd_info.state = ZX_THREAD_STATE_BOGUS;
      if (exec_thread_ != ZX_HANDLE_INVALID &&
          ZX_OK == zx_object_get_info(exec_thread_, ZX_INFO_THREAD, &thd_info, sizeof(thd_info),
                                      nullptr, nullptr) &&
          thd_info.state == ZX_THREAD_STATE_BLOCKED_CHANNEL) {
        blocked = true;

      } else {
        zx_nanosleep(zx_deadline_after(ZX_NSEC(POLL_TIME_NS)));
      }
    }
  }
#else
#ifdef LINUX_HAS_WCHAN
  void registerThreadInfo() {
    proc_status_ = std::string("/proc/") + std::to_string(getpid()) + std::string("/task/") +
                   std::to_string(gettid()) + std::string("/wchan");
  }

  void pollForBlock() {
    bool blocked = false;
    while (!blocked) {
      std::ifstream ifs(proc_status_);
      std::string wait_status((std::istreambuf_iterator<char>(ifs)),
                              std::istreambuf_iterator<char>());
      ifs.close();
      if (wait_status.compare("locks_lock_inode_wait") == 0) {
        blocked = true;
      } else {
        timespec wait{.tv_sec = 0, .tv_nsec = POLL_TIME_NS};
        nanosleep(&wait, nullptr);
      }
    }
  }
#else
  void registerThreadInfo() {}
  // all tests run with this pollForBlock fail
  void pollForBlock() {
    (void)POLL_TIME_NS;
    ASSERT_EQ(true, false);
  }
#endif
#endif

 private:
  int fd_;
  bool exclusive_;
  std::thread lock_thread_;
#ifdef __Fuchsia__
  zx_handle_t exec_thread_;
#else
  std::string proc_status_;
#endif
};

class TempFile {
  std::string file_name_;
  ssize_t size_;
  std::vector<int> fds_;
  bool use_first_fd_;
#ifdef __Fuchsia__
  memfs_filesystem_t* memfs_;
  async::Loop loop_;
#endif

 public:
  TempFile() : TempFile(FILE_SIZE) {}
  TempFile(ssize_t size)
      : file_name_(flock_root + std::string("/flock_smoke")),
        size_(size)
#ifdef __Fuchsia__
        ,
        memfs_(nullptr),
        loop_(&kAsyncLoopConfigNoAttachToCurrentThread)
#endif
  {

#ifdef __Fuchsia__
    ASSERT_EQ(loop_.StartThread(), ZX_OK);
    ASSERT_EQ(memfs_install_at(loop_.dispatcher(), flock_root.c_str(), &memfs_), ZX_OK);
#endif
    int fd = open(file_name_.c_str(), O_CREAT | O_RDWR, S_IRUSR | S_IWUSR);
    use_first_fd_ = true;  // first |GetFd| gets this one
    EXPECT_LT(-1, fd);
    fds_.push_back(fd);
    char buf[size_];
    memset(buf, 0, size_);
    EXPECT_EQ(size_, write(fd, buf, size_));
    EXPECT_EQ(0, lseek(fd, SEEK_SET, 0));
  }

  ~TempFile() {
    std::for_each(fds_.begin(), fds_.end(), &close);
    unlink(file_name_.c_str());

#ifdef __Fuchsia__
    sync_completion_t unmounted;
    memfs_free_filesystem(memfs_, &unmounted);
    memfs_ = nullptr;
    ASSERT_EQ(sync_completion_wait(&unmounted, zx::duration::infinite().get()), ZX_OK);

    loop_.Shutdown();
#endif
  }

  int GetFd() {
    if (use_first_fd_) {
      EXPECT_EQ(1, fds_.size());
      use_first_fd_ = false;
      return fds_[0];
    }
    int fd = open(file_name_.c_str(), O_RDWR);
    EXPECT_LT(-1, fd);
    return fd;
  }
};

TEST(LockingTest, OpenClose) {
  { TempFile tmp(FILE_SIZE); }
  { TempFile tmp(FILE_SIZE); }
}

constexpr const char* kFlockDir = "/tmp/flock-dir";

TEST(LockingTest, FlockOnDir) {
  EXPECT_EQ(0, mkdir(kFlockDir, 0777));

  int fd = open(kFlockDir, O_RDONLY | O_DIRECTORY);
  EXPECT_LT(-1, fd);

  EXPECT_EQ(0, flock(fd, LOCK_EX));

  int fd2 = open(kFlockDir, O_RDONLY | O_DIRECTORY);
  EXPECT_LT(-1, fd2);
  EXPECT_EQ(-1, flock(fd2, LOCK_EX | LOCK_NB));

  EXPECT_EQ(0, flock(fd, LOCK_UN));
  close(fd);
  EXPECT_EQ(0, rmdir(kFlockDir));
  close(fd2);
}

TEST(LockingTest, FlockExclusiveNoBlock) {
  TempFile tmp(FILE_SIZE);
  int fd_a = tmp.GetFd();
  int fd_b = tmp.GetFd();

  EXPECT_EQ(0, flock(fd_a, LOCK_EX));
  EXPECT_EQ(-1, flock(fd_b, LOCK_EX | LOCK_NB));
  EXPECT_EQ(errno, EWOULDBLOCK);
  EXPECT_EQ(0, flock(fd_a, LOCK_UN));

  EXPECT_EQ(0, flock(fd_b, LOCK_EX));
  EXPECT_EQ(0, flock(fd_b, LOCK_UN));
}

TEST(LockingTest, FlockVsShare) {
  TempFile tmp(FILE_SIZE);
  int fd_a = tmp.GetFd();
  int fd_b = tmp.GetFd();

  ASSERT_EQ(0, flock(fd_a, LOCK_SH));
  ASSERT_EQ(0, flock(fd_b, LOCK_SH));
  ASSERT_EQ(0, flock(fd_a, LOCK_UN));
  ASSERT_EQ(0, flock(fd_b, LOCK_UN));
}

TEST(LockingTest, FlockLockUnlock) {
  TempFile tmp(FILE_SIZE);
  int fd_a = tmp.GetFd();
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
}

TEST(LockingTest, FlockTwoShared) {
  TempFile tmp(FILE_SIZE);
  int fd_a = tmp.GetFd();
  int fd_b = tmp.GetFd();
  ASSERT_EQ(0, flock(fd_a, LOCK_SH));
  ASSERT_EQ(0, flock(fd_b, LOCK_SH));
  ASSERT_EQ(0, flock(fd_a, LOCK_UN));
  ASSERT_EQ(0, flock(fd_b, LOCK_UN));
}

#if defined(__Fuchsia__) || defined(LINUX_HAS_WCHAN)
TEST(LockingTest, FlockExclusive) {
  TempFile tmp(FILE_SIZE);
  int fd_a = tmp.GetFd();
  int fd_b = tmp.GetFd();

  ASSERT_EQ(0, flock(fd_a, LOCK_EX));

  LockThread thread_b(fd_b, true);
  thread_b.start();

  thread_b.pollForBlock();
  ASSERT_EQ(0, flock(fd_a, LOCK_UN));
}

TEST(LockingTest, FlockExclusiveBlocksShareds) {
  TempFile tmp(FILE_SIZE);
  int fd_a = tmp.GetFd();
  int fd_b = tmp.GetFd();
  int fd_c = tmp.GetFd();
  ASSERT_EQ(0, flock(fd_a, LOCK_EX));

  LockThread thread_b(fd_b, false);
  thread_b.start();

  LockThread thread_c(fd_c, false);
  thread_c.start();

  ASSERT_EQ(0, flock(fd_a, LOCK_UN));
}

TEST(LockingTest, FlockSharedsBlockExclusive) {
  TempFile tmp(FILE_SIZE);
  int fd_a = tmp.GetFd();
  int fd_b = tmp.GetFd();
  int fd_c = tmp.GetFd();
  ASSERT_EQ(0, flock(fd_a, LOCK_SH));
  ASSERT_EQ(0, flock(fd_b, LOCK_SH));

  LockThread thread_c(fd_c, true);
  thread_c.start();

  ASSERT_EQ(0, flock(fd_a, LOCK_UN));
  ASSERT_EQ(0, flock(fd_b, LOCK_UN));
}
#endif

TEST(LockingTest, FlockSharedNoBlockExclusive) {
  TempFile tmp(FILE_SIZE);
  int fd_a = tmp.GetFd();
  int fd_b = tmp.GetFd();

  ASSERT_EQ(0, flock(fd_a, LOCK_SH));
  ASSERT_EQ(-1, flock(fd_b, LOCK_EX | LOCK_NB));
  ASSERT_EQ(EWOULDBLOCK, errno);
  ASSERT_EQ(0, flock(fd_a, LOCK_UN));
}

TEST(LockingTest, FlockExclusiveNoBlockShared) {
  TempFile tmp(FILE_SIZE);
  int fd_a = tmp.GetFd();
  int fd_b = tmp.GetFd();

  ASSERT_EQ(0, flock(fd_a, LOCK_EX));
  ASSERT_EQ(-1, flock(fd_b, LOCK_SH | LOCK_NB));
  ASSERT_EQ(EWOULDBLOCK, errno);
  ASSERT_EQ(0, flock(fd_a, LOCK_UN));
}

TEST(LockingTest, FlockExclusiveNoBlockExclusive) {
  TempFile tmp(FILE_SIZE);
  int fd_a = tmp.GetFd();
  int fd_b = tmp.GetFd();

  ASSERT_EQ(0, flock(fd_a, LOCK_EX));
  ASSERT_EQ(-1, flock(fd_b, LOCK_EX | LOCK_NB));
  ASSERT_EQ(EWOULDBLOCK, errno);
  ASSERT_EQ(0, flock(fd_a, LOCK_UN));
}

TEST(LockingTest, FlockExclusiveToShared) {
  TempFile tmp(FILE_SIZE);
  int fd_a = tmp.GetFd();
  int fd_b = tmp.GetFd();

  ASSERT_EQ(0, flock(fd_a, LOCK_EX));
  ASSERT_EQ(-1, flock(fd_b, LOCK_SH | LOCK_NB));
  ASSERT_EQ(EWOULDBLOCK, errno);
  ASSERT_EQ(0, flock(fd_a, LOCK_SH));
  ASSERT_EQ(0, flock(fd_b, LOCK_SH));
  ASSERT_EQ(0, flock(fd_a, LOCK_UN));
  ASSERT_EQ(0, flock(fd_b, LOCK_UN));
}

TEST(LockingTest, FlockSharedToExclusive) {
  TempFile tmp(FILE_SIZE);
  int fd_a = tmp.GetFd();
  int fd_b = tmp.GetFd();

  ASSERT_EQ(0, flock(fd_a, LOCK_SH));
  ASSERT_EQ(0, flock(fd_b, LOCK_SH));
  ASSERT_EQ(0, flock(fd_b, LOCK_UN));
  ASSERT_EQ(0, flock(fd_a, LOCK_EX));
  ASSERT_EQ(-1, flock(fd_b, LOCK_SH | LOCK_NB));
  ASSERT_EQ(EWOULDBLOCK, errno);
}

#if defined(__Fuchsia__) || defined(LINUX_HAS_WCHAN)
TEST(LockingTest, FlockCloseUnlocks) {
  TempFile tmp(FILE_SIZE);
  int fd_a = tmp.GetFd();
  int fd_b = tmp.GetFd();

  ASSERT_EQ(0, flock(fd_a, LOCK_EX));

  LockThread thread_b(fd_b, true);
  thread_b.start();
  thread_b.pollForBlock();

  EXPECT_EQ(0, close(fd_a));
}
#endif
