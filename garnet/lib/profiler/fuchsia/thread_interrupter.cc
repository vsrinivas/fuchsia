// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "thread_interrupter.h"

#include <fbl/function.h>
#include <lib/async/cpp/task.h>
#include <src/lib/fxl/logging.h>
#include <stdarg.h>
#include <zircon/process.h>
#include <zircon/status.h>
#include <zircon/syscalls/debug.h>
#include <lib/syslog/global.h>

#define print_error(...)                                                    \
    do {                                                                    \
        fx_logger_t* logger = fx_log_get_logger();                          \
        if (logger && fx_logger_get_min_severity(logger) <= FX_LOG_ERROR) { \
            fx_logger_logf(logger, (FX_LOG_ERROR), nullptr,                 \
                           __VA_ARGS__);                                    \
        }                                                                   \
    } while (0)

#define log_zx_error(status, ...)                                           \
    do {                                                                    \
        fx_logger_t* logger = fx_log_get_logger();                          \
        if (logger && fx_logger_get_min_severity(logger) <= FX_LOG_ERROR) { \
            fx_logger_logf(logger, (FX_LOG_ERROR), nullptr,                 \
                           "%d(%s)" __VA_ARGS__,                            \
                           status, zx_status_get_string(status));           \
        }                                                                   \
    } while (0)


static zx_koid_t get_koid(zx_handle_t thread_handle) {
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

bool ThreadInterrupter::initialized_ = false;
bool ThreadInterrupter::shutdown_ = false;
bool ThreadInterrupter::thread_running_ = false;
bool ThreadInterrupter::woken_up_ = false;
intptr_t ThreadInterrupter::interrupt_period_ = 1000;   // msec
async::Loop* ThreadInterrupter::loop_ = nullptr;
CpuProfiler* ThreadInterrupter::profiler_ = nullptr;
HandlerCallback ThreadInterrupter::callback_ = nullptr;

void ThreadInterrupter::InitOnce(CpuProfiler* profiler) {
  assert(!initialized_);
  profiler_ = profiler;
  initialized_ = true;
}

void ThreadInterrupter::Startup() {
  assert(initialized_);
  loop_ = new async::Loop(&kAsyncLoopConfigNoAttachToThread);
  loop_->StartThread();
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
  shutdown_ = false;

  async::PostTask(loop_->dispatcher(), [] {
    ThreadInterrupt();
  });
}

void ThreadInterrupter::UnregisterHandler() {
  callback_ = nullptr;
  Shutdown();
}

void ThreadInterrupter::ThreadSnapshot(zx_handle_t thread) {
  HandlerCallback callback = callback_;
  if (callback) {
    (*callback)(thread, profiler_);
  }
}

// A scope within which a target thread is suspended. When the scope is exited,
// the thread is resumed and its handle is closed.
class ThreadSuspendScope {
 public:
  explicit ThreadSuspendScope(zx_handle_t thread_handle)
      : thread_handle_(thread_handle), suspend_token_(ZX_HANDLE_INVALID) {
    zx_status_t status = zx_task_suspend_token(thread_handle, &suspend_token_);
    // If a thread is somewhere where suspend is impossible, zx_task_suspend()
    // can return ZX_ERR_NOT_SUPPORTED.
    if (status != ZX_OK) {
      print_error("ThreadInterrupter: zx_task_suspend failed: %s\n",
                   zx_status_get_string(status));
    }
  }

  ~ThreadSuspendScope() {
    if (suspend_token_ != ZX_HANDLE_INVALID) {
      zx_handle_close(suspend_token_);
    }
    zx_handle_close(thread_handle_);
  }

  bool suspended() const { return suspend_token_ != ZX_HANDLE_INVALID; }

 private:
  zx_handle_t thread_handle_;
  zx_handle_t suspend_token_;  // ZX_HANDLE_INVALID when not suspended.
};

void ThreadInterrupter::ThreadInterrupt() {
  assert(initialized_);

  zx_koid_t tid_self = get_koid(zx_thread_self());

  while (!shutdown_) {
    // Sleep N milliseconds
    zx_nanosleep(zx_deadline_after(ZX_MSEC(interrupt_period_)));

    if (shutdown_) {
      break;
    }

    zx_handle_t process_handle = zx_process_self();
    if (process_handle == ZX_HANDLE_INVALID) {
      print_error("failed to get process handle");
      break;  // Too broken to continue.
    }

    size_t num_threads;
    zx_status_t status = zx_object_get_info(process_handle, ZX_INFO_PROCESS_THREADS,
                                            nullptr, 0, nullptr, &num_threads);
    if (status != ZX_OK) {
      print_error("failed to get process thread info (#threads)");
      break;  // Too broken to continue.
    }
    if (num_threads < 1) {
      print_error("failed to get sane number of threads");
      break;  // Too broken to continue.
    }

    // TODO: this is dangerous
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

    zx_koid_t pid = get_koid(zx_process_self());

    for (size_t i = 0; i < num_threads; ++i) {
      zx_koid_t tid = threads[i];

      if (tid == tid_self) {
        continue;
      }

      zx_handle_t thread;
      status = zx_object_get_child(process_handle, tid, ZX_RIGHT_SAME_RIGHTS,
                                   &thread);
      if (status != ZX_OK) {
        log_zx_error(status, "failed to get a handle to [%ld.%ld]", pid, tid);
        continue;  // Skip this thread.
      }

      zx_info_thread_t thread_info;
      status = zx_object_get_info(thread, ZX_INFO_THREAD, &thread_info, sizeof(thread_info), NULL, NULL);
      if (status != ZX_OK) {
        log_zx_error(status, "unable to get thread info, skipping");
        continue;  // Skip this thread.
      }

      if (thread_info.state != ZX_THREAD_STATE_RUNNING) {
        // Skip blocked threads, they don't count as work...
        zx_handle_close(thread);
        continue;
      }

      // This scope suspends the thread. When we exit the scope, the thread is
      // resumed, and the thread handle is closed.
      ThreadSuspendScope tss(thread);
      if (!tss.suspended()) {
        return;
      }

      // Currently, asking to wait for suspended means only waiting for the
      // thread to suspend. If the thread terminates instead this will wait
      // forever (or until the timeout). Thus we need to explicitly wait for
      // ZX_THREAD_TERMINATED too.

      zx_signals_t signals = ZX_THREAD_SUSPENDED | ZX_THREAD_TERMINATED;
      zx_signals_t observed = 0;
      zx_time_t deadline = zx_deadline_after(ZX_MSEC(100));
      status = zx_object_wait_one(thread, signals, deadline, &observed);

      if (status != ZX_OK) {
        log_zx_error(status,
                     "failure waiting for thread %ld.%ld to suspend, skipping",
                     pid, tid);
        continue;  // Skip this thread.
      }

      if (observed & ZX_THREAD_TERMINATED) {
        print_error("unable to backtrace of thread [%ld.%ld]: terminated", pid, tid);
        continue;  // Skip this thread.
      }

      ThreadSnapshot(thread);
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
