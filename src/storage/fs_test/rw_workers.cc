// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <threads.h>
#include <unistd.h>

#include <algorithm>
#include <iostream>
#include <random>
#include <string>
#include <vector>

#include <fbl/unique_fd.h>

#include "src/storage/fs_test/fs_test_fixture.h"

namespace fs_test {
namespace {

enum Status { kFail = -1, kBusy = 0, kDone = 1 };

constexpr int kBufSize = 65536;

static_assert(kBufSize == kBufSize / sizeof(uint64_t) * sizeof(uint64_t),
              "kBufSize not multiple of uint64_t");

class Worker;

class RwWorkersTest : public FilesystemTest {
 public:
  RwWorkersTest();

 protected:
  Status DoWork();

  std::vector<Worker> workers_;
  std::vector<thrd_t> threads_;
};

class Worker {
 public:
  using WorkFn = Status (Worker::*)(void);

  Worker(const std::string& where, const char* fn, WorkFn work, uint32_t size, uint32_t flags);

  Status status() const { return status_; }
  const std::string name() const { return name_; }

  Status Work() {
    status_ = (this->*work_)();
    return status_;
  }

  Status Rw(bool do_read);
  Status Writer();

 private:
  Status Verify();

  WorkFn work_;
  const std::default_random_engine::result_type seed_;
  std::default_random_engine data_random_;
  std::default_random_engine io_size_random_;

  fbl::unique_fd fd_;
  Status status_ = Status::kBusy;
  const uint32_t size_;
  const uint32_t flags_;
  uint32_t pos_ = 0;

  union {
    uint8_t u8[kBufSize];
    uint64_t u64[kBufSize / sizeof(uint64_t)];
  } buffer_;

  const std::string name_;
};

constexpr uint32_t kUseRandomIoSize = 1;

Status Worker::Rw(bool do_read) {
  if (pos_ == size_) {
    return kDone;
  }

  // offset into buffer
  uint32_t off = pos_ % kBufSize;

  // fill our content buffer if it's empty
  if (off == 0) {
    std::uniform_int_distribution<uint64_t> dist;
    for (unsigned n = 0; n < (kBufSize / sizeof(uint64_t)); n++) {
      buffer_.u64[n] = dist(data_random_);
    }
  }

  // data in buffer available to write
  uint32_t xfer = kBufSize - off;

  // do not exceed our desired size
  if (xfer > (size_ - pos_)) {
    xfer = size_ - pos_;
  }

  if ((flags_ & kUseRandomIoSize) && (xfer > 3000)) {
    xfer = 3000 + (std::uniform_int_distribution<uint32_t>()(io_size_random_) % (xfer - 3000));
  }

  ssize_t r;
  if (do_read) {
    uint8_t buffer[kBufSize];
    if ((r = read(fd_.get(), buffer, xfer)) < 0) {
      std::cerr << "worker('" << name_ << "') read failed @" << pos_ << ": " << strerror(errno)
                << std::endl;
      return kFail;
    }
    if (memcmp(buffer, buffer_.u8 + off, r)) {
      std::cerr << "worker('" << name_ << ") verify failed @" << pos_ << std::endl;
      return kFail;
    }
  } else {
    if ((r = write(fd_.get(), buffer_.u8 + off, xfer)) < 0) {
      std::cerr << "worker('" << name_ << "') write failed @" << pos_ << ": " << strerror(errno)
                << std::endl;
      return kFail;
    }
  }

  // advance
  pos_ += r;
  return kBusy;
}

Status Worker::Verify() { return Rw(true); }

Status Worker::Writer() {
  Status r = Rw(false);
  if (r == kDone) {
    if (lseek(fd_.get(), 0, SEEK_SET) != 0) {
      std::cerr << "worker('" << name_ << "') seek failed: " << strerror(errno) << std::endl;
      return kFail;
    }
    // Reset data_random_.
    data_random_.seed(seed_);
    pos_ = 0;
    work_ = &Worker::Verify;
    return kBusy;
  }
  return r;
}

Worker::Worker(const std::string& where, const char* fn, WorkFn work, uint32_t size, uint32_t flags)
    : work_(work),
      seed_(std::random_device()()),
      data_random_(seed_),
      io_size_random_(seed_),
      size_(size),
      flags_(flags),
      name_(where + fn) {
  fd_ = fbl::unique_fd(open(name_.c_str(), O_RDWR | O_CREAT | O_EXCL, 0644));
  EXPECT_TRUE(fd_);
}

Status RwWorkersTest::DoWork() {
  int busy_count = 0;
  for (Worker& w : workers_) {
    if (w.status() == kBusy) {
      busy_count++;
      if (w.Work() == kFail) {
        EXPECT_EQ(unlink(w.name().c_str()), 0);
        return kFail;
      }
      if (w.status() == kDone) {
        std::cerr << "worker('" << w.name() << "') finished" << std::endl;
        EXPECT_EQ(unlink(w.name().c_str()), 0);
      }
    }
  }
  return busy_count ? kBusy : kDone;
}

TEST_P(RwWorkersTest, SingleThread) {
  Status r;
  do {
    r = DoWork();
    ASSERT_NE(r, kFail);
  } while (r != kDone);
}

constexpr uint32_t KiB(int n) { return n * 1024; }
constexpr uint32_t MiB(int n) { return n * 1024; }

constexpr struct {
  const char* name;
  uint32_t size;
  uint32_t flags;
} kWork[] = {
    {"file0000", KiB(512), kUseRandomIoSize},
    {"file0001", MiB(10), kUseRandomIoSize},
    {"file0002", KiB(512), kUseRandomIoSize},
    {"file0003", KiB(512), kUseRandomIoSize},
    {"file0004", KiB(512), 0},
    {"file0005", MiB(20), 0},
    {"file0006", KiB(512), 0},
    {"file0007", KiB(512), 0},
};

RwWorkersTest::RwWorkersTest() {
  // Assemble the work.
  for (const auto& work : kWork) {
    workers_.push_back(
        Worker(fs().mount_path(), work.name, &Worker::Writer, work.size, work.flags));
  }
}

int DoThreadedWork(void* arg) {
  Worker* w = static_cast<Worker*>(arg);

  std::cerr << "work thread(" << w->name() << ") started" << std::endl;
  while (w->Work() == kBusy) {
    thrd_yield();
  }

  std::cerr << "work thread(" << w->name() << ") " << (w->status() == kDone ? "finished" : "failed")
            << std::endl;
  EXPECT_EQ(unlink(w->name().c_str()), 0);

  return w->status();
}

TEST_P(RwWorkersTest, Concurrent) {
  std::vector<thrd_t> threads;

  for (Worker& w : workers_) {
    // start the workers on separate threads
    thrd_t t;
    ASSERT_EQ(thrd_create(&t, DoThreadedWork, &w), thrd_success);
    threads.push_back(t);
  }

  for (thrd_t t : threads) {
    int rc;
    ASSERT_EQ(thrd_join(t, &rc), thrd_success);
    ASSERT_EQ(rc, kDone) << "Thread joined, but failed";
  }
}

INSTANTIATE_TEST_SUITE_P(/*no prefix*/, RwWorkersTest, testing::ValuesIn(AllTestFilesystems()),
                         testing::PrintToStringParamName());

}  // namespace
}  // namespace fs_test
