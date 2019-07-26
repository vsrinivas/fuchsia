// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_DEBUGGER_UTILS_THREADS_H_
#define GARNET_LIB_DEBUGGER_UTILS_THREADS_H_

#include <functional>
#include <string>
#include <vector>

#include <lib/zx/job.h>
#include <lib/zx/thread.h>
#include <lib/zx/time.h>

namespace debugger_utils {

using WithThreadSuspendedFunction = std::function<zx_status_t(const zx::thread& thread)>;

// Return the thread's current state according to the o/s.
// The result is one if ZX_THREAD_STATE_*.
uint32_t GetThreadOsState(zx_handle_t thread);

// Return the thread's current state according to the o/s.
// The result is one if ZX_THREAD_STATE_*.
uint32_t GetThreadOsState(const zx::thread& thread);

// Return the name of ZX_THREAD_STATE_* value |state|.
// Returns nullptr if |state| is invalid.
const char* ThreadOsStateName(uint32_t state);

// Return the name of ZX_THREAD_STATE_* value |state|.
// Returns UNKNOWN(value) if |state| is invalid.
const std::string ThreadOsStateNameAsString(uint32_t state);

// Suspend |thread| and then run |function|.
// If waiting for |thread| to suspend times out then |function| is not called,
// and the result is ZX_ERR_TIMED_OUT.
// If the thread terminates while waiting for it to suspend then the result
// is ZX_ERR_NOT_FOUND.
// Otherwise the result is the result of |function|.
zx_status_t WithThreadSuspended(const zx::thread& thread, zx::duration thread_suspend_timeout,
                                const WithThreadSuspendedFunction& function);

// Suspend all |threads|, run |function| on each one in order, and then
// resume them. If any call to |function| returns !ZX_OK then the iteration
// stops immediately and the result of this function is that status.
// If any thread terminates while waiting for it to suspend then that thread
// is ignored but the other threads are processed.
// If waiting for a thread to suspend times out then |function| is not called
// on any thread, and the result is ZX_ERR_TIMED_OUT.
// Otherwise the result is ZX_OK.
zx_status_t WithAllThreadsSuspended(const std::vector<zx::thread>& threads,
                                    zx::duration thread_suspend_timeout,
                                    const WithThreadSuspendedFunction& function);

// Print a dump of |thread| suitable for use with the symbolizer.
// If |in_exception| is true then the thread is in an exception and extra
// information related to the exception is printed.
// If |in_exception| is false then the thread must be suspended.
void DumpThread(zx_handle_t process, zx_handle_t thread, bool in_exception);

}  // namespace debugger_utils

#endif  // GARNET_LIB_DEBUGGER_UTILS_THREADS_H_
