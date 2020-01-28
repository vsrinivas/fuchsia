// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <lib/zx/process.h>
#include <lib/zx/thread.h>
#include <lib/zx/vmo.h>
#include <zircon/compiler.h>
#include <zircon/process.h>
#include <zircon/sanitizer.h>
#include <zircon/syscalls/debug.h>

#include <algorithm>
#include <utility>

#include "dynlink.h"
#include "threads_impl.h"

namespace {

// This is a simple container similar to std::vector but using only whole-page
// allocations in a private VMO to avoid interactions with any normal memory
// allocator.  Resizing the vector may remap the data in the VMO to a new
// memory location without changing its contents, so the element type must not
// contain any pointers into itself or the like.
template <typename T>
class RelocatingPageAllocatedVector {
 public:
  RelocatingPageAllocatedVector(const RelocatingPageAllocatedVector&) = delete;
  RelocatingPageAllocatedVector(RelocatingPageAllocatedVector&&) = delete;

  RelocatingPageAllocatedVector() = default;

  ~RelocatingPageAllocatedVector() {
    for (auto& elt : *this) {
      elt.~T();
    }
    if (data_) {
      Unmap(data_);
    }
  }

  using size_type = size_t;
  using value_type = T;
  using iterator = T*;
  using const_iterator = const T*;

  size_type size() const { return size_; }
  size_type capacity() const { return capacity_; }
  T* data() { return data_; }
  const T* data() const { return data_; }
  iterator begin() { return data_; }
  iterator end() { return &data_[size_]; }
  const_iterator cbegin() const { return data_; }
  const_iterator cend() const { return &data_[size_]; }
  T& operator[](size_type i) {
    assert(i < size_);
    return data_[i];
  }
  const T& operator[](size_type i) const {
    assert(i < size_);
    return data_[i];
  }

  // On success, size() < capacity().
  zx_status_t reserve_some_more() {
    if (size_ < capacity_) {
      return ZX_OK;
    }
    static_assert(sizeof(T) <= ZX_PAGE_SIZE);
    const size_t alloc_size = AllocatedSize() + ZX_PAGE_SIZE;
    zx_status_t status =
        vmo_ ? vmo_.set_size(alloc_size) : zx::vmo::create(alloc_size, ZX_VMO_RESIZABLE, &vmo_);
    if (status == ZX_OK) {
      // Leave the old mapping in place while making the new mapping so that
      // it's still accessible for element destruction in case of failure.
      auto old = data_;
      status = Map(alloc_size);
      if (status == ZX_OK) {
        assert(size_ < capacity_);
        Unmap(old);
      }
    }
    return status;
  }

  // This is like the standard resize method, but it doesn't initialize new
  // elements.  Instead, it's expected that the caller has already initialized
  // them by writing data() elements between size() and capacity().
  void resize_in_place(size_t new_size) {
    assert(new_size <= capacity_);
    size_ = new_size;
  }

  // Unlike standard containers, this never allocates and must only be called
  // when capacity() > size(), e.g. after reserve_some_more().
  template <typename U>
  void push_back(U&& value) {
    assert(size_ < capacity_);
    data_[size_++] = std::forward<U>(value);
  }

 private:
  T* data_ = nullptr;
  size_t size_ = 0;
  size_t capacity_ = 0;
  zx::vmo vmo_;

  size_t AllocatedSize() const {
    size_t total = capacity_ * sizeof(T);
    return (total + ZX_PAGE_SIZE - 1) & -ZX_PAGE_SIZE;
  }

  zx_status_t Map(size_t alloc_size) {
    uintptr_t addr;
    zx_status_t status = _zx_vmar_map(_zx_vmar_root_self(), ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, 0,
                                      vmo_.get(), 0, alloc_size, &addr);
    if (status == ZX_OK) {
      data_ = reinterpret_cast<T*>(addr);
      capacity_ = alloc_size / sizeof(T);
    }
    return status;
  }

  void Unmap(void* data) {
    _zx_vmar_unmap(_zx_vmar_root_self(), reinterpret_cast<uintptr_t>(data), AllocatedSize());
  }
};

// Just keeping the suspend_token handle alive is what keeps the thread
// suspended.  So destruction of the Thread object implicitly resumes it.
struct Thread {
  zx_koid_t koid;
  zx::thread thread;
  zx::suspend_token token;
};

class ThreadSuspender {
 public:
  ThreadSuspender() {
    // Take important locks before suspending any threads.  These protect data
    // structures that MemorySnapshot needs to scan.  Once all threads are
    // suspended, the locks are released since any potential contenders should
    // be quiescent for the remainder of the snapshot, and it's inadvisable to
    // call user callbacks with internal locks held.
    //
    // N.B. The lock order here matches dlopen_internal to avoid A/B deadlock.

    // The dynamic linker data structures are used to find all the global
    // ranges, so they must be in a consistent state.
    _dl_rdlock();

    // This approximately prevents thread creation.  It doesn't affirmatively
    // prevent thread creation per se.  Rather, it prevents thrd_create or
    // pthread_create from allocating new thread data structures.  The lock is
    // not held while actually creating the thread, however, so there is
    // always a race with actual thread creation that has to be addressed by
    // the looping logic in Collect, below.  Also, nothing prevents racing
    // with other direct zx_thread_create calls in the process that don't use
    // the libc facilities.
    __thread_allocation_inhibit();

    // Importantly, this lock protects consistency of the global list of
    // all threads so that it can be traversed safely below.
    __thread_list_acquire();
  }

  ~ThreadSuspender() {
    __thread_list_release();
    __thread_allocation_release();
    _dl_unlock();
  }

  zx_status_t Collect(RelocatingPageAllocatedVector<Thread>* threads) {
    zx_status_t status = Init();
    if (status != ZX_OK) {
      return status;
    }

    size_t filled, count;
    bool any_new;
    do {
      // Prepare to handle more than the last iteration (or "some" on the
      // first iteration).
      status = koids_.reserve_some_more();

      if (status == ZX_OK) {
        // Collect all the thread KOIDs in the process.
        status = process()->get_info(ZX_INFO_PROCESS_THREADS, koids_.data(),
                                     koids_.capacity() * sizeof(zx_koid_t), &filled, &count);
      }

      if (status == ZX_OK) {
        // Check for threads not already suspended.
        koids_.resize_in_place(filled);
        status = SuspendNewThreads(threads, &any_new);
      }

      if (status != ZX_OK) {
        return status;
      }

      // Loop as long as either the scan found any new threads or the buffer
      // didn't include all the threads in the process.  Any time there is a
      // newly-suspended thread, it might have just created another thread
      // before being suspended, so another pass is needed to ensure all live
      // threads have been caught.
    } while (any_new || filled < count);

    // Now wait for all the threads to have finished suspending.
    for (auto& t : *threads) {
      zx_signals_t pending;
      status = t.thread.wait_one(ZX_THREAD_SUSPENDED | ZX_THREAD_TERMINATED, zx::time::infinite(),
                                 &pending);
      if (status != ZX_OK) {
        return status;
      }
      if (pending & ZX_THREAD_TERMINATED) {
        // The thread died before getting fully suspended.
        t.koid = ZX_KOID_INVALID;
      } else {
        assert(pending & ZX_THREAD_SUSPENDED);
      }
    }

    return ZX_OK;
  }

 private:
  RelocatingPageAllocatedVector<zx_koid_t> koids_;
  zx_koid_t this_thread_koid_ = ZX_KOID_INVALID;

  zx::unowned_process process() { return zx::unowned_process{_zx_process_self()}; }

  zx_status_t Init() {
    // First determine this thread's KOID to distinguish it from siblings.
    zx::unowned_thread this_thread{_zx_thread_self()};
    zx_info_handle_basic_t self_info;
    zx_status_t status = this_thread->get_info(ZX_INFO_HANDLE_BASIC, &self_info, sizeof(self_info),
                                               nullptr, nullptr);
    if (status == ZX_OK) {
      this_thread_koid_ = self_info.koid;
    }
    return status;
  }

  // Scan koids_ for threads not already present in the vector.
  // For each new thread, suspend it and push it onto the vector.
  //
  // TODO(mcgrathr): Performance considerations for this path:
  //
  // Most often this will be called exactly twice: first when the vector is
  // empty, and then again when the refreshed list of threads is verified to
  // exactly match the set already in the vector.  It will only be called for
  // additional iterations if there is a race with one of the live threads
  // creating a new thread.  Since the usual use of this facility is for
  // shutdown-time leak checking, such races should be unlikely.  However, if
  // it's used in the future for more performance-sensitive cases such as
  // conservative GC implementation then it may become important to minimize
  // the overhead of this work in a wider variety of situations.
  //
  // The first pass of this function will be O(n) in the number of threads.
  // The second pass will be O(n^2) in the number of threads.  However, note
  // that it's not safe to short-circuit that second pass in the common case
  // by simply noting that the number of threads is the same as observed in
  // the first pass, because it could be that some threads observed and
  // suspended in the first pass died but new ones were created that haven't
  // been observed and suspended yet.  Again, since the usual use of this
  // facility is at shutdown-time it's expected that there will not be an
  // inordinate number of threads still live at that point in a program.
  // However if that turns out not to be a safe enough presumption in
  // practice, this could be optimized with a less trivial data structure.
  // The implementation constraints here (not using normal allocators and
  // non-fatal recovery from allocation failures) preclude using any
  // conveniently-available data structure implementations.
  //
  // If this path is truly performance sensitive then the best solution would
  // be a new "suspend all threads but me" facility in the kernel, which can
  // straightforwardly use internal synchronization to implement a one-pass
  // solution that's O(n) in the number of threads with no need to mitigate
  // race conditions.
  zx_status_t SuspendNewThreads(RelocatingPageAllocatedVector<Thread>* threads, bool* any) {
    *any = false;
    for (const zx_koid_t koid : koids_) {
      if (koid != this_thread_koid_ &&
          std::none_of(threads->begin(), threads->end(),
                       [koid](const Thread& t) { return t.koid == koid; })) {
        Thread t{koid, {}, {}};
        zx_status_t status =
            process()->get_child(koid, ZX_RIGHT_READ | ZX_RIGHT_WRITE | ZX_RIGHT_WAIT, &t.thread);
        if (status == ZX_ERR_NOT_FOUND) {
          // The thread must have died in a race.
          continue;
        }
        if (status == ZX_OK) {
          status = t.thread.suspend(&t.token);
          if (status == ZX_ERR_BAD_STATE) {
            // The thread is already dying.
            continue;
          }
        }
        if (status == ZX_OK) {
          status = threads->reserve_some_more();
        }
        if (status != ZX_OK) {
          return status;
        }
        threads->push_back(std::move(t));
        *any = true;
      }
    }
    return ZX_OK;
  }
};

class MemorySnapshot {
 public:
  MemorySnapshot() = delete;
  MemorySnapshot(void (*done)(zx_status_t, void*), void* arg)
      : done_callback_(done), callback_arg_(arg) {}

  ~MemorySnapshot() {
    if (done_callback_) {
      done_callback_(status_, callback_arg_);
    }
  }

  bool Ok() const { return status_ == ZX_OK; }

  void SuspendThreads() { status_ = ThreadSuspender().Collect(&threads_); }

  void ReportGlobals(sanitizer_memory_snapshot_callback_t* callback) {
    _dl_locked_report_globals(callback, callback_arg_);
  }

  void ReportThreads(sanitizer_memory_snapshot_callback_t* stacks,
                     sanitizer_memory_snapshot_callback_t* regs,
                     sanitizer_memory_snapshot_callback_t* tls) {
    for (const auto& t : threads_) {
      if (t.koid != ZX_KOID_INVALID) {
        ReportThread(t, stacks, regs, tls);
      }
    }
    if (tls) {
      ReportJoinValues(tls);
    }
  }

  void ReportTcb(pthread* tcb, uintptr_t thread_sp,
                 sanitizer_memory_snapshot_callback_t* stacks_callback,
                 sanitizer_memory_snapshot_callback_t* tls_callback) {
    if (stacks_callback) {
      ReportStack(tcb->safe_stack, thread_sp, stacks_callback);
      ReportStack(tcb->unsafe_stack, tcb->abi.unsafe_sp, stacks_callback);
      // The shadow call stack never contains pointers to mutable data,
      // so there is no reason to report its contents.
    }
    if (tls_callback) {
      ReportTls(tcb, tls_callback);
    }
  }

 private:
  RelocatingPageAllocatedVector<Thread> threads_;
  void (*done_callback_)(zx_status_t, void*);
  void* callback_arg_;
  zx_status_t status_ = ZX_OK;

#if defined(__aarch64__)
  static constexpr auto kSpReg = &zx_thread_state_general_regs_t::sp;
  static constexpr auto kThreadReg = &zx_thread_state_general_regs_t::tpidr;
#elif defined(__x86_64__)
  static constexpr auto kSpReg = &zx_thread_state_general_regs_t::rsp;
  static constexpr auto kThreadReg = &zx_thread_state_general_regs_t::fs_base;
#else
#error "what machine?"
#endif

  void ReportThread(const Thread& t, sanitizer_memory_snapshot_callback_t* stacks_callback,
                    sanitizer_memory_snapshot_callback_t* regs_callback,
                    sanitizer_memory_snapshot_callback_t* tls_callback) {
    // Collect register data, which is needed to find stack and TLS locations.
    zx_thread_state_general_regs_t regs;
    zx_status_t status = t.thread.read_state(ZX_THREAD_STATE_GENERAL_REGS, &regs, sizeof(regs));
    if (status != ZX_OK) {
      return;
    }

    if (regs_callback) {
      // Report the register data.
      regs_callback(&regs, sizeof(regs), callback_arg_);
    }

    if (stacks_callback || tls_callback) {
      // Find the TCB to determine the TLS and stack regions.
      if (auto tcb = FindValidTcb(regs.*kThreadReg)) {
        ReportTcb(tcb, regs.*kSpReg, stacks_callback, tls_callback);
      }
    }
  }

  void ReportStack(const iovec& stack, uintptr_t sp,
                   sanitizer_memory_snapshot_callback_t* callback) {
    uintptr_t base = reinterpret_cast<uintptr_t>(stack.iov_base);
    uintptr_t limit = base + stack.iov_len;
    // If the current SP is not woefully misaligned and falls within the
    // expected bounds, so just report the currently active range.  Otherwise
    // assume the thread is off on some other special stack and the whole
    // thread stack might actually be in use when it gets back to it.
    if (sp % sizeof(uintptr_t) == 0 && sp >= base && sp <= limit) {
      // Stacks grow downwards.
      base = sp;
    }
    callback(reinterpret_cast<void*>(base), limit - base, callback_arg_);
  }

  void ReportTls(pthread* tcb, sanitizer_memory_snapshot_callback_t* callback) {
    if (tcb->tsd_used) {
      // Report all tss_set (aka pthread_setspecific) values.
      callback(tcb->tsd, sizeof(tcb->tsd), callback_arg_);
    }

    // Report the handful of particular pointers stashed in the TCB itself.
    // For a thread just starting or in the middle of exiting, the start_arg
    // and result values might not appear anywhere else and those might hold
    // pointers.  The others are literal cached malloc allocations.
    void* ptrs[] = {
        tcb->start_arg,
        tcb->locale,
        tcb->dlerror_buf,
    };
    callback(ptrs, sizeof(ptrs), callback_arg_);

    // Report each DTV element with its segment's precise address range.
    const size_t gen = (size_t)tcb->head.dtv[0];
    size_t modid = 0;
    for (auto* mod = __libc.tls_head; mod && ++modid <= gen; mod = mod->next) {
      callback(tcb->head.dtv[modid], mod->size, callback_arg_);
    }
  }

  // For dead threads awaiting pthread_join, report the return values.  Rather
  // than a costly check for whether the TCB was found with a live thread,
  // just report all threads' join values here and not in ReportTls (above).
  void ReportJoinValues(sanitizer_memory_snapshot_callback_t* callback) {
    // Don't hold the lock during callbacks.  It should be safe to pretend
    // it's locked assuming the callback doesn't create or join threads.
    // ScopedThreadList's destructor releases the lock after the copy.
    LockedThreadList all_threads = ScopedThreadList();
    for (auto tcb : all_threads) {
      callback(&tcb->result, sizeof(tcb->result), callback_arg_);
    }
  }

  pthread* FindValidTcb(uintptr_t tp) {
    // In a race with a freshly-created thread setting up its thread
    // pointer, it might still be zero.
    if (tp == 0) {
      return nullptr;
    }

    // Compute the TCB pointer from the thread pointer.
    const auto tcb = tp_to_pthread(reinterpret_cast<void*>(tp));

    // Verify that it's one of the live threads.  If it's not there this
    // could be a thread not created by libc, or a detached thread that got
    // suspended while exiting (so its TCB has already been unmapped, but
    // the thread pointer wasn't cleared).  In either case we can't safely
    // use the pointer since it might be bogus or point to a data structure
    // we don't grok.  So no TCB-based information (TLS, stack bounds) can
    // be discovered and reported.
    ScopedThreadList all_threads;
    auto it = std::find(all_threads.begin(), all_threads.end(), tcb);
    return it == all_threads.end() ? nullptr : *it;
  }
};

auto CurrentThreadRegs() {
  zx_thread_state_general_regs_t regs;
#if defined(__aarch64__)
  __asm__ volatile(
      "stp x0, x1, [%1, #(8 * 0)]\n"
      "stp x2, x3, [%1, #(8 * 2)]\n"
      "stp x4, x5, [%1, #(8 * 4)]\n"
      "stp x6, x7, [%1, #(8 * 6)]\n"
      "stp x8, x9, [%1, #(8 * 8)]\n"
      "stp x10, x11, [%1, #(8 * 10)]\n"
      "stp x12, x13, [%1, #(8 * 12)]\n"
      "stp x14, x15, [%1, #(8 * 14)]\n"
      "stp x16, x17, [%1, #(8 * 16)]\n"
      "stp x18, x19, [%1, #(8 * 18)]\n"
      "stp x20, x21, [%1, #(8 * 20)]\n"
      "stp x22, x23, [%1, #(8 * 22)]\n"
      "stp x24, x25, [%1, #(8 * 24)]\n"
      "stp x26, x27, [%1, #(8 * 26)]\n"
      "stp x28, x29, [%1, #(8 * 28)]\n"
      : "=m"(regs)
      : "r"(&regs.r[0]));
  regs.lr = regs.pc = reinterpret_cast<uintptr_t>(__builtin_return_address(0));
  regs.sp = reinterpret_cast<uintptr_t>(__builtin_frame_address(0));
  __asm__("mrs %0, nzcv" : "=r"(regs.cpsr));
  __asm__("mrs %0, tpidr_el0" : "=r"(regs.tpidr));
#elif defined(__x86_64__)
  __asm__ volatile(
      "mov %%rax, %[rax]\n"
      "mov %%rbx, %[rbx]\n"
      "mov %%rcx, %[rcx]\n"
      "mov %%rdx, %[rdx]\n"
      "mov %%rsi, %[rsi]\n"
      "mov %%rdi, %[rdi]\n"
      "mov %%rbp, %[rbp]\n"
      "mov %%rsp, %[rsp]\n"
      "mov %%rsp, %[rsp]\n"
      "mov %%r8, %[r8]\n"
      "mov %%r9, %[r9]\n"
      "mov %%r10, %[r10]\n"
      "mov %%r11, %[r11]\n"
      "mov %%r12, %[r12]\n"
      "mov %%r13, %[r13]\n"
      "mov %%r14, %[r14]\n"
      "mov %%r15, %[r15]\n"
      : [ rax ] "=m"(regs.rax), [ rbx ] "=m"(regs.rbx), [ rcx ] "=m"(regs.rcx),
        [ rdx ] "=m"(regs.rdx), [ rsi ] "=m"(regs.rsi), [ rdi ] "=m"(regs.rdi),
        [ rbp ] "=m"(regs.rbp), [ rsp ] "=m"(regs.rsp), [ r8 ] "=m"(regs.r8), [ r9 ] "=m"(regs.r9),
        [ r10 ] "=m"(regs.r10), [ r11 ] "=m"(regs.r11), [ r12 ] "=m"(regs.r12),
        [ r13 ] "=m"(regs.r13), [ r14 ] "=m"(regs.r14), [ r15 ] "=m"(regs.r15));
  __asm__(
      "pushf\n"
      ".cfi_adjust_cfa_offset 8\n"
      "pop %0\n"
      ".cfi_adjust_cfa_offset -8\n"
      : "=r"(regs.rflags));
  // Proxy for fs.base since rdfsbase isn't always available.
  __asm__("mov %%fs:0, %0" : "=r"(regs.fs_base));
  regs.gs_base = 0;  // Don't even try for gs.base.
#else
#error "what machine?"
#endif
  return regs;
}

}  // namespace

__EXPORT
void __sanitizer_memory_snapshot(sanitizer_memory_snapshot_callback_t* globals,
                                 sanitizer_memory_snapshot_callback_t* stacks,
                                 sanitizer_memory_snapshot_callback_t* regs,
                                 sanitizer_memory_snapshot_callback_t* tls,
                                 void (*done)(zx_status_t, void*), void* arg) {
  // The only real reason to capture the registers this early is for the
  // test case that tries to use a register it hopes won't be touched.
  // This is the first thing after the test sets that register, and the
  // volatile on the asms should prevent hoisting down into the if below.
  auto regdata = CurrentThreadRegs();

  MemorySnapshot snapshot(done, arg);
  snapshot.SuspendThreads();
  if (snapshot.Ok() && globals) {
    snapshot.ReportGlobals(globals);
  }
  if (snapshot.Ok() && (stacks || regs || tls)) {
    // Use the boundary of this call frame itself as the stack bound, since it
    // shouldn't contain any interesting pointers.
    auto sp = reinterpret_cast<uintptr_t>(__builtin_frame_address(0));
    snapshot.ReportTcb(__pthread_self(), sp, stacks, tls);
    if (regs) {
      // Report the register data.
      regs(&regdata, sizeof(regdata), arg);
    }
    snapshot.ReportThreads(stacks, regs, tls);
  }
}
