// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <errno.h>

#include <iostream>
#include <iterator>
#include <memory>
#include <random>

#include "src/storage/host_fs_test/fixture.h"

namespace fs_test {
namespace {

constexpr int kFail = -1;
constexpr int kBusy = 0;
constexpr int kDone = 1;
constexpr int kBufSize = 65536;

static_assert(kBufSize == ((kBufSize / sizeof(uint64_t)) * sizeof(uint64_t)),
              "kBufSize not multiple of uint64_t");

struct Worker;

using WorkerFn = int (*)(Worker* w, bool* fsck_needed);

struct Worker {
  WorkerFn work;

  std::default_random_engine rdata;
  std::default_random_engine rops;
  std::default_random_engine::result_type rdata_seed;

  int fd;
  int status;
  uint32_t flags;
  uint32_t size;
  uint32_t pos;

  union {
    uint8_t u8[kBufSize];
    uint64_t u64[kBufSize / sizeof(uint64_t)];
  };

  char name[256];
};

class RwWorkersTest : public HostFilesystemTest {
 protected:
  void NewWorker(const char* where, const char* fn, WorkerFn, uint32_t size, uint32_t flags);
  int DoWork();

  std::vector<std::unique_ptr<Worker>> all_workers_;
};

int WorkerWriter(Worker* w, bool* fsck_needed);

constexpr int kRandomIoSize = 1;
constexpr int KiB(int n) { return n * 1024; }
constexpr int MiB(int n) { return n * 1024 * 1024; }

struct {
  WorkerFn work;
  const char* name;
  uint32_t size;
  uint32_t flags;
} kWork[] = {
    {
        WorkerWriter,
        "file0000",
        KiB(512),
        kRandomIoSize,
    },
    {
        WorkerWriter,
        "file0001",
        MiB(10),
        kRandomIoSize,
    },
    {
        WorkerWriter,
        "file0002",
        KiB(512),
        kRandomIoSize,
    },
    {
        WorkerWriter,
        "file0003",
        KiB(512),
        kRandomIoSize,
    },
    {
        WorkerWriter,
        "file0004",
        KiB(512),
        0,
    },
    {
        WorkerWriter,
        "file0005",
        MiB(20),
        0,
    },
    {
        WorkerWriter,
        "file0006",
        KiB(512),
        0,
    },
    {
        WorkerWriter,
        "file0007",
        KiB(512),
        0,
    },
};

int WorkerRw(Worker* w, bool do_read) {
  if (w->pos == w->size) {
    return kDone;
  }

  // offset into buffer
  uint32_t off = w->pos % kBufSize;

  // fill our content buffer if it's empty
  if (off == 0) {
    std::uniform_int_distribution<uint64_t> distribution;
    for (unsigned n = 0; n < kBufSize / sizeof(uint64_t); ++n) {
      w->u64[n] = distribution(w->rdata);
    }
  }

  // data in buffer available to write
  uint32_t xfer = kBufSize - off;

  // do not exceed our desired size
  if (xfer > (w->size - w->pos)) {
    xfer = w->size - w->pos;
  }

  std::uniform_int_distribution<uint32_t> distribution;
  if ((w->flags & kRandomIoSize) && (xfer > 3000)) {
    xfer = 3000 + distribution(w->rops) % (xfer - 3000);
  }

  ssize_t r;
  if (do_read) {
    uint8_t buffer[kBufSize];
    if ((r = emu_read(w->fd, buffer, xfer)) < 0) {
      std::cerr << "worker('" << w->name << "') read failed @" << w->pos << ": " << strerror(errno)
                << std::endl;
      return kFail;
    }

    if (memcmp(buffer, w->u8 + off, r) != 0) {
      std::cerr << "worker('" << w->name << ") verify failed @" << w->pos << std::endl;
      return kFail;
    }
  } else {
    if ((r = emu_write(w->fd, w->u8 + off, xfer)) < 0) {
      std::cerr << "worker('" << w->name << "') write failed @" << w->pos << ": " << strerror(errno)
                << std::endl;
      return kFail;
    }
  }

  // advance
  w->pos += r;
  return kBusy;
}

int WorkerVerify(Worker* w, bool* fsck_needed) {
  int r = WorkerRw(w, true);
  if (r == kDone) {
    emu_close(w->fd);
    *fsck_needed = true;
  }
  return r;
}

int WorkerWriter(Worker* w, bool* fsck_needed) {
  int r = WorkerRw(w, false);
  *fsck_needed = true;
  if (r == kDone) {
    if (emu_lseek(w->fd, 0, SEEK_SET) != 0) {
      std::cerr << "worker('" << w->name << "') seek failed: " << strerror(errno) << std::endl;
      return kFail;
    }
    // start at 0 and reset our data generator seed
    w->rdata.seed(w->rdata_seed);
    w->pos = 0;
    w->work = WorkerVerify;
    return kBusy;
  }
  return r;
}

void RwWorkersTest::NewWorker(const char* where, const char* fn, WorkerFn work,
                              uint32_t size, uint32_t flags) {
  auto w = std::make_unique<Worker>();

  snprintf(w->name, sizeof(w->name), "%s%s", where, fn);
  std::random_device random_device;
  w->rdata.seed(w->rdata_seed = random_device());
  w->rops.seed(random_device());
  w->size = size;
  w->work = work;
  w->flags = flags;

  if ((w->fd = emu_open(w->name, O_RDWR | O_CREAT | O_EXCL, 0644)) < 0) {
    std::cerr << "worker('" << w->name << "') cannot create file" << std::endl;
    return;
  }

  all_workers_.push_back(std::move(w));
}

int RwWorkersTest::DoWork() {
  uint32_t busy_count = 0;
  bool fsck_needed = false;
  for (const auto& w : all_workers_) {
    if (w->status == kBusy) {
      ++busy_count;
      if ((w->status = w->work(w.get(), &fsck_needed)) == kFail) {
        return kFail;
      }
      if (w->status == kDone) {
        std::cerr << "worker('" << w->name << "') finished" << std::endl;
      }
    }
  }
  if (fsck_needed) {
    EXPECT_EQ(RunFsck(), 0);
  }
  return busy_count ? kBusy : kDone;
}

TEST_F(RwWorkersTest, SingleThread) {
  const char* where = "::";
  for (const auto& work : kWork) {
    NewWorker(where, work.name, work.work, work.size, work.flags);
  }

  for (;;) {
    int r = DoWork();
    ASSERT_NE(r, kFail);
    if (r == kDone) {
      break;
    }
  }
}

}  // namespace
}  // namespace fs_test
