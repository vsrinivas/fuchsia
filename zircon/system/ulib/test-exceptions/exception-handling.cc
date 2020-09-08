// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/test-exceptions/exception-handling.h>
#include <lib/zx/thread.h>
#include <threads.h>
#include <zircon/syscalls/debug.h>

#include <thread>

namespace test_exceptions {

namespace {

// Extracts the thread from |exception| and causes it to exit.
zx_status_t ExitExceptionThread(zx::exception exception, uintptr_t task_exit_fn) {
  zx::thread thread;
  zx_status_t status = exception.get_thread(&thread);
  if (status != ZX_OK) {
    return status;
  }

  // Set the thread's registers to `task_exit_fn`.
  zx_thread_state_general_regs_t regs;
  status = thread.read_state(ZX_THREAD_STATE_GENERAL_REGS, &regs, sizeof(regs));
  if (status != ZX_OK) {
    return status;
  }

#if defined(__aarch64__)
  regs.pc = task_exit_fn;
#elif defined(__x86_64__)
  regs.rip = task_exit_fn;
#else
#error "what machine?"
#endif

  status = thread.write_state(ZX_THREAD_STATE_GENERAL_REGS, &regs, sizeof(regs));
  if (status != ZX_OK) {
    return status;
  }

  // Clear the exception so the thread continues.
  uint32_t state = ZX_EXCEPTION_STATE_HANDLED;
  status = exception.set_property(ZX_PROP_EXCEPTION_STATE, &state, sizeof(state));
  if (status != ZX_OK) {
    return status;
  }
  exception.reset();

  // Wait until the thread exits.
  return thread.wait_one(ZX_THREAD_TERMINATED, zx::time::infinite(), nullptr);
}

_Noreturn __NO_SAFESTACK void exception_pthread_exit() { pthread_exit(nullptr); }

_Noreturn __NO_SAFESTACK void exception_thrd_exit() { thrd_exit(0); }

}  // namespace

__EXPORT zx_status_t ExitExceptionZxThread(zx::exception exception) {
  return ExitExceptionThread(std::move(exception), reinterpret_cast<uintptr_t>(zx_thread_exit));
}

__EXPORT zx_status_t ExitExceptionCThread(zx::exception exception) {
  return ExitExceptionThread(std::move(exception),
                             reinterpret_cast<uintptr_t>(exception_thrd_exit));
}

__EXPORT zx_status_t ExitExceptionPThread(zx::exception exception) {
  return ExitExceptionThread(std::move(exception),
                             reinterpret_cast<uintptr_t>(exception_pthread_exit));
}

}  // namespace test_exceptions
