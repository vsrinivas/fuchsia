// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <inttypes.h>
#include <lib/zx/bti.h>
#include <lib/zx/channel.h>
#include <lib/zx/clock.h>
#include <lib/zx/exception.h>
#include <lib/zx/iommu.h>
#include <lib/zx/pager.h>
#include <lib/zx/port.h>
#include <lib/zx/thread.h>
#include <lib/zx/vmar.h>
#include <lib/zx/vmo.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>
#include <unistd.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/debug.h>
#include <zircon/syscalls/exception.h>
#include <zircon/syscalls/iommu.h>
#include <zircon/syscalls/port.h>
#include <zircon/threads.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <iterator>
#include <memory>
#include <optional>
#include <shared_mutex>
#include <thread>

#include <fbl/algorithm.h>
#include <fbl/array.h>
#include <fbl/auto_call.h>
#include <fbl/auto_lock.h>
#include <fbl/mutex.h>
#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>
#include <fbl/span.h>

#include "stress_test.h"

// Helper to generate values in the full inclusive range [a,b].
template <typename IntType = uint64_t>
static inline IntType uniform_rand_range(IntType a, IntType b, StressTest::Rng& rng) {
  return std::uniform_int_distribution<IntType>(a, b)(rng);
}

// Helper to generate the common [0, max(1,a)). That is, if a range of size 0 is returned, this is
// considered valid and always generates the result 0.
template <typename IntType = uint64_t>
static inline IntType uniform_rand(IntType range, StressTest::Rng& rng) {
  if (range == static_cast<IntType>(0)) {
    return range;
  }
  return uniform_rand_range<IntType>(static_cast<IntType>(0), range - 1, rng);
}

class VmStressTest;

// VM Stresser
//
// The current stress test runs multiple independent test instance which get randomly
// initialized and torn down over time. Each creates a single pager vmo and hands it to a
// pool of worker threads. Some of the worker threads randomly commit/decommit/read/write/map/unmap
// the vmo. The rest of the worker threads randomly service pager requests or randomly supply
// their own 'prefetch' pages. This is intended to pick out any internal races with the
// VMO/VMAR/Pager system.
//
// Currently does not validate that any given operation was successfully performed, only
// that the apis do not return an error (or crash).
//
// Will evolve over time to use cloned vmos.

class VmStressTest : public StressTest {
 public:
  VmStressTest() = default;
  virtual ~VmStressTest() = default;

  virtual zx_status_t Start();
  virtual zx_status_t Stop();

  virtual const char* name() const { return "VM Stress"; }

  zx::unowned_resource RootResource() { return zx::unowned_resource{root_resource_}; }

 private:
  int test_thread();

  std::atomic<bool> shutdown_{false};

  thrd_t test_thread_;
} vmstress;

class TestInstance {
 public:
  TestInstance(VmStressTest* test) : test_(test) {}
  virtual ~TestInstance() {}

  virtual zx_status_t Start() = 0;
  virtual zx_status_t Stop() = 0;

  zx::unowned_resource RootResource() { return test_->RootResource(); }

 protected:
  // TODO: scale based on the number of cores in the system and/or command line arg
  static constexpr uint64_t kNumThreads = 6;

  template <typename... Args>
  void Printf(const char* str, Args... args) const {
    test_->Printf(str, args...);
  }
  template <typename... Args>
  void PrintfAlways(const char* str, Args... args) const {
    test_->PrintfAlways(str, args...);
  }

  std::mt19937_64 RngGen() { return test_->RngGen(); }

  VmStressTest* const test_;
};

class SingleVmoTestInstance : public TestInstance {
 public:
  SingleVmoTestInstance(VmStressTest* test, bool use_pager, uint64_t vmo_size)
      : TestInstance(test), use_pager_(use_pager), vmo_size_(vmo_size) {}

  zx_status_t Start() final;
  zx_status_t Stop() final;

 private:
  int vmo_thread();
  int pager_thread();

  void CheckVmoThreadError(zx_status_t status, const char* error);

  const bool use_pager_;
  const uint64_t vmo_size_;

  static constexpr uint64_t kNumVmoThreads = 3;
  thrd_t threads_[kNumThreads];
  zx::thread thread_handles_[kNumThreads];

  // Array used for storing vmo mappings. All mappings are cleaned up by the test thread,
  // as vmo threads will sometimes crash if the instance is torn down during a page fault.
  std::atomic<uint32_t> vmo_thread_idx_{0};
  uintptr_t ptrs_[kNumThreads] = {};
  fbl::Array<uint8_t> bufs_[kNumThreads] = {};

  // Vector of page requests shared by all pager threads of the instance, to allow
  // requests to be serviced out-of-order.
  fbl::Mutex mtx_;
  fbl::Vector<zx_packet_page_request_t> requests_;

  // Flag used to signal shutdown to worker threads.
  std::atomic<bool> shutdown_{false};

  // Counter that allows the last pager thread to clean up the pager itself.
  std::atomic<uint32_t> pager_thread_count_{kNumThreads - kNumVmoThreads};

  zx::vmo vmo_{};
  zx::pager pager_;
  zx::port port_;
};

int SingleVmoTestInstance::vmo_thread() {
  zx_status_t status;

  uint64_t idx = vmo_thread_idx_++;

  // allocate a local buffer
  const size_t buf_size = PAGE_SIZE * 16;
  bufs_[idx] = fbl::Array<uint8_t>(new uint8_t[buf_size], buf_size);
  const fbl::Array<uint8_t>& buf = bufs_[idx];

  auto rng = RngGen();

  // local helper routines to calculate a random range within a vmo and
  // a range appropriate to read into the local buffer above
  auto rand_vmo_range = [this, &rng](uint64_t* out_offset, uint64_t* out_size) {
    *out_offset = uniform_rand(vmo_size_, rng);
    *out_size = std::min(uniform_rand(vmo_size_, rng), vmo_size_ - *out_offset);
  };
  auto rand_buffer_range = [this, &rng](uint64_t* out_offset, uint64_t* out_size) {
    *out_size = uniform_rand(buf_size, rng);
    *out_offset = uniform_rand(vmo_size_ - *out_size, rng);
  };

  ZX_ASSERT(buf_size < vmo_size_);

  while (!shutdown_.load()) {
    uint64_t off, len;

    int r = uniform_rand(100, rng);
    switch (r) {
      case 0 ... 4:  // commit a range of the vmo
        Printf("c");
        rand_vmo_range(&off, &len);
        status = vmo_.op_range(ZX_VMO_OP_COMMIT, off, len, nullptr, 0);
        CheckVmoThreadError(status, "Failed to commit range");
        break;
      case 5 ... 19:
        if (ptrs_[idx]) {
          // unmap the vmo if it already was
          Printf("u");
          status = zx::vmar::root_self()->unmap(ptrs_[idx], vmo_size_);
          CheckVmoThreadError(status, "failed to unmap range");
          ptrs_[idx] = 0;
        }
        // map it somewhere
        Printf("m");
        status = zx::vmar::root_self()->map(0, vmo_, 0, vmo_size_,
                                            ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, ptrs_ + idx);
        CheckVmoThreadError(status, "failed to map range");
        break;
      case 20 ... 34:
        // read from a random range of the vmo
        Printf("r");
        rand_buffer_range(&off, &len);
        status = vmo_.read(buf.data(), off, len);
        CheckVmoThreadError(status, "error reading from vmo");
        break;
      case 35 ... 49:
        // write to a random range of the vmo
        Printf("w");
        rand_buffer_range(&off, &len);
        status = vmo_.write(buf.data(), off, len);
        CheckVmoThreadError(status, "error writing to vmo");
        break;
      case 50 ... 74:
        // read from a random range of the vmo via a direct memory reference
        if (ptrs_[idx]) {
          Printf("R");
          rand_buffer_range(&off, &len);
          memcpy(buf.data(), reinterpret_cast<const void*>(ptrs_[idx] + off), len);
        }
        break;
      case 75 ... 99:
        // write to a random range of the vmo via a direct memory reference
        if (ptrs_[idx]) {
          Printf("W");
          rand_buffer_range(&off, &len);
          memcpy(reinterpret_cast<void*>(ptrs_[idx] + off), buf.data(), len);
        }
        break;
    }

    fflush(stdout);
  }

  return 0;
}

void SingleVmoTestInstance::CheckVmoThreadError(zx_status_t status, const char* error) {
  // Ignore errors while shutting down, since they're almost certainly due to the
  // pager disappearing.
  if (!shutdown_ && status != ZX_OK) {
    fprintf(stderr, "%s, error %d\n", error, status);
  }
}

static bool is_thread_blocked(zx_handle_t handle) {
  zx_info_thread_t info;
  uint64_t actual, actual_count;
  ZX_ASSERT(zx_object_get_info(handle, ZX_INFO_THREAD, &info, sizeof(info), &actual,
                               &actual_count) == ZX_OK);
  return info.state == ZX_THREAD_STATE_BLOCKED_PAGER;
}

int SingleVmoTestInstance::pager_thread() {
  zx_status_t status;

  uint64_t vmo_page_count = vmo_size_ / ZX_PAGE_SIZE;
  ZX_ASSERT(vmo_page_count > 0);

  auto supply_pages = [this](uint64_t off, uint64_t len) {
    zx::vmo tmp_vmo;
    zx_status_t status = zx::vmo::create(len, 0, &tmp_vmo);
    if (status != ZX_OK) {
      fprintf(stderr, "failed to create tmp vmo, error %d (%s)\n", status,
              zx_status_get_string(status));
      return;
    }
    status = tmp_vmo.op_range(ZX_VMO_OP_COMMIT, 0, len, nullptr, 0);
    if (status != ZX_OK) {
      fprintf(stderr, "failed to commit tmp vmo, error %d (%s)\n", status,
              zx_status_get_string(status));
      return;
    }
    status = pager_.supply_pages(vmo_, off, len, tmp_vmo, 0);
    if (status != ZX_OK) {
      fprintf(stderr, "failed to supply pages %d, error %d (%s)\n", pager_.get(), status,
              zx_status_get_string(status));
      return;
    }
  };

  auto rng = RngGen();

  while (!shutdown_.load()) {
    zx::vmo tmp_vmo;
    uint64_t off, size;
    zx::time deadline;

    int r = uniform_rand<int>(100, rng);
    switch (r) {
      case 0 ... 4:  // supply a random range of pages
      {
        off = uniform_rand(vmo_page_count, rng);
        size = std::min(uniform_rand(vmo_page_count, rng), vmo_page_count - off);
        supply_pages(off * PAGE_SIZE, size * PAGE_SIZE);
        break;
      }
      case 5 ... 54:  // read from the port
      {
        fbl::AutoLock lock(&mtx_);
        if (requests_.size() == kNumVmoThreads) {
          break;
        } else {
          // We still need to at least query the port if all vmo threads are
          // blocked, in case we need to read the last thread's packet.
          deadline = zx::time::infinite_past();
          for (unsigned i = 0; i < kNumVmoThreads; i++) {
            if (!is_thread_blocked(thread_handles_[i].get())) {
              deadline = zx::clock::get_monotonic() + zx::msec(10);
              break;
            }
          }
        }
      }

        zx_port_packet_t packet;
        status = port_.wait(deadline, &packet);
        if (status != ZX_OK) {
          if (status != ZX_ERR_TIMED_OUT) {
            fprintf(stderr, "failed to read port, error %d (%s)\n", status,
                    zx_status_get_string(status));
          }
        } else if (packet.type != ZX_PKT_TYPE_PAGE_REQUEST ||
                   packet.page_request.command != ZX_PAGER_VMO_READ) {
          fprintf(stderr, "unexpected packet, error %d %d\n", packet.type,
                  packet.page_request.command);
        } else {
          fbl::AutoLock lock(&mtx_);
          requests_.push_back(packet.page_request);
        }
        break;
      case 55 ... 99:  // fullfil a random request
        fbl::AutoLock lock(&mtx_);
        if (requests_.is_empty()) {
          break;
        }
        off = uniform_rand(requests_.size(), rng);
        zx_packet_page_request_t req = requests_.erase(off);
        lock.release();

        supply_pages(req.offset, req.length);
        break;
    }

    fflush(stdout);
  }

  // Have the last pager thread tear down the pager. Randomly either detach the vmo (and
  // close the pager after all test threads are done) or immediately close the pager handle.
  if (--pager_thread_count_ == 0) {
    if (uniform_rand(2, rng)) {
      pager_.detach_vmo(vmo_);
    } else {
      pager_.reset();
    }
  }

  return 0;
}

zx_status_t SingleVmoTestInstance::Start() {
  auto status = zx::port::create(0, &port_);
  if (status != ZX_OK) {
    return status;
  }

  if (use_pager_) {
    status = zx::pager::create(0, &pager_);
    if (status != ZX_OK) {
      return status;
    }

    // create a test vmo
    status = pager_.create_vmo(0, port_, 0, vmo_size_, &vmo_);
  } else {
    status = zx::vmo::create(vmo_size_, 0, &vmo_);
  }

  if (status != ZX_OK) {
    return status;
  }

  // create a pile of threads
  auto worker = [](void* arg) -> int {
    return static_cast<SingleVmoTestInstance*>(arg)->vmo_thread();
  };
  auto pager_worker = [](void* arg) -> int {
    return static_cast<SingleVmoTestInstance*>(arg)->pager_thread();
  };

  for (uint32_t i = 0; i < std::size(threads_); i++) {
    // vmo threads need to come first, since the pager workers need to reference
    // the vmo worker thread handles.
    bool is_vmo_worker = i < kNumVmoThreads || !use_pager_;
    thrd_create_with_name(threads_ + i, is_vmo_worker ? worker : pager_worker, this,
                          is_vmo_worker ? "vmstress_worker" : "pager_worker");

    zx::unowned_thread unowned(thrd_get_zx_handle(threads_[i]));
    ZX_ASSERT(unowned->duplicate(ZX_RIGHT_SAME_RIGHTS, thread_handles_ + i) == ZX_OK);
  }
  return ZX_OK;
}

zx_status_t SingleVmoTestInstance::Stop() {
  zx::port port;
  zx::port::create(0, &port);
  std::array<zx::channel, kNumVmoThreads> channels;

  if (use_pager_) {
    // We need to handle potential crashes in the vmo threads when the pager is torn down. Since
    // not all threads will actually crash, we can't stop handling crashes until all threads
    // have terminated.
    // TODO: Note that these crashes may produce visible output on the system logs and this
    // shutdown should maybe be restructured to avoid this from happening.
    for (unsigned i = 0; i < kNumVmoThreads; i++) {
      zx_status_t status = thread_handles_[i].create_exception_channel(0, &channels[i]);
      ZX_ASSERT(status == ZX_OK);
      status = channels[i].wait_async(port, i, ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED, 0);
      ZX_ASSERT(status == ZX_OK);
    }
  }

  shutdown_.store(true);

  if (use_pager_) {
    uint64_t running_count = kNumVmoThreads;
    while (running_count) {
      zx_port_packet_t packet;
      ZX_ASSERT(port.wait(zx::time::infinite(), &packet) == ZX_OK);

      if (packet.signal.observed & ZX_CHANNEL_READABLE) {
        const zx::channel& channel = channels[packet.key];

        zx_exception_info_t exception_info;
        zx::exception exception;
        ZX_ASSERT(channel.read(0, &exception_info, exception.reset_and_get_address(),
                               sizeof(exception_info), 1, nullptr, nullptr) == ZX_OK);

        zx::thread& thrd = thread_handles_[packet.key];

        zx_exception_report_t report;
        ZX_ASSERT(thrd.get_info(ZX_INFO_THREAD_EXCEPTION_REPORT, &report, sizeof(report), NULL,
                                NULL) == ZX_OK);
        ZX_ASSERT(report.header.type == ZX_EXCP_FATAL_PAGE_FAULT);

        // thrd_exit takes a parameter, but we don't actually read it when we join
        zx_thread_state_general_regs_t regs;
        ZX_ASSERT(thrd.read_state(ZX_THREAD_STATE_GENERAL_REGS, &regs, sizeof(regs)) == ZX_OK);
#if defined(__x86_64__)
        regs.rip = reinterpret_cast<uintptr_t>(thrd_exit);
#else
        regs.pc = reinterpret_cast<uintptr_t>(thrd_exit);
#endif
        ZX_ASSERT(thrd.write_state(ZX_THREAD_STATE_GENERAL_REGS, &regs, sizeof(regs)) == ZX_OK);

        uint32_t exception_state = ZX_EXCEPTION_STATE_HANDLED;
        ZX_ASSERT(exception.set_property(ZX_PROP_EXCEPTION_STATE, &exception_state,
                                         sizeof(exception_state)) == ZX_OK);

        ZX_ASSERT(channel.wait_async(port, packet.key, ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED,
                                     0) == ZX_OK);
      } else {
        running_count--;
      }
    }
  }

  for (unsigned i = 0; i < std::size(threads_); i++) {
    thrd_join(threads_[i], nullptr);
  }

  for (unsigned i = 0; i < (use_pager_ ? kNumVmoThreads : kNumThreads); i++) {
    if (ptrs_[i]) {
      zx::vmar::root_self()->unmap(ptrs_[i], vmo_size_);
    }
  }

  return ZX_OK;
}

// This test case randomly creates vmos and COW clones, randomly writes into the vmos,
// and performs basic COW integrity checks.
//
// Each created vmo has a 32-bit id. These ids are monotonically increasing. Each vmo has
// its own 32-bit op-id, which is incremented on each write operation. These two 32-bit ids
// are combined into a single 64-bit id which uniquely identifies every write operation. The
// test uses these 64-bit ids to perform various COW integrity checks which are documented
// in more detail within the test implementation.
//
// This test generally does not handle id overflow due to the fact that the random teardown
// of various parts of the test makes the chance of overflow vanishingly small.
class CowCloneTestInstance : public TestInstance {
 public:
  CowCloneTestInstance(VmStressTest* test) : TestInstance(test) {}

  zx_status_t Start() final;
  zx_status_t Stop() final;

 private:
  int op_thread();

  // Aggregate for holding the test info associated with a single vmo.
  struct TestData : public fbl::RefCounted<struct test_data> {
    TestData(uint32_t id, uint32_t data_idx, zx::vmo test_vmo, uint32_t pc, uint32_t offset_page,
             uintptr_t mapped_ptr, fbl::RefPtr<struct TestData> p, uint32_t clone_start_op,
             uint32_t clone_end_op)
        : vmo_id(id),
          idx(data_idx),
          vmo(std::move(test_vmo)),
          page_count(pc),
          offset_page_idx(offset_page),
          ptr(mapped_ptr),
          parent(std::move(p)),
          parent_clone_start_op_id(clone_start_op),
          parent_clone_end_op_id(clone_end_op) {}

    // An identifer for the vmo.
    const uint32_t vmo_id;
    // The index of the test data in the test_datas_ array.
    const uint32_t idx;
    // The vmo under test.
    zx::vmo vmo;
    // The number of pages in the vmo.
    const uint32_t page_count;
    // The page offset into the parent where this vmo starts. This has no
    // meaning if this vmo has no parent.
    const uint32_t offset_page_idx;
    // The pointer to the vmo mapping.
    const uintptr_t ptr;

    // A pointer to the TestData struct which holds the parent of |vmo|, or
    // null if |vmo| has no parent.
    //
    // Note that this reference does not keep the parent->vmo handle from being closed.
    const fbl::RefPtr<struct TestData> parent;

    // The id of the parent TestData at the beginning and end of the clone operation
    // which created this vmo. Used for integrity checks.
    const uint32_t parent_clone_start_op_id;
    const uint32_t parent_clone_end_op_id;

    // This can technically overflow, but the chance of a VMO living
    // long enough for that to happen is astronomically low.
    std::atomic<uint32_t> next_op_id = 1;
  };

  // Debug function for printing information associated with a particular access operation.
  void DumpTestVmoAccessInfo(const fbl::RefPtr<TestData>& vmo, uint32_t page_index, uint64_t val);

  static constexpr uint64_t kMaxTestVmos = 32;
  static constexpr uint64_t kMaxVmoPageCount = 128;
  struct {
    fbl::RefPtr<struct TestData> vmo;

    // Shared mutex which protects |vmo|. The mutex is taken in exclusive mode
    // when creating/destroying the |vmo| in this slot. It is taken in shared
    // mode when writing or when creating a clone based on this slot.
    std::shared_mutex mtx;
  } test_datas_[kMaxTestVmos];

  // Helper function that creates a new test vmo that will be inserted at |idx| in test_datas_.
  fbl::RefPtr<TestData> CreateTestVmo(uint32_t idx, StressTest::Rng& rng);
  // Helper function that performs a write operation on |TestData|, which is currently
  // in |idx| in test_datas_.
  bool TestVmoWrite(uint32_t idx, const fbl::RefPtr<TestData>& TestData, StressTest::Rng& rng);

  thrd_t threads_[kNumThreads] = {};
  std::atomic<bool> shutdown_{false};

  // Id counter for vmos.
  std::atomic<uint32_t> next_vmo_id_{1};
  // Limit for next_vmo_id_ to prevent overflow concerns.
  static constexpr uint32_t kMaxVmoId = UINT32_MAX - kNumThreads;

  static uint32_t get_op_id(uint64_t full_id) { return static_cast<uint32_t>(full_id >> 32); }
  static uint32_t get_vmo_id(uint64_t full_id) { return full_id & 0xffffffff; }
  static uint64_t make_full_id(uint32_t vmo_id, uint32_t op_id) {
    return vmo_id | (static_cast<uint64_t>(op_id) << 32);
  }
};

zx_status_t CowCloneTestInstance::Start() {
  for (unsigned i = 0; i < kNumThreads; i++) {
    auto fn = [](void* arg) -> int { return static_cast<CowCloneTestInstance*>(arg)->op_thread(); };
    thrd_create_with_name(threads_ + i, fn, this, "op_worker");
  }
  return ZX_OK;
}

zx_status_t CowCloneTestInstance::Stop() {
  shutdown_.store(true);

  bool success = true;
  for (unsigned i = 0; i < kNumThreads; i++) {
    int32_t res;
    thrd_join(threads_[i], &res);
    success &= (res == 0);
  }

  for (unsigned i = 0; i < kMaxTestVmos; i++) {
    if (test_datas_[i].vmo) {
      zx::vmar::root_self()->unmap(test_datas_[i].vmo->ptr,
                                   test_datas_[i].vmo->page_count * ZX_PAGE_SIZE);
    }
  }

  if (!success) {
    PrintfAlways("Test failure, hanging to preserve state\n");
    zx_nanosleep(ZX_TIME_INFINITE);
  }

  return ZX_OK;
}

void CowCloneTestInstance::DumpTestVmoAccessInfo(const fbl::RefPtr<TestData>& vmo,
                                                 uint32_t page_index, uint64_t val) {
  PrintfAlways("Got value %lx (%x)\n", val, page_index);
  zx_info_vmo_t info;
  ZX_ASSERT(vmo->vmo.get_info(ZX_INFO_VMO, &info, sizeof(info), nullptr, nullptr) == ZX_OK);
  PrintfAlways("koid=%lx(%lu)\n", info.koid, info.koid);
  PrintfAlways("vmo ids are: ");
  auto cur = vmo;
  while (cur) {
    PrintfAlways("%x ", cur->vmo_id);
    cur = cur->parent;
  }
  PrintfAlways("\n");
}

fbl::RefPtr<CowCloneTestInstance::TestData> CowCloneTestInstance::CreateTestVmo(
    uint32_t idx, StressTest::Rng& rng) {
  uint32_t parent_idx = uniform_rand<uint32_t>(kMaxTestVmos, rng);
  auto& parent_vmo = test_datas_[parent_idx];

  zx::vmo vmo;
  fbl::RefPtr<struct TestData> parent;
  uint32_t parent_clone_start_op_id;
  uint32_t parent_clone_end_op_id;
  uint32_t page_count = uniform_rand_range<uint32_t>(1, kMaxVmoPageCount, rng);
  uint32_t page_offset = 0;

  if (parent_idx != idx) {
    if (!parent_vmo.mtx.try_lock_shared()) {
      // If something has an exclusive lock on the target vmo,
      // then just abort the operation.
      return nullptr;
    }

    if (parent_vmo.vmo) {
      parent = parent_vmo.vmo;

      page_offset = uniform_rand(parent->page_count, rng);

      parent_clone_start_op_id = parent->next_op_id.load();
      zx_status_t status = parent->vmo.create_child(
          ZX_VMO_CHILD_COPY_ON_WRITE, page_offset * ZX_PAGE_SIZE, page_count * ZX_PAGE_SIZE, &vmo);
      ZX_ASSERT_MSG(status == ZX_OK, "Failed to clone vmo %d", status);
      parent_clone_end_op_id = parent->next_op_id.load();
    } else {
      // There's no parent, so we're just going to create a new vmo.
      parent = nullptr;
    }

    parent_vmo.mtx.unlock_shared();
  } else {
    // We're creating a vmo at |idx|, so there isn't a vmo there now.
    parent = nullptr;
  }

  if (!parent) {
    parent_clone_start_op_id = parent_clone_end_op_id = 0;
    zx_status_t status = zx::vmo::create(page_count * ZX_PAGE_SIZE, 0, &vmo);
    ZX_ASSERT_MSG(status == ZX_OK, "Failed to clone vmo %d", status);
  }

  uintptr_t ptr;
  zx::vmar::root_self()->map(0, vmo, 0, page_count * ZX_PAGE_SIZE,
                             ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, &ptr);

  uint32_t vmo_id = next_vmo_id_.fetch_add(1);
  // The chance that an individual instance lives this long is vanishingly small, and it
  // would take a long time. So just abort the test so we don't have to deal with it.
  ZX_ASSERT(vmo_id < kMaxVmoId);

  auto res = fbl::MakeRefCounted<TestData>(vmo_id, idx, std::move(vmo), page_count, page_offset,
                                           ptr, std::move(parent), parent_clone_start_op_id,
                                           parent_clone_end_op_id);
  ZX_ASSERT(res);
  return res;
}

bool CowCloneTestInstance::TestVmoWrite(uint32_t idx, const fbl::RefPtr<TestData>& test_data,
                                        StressTest::Rng& rng) {
  uint32_t page_idx = uniform_rand(test_data->page_count, rng);

  auto p = reinterpret_cast<std::atomic_uint64_t*>(test_data->ptr + page_idx * ZX_PAGE_SIZE);

  // We want the ids to be atomically increasing. To prevent races between two
  // threads at the same location mixing up the order of their op-ids, do a cmpxchg
  // and regenerate the op-id if we see a race.
  uint64_t old = p->load();
  uint32_t my_op_id = test_data->next_op_id.fetch_add(1);
  uint64_t desired = make_full_id(test_data->vmo_id, my_op_id);
  while (!p->compare_exchange_strong(old, desired)) {
    my_op_id = test_data->next_op_id.fetch_add(1);
    desired = make_full_id(test_data->vmo_id, my_op_id);
  }

  uint32_t write_vmo_id = get_vmo_id(old);

  if (write_vmo_id == test_data->vmo_id) {
    // If the vmo id is for this vmo, then the old op id must be
    // less than whatever we wrote.
    if (get_op_id(old) < get_op_id(desired)) {
      return true;
    } else {
      PrintfAlways("Got high op id for current vmo\n");
      DumpTestVmoAccessInfo(test_data, page_idx, old);
      return false;
    }
  } else if (write_vmo_id == 0) {
    // Nothing has ever written to the page.
    if (old == 0) {
      return true;
    } else {
      PrintfAlways("Got non-zero op id for zero vmo id\n");
      DumpTestVmoAccessInfo(test_data, page_idx, old);
      return false;
    }
  }

  // Look up the parent chain for the vmo which is responsible for writing
  // the old data that we saw.
  auto cur = test_data;
  uint32_t parent_idx = page_idx;
  while (cur != nullptr) {
    // cur isn't responsible for writing the data, so it must have an ancestor that did it.
    ZX_ASSERT(cur->parent);

    parent_idx += cur->offset_page_idx;

    // The index we're inspecting lies past the end of the parent, which means
    // it was initialized to 0 in cur and we somehow didn't see the vmo originally
    // responsible for the write.
    if (parent_idx >= cur->parent->page_count) {
      PrintfAlways("Parent search overflow\n");
      DumpTestVmoAccessInfo(test_data, page_idx, old);
      return false;
    }

    if (cur->parent->vmo_id != write_vmo_id) {
      // No match, so continue up the chain.
      cur = cur->parent;
      continue;
    }

    // The op id we saw must be smaller than the next op id at the time of the clone op.
    if (get_op_id(old) >= cur->parent_clone_end_op_id) {
      PrintfAlways("Got op-id from after clone operation\n");
      DumpTestVmoAccessInfo(test_data, page_idx, old);
      return false;
    }

    // It's possible that the parent vmo has already been destroyed, so
    // lock its index and check if it's what we expect.
    auto& maybe_parent = test_datas_[cur->parent->idx];
    if (cur->parent->idx != cur->idx && maybe_parent.mtx.try_lock_shared()) {
      if (maybe_parent.vmo == cur->parent) {
        auto val = reinterpret_cast<std::atomic_uint64_t*>(maybe_parent.vmo->ptr +
                                                           parent_idx * ZX_PAGE_SIZE)
                       ->load();
        // If the clone sees a particular write_vmo_id, then that means the VMO
        // with the associated id wrote to that address before the clone operation.
        // Once that happens, the write ID that ancestor sees can't change.
        // Furthermore, the op ID can only increase.
        if (get_vmo_id(val) != write_vmo_id || (get_op_id(val) < get_op_id(old))) {
          DumpTestVmoAccessInfo(test_data, page_idx, old);
          DumpTestVmoAccessInfo(maybe_parent.vmo, parent_idx, val);

          shutdown_.store(true);
          maybe_parent.mtx.unlock_shared();
          return false;
        }
      }
      maybe_parent.mtx.unlock_shared();
    }
    break;
  }
  if (cur == nullptr) {
    // We somehow didn't find what performed the write.
    PrintfAlways("Parent search failure\n");
    DumpTestVmoAccessInfo(test_data, page_idx, old);
    return false;
  }
  return true;
}

int CowCloneTestInstance::op_thread() {
  auto rng = RngGen();

  while (!shutdown_.load()) {
    uint32_t idx = uniform_rand<uint32_t>(kMaxTestVmos, rng);
    auto& test_data = test_datas_[idx];
    uint32_t rand_op = uniform_rand(1000, rng);

    // 0 -> 14: create vmo
    // 15 -> 19: destroy vmo
    // 20 -> 999: random write
    if (rand_op < 20) {
      test_data.mtx.lock();

      if (rand_op < 14 && test_data.vmo == nullptr) {
        test_data.vmo = CreateTestVmo(idx, rng);
      } else if (rand_op >= 15 && test_data.vmo != nullptr) {
        for (unsigned i = 0; i < test_data.vmo->page_count; i++) {
          auto val = reinterpret_cast<std::atomic_uint64_t*>(test_data.vmo->ptr + i * ZX_PAGE_SIZE)
                         ->load();
          // vmo ids are monotonically increasing, so we shouldn't see
          // any ids greater than the current vmo's.
          if (get_vmo_id(val) > test_data.vmo->vmo_id) {
            DumpTestVmoAccessInfo(test_data.vmo, i, val);
            shutdown_.store(true);
            test_data.mtx.unlock();
            return -1;
          }
        }

        zx::vmar::root_self()->unmap(test_data.vmo->ptr, test_data.vmo->page_count * ZX_PAGE_SIZE);
        test_data.vmo->vmo.reset();
        test_data.vmo = nullptr;
      }
      test_data.mtx.unlock();
    } else {
      test_data.mtx.lock_shared();

      if (test_data.vmo != nullptr) {
        if (!TestVmoWrite(idx, test_data.vmo, rng)) {
          test_data.mtx.unlock_shared();
          return -1;
        }
      }

      test_data.mtx.unlock_shared();
    }
  }
  return 0;
}

// This test instances runs multiple VMOs across multiple threads and is trying to trigger unusual
// race conditions and kernel failures that come from mixing parallelism of all kinds of operations.
// The trade off is that the test almost never knows what the outcome of an operation should be and
// so this only catches bugs where we can ultimately trip a kernel assert or something similar.
class MultiVmoTestInstance : public TestInstance {
 public:
  MultiVmoTestInstance(VmStressTest* test, uint64_t mem_limit)
      : TestInstance(test),
        memory_limit_pages_(mem_limit / ZX_PAGE_SIZE),
        // Scale our maximum threads to ensure that if all threads allocate a full size vmo (via
        // copy-on-write or otherwise) we wouldn't exceed our memory limit
        max_threads_(memory_limit_pages_ / kMaxVmoPages) {}

  zx_status_t Start() override {
    // If max threads was calculated smaller than low threads then that means we really don't have
    // much memory. Don't try and recover this case, just fail.
    if (max_threads_ < low_threads_) {
      PrintfAlways("Not enough free memory to run test instance\n");
      return ZX_ERR_NO_MEMORY;
    }
    if (shutdown_) {
      return ZX_ERR_INTERNAL;
    }

    zx::unowned_resource root_resource = RootResource();
    if (*root_resource) {
      zx_iommu_desc_dummy_t desc;
      zx_status_t result =
          zx::iommu::create(*root_resource, ZX_IOMMU_TYPE_DUMMY, &desc, sizeof(desc), &iommu_);
      if (result != ZX_OK) {
        return result;
      }

      result = zx::bti::create(iommu_, 0, 0xdeadbeef, &bti_);
      if (result != ZX_OK) {
        return ZX_OK;
      }
    }

    auto rng = RngGen();

    spawn_root_vmo(rng);
    return ZX_OK;
  }
  zx_status_t Stop() override {
    // Signal shutdown and wait for everyone. Its possible for living_threads_ to increase after
    // shutdown_ is set if a living thread creates another thread. This is fine since it means
    // living_threads goes from a non-zero value to a non-zero value, but it will never go from 0
    // to non-zero.
    shutdown_ = true;
    while (living_threads_ > 0) {
      zx::nanosleep(zx::deadline_after(zx::msec(500)));
    }
    return ZX_OK;
  }

 private:
  void spawn_root_vmo(StressTest::Rng& rng) {
    zx::vmo vmo;
    bool reliable_mappings = true;
    uint64_t vmo_size = uniform_rand(kMaxVmoPages, rng) * PAGE_SIZE;

    // Skew heavily away from contiguous VMOs as they are very limited in what operations are
    // supported and need less testing.
    if (bti_ && uniform_rand(6, rng) == 0) {
      zx_status_t result = zx::vmo::create_contiguous(bti_, vmo_size, 0, &vmo);
      if (result != ZX_OK) {
        return;
      }
    } else {
      uint32_t options = 0;
      // Skew away from resizable VMOs as they are not common and many operations don't work.
      if (uniform_rand(4, rng) == 0) {
        options |= ZX_VMO_RESIZABLE;
        reliable_mappings = false;
      }

      if (uniform_rand(2, rng) == 0) {
        zx::pager pager;
        zx::port port;
        zx_status_t result = zx::pager::create(0, &pager);
        ZX_ASSERT(result == ZX_OK);
        result = zx::port::create(0, &port);
        ZX_ASSERT(result == ZX_OK);

        result = pager.create_vmo(options, port, 0, vmo_size, &vmo);
        ZX_ASSERT(result == ZX_OK);
        // Randomly discard reliable mappings even though not resizable to give the pager a chance
        // to generate faults on non-resizable vmos.
        if (reliable_mappings && uniform_rand(4, rng) == 0) {
          reliable_mappings = false;
        }
        // Force spin up the pager thread as it's required to ensure the VMO threads do not block
        // forever.
        zx::vmo dup_vmo;
        result = vmo.duplicate(ZX_RIGHT_SAME_RIGHTS, &dup_vmo);
        ZX_ASSERT(result == ZX_OK);
        if (!make_thread([this, pager = std::move(pager), port = std::move(port),
                          vmo = std::move(dup_vmo), reliable_mappings]() mutable {
              pager_thread(std::move(pager), std::move(port), std::move(vmo), reliable_mappings);
            })) {
          // if the pager thread couldn't spin up bail right now and don't make the client thread as
          // that client thread will either block or hard crash, either scenario will make that
          // thread unrecoverable.
          return;
        }
      } else {
        zx_status_t result = zx::vmo::create(vmo_size, options, &vmo);
        ZX_ASSERT(result == ZX_OK);
      }
    }

    auto ops = make_ops(rng);

    make_thread([this, vmo = std::move(vmo), ops = std::move(ops), reliable_mappings]() mutable {
      op_thread(std::move(vmo), std::move(ops), reliable_mappings);
    });
  }

  // TODO: pager_thread currently just fulfills any page faults correctly. This should be expanded
  // to detach, error ranges, pre-supply pages etc.
  void pager_thread(zx::pager pager, zx::port port, zx::vmo vmo, bool reliable_mappings) {
    // To exit the pager thread we need to know once we have the only reference to the vmo. This
    // requires two things, our vmo handle be the only handle to that vmo, and the vmo have no
    // children. The first condition has no signal and so until we know we have the only handle we
    // will used timed waits and poll. Once we are the solo_owner, tracked in this variable, we
    // will be able to use the zero children signal.
    bool solo_owner = false;
    while (1) {
      zx_port_packet_t packet;
      zx_status_t result =
          port.wait(solo_owner ? zx::time::infinite() : zx::deadline_after(zx::msec(100)), &packet);
      if (result == ZX_ERR_TIMED_OUT) {
        zx_info_handle_count_t info;
        result = vmo.get_info(ZX_INFO_HANDLE_COUNT, &info, sizeof(info), nullptr, nullptr);
        ZX_ASSERT(result == ZX_OK);
        // Check if we have the only handle
        if (info.handle_count == 1) {
          // Start watching for the zero children signal.
          result = vmo.wait_async(port, 1, ZX_VMO_ZERO_CHILDREN, 0);
          ZX_ASSERT(result == ZX_OK);
          solo_owner = true;
        }
        continue;
      }
      if (packet.key == 1) {
        ZX_ASSERT(solo_owner);
        // No children, and we have the only handle. Done.
        break;
      }
      ZX_ASSERT(packet.key == 0);
      ZX_ASSERT(packet.type == ZX_PKT_TYPE_PAGE_REQUEST);

      if (packet.page_request.command == ZX_PAGER_VMO_COMPLETE) {
        // VMO is finished, so we have nothing to do. Technically since we have a handle to the vmo
        // this case will never happen.
        break;
      } else if (packet.page_request.command != ZX_PAGER_VMO_READ) {
        PrintfAlways("Unknown page_request command %d\n", packet.page_request.command);
        return;
      }

      // No matter what we decide to do we *MUST* ensure we also fullfill the page fault in some way
      // otherwise we risk blocking the faulting thread (and also ourselves) forever. Above all we
      // must guarantee that the op_thread can progress to the point of closing the VMO such that
      // we end up with the only VMO handle.

      zx::vmo aux_vmo;
      if (zx::vmo::create(packet.page_request.length, 0, &aux_vmo) != ZX_OK) {
        PrintfAlways("Failed to create VMO of length %" PRIu64 " to fulfill page fault\n",
                     packet.page_request.length);
        return;
      }

      result = pager.supply_pages(vmo, packet.page_request.offset, packet.page_request.length,
                                  aux_vmo, 0);
      // If the underlying VMO was resized then its possible the supply destination is now out of
      // range. This is okay and we can just continue. In any other case something has gone
      // horribly wrong.
      if (result != ZX_OK && result != ZX_ERR_OUT_OF_RANGE) {
        PrintfAlways("Failed to supply pages: %d\n", result);
        return;
      }
    }
  }

  // This is the main function that performs continuous operations on a vmo. It may try to spawn
  // additional threads for parallelism, but they all share the same op counter to prevent any
  // particular vmo hierarchy living 'forever' by spawning new children all the time.
  void op_thread(zx::vmo vmo, std::shared_ptr<std::atomic<uint64_t>> op_count,
                 bool reliable_mappings) {
    auto rng = RngGen();

    zx::pmt pmt;
    std::optional<fbl::Span<uint8_t>> mapping;
    auto unmap_mapping = [&mapping]() {
      if (auto span = mapping) {
        zx::vmar::root_self()->unmap(reinterpret_cast<uintptr_t>(span->data()), span->size_bytes());
        mapping = std::nullopt;
      }
    };
    auto cleanup = fbl::AutoCall([&unmap_mapping, &pmt]() {
      unmap_mapping();
      if (pmt) {
        pmt.unpin();
      }
    });

    // Query for the current size of the vmo. This could change due to other threads with handles to
    // this vmo calling set-size, but should ensure a decent hit rate of random range operations.
    uint64_t vmo_size;
    if (vmo.get_size(&vmo_size) != ZX_OK) {
      vmo_size = kMaxVmoPages * ZX_PAGE_SIZE;
    }
    while (!shutdown_ && op_count->fetch_add(1) < kMaxOps) {
      // Produce a random offset and size up front since many ops will need it.
      uint64_t op_off, op_size;
      random_off_size(rng, vmo_size, &op_off, &op_size);
      switch (uniform_rand(10, rng)) {
        case 0:  // give up early
          Printf("G");
          return;
          break;
        case 1: {  // duplicate
          Printf("D");
          zx::vmo dup_vmo;
          zx_status_t result = vmo.duplicate(ZX_RIGHT_SAME_RIGHTS, &dup_vmo);
          ZX_ASSERT(result == ZX_OK);
          make_thread(
              [this, dup = std::move(dup_vmo), ops = op_count, reliable_mappings]() mutable {
                op_thread(std::move(dup), std::move(ops), reliable_mappings);
              });
          break;
        }
        case 2: {  // read
          Printf("R");
          std::vector<uint8_t> buffer;
          bool use_map = false;
          if (mapping.has_value() && uniform_rand(2, rng) == 0) {
            op_off = uniform_rand(mapping.value().size_bytes(), rng);
            op_size = uniform_rand(mapping.value().size_bytes() - op_off, rng);
            use_map = true;
          }
          buffer.resize(op_size);
          // pre-commit some portion of the buffer
          const size_t end = uniform_rand(op_size, rng);
          const size_t start = uniform_rand(op_size, rng);
          memset(&buffer[start], 42, end - std::min(end, start));
          if (use_map) {
            memcpy(buffer.data(), &mapping.value()[op_off], op_size);
          } else {
            vmo.read(buffer.data(), op_off, op_size);
          }
          break;
        }
        case 3: {  // write
          Printf("W");
          std::vector<uint8_t> buffer;
          bool use_map = false;
          if (mapping.has_value() && uniform_rand(2, rng) == 0) {
            op_off = uniform_rand(mapping.value().size_bytes(), rng);
            op_size = uniform_rand(mapping.value().size_bytes() - op_off, rng);
            use_map = true;
          }
          buffer.resize(op_size);
          // write some portion of the buffer with 'random' data.
          const size_t end = uniform_rand(op_size, rng);
          const size_t start = uniform_rand(op_size, rng);
          memset(&buffer[start], 42, end - std::min(end, start));
          if (use_map) {
            memcpy(&mapping.value()[op_off], buffer.data(), op_size);
          } else {
            vmo.write(buffer.data(), op_off, op_size);
          }
          break;
        }
        case 4:  // vmo_set_size
          Printf("S");
          vmo.set_size(uniform_rand(kMaxVmoPages * ZX_PAGE_SIZE, rng));
          break;
        case 5: {  // vmo_op_range
          Printf("O");
          static const uint32_t ops[] = {ZX_VMO_OP_COMMIT,
                                         ZX_VMO_OP_DECOMMIT,
                                         ZX_VMO_OP_ZERO,
                                         ZX_VMO_OP_LOCK,
                                         ZX_VMO_OP_UNLOCK,
                                         ZX_VMO_OP_CACHE_SYNC,
                                         ZX_VMO_OP_CACHE_INVALIDATE,
                                         ZX_VMO_OP_CACHE_CLEAN,
                                         ZX_VMO_OP_CACHE_CLEAN_INVALIDATE};
          vmo.op_range(ops[uniform_rand(std::size(ops), rng)], op_off, op_size, nullptr, 0);
          break;
        }
        case 6: {  // vmo_set_cache_policy
          Printf("P");
          static const uint32_t policies[] = {ZX_CACHE_POLICY_CACHED, ZX_CACHE_POLICY_UNCACHED,
                                              ZX_CACHE_POLICY_UNCACHED_DEVICE,
                                              ZX_CACHE_POLICY_WRITE_COMBINING};
          vmo.set_cache_policy(policies[uniform_rand(std::size(policies), rng)]);
          break;
        }
        case 7: {  // vmo_create_child
          Printf("C");
          static const uint32_t type[] = {
              ZX_VMO_CHILD_SNAPSHOT, ZX_VMO_CHILD_SNAPSHOT_AT_LEAST_ON_WRITE, ZX_VMO_CHILD_SLICE};
          uint32_t options = type[uniform_rand(std::size(type), rng)];
          bool child_reliable_mappings = reliable_mappings;
          if (uniform_rand(3, rng) == 0) {
            options |= ZX_VMO_CHILD_RESIZABLE;
            child_reliable_mappings = false;
          }
          if (uniform_rand(4, rng)) {
            options |= ZX_VMO_CHILD_NO_WRITE;
          }
          zx::vmo child;
          if (vmo.create_child(options, op_off, op_size, &child) == ZX_OK) {
            make_thread([this, child = std::move(child), ops = op_count,
                         child_reliable_mappings]() mutable {
              op_thread(std::move(child), std::move(ops), child_reliable_mappings);
            });
          }
          break;
        }
        case 8: {  // vmar_map/unmap
          // If reliable mappings is true it means we know that no one else is going to mess with
          // the VMO in a way that would cause access to a valid mapping to generate a fault.
          // Generally this means that the VMO is not resizable.
          Printf("V");
          if (reliable_mappings) {
            if (!mapping.has_value() || uniform_rand(2, rng) == 0) {
              uint32_t options = ZX_VM_PERM_READ | ZX_VM_PERM_WRITE;
              if (uniform_rand(2, rng) == 0) {
                options |= ZX_VM_MAP_RANGE;
              }
              zx_vaddr_t addr;
              // Currently fault prevention isn't enforced in mappings and so we must be *very*
              // careful to not map in outside the actual range of the vmo.
              if (op_off + op_size <= vmo_size &&
                  zx::vmar::root_self()->map(0, vmo, op_off, op_size, options, &addr) == ZX_OK) {
                unmap_mapping();
                mapping = fbl::Span<uint8_t>{reinterpret_cast<uint8_t*>(addr), op_size};
              }
            } else {
              unmap_mapping();
            }
          }
          break;
        }
        case 9: {  // bti_pin/bti_unpin
          Printf("I");
          if (bti_) {
            if (pmt || uniform_rand(2, rng) == 0) {
              zx::pmt new_pmt;
              std::vector<zx_paddr_t> paddrs{op_size / PAGE_SIZE, 0};
              if (bti_.pin(ZX_BTI_PERM_READ | ZX_BTI_PERM_WRITE, vmo, op_off, op_size,
                           paddrs.data(), paddrs.size(), &new_pmt) == ZX_OK) {
                if (pmt) {
                  pmt.unpin();
                }
                pmt = std::move(new_pmt);
              }
            } else {
              if (pmt) {
                pmt.unpin();
              }
            }
          }
          break;
        }
      }
    }
    if (!shutdown_) {
      // Achieved max ops.
      Printf("M");
    }
  }

  static void random_off_size(StressTest::Rng& rng, uint64_t vmo_size, uint64_t* off_out,
                              uint64_t* size_out) {
    // When calculating out of bounds values pick a limit that still gives chance to be in bounds
    constexpr uint64_t kOobLimitPages = kMaxVmoPages * 2;
    // We don't want a uniform distribution of offsets and sizes as a lot of interesting things
    // happen with page alignment and entire vmo ranges.
    switch (uniform_rand(5, rng)) {
      case 0:  // Anchor offset at 0
        *off_out = 0;
        break;
      case 1:  // Page aligned offset, in bounds
        *off_out = uniform_rand(vmo_size / ZX_PAGE_SIZE, rng) * ZX_PAGE_SIZE;
        break;
      case 2:  // Page aligned offset, out of bounds
        *off_out = uniform_rand(kOobLimitPages, rng) * ZX_PAGE_SIZE;
        break;
      case 3:  // In bounds
        *off_out = uniform_rand(vmo_size, rng);
        break;
      case 4:  // Out of bounds
        *off_out = uniform_rand(kOobLimitPages * ZX_PAGE_SIZE, rng);
        break;
    }
    const uint64_t remaining = vmo_size - std::min(vmo_size, *off_out);
    switch (uniform_rand(5, rng)) {
      case 0:  // Maximum remaining vmo size
        *size_out = remaining;
        break;
      case 1:  // In range page aligned size
        *size_out = uniform_rand(remaining / ZX_PAGE_SIZE, rng) * ZX_PAGE_SIZE;
        break;
      case 2:  // Out of range page aligned size
        *size_out = uniform_rand(kOobLimitPages, rng) * ZX_PAGE_SIZE;
        break;
      case 3:  // In range size
        *size_out = uniform_rand(remaining, rng);
        break;
      case 4:  // Out of range size
        *size_out = uniform_rand(kOobLimitPages * ZX_PAGE_SIZE, rng);
        break;
    }
  }

  // This wrapper spawns a new thread to run F and automatically updates the living_threads_ count
  // and spawns any new root threads should we start running low.
  template <typename F>
  bool make_thread(F func) {
    uint64_t prev_count = living_threads_.fetch_add(1);
    if (prev_count >= max_threads_) {
      living_threads_.fetch_sub(1);
      return false;
    }
    std::thread t{[this, func = std::move(func)]() mutable {
      func();
      // Spawn threads *before* decrementing our count as the shutdown logic assumes once shutdown_
      // then once living_threads_ becomes 0 it must never increment.
      while (!shutdown_ && living_threads_ < low_threads_) {
        auto rng = RngGen();
        spawn_root_vmo(rng);
      }
      living_threads_.fetch_sub(1);
    }};
    t.detach();
    return true;
  }

  std::shared_ptr<std::atomic<uint64_t>> make_ops(StressTest::Rng& rng) {
    uint64_t start_ops = uniform_rand(kMaxOps, rng);
    return std::make_shared<std::atomic<uint64_t>>(start_ops);
  }

  // To explore interesting scenarios, especially involving parallelism, we want to run every VMO
  // tree for a decent number of ops, but not too long as at some point running longer is the same
  // as just spawning a new tree. This number was chosen fairly arbitrarily, but given that all
  // previous VM bugs had unit test reproductions in the <20 ops, this seems reasonable.
  static constexpr uint64_t kMaxOps = 4096;

  // 128 pages in a vmo should be all we need to create sufficiently interesting hierarchies, so
  // cap our spending there. This allows us to spin up more threads and copy-on-write hierarchies
  // without worrying that they all commit and blow the memory limit.
  static constexpr uint64_t kMaxVmoPages = 128;

  // This will be set to the total memory limit (in pages) that this test instance is constructed
  // with. We should not spend more than that.
  const uint64_t memory_limit_pages_;

  // The maximum number of threads we can create that will not cause us to exceed our memory limit.
  const uint64_t max_threads_;

  // Generally we don't want too many threads, so set low_threads (which is the threshold at which
  // we start spawning more root threads) to be fairly low. max_threads_ can be arbitrarily high,
  // which allows our low amount of root threads to (potentially) spin up a lot of parallelism.
  const uint64_t low_threads_ = 8;

  // Set to true when we are trying to shutdown.
  std::atomic<bool> shutdown_ = false;
  // Number of alive threads. Used to coordinate shutdown.
  std::atomic<uint64_t> living_threads_ = 0;

  // Valid if we got the root resource.
  zx::iommu iommu_;
  zx::bti bti_;
};

// Test thread which initializes/tears down TestInstances
int VmStressTest::test_thread() {
  constexpr uint64_t kMaxInstances = 8;
  constexpr uint64_t kVariableInstances = kMaxInstances - 1;
  std::unique_ptr<TestInstance> test_instances[kMaxInstances] = {};

  const uint64_t free_bytes = kmem_stats_.free_bytes;
  // scale the size of the VMO we create based on the size of memory in the system.
  // 1/64th the size of total memory generates a fairly sizeable vmo (16MB per 1GB)
  const uint64_t vmo_test_size = free_bytes / 64 / kMaxInstances;

  PrintfAlways("VM stress test: using vmo of size %" PRIu64 "\n", vmo_test_size);

  // The MultiVmoTestInstance already does spin up / tear down of threads internally and there is
  // no benefit in also spinning up and tearing down the whole thing. So we just run 1 of them
  // explicitly as a static instance and randomize the others as variable instances. We give this
  // instance a 'full slice' of free memory as it is incredibly unlikely that it even allocates
  // anywhere near that.
  test_instances[kVariableInstances] =
      std::make_unique<MultiVmoTestInstance>(this, free_bytes / kMaxInstances);
  test_instances[kVariableInstances]->Start();

  zx::time deadline = zx::clock::get_monotonic();
  auto rng = RngGen();
  while (!shutdown_.load()) {
    uint64_t r = uniform_rand(kVariableInstances, rng);
    if (test_instances[r]) {
      test_instances[r]->Stop();
      test_instances[r].reset();
    } else {
      switch (uniform_rand(3, rng)) {
        case 0:
          test_instances[r] = std::make_unique<SingleVmoTestInstance>(this, true, vmo_test_size);
          break;
        case 1:
          test_instances[r] = std::make_unique<SingleVmoTestInstance>(this, false, vmo_test_size);
          break;
        case 2:
          test_instances[r] = std::make_unique<CowCloneTestInstance>(this);
          break;
      }

      if (test_instances[r]) {
        ZX_ASSERT(test_instances[r]->Start() == ZX_OK);
      }
    }

    constexpr uint64_t kOpsPerSec = 25;
    deadline += zx::duration(ZX_SEC(1) / kOpsPerSec);
    zx::nanosleep(deadline);
  }

  for (uint64_t i = 0; i < kMaxInstances; i++) {
    if (test_instances[i]) {
      test_instances[i]->Stop();
    }
  }
  return 0;
}

zx_status_t VmStressTest::Start() {
  auto test_worker = [](void* arg) -> int {
    return static_cast<VmStressTest*>(arg)->test_thread();
  };
  thrd_create_with_name(&test_thread_, test_worker, this, "test_worker");

  return ZX_OK;
}

zx_status_t VmStressTest::Stop() {
  shutdown_.store(true);
  thrd_join(test_thread_, nullptr);
  return ZX_OK;
}
