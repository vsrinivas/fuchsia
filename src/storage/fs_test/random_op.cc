// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <threads.h>
#include <time.h>
#include <unistd.h>

#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <fbl/unique_fd.h>

#include "src/storage/fs_test/fs_test_fixture.h"

namespace fs_test {
namespace {

constexpr int kFail = -1;
constexpr int kDone = 0;
constexpr int kBlockSize = 8192;
constexpr int kBufferSize = 65536;

class RandomOpTest;

unsigned GenerateSeed() {
  struct timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);
  return ts.tv_nsec;
}

struct Worker {
  Worker(RandomOpTest& env, std::string_view name, ssize_t size)
      : env(env), size(size), name(name) {}

  RandomOpTest& env;
  fbl::unique_fd fd;
  ssize_t size;
  std::string name;
  unsigned seed = GenerateSeed();
  unsigned opcnt = 0;
};

constexpr int KiB(int n) { return n * 1024; }
constexpr int MiB(int n) { return n * 1024; }

constexpr struct {
  std::string_view name;
  uint32_t size;
} kWork[] = {
    // one thread per work entry
    {"thd0000", KiB(5)},   {"thd0001", MiB(10)}, {"thd0002", KiB(512)}, {"thd0003", KiB(512)},
    {"thd0004", KiB(512)}, {"thd0005", MiB(20)}, {"thd0006", KiB(512)}, {"thd0007", KiB(512)},
};

class RandomOpTest : public FilesystemTest {
 public:
  struct RandomOp {
    const char* name;
    int (*fn)(Worker*);
    unsigned weight;
  };

  const std::vector<RandomOp>& operations() const { return operations_; }
  bool debug() const { return debug_; }

 protected:
  RandomOpTest() {
    AddRandomOperations();

    for (size_t n = 0; n < std::size(kWork); n++) {
      NewWorker(kWork[n].name, kWork[n].size);
    }
  }

  void NewWorker(std::string_view name, uint32_t size) {
    all_workers_.push_back(std::make_unique<Worker>(*this, name, size));
  }

  static void TaskDebugOp(Worker* w, const char* fn) {
    RandomOpTest& env = w->env;

    w->opcnt++;
    if (env.debug()) {
      std::cout << w->name << "[" << w->opcnt << "] " << fn << std::endl;
    }
  }

  static void TaskError(Worker* w, const char* fn, const char* msg) {
    int errnum = errno;
    char buf[128];
    strerror_r(errnum, buf, sizeof(buf));
    ADD_FAILURE() << w->name << " ERROR " << fn << "(" << msg << "): " << buf << "(" << errnum
                  << ")";
  }

  static int TaskCreateA(Worker* w) {
    // put a page of data into /a
    TaskDebugOp(w, "t: create_a");
    int fd = open(w->env.GetPath("a").c_str(), O_RDWR + O_CREAT, 0666);
    if (fd < 0) {
      // errno may be one of EEXIST
      if (errno != EEXIST) {
        TaskError(w, "t: create_a", "open");
        return kFail;
      }
    } else {
      char buf[kBlockSize];
      memset(buf, 0xab, sizeof(buf));
      ssize_t len = write(fd, buf, sizeof(buf));
      if (len < 0) {
        TaskError(w, "t: create_a", "write");
        return kFail;
      }
      assert(len == sizeof(buf));
      EXPECT_EQ(close(fd), 0);
    }
    return kDone;
  }

  static int TaskCreateB(Worker* w) {
    // put a page of data into /b
    TaskDebugOp(w, "t: create_b");
    int fd = open(w->env.GetPath("b").c_str(), O_RDWR + O_CREAT, 0666);
    if (fd < 0) {
      // errno may be one of EEXIST
      if (errno != EEXIST) {
        TaskError(w, "t: create_b", "open");
        return kFail;
      }
    } else {
      char buf[kBlockSize];
      memset(buf, 0xba, sizeof(buf));
      ssize_t len = write(fd, buf, sizeof(buf));
      if (len < 0) {
        TaskError(w, "t: create_a", "write");
        return kFail;
      }
      assert(len == sizeof(buf));
      EXPECT_EQ(close(fd), 0);
    }
    return kDone;
  }

  static int TaskRenameAB(Worker* w) {
    // rename /a -> /b
    TaskDebugOp(w, "t: rename_ab");
    int rc = rename(w->env.GetPath("a").c_str(), w->env.GetPath("b").c_str());
    if (rc < 0) {
      // errno may be one of ENOENT
      if (errno != ENOENT) {
        TaskError(w, "t: rename_ab", "rename");
        return kFail;
      }
    }
    return kDone;
  }

  static int TaskRenameBA(Worker* w) {
    // rename ::/b -> ::/a
    TaskDebugOp(w, "t: rename_ba");
    int rc = rename(w->env.GetPath("b").c_str(), w->env.GetPath("a").c_str());
    if (rc < 0) {
      // errno may be one of ENOENT
      if (errno != ENOENT) {
        TaskError(w, "t: rename_ba", "rename");
        return kFail;
      }
    }
    return kDone;
  }

  static int TaskMakePrivateDir(Worker* w) {
    // mkdir ::/threadname
    TaskDebugOp(w, "t: make_private_dir");
    int rc = mkdir(w->env.GetPath(w->name).c_str(), 0755);
    if (rc < 0) {
      // errno may be one of EEXIST, ENOENT
      if (errno != ENOENT && errno != EEXIST) {
        TaskError(w, "t: make_private_dir", "mkdir");
        return kFail;
      }
    }
    return kDone;
  }

  static int TaskMoveAToPrivate(Worker* w) {
    // mv ::/a -> ::/threadname/a
    TaskDebugOp(w, "t: mv_a_to__private");
    int rc = rename(w->env.GetPath("a").c_str(), w->env.GetPath(w->name + "/a").c_str());
    if (rc < 0) {
      // errno may be one of EEXIST, ENOENT, ENOTDIR
      if (errno != EEXIST && errno != ENOENT && errno != ENOTDIR) {
        TaskError(w, "t: mv_a_to__private", "rename");
        return kFail;
      }
    }
    return kDone;
  }

  static int TaskWritePrivateB(Worker* w) {
    // put a page of data into /threadname/b
    TaskDebugOp(w, "t: write_private_b");
    int fd = open(w->env.GetPath(w->name + "/b").c_str(), O_RDWR + O_EXCL + O_CREAT, 0666);
    if (fd < 0) {
      // errno may be one of ENOENT, EISDIR, ENOTDIR, EEXIST
      if (errno != ENOENT && errno != EISDIR && errno != ENOTDIR && errno != EEXIST) {
        TaskError(w, "t: write_private_b", "open");
        return kFail;
      }
    } else {
      char buf[kBlockSize];
      memset(buf, 0xba, sizeof(buf));
      ssize_t len = write(fd, buf, sizeof(buf));
      if (len < 0) {
        TaskError(w, "t: write_private_b", "write");
        return kFail;
      }
      assert(len == sizeof(buf));
      EXPECT_EQ(close(fd), 0);
    }
    return kDone;
  }

  static int TaskRenamePrivateBA(Worker* w) {
    // move /threadname/b -> /a
    TaskDebugOp(w, "t: rename_private_ba");
    int rc = rename((w->env.GetPath(w->name) + "/b").c_str(), w->env.GetPath("a").c_str());
    if (rc < 0) {
      // errno may be one of EEXIST, ENOENT
      if (errno != EEXIST && errno != ENOENT) {
        TaskError(w, "t: rename_private_ba", "rename");
        return kFail;
      }
    }
    return kDone;
  }

  static int TaskRenamePrivateAB(Worker* w) {
    // move /threadname/a -> /b
    TaskDebugOp(w, "t: rename_private_ab");
    int rc = rename((w->env.GetPath(w->name) + "/a").c_str(), w->env.GetPath("b").c_str());
    if (rc < 0) {
      // errno may be one of EEXIST, ENOENT
      if (errno != EEXIST && errno != ENOENT) {
        TaskError(w, "t: rename_private_ab", "rename");
        return kFail;
      }
    }
    return kDone;
  }

  static int TaskOpenPrivateA(Worker* w) {
    // close(fd); fd <- open("/threadhame/a")
    TaskDebugOp(w, "t: open_private_a");
    const std::string file_name = w->env.GetPath(w->name) + "/a";
    w->fd = fbl::unique_fd(open(file_name.c_str(), O_RDWR + O_CREAT + O_EXCL, 0666));
    if (!w->fd) {
      if (errno == EEXIST) {
        w->fd = fbl::unique_fd(open(file_name.c_str(), O_RDWR));
        if (!w->fd) {
          TaskError(w, "t: open_private_a", "open-existing");
          return kFail;
        }
      } else {
        // errno may be one of EEXIST, ENOENT
        if (errno != ENOENT) {
          TaskError(w, "t: open_private_a", "open");
          return kFail;
        }
      }
    }
    return kDone;
  }

  static int TaskCloseFd(Worker* w) {
    w->fd.reset();
    return kDone;
  }

  static int TaskWriteFdBig(Worker* w) {
    // write(fd, big buffer, ...)
    TaskDebugOp(w, "t: write_fd_big");
    if (w->fd) {
      char buf[kBufferSize];
      memset(buf, 0xab, sizeof(buf));
      ssize_t len = write(w->fd.get(), buf, sizeof(buf));
      if (len < 0) {
        // errno may be one of ??
        TaskError(w, "t: write_fd_small", "write");
        return kFail;
      } else {
        assert(len == sizeof(buf));
        off_t off = lseek(w->fd.get(), 0, SEEK_CUR);
        assert(off >= 0);
        if (off >= w->size) {
          off = lseek(w->fd.get(), 0, SEEK_SET);
          assert(off == 0);
        }
      }
    }
    return kDone;
  }

  static int TaskWriteFdSmall(Worker* w) {
    // write(fd, small buffer, ...)
    TaskDebugOp(w, "t: write_fd_small");
    if (w->fd) {
      char buf[kBlockSize];
      memset(buf, 0xab, sizeof(buf));
      ssize_t len = write(w->fd.get(), buf, sizeof(buf));
      if (len < 0) {
        // errno may be one of ??
        TaskError(w, "t: write_fd_small", "write");
        return kFail;
      } else {
        assert(len == sizeof(buf));
        off_t off = lseek(w->fd.get(), 0, SEEK_CUR);
        assert(off >= 0);
        if (off >= w->size) {
          off = lseek(w->fd.get(), 0, SEEK_SET);
          assert(off == 0);
        }
      }
    }
    return kDone;
  }

  static int TaskTruncateFd(Worker* w) {
    // ftruncate(fd)
    TaskDebugOp(w, "t: truncate_fd");
    if (w->fd) {
      int rc = ftruncate(w->fd.get(), 0);
      if (rc < 0) {
        // errno may be one of ??
        TaskError(w, "t: truncate_fd", "truncate");
        return kFail;
      }
    }
    return kDone;
  }

  static int TaskUtimeFd(Worker* w) {
    // utime(fd)
    TaskDebugOp(w, "t: utime_fd");
    if (w->fd) {
      struct timespec ts[2] = {
          {.tv_nsec = UTIME_OMIT},  // no atime
          {.tv_nsec = UTIME_NOW},   // mtime == now
      };
      int rc = futimens(w->fd.get(), ts);
      if (rc < 0) {
        TaskError(w, "t: utime_fd", "futimens");
        return kFail;
      }
    }
    return kDone;
  }

  static int TaskSeekFdEnd(Worker* w) {
    TaskDebugOp(w, "t: seek_fd_end");
    if (w->fd) {
      off_t rc = lseek(w->fd.get(), 0, SEEK_END);
      if (rc < 0) {
        // errno may be one of ??
        TaskError(w, "t: seek_fd_end", "lseek");
        return kFail;
      }
    }
    return kDone;
  }

  static int TaskSeekFdStart(Worker* w) {
    // fseek(fd, SEEK_SET, 0)
    TaskDebugOp(w, "t: seek_fd_start");
    if (w->fd) {
      off_t rc = lseek(w->fd.get(), 0, SEEK_SET);
      if (rc < 0) {
        // errno may be one of ??
        TaskError(w, "t: seek_fd_start", "lseek");
        return kFail;
      }
    }
    return kDone;
  }

  static int TaskTruncateA(Worker* w) {
    // truncate("/a")
    int rc = truncate(w->env.GetPath("a").c_str(), 0);
    if (rc < 0) {
      // errno may be one of ENOENT
      if (errno != ENOENT) {
        TaskError(w, "t: truncate_a", "truncate");
        return kFail;
      }
    }
    return kDone;
  }

  // create a weighted list of operations for each thread
  void AddRandomOperations() {
    for (size_t i = 0; i < std::size(kOperations); ++i) {
      operations_.insert(operations_.end(), kOperations[i].weight, kOperations[i]);
    }
  }

  static int DoRandomOperations(void* arg) {
    constexpr int kNumSerialOperations = 4;  // yield every 1/n ops
    constexpr int kMaxOperations = 1000;

    Worker* w = static_cast<Worker*>(arg);
    RandomOpTest& env = w->env;

    // for some big number of operations
    // do an operation and yield, repeat
    for (int i = 0; i < kMaxOperations; i++) {
      int idx = rand_r(&w->seed) % env.operations().size();
      const RandomOp& op = env.operations()[idx];

      if (op.fn(w) != kDone) {
        ADD_FAILURE() << w->name << ": op " << op.name << " failed";
      }
      if (idx % kNumSerialOperations != 0)
        thrd_yield();
    }

    // Close the worker's personal fd (if it is open) and
    // unlink the worker directory.
    std::cout << "work thread(" << w->name << ") done" << std::endl;
    TaskCloseFd(w);
    unlink(w->env.GetPath(w->name).c_str());

    // currently, threads either return kDone or exit the test
    return kDone;
  }

  static constexpr RandomOp kOperations[] = {
      {"TaskCreateA", TaskCreateA, 1},
      {"TaskCreateB", TaskCreateB, 1},
      {"TaskRenameAB", TaskRenameAB, 4},
      {"TaskRenameBA", TaskRenameBA, 4},
      {"TaskMakePrivateDir", TaskMakePrivateDir, 4},
      {"TaskMoveAToPrivate", TaskMoveAToPrivate, 1},
      {"TaskWritePrivateB", TaskWritePrivateB, 1},
      {"TaskRenamePrivateBA", TaskRenamePrivateBA, 1},
      {"TaskRenamePrivateAB", TaskRenamePrivateAB, 1},
      {"TaskOpenPrivateA", TaskOpenPrivateA, 5},
      {"TaskCloseFd", TaskCloseFd, 2},
      {"TaskWriteFdBig", TaskWriteFdBig, 20},
      {"TaskWriteFdSmall", TaskWriteFdSmall, 20},
      {"TaskTruncateFd", TaskTruncateFd, 2},
      {"TaskUtimeFd", TaskUtimeFd, 2},
      {"TaskSeekFd", TaskSeekFdStart, 2},
      {"TaskSeekFdEnd", TaskSeekFdEnd, 2},
      {"TaskTruncateA", TaskTruncateA, 1},
  };

  std::vector<std::unique_ptr<Worker>> all_workers_;
  std::vector<RandomOp> operations_;
  std::vector<thrd_t> threads_;
  bool debug_ = false;
};

TEST_P(RandomOpTest, MultiThreaded) {
  for (auto& w : all_workers_) {
    // start the workers on separate threads
    thrd_t t;
    ASSERT_EQ(thrd_create(&t, DoRandomOperations, w.get()), thrd_success);
    threads_.push_back(t);
  }

  for (thrd_t t : threads_) {
    int rc;
    ASSERT_EQ(thrd_join(t, &rc), thrd_success);
    ASSERT_EQ(rc, kDone) << "Background thread joined, but failed";
  }
}

INSTANTIATE_TEST_SUITE_P(/*no prefix*/, RandomOpTest, testing::ValuesIn(AllTestFilesystems()),
                         testing::PrintToStringParamName());

}  // namespace
}  // namespace fs_test
