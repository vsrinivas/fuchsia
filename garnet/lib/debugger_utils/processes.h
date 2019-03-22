// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_DEBUGGER_UTILS_PROCESSES_H_
#define GARNET_LIB_DEBUGGER_UTILS_PROCESSES_H_

#include <stddef.h>
#include <vector>

#include <zircon/types.h>

#include <lib/zx/process.h>
#include <lib/zx/thread.h>

namespace debugger_utils {

// Return the set of all threads of |process| in |*out_threads|.
// New threads may be created while we're trying to obtain the list.
// |try_count| specifies how many attempts to collect all threads.
// To help minimize the number of iterations required to collect all threads
// |max_num_new_threads| is added to the expected number of threads for each
// iteration. A value of 10 is plenty: How many threads are usually created in
// between two successive calls to zx_object_get_info(ZX_INFO_PROCESS_THREADS)?
// The first call collects the current number of threads and the second call
// collects their koids after space is allocated to hold them.
// The parameter is present in the API to give the client control.
//
// On return, if the result is not ZX_OK then the contents of |*out_threads|
// and |*out_num_available_threads| are unchanged.
// If the result is ZX_OK, then |*out_threads| is filled in with the set of
// threads, and |*out_num_available_threads| is set to the number of
// available threads after the last iteration. Note that it may be more than
// the number of recorded threads, but it will never be less than that.
//
// TODO(ZX-3687): This is a heuristic and thus suboptimal. It is for cases
// where the caller cannot or does not want to first suspend the process,
// which is currently the only non-racy way to build the list of all the
// threads.
zx_status_t GetProcessThreadKoids(const zx_handle_t process, size_t try_count,
                                  size_t max_num_new_threads,
                                  std::vector<zx_koid_t>* out_threads,
                                  size_t* out_num_available_threads);
zx_status_t GetProcessThreadKoids(const zx::process& process, size_t try_count,
                                  size_t max_num_new_threads,
                                  std::vector<zx_koid_t>* out_threads,
                                  size_t* out_num_available_threads);

// Helper function for testing purposes.
zx_status_t TryGetProcessThreadKoidsForTesting(
    const zx::process& process, size_t try_count, size_t initial_num_threads,
    size_t max_num_new_threads, std::vector<zx_koid_t>* out_threads,
    size_t* out_num_available_threads);

}  // namespace debugger_utils

#endif  // GARNET_LIB_DEBUGGER_UTILS_PROCESSES_H_
