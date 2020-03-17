// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <inttypes.h>
#include <lib/zx/channel.h>
#include <lib/zx/clock.h>
#include <lib/zx/exception.h>
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
#include <zircon/syscalls/port.h>
#include <zircon/threads.h>

#include <array>
#include <atomic>
#include <memory>
#include <shared_mutex>

#include <fbl/algorithm.h>
#include <fbl/array.h>
#include <fbl/auto_call.h>
#include <fbl/auto_lock.h>
#include <fbl/mutex.h>
#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>

#include "stress_test.h"

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

  // local helper routines to calculate a random range within a vmo and
  // a range appropriate to read into the local buffer above
  auto rand_vmo_range = [this](uint64_t* out_offset, uint64_t* out_size) {
    *out_offset = rand() % vmo_size_;
    *out_size = fbl::min(rand() % vmo_size_, vmo_size_ - *out_offset);
  };
  auto rand_buffer_range = [this](uint64_t* out_offset, uint64_t* out_size) {
    *out_size = rand() % buf_size;
    *out_offset = rand() % (vmo_size_ - *out_size);
  };

  ZX_ASSERT(buf_size < vmo_size_);

  while (!shutdown_.load()) {
    uint64_t off, len;

    int r = rand() % 100;
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

  while (!shutdown_.load()) {
    zx::vmo tmp_vmo;
    uint64_t off, size;
    zx::time deadline;

    int r = rand() % 100;
    switch (r) {
      case 0 ... 4:  // supply a random range of pages
        off = rand() % vmo_page_count;
        size = fbl::min(rand() % vmo_page_count, vmo_page_count - off);
        supply_pages(off * PAGE_SIZE, size * PAGE_SIZE);
        break;
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
        off = rand() % requests_.size();
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
    if (rand() % 2) {
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

  for (uint32_t i = 0; i < fbl::count_of(threads_); i++) {
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
    for (unsigned i = 0; i < kNumVmoThreads; i++) {
      zx_status_t status = thread_handles_[i].create_exception_channel(0, &channels[i]);
      ZX_ASSERT(status == ZX_OK);
      status = channels[i].wait_async(port, i, ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED,
                                      ZX_WAIT_ASYNC_ONCE);
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
                                     ZX_WAIT_ASYNC_ONCE) == ZX_OK);
      } else {
        running_count--;
      }
    }
  }

  for (unsigned i = 0; i < fbl::count_of(threads_); i++) {
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
  fbl::RefPtr<TestData> CreateTestVmo(uint32_t idx);
  // Helper function that performs a write operation on |TestData|, which is currently
  // in |idx| in test_datas_.
  bool TestVmoWrite(uint32_t idx, const fbl::RefPtr<TestData>& TestData);

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

fbl::RefPtr<CowCloneTestInstance::TestData> CowCloneTestInstance::CreateTestVmo(uint32_t idx) {
  uint32_t parent_idx = rand() % kMaxTestVmos;
  auto& parent_vmo = test_datas_[parent_idx];

  zx::vmo vmo;
  fbl::RefPtr<struct TestData> parent;
  uint32_t parent_clone_start_op_id;
  uint32_t parent_clone_end_op_id;
  uint32_t page_count = static_cast<uint32_t>((rand() % kMaxVmoPageCount) + 1);
  uint32_t page_offset = 0;

  if (parent_idx != idx) {
    if (!parent_vmo.mtx.try_lock_shared()) {
      // If something has an exclusive lock on the target vmo,
      // then just abort the operation.
      return nullptr;
    }

    if (parent_vmo.vmo) {
      parent = parent_vmo.vmo;

      page_offset = rand() % parent->page_count;

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

bool CowCloneTestInstance::TestVmoWrite(uint32_t idx, const fbl::RefPtr<TestData>& test_data) {
  uint32_t page_idx = rand() % test_data->page_count;

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
  while (!shutdown_.load()) {
    uint32_t idx = rand() % kMaxTestVmos;
    auto& test_data = test_datas_[idx];
    uint32_t rand_op = rand() % 1000;

    // 0 -> 14: create vmo
    // 15 -> 19: destroy vmo
    // 20 -> 999: random write
    if (rand_op < 20) {
      test_data.mtx.lock();

      if (rand_op < 14 && test_data.vmo == nullptr) {
        test_data.vmo = CreateTestVmo(idx);
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
        if (!TestVmoWrite(idx, test_data.vmo)) {
          test_data.mtx.unlock_shared();
          return -1;
        }
      }

      test_data.mtx.unlock_shared();
    }
  }
  return 0;
}

// Test thread which initializes/tears down TestInstances
int VmStressTest::test_thread() {
  constexpr uint64_t kMaxInstances = 8;
  std::unique_ptr<TestInstance> test_instances[kMaxInstances] = {};

  const uint64_t free_bytes = kmem_stats_.free_bytes;
  // scale the size of the VMO we create based on the size of memory in the system.
  // 1/64th the size of total memory generates a fairly sizeable vmo (16MB per 1GB)
  const uint64_t vmo_test_size = free_bytes / 64 / kMaxInstances;

  PrintfAlways("VM stress test: using vmo of size %" PRIu64 "\n", vmo_test_size);

  zx::time deadline = zx::clock::get_monotonic();
  while (!shutdown_.load()) {
    uint64_t r = rand() % kMaxInstances;
    if (test_instances[r]) {
      test_instances[r]->Stop();
      test_instances[r].reset();
    } else {
      switch (rand() % 3) {
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

      ZX_ASSERT(test_instances[r]->Start() == ZX_OK);
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
