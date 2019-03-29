// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <string>

#include <src/lib/fxl/logging.h>
#include <lib/zx/job.h>

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

zx_status_t CreateProcessBuilder(
    const zx::job& job, const std::string& path,
    const debugger_utils::Argv& argv,
    std::shared_ptr<sys::ServiceDirectory> services,
    std::unique_ptr<process::ProcessBuilder>* out_builder) {
  FXL_DCHECK(argv.size() > 0);
  zx::job builder_job;
  zx_status_t status = zx_handle_duplicate(job.get(), ZX_RIGHT_SAME_RIGHTS,
                                           builder_job.reset_and_get_address());
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Unable to duplicate job handle: "
                   << debugger_utils::ZxErrorString(status);
    return status;
  }

  auto builder = std::make_unique<process::ProcessBuilder>(
      std::move(builder_job), std::move(services));

  status = builder->LoadPath(path);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to load binary: " << path
                   << ": " << debugger_utils::ZxErrorString(status);
    return status;
  }
  FXL_VLOG(2) << "Binary loaded";

  builder->AddArgs(argv);

  FXL_VLOG(2) << "path: " << path;
  FXL_VLOG(2) << "argv: " << debugger_utils::ArgvToString(argv);

  *out_builder = std::move(builder);
  return ZX_OK;
}

zx_status_t GetProcessReturnCode(zx_handle_t process, int* out_return_code) {
  zx_info_process_t info;
  auto status = zx_object_get_info(process, ZX_INFO_PROCESS, &info,
                                   sizeof(info), nullptr, nullptr);
  if (status == ZX_OK) {
    FXL_VLOG(2) << "Process exited with code " << info.return_code;
    *out_return_code = info.return_code;
  } else {
    FXL_VLOG(2) << "Error getting process return code: "
                << debugger_utils::ZxErrorString(status);
  }
  return status;
}

zx_status_t GetProcessReturnCode(const zx::process& process,
                                 int* out_return_code) {
  return GetProcessReturnCode(process.get(), out_return_code);
}

}  // namespace debugger_utils
