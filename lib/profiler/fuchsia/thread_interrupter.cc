// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "thread_interrupter.h"

#include <fbl/function.h>
#include <inttypes.h>
#include <lib/async/cpp/task.h>
#include <lib/fxl/logging.h>
#include <stdarg.h>
#include <zircon/process.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/debug.h>
#include <zircon/types.h>

static zx_koid_t handle_koid(zx_handle_t thread_handle) {
  zx_status_t status;
  zx_info_handle_basic_t info_handle_basic;
  status = zx_object_get_info(thread_handle, ZX_INFO_HANDLE_BASIC,
                              &info_handle_basic, sizeof(info_handle_basic),
                              nullptr, nullptr);
  if (status != ZX_OK) {
    return ZX_KOID_INVALID;
  }
  return info_handle_basic.koid;
}

static void print_error(const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  fprintf(stderr, "ERROR: ");
  vfprintf(stderr, fmt, args);
  fprintf(stderr, "\n");
  va_end(args);
}

static void log_zx_error(zx_status_t status, const char* fmt, ...) {
  char buffer[4096];  // UGH! HACK!
  int n;
  va_list args;
  va_start(args, fmt);
  n = sprintf(buffer, "ERROR: ");
  n = vsprintf(&buffer[n], fmt, args);
  n = sprintf(&buffer[n], ": %d(%s)", status, zx_status_get_string(status));
  sprintf(&buffer[n], "\n");
  va_end(args);
}

// Notes:
//
// The ThreadInterrupter interrupts all threads actively running isolates once
// per interrupt period (default is 1 millisecond). While the thread is
// interrupted, the thread's interrupt callback is invoked. Callbacks cannot
// rely on being executed on the interrupted thread.
//
// There are two mechanisms used to interrupt a thread. The first, used on OSs
// with pthreads (Android, Linux, and Mac), is thread specific signal delivery.
// The second, used on Windows, is explicit suspend and resume thread system
// calls. Signal delivery forbids taking locks and allocating memory (which
// takes a lock). Explicit suspend and resume means that the interrupt callback
// will not be executing on the interrupted thread, making it meaningless to
// access TLS from within the thread interrupt callback. Combining these
// limitations, thread interrupt callbacks are forbidden from:
//
//   * Accessing TLS.
//   * Allocating memory.
//   * Taking a lock.
//
// The ThreadInterrupter has a single monitor (monitor_). This monitor is used
// to synchronize startup, shutdown, and waking up from a deep sleep.
//
// A thread can only register and unregister itself. Each thread has a heap
// allocated ThreadState. A thread's ThreadState is lazily allocated the first
// time the thread is registered. A pointer to a thread's ThreadState is stored
// in the list of threads registered to receive interrupts (threads_) and in
// thread local storage. When a thread's ThreadState is being modified, the
// thread local storage pointer is temporarily set to NULL while the
// modification is occurring. After the ThreadState has been updated, the
// thread local storage pointer is set again. This has an important side
// effect: if the thread is interrupted by a signal handler during a ThreadState
// update the signal handler will immediately return.

bool ThreadInterrupter::initialized_ = false;
bool ThreadInterrupter::shutdown_ = false;
bool ThreadInterrupter::thread_running_ = false;
bool ThreadInterrupter::woken_up_ = false;
intptr_t ThreadInterrupter::interrupt_period_ = 1000;   // msec
intptr_t ThreadInterrupter::current_wait_time_ = 1000;  // msec
async::Loop* ThreadInterrupter::loop_ = nullptr;
CpuProfiler* ThreadInterrupter::profiler_ = nullptr;
HandlerCallback ThreadInterrupter::callback_ = nullptr;

void ThreadInterrupter::InitOnce(CpuProfiler* profiler) {
  assert(!initialized_);
  profiler_ = profiler;
  initialized_ = true;
}

void ThreadInterrupter::LoopFunc() {
  ThreadInterrupt();
  async::PostDelayedTask(loop_->dispatcher(), LoopFunc,
                         (zx::duration)interrupt_period_);
}

void ThreadInterrupter::Startup() {
  loop_ = new async::Loop(&kAsyncLoopConfigNoAttachToThread);
  loop_->StartThread();
  async::PostTask(loop_->dispatcher(), LoopFunc);
}

void ThreadInterrupter::Shutdown() {
  if (shutdown_) {
    // Already shutdown.
    return;
  }
  shutdown_ = true;
  assert(initialized_);
}

// Delay between interrupts.
void ThreadInterrupter::SetInterruptPeriod(intptr_t period) {
  if (shutdown_) {
    return;
  }
  assert(initialized_);
  assert(period > 0);
  interrupt_period_ = (zx_duration_t)period;
}

void ThreadInterrupter::RegisterHandler(HandlerCallback callback) {
  callback_ = callback;
}

void ThreadInterrupter::UnregisterHandler() { callback_ = nullptr; }

void ThreadInterrupter::ThreadSnapshot(zx_handle_t thread) {
  HandlerCallback callback = callback_;
  if (callback) {
    (*callback)(thread, profiler_);
  }
}

void ThreadInterrupter::ThreadInterrupt() {
  assert(initialized_);

  {
    zx_status_t status;
    zx_koid_t tid_self = handle_koid(zx_thread_self());
    intptr_t interrupted_thread_count = 0;
    current_wait_time_ = interrupt_period_;
    while (!shutdown_) {
      // Sleep N milliseconds
      zx_nanosleep(zx_deadline_after(ZX_MSEC(current_wait_time_)));

      if (shutdown_) {
        break;
      }

      // Reset count before interrupting any threads.
      interrupted_thread_count = 0;

      zx_handle_t process_handle = zx_process_self();
      if (process_handle == ZX_HANDLE_INVALID) {
        print_error("failed to get process handle");
        break;  // Too broken to continue.
      }

      size_t num_threads;
      status = zx_object_get_info(process_handle, ZX_INFO_PROCESS_THREADS,
                                  nullptr, 0, nullptr, &num_threads);
      if (status != ZX_OK) {
        print_error("failed to get process thread info (#threads)");
        break;  // Too broken to continue.
      }
      if (num_threads < 1) {
        print_error("failed to get sane number of threads");
        break;  // Too broken to continue.
      }

      zx_koid_t threads[num_threads];
      size_t records_read;
      status = zx_object_get_info(process_handle, ZX_INFO_PROCESS_THREADS,
                                  threads, num_threads * sizeof(threads[0]),
                                  &records_read, nullptr);
      if (status != ZX_OK) {
        log_zx_error(status, "failed to get process thread info");
        break;  // Too broken to continue.
      }

      if (records_read != num_threads) {
        print_error("records_read != num_threads");
        break;  // Too broken to continue.
      }

      zx_koid_t pid = handle_koid(zx_process_self());

      for (size_t i = 0; i < num_threads; ++i) {
        zx_koid_t tid = threads[i];

        if (tid == tid_self) {
          continue;
        }

        zx_handle_t thread;
        status = zx_object_get_child(process_handle, tid, ZX_RIGHT_SAME_RIGHTS,
                                     &thread);
        if (status != ZX_OK) {
          log_zx_error(status,
                       "failed to get a handle to [%" PRIu64 ".%" PRIu64 "]",
                       pid, tid);
          continue;  // Skip this thread.
        }

        zx_handle_t suspend_token = ZX_HANDLE_INVALID;
        status = zx_task_suspend_token(thread, &suspend_token);
        if (status != ZX_OK) {
          log_zx_error(status, "unable to suspend thread, skipping");
          continue;  // Skip this thread.
        }

        zx_signals_t observed = 0u;

        // Try to be robust and don't wait forever. The timeout is a little
        // high as we want to work well in really loaded systems.
        auto deadline = zx_deadline_after(ZX_SEC(5));

        // Currently, asking to wait for suspended means only waiting for the
        // thread to suspend. If the thread terminates instead this will wait
        // forever (or until the timeout). Thus we need to explicitly wait for
        // ZX_THREAD_TERMINATED too.
        zx_signals_t signals = ZX_THREAD_SUSPENDED | ZX_THREAD_TERMINATED;
        status = zx_object_wait_one(thread, signals, deadline, &observed);
        if (status != ZX_OK) {
          log_zx_error(status,
                       "failure waiting for thread %" PRIu64 ".%" PRIu64
                       " to suspend, skipping",
                       pid, tid);
          continue;  // Skip this thread.
        }
        if (observed & ZX_THREAD_TERMINATED) {
          print_error("unable to backtrace of thread [%" PRIu64 ".%" PRIu64
                      "]: terminated",
                      pid, tid);
          continue;  // Skip this thread.
        }

        ThreadSnapshot(thread);

        zx_handle_close(suspend_token);
        zx_handle_close(thread);
      }
      // TODO: What is Monitor::kNoTimeout? assert(current_wait_time_ !=
      // Monitor::kNoTimeout);
    }
  }
}

bool ThreadInterrupter::GrabRegisters(zx_handle_t thread,
                                      InterruptedThreadState* state) {
  zx_thread_state_general_regs regs;
  zx_status_t status = zx_thread_read_state(
      thread, ZX_THREAD_STATE_GENERAL_REGS, &regs, sizeof(regs));
  if (status != ZX_OK) {
    log_zx_error(status, "ThreadInterrupter: failed to get registers");
    return false;
  }

#if defined(__aarch64__)
  state->pc = static_cast<uintptr_t>(regs.pc);
  state->fp = static_cast<uintptr_t>(regs.r[29]);
#elif defined(__x86_64__)
  state->pc = static_cast<uintptr_t>(regs.rip);
  state->fp = static_cast<uintptr_t>(regs.rbp);
#else
#error "Unsupported Architecture"
#endif

  return true;
}
