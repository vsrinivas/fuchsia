// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#define TARGET_ARCH_X64 1

#include "thread_interrupter.h"

#include <assert.h>
#include <stdarg.h>

#include <zircon/process.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/debug.h>
#include <zircon/syscalls/object.h>
#include <zircon/types.h>

static void print_error(const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  fprintf(stderr, "ERROR: ");
  vfprintf(stderr, fmt, args);
  fprintf(stderr, "\n");
  va_end(args);
}

static void print_zx_error(zx_status_t status, const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  fprintf(stderr, "ERROR: ");
  vfprintf(stderr, fmt, args);
  fprintf(stderr, ": %d(%s)", status, zx_status_get_string(status));
  fprintf(stderr, "\n");
  va_end(args);
}

// TODO(ZX-430): Currently, CPU profiling for Fuchsia is arranged very similarly
// to our Windows profiling. That is, the interrupter thread iterates over
// all threads, suspends them, samples various things, and then resumes them.
// When ZX-430 is resolved, the code below should be rewritten to use whatever
// feature is added for it.

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
      print_zx_error(status, "ThreadInterrupter: zx_task_suspend failed");
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

class ThreadInterrupterFuchsia {
 public:
#if defined(__x86_64__)
  static bool GrabRegisters(zx_handle_t thread, InterruptedThreadState* state) {
    zx_thread_state_general_regs regs;
    zx_status_t status = zx_thread_read_state(
        thread, ZX_THREAD_STATE_GENERAL_REGS, &regs, sizeof(regs));
    if (status != ZX_OK) {
      print_zx_error(status, "ThreadInterrupter: failed to get registers");
      return false;
    }
    state->pc = static_cast<uintptr_t>(regs.rip);
    state->fp = static_cast<uintptr_t>(regs.rbp);
    return true;
  }
#elif defined(__aarch64__)
  static bool GrabRegisters(zx_handle_t thread, InterruptedThreadState* state) {
    zx_thread_state_general_regs regs;
    zx_status_t status = zx_thread_read_state(
        thread, ZX_THREAD_STATE_GENERAL_REGS, &regs, sizeof(regs));
    if (status != ZX_OK) {
      print_zx_error(status, "ThreadInterrupter: failed to get registers");
      return false;
    }
    state->pc = static_cast<uintptr_t>(regs.pc);
    state->fp = static_cast<uintptr_t>(regs.r[29]);
    return true;
  }
#else
#error "Unsupported architecture"
#endif

  static void Interrupt(zx_handle_t thread) {
    assert(thread != ZX_HANDLE_INVALID);
    zx_status_t status;

    // Get a handle on the target thread.
    zx_handle_t target_thread;
    status = zx_object_get_child(zx_process_self(), thread,
                                 ZX_RIGHT_SAME_RIGHTS, &target_thread);
    if (status != ZX_OK) {
      print_zx_error(status, "ThreadInterrupter: zx_object_get_child failed");
      return;
    }
    if (target_thread == ZX_HANDLE_INVALID) {
      print_error(
          "ThreadInterrupter: zx_object_get_child gave an invalid "
          "thread handle");
      return;
    }

    // This scope suspends the thread. When we exit the scope, the thread is
    // resumed, and the thread handle is closed.
    ThreadSuspendScope tss(target_thread);
    if (!tss.suspended()) {
      return;
    }

    // Check that the thread is suspended.
    status = PollThreadUntilSuspended(target_thread);
    if (status != ZX_OK) {
      return;
    }

    // Grab the target thread's registers.
    InterruptedThreadState its;
    if (!GrabRegisters(target_thread, &its)) {
      return;
    }

    // Currently we sample only threads that are associated
    // with an isolate. It is safe to call 'os_thread->thread()'
    // here as the thread which is being queried is suspended.
  }

 private:
  static const char* ThreadStateGetString(uint32_t state) {
// TODO(dje): This #ifdef is temporary to handle the transition.
// It can be deleted once the new version of zircon rolls out.
#ifdef ZX_THREAD_STATE_BASIC
    state = ZX_THREAD_STATE_BASIC(state);
#endif
    switch (state) {
      case ZX_THREAD_STATE_NEW:
        return "ZX_THREAD_STATE_NEW";
      case ZX_THREAD_STATE_RUNNING:
        return "ZX_THREAD_STATE_RUNNING";
      case ZX_THREAD_STATE_SUSPENDED:
        return "ZX_THREAD_STATE_SUSPENDED";
      case ZX_THREAD_STATE_BLOCKED:
        return "ZX_THREAD_STATE_BLOCKED";
      case ZX_THREAD_STATE_DYING:
        return "ZX_THREAD_STATE_DYING";
      case ZX_THREAD_STATE_DEAD:
        return "ZX_THREAD_STATE_DEAD";
      default:
        return "<Unknown>";
    }
  }

  static zx_status_t PollThreadUntilSuspended(zx_handle_t thread_handle) {
    const intptr_t kMaxPollAttempts = 10;
    intptr_t poll_tries = 0;
    while (poll_tries < kMaxPollAttempts) {
      zx_info_thread_t thread_info;
      zx_status_t status =
          zx_object_get_info(thread_handle, ZX_INFO_THREAD, &thread_info,
                             sizeof(thread_info), NULL, NULL);
      poll_tries++;
      if (status != ZX_OK) {
        fprintf(stderr, "ThreadInterrupter: zx_object_get_info failed: %s\n",
                zx_status_get_string(status));
        return status;
      }
      if (thread_info.state == ZX_THREAD_STATE_SUSPENDED) {
        // Success.
        return ZX_OK;
      }
      if (thread_info.state == ZX_THREAD_STATE_RUNNING) {
        // Poll.
        continue;
      }
      fprintf(stderr, "ThreadInterrupter: Thread is not suspended: %s\n",
              ThreadStateGetString(thread_info.state));
      return ZX_ERR_BAD_STATE;
    }
    fprintf(stderr, "ThreadInterrupter: Exceeded max suspend poll tries\n");
    return ZX_ERR_BAD_STATE;
  }
};
