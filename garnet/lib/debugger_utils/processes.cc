// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fxl/logging.h>

#include "processes.h"
#include "util.h"

namespace debugger_utils {

// This function has the signature that it does to provide for easy testing
// of handling new threads appearing while we're trying to build the list.
// This is done by having the caller provide the initial expected number of
// threads present in |initial_num_threads|.

static zx_status_t TryGetProcessThreadKoids(
    zx_handle_t process, size_t try_count, size_t initial_num_threads,
    size_t max_num_new_threads, std::vector<zx_koid_t>* out_threads,
    size_t* out_num_available_threads) {
  FXL_DCHECK(process != ZX_HANDLE_INVALID);

  size_t num_available_threads = initial_num_threads;

  for (size_t i = 0; i < try_count; ++i) {
    // Adjust the number to account for new threads created since.
    // TODO(ZX-3687): If a debugging tool is attached to |process| then new
    // threads will get THREAD_STARTING and we'll find out about them that way.
    // We just need a way to have new threads not crowd out older threads in
    // ZX_INFO_PROCESS_THREADS. An alternative for now is to keep looping if
    // the count we use is insufficient.
    size_t num_threads =
        num_available_threads + max_num_new_threads;
    out_threads->resize(num_threads);

    size_t records_read;
    zx_status_t status = zx_object_get_info(
        process, ZX_INFO_PROCESS_THREADS, out_threads->data(),
        out_threads->size() * sizeof((*out_threads)[0]),
        &records_read, &num_available_threads);
    if (status != ZX_OK) {
      FXL_VLOG(2) << "Failed to get process thread info: "
                  << debugger_utils::ZxErrorString(status);
      return status;
    }

    // If our buffer was large enough to hold all threads we're done.
    if (num_available_threads <= num_threads) {
      FXL_DCHECK(records_read == num_available_threads);
      out_threads->resize(num_available_threads);
      break;
    }
    FXL_DCHECK(records_read == num_threads);
  }

  *out_num_available_threads = num_available_threads;
  return ZX_OK;
}

// Exported version for testing purposes.
zx_status_t TryGetProcessThreadKoidsForTesting(
    const zx::process& process, size_t try_count, size_t initial_num_threads,
    size_t max_num_new_threads, std::vector<zx_koid_t>* out_threads,
    size_t* out_num_available_threads) {
  return TryGetProcessThreadKoids(process.get(), try_count, initial_num_threads,
                                  max_num_new_threads, out_threads,
                                  out_num_available_threads);
}

zx_status_t GetProcessThreadKoids(const zx_handle_t process, size_t try_count,
                                  size_t max_num_new_threads,
                                  std::vector<zx_koid_t>* out_threads,
                                  size_t* out_num_available_threads) {
  FXL_DCHECK(process != ZX_HANDLE_INVALID);

  // First get the thread count so that we can allocate an appropriately sized
  // buffer. This is racy but unless the caller stops all threads we need to
  // cope (unless/until a better way to collect all the threads comes along).
  size_t num_available_threads;
  zx_status_t status = zx_object_get_info(process, ZX_INFO_PROCESS_THREADS,
                                          nullptr, 0, nullptr,
                                          &num_available_threads);
  if (status != ZX_OK) {
    FXL_VLOG(2) << "Failed to get process thread info (#threads): "
                << debugger_utils::ZxErrorString(status);
    return status;
  }

  std::vector<zx_koid_t> threads;
  status = TryGetProcessThreadKoids(process, try_count, num_available_threads,
                                    max_num_new_threads, &threads,
                                    &num_available_threads);
  if (status == ZX_OK) {
    *out_threads = std::move(threads);
    *out_num_available_threads = num_available_threads;
  }
  return status;
}

zx_status_t GetProcessThreadKoids(const zx::process& process, size_t try_count,
                                  size_t max_num_new_threads,
                                  std::vector<zx_koid_t>* out_threads,
                                  size_t* out_num_available_threads) {
  return GetProcessThreadKoids(process.get(), try_count, max_num_new_threads,
                               out_threads, out_num_available_threads);
}

}  // namespace debugger_utils
