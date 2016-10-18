// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "process.h"

#include <launchpad/vmo.h>
#include <magenta/syscalls.h>
#include <magenta/syscalls/object.h>

#include "lib/ftl/logging.h"

#include "util.h"

using std::string;
using std::vector;

namespace debugserver {
namespace {

bool SetupLaunchpad(launchpad_t** out_lp, const vector<string>& argv) {
  FTL_DCHECK(out_lp);
  FTL_DCHECK(argv.size() > 0);

  // Construct the argument array.
  const char* c_args[argv.size()];
  for (size_t i = 0; i < argv.size(); ++i)
    c_args[i] = argv[i].c_str();

  launchpad_t* lp = nullptr;
  mx_status_t status = launchpad_create(c_args[0], &lp);
  if (status != NO_ERROR)
    goto fail;

  status = launchpad_arguments(lp, argv.size(), c_args);
  if (status != NO_ERROR)
    goto fail;

  // TODO(armansito): Make the inferior inherit the environment (i.e.
  // launchpad_environ)?

  status = launchpad_add_vdso_vmo(lp);
  if (status != NO_ERROR)
    goto fail;

  status = launchpad_add_all_mxio(lp);
  if (status != NO_ERROR)
    goto fail;

  *out_lp = lp;
  return true;

fail:
  util::LogErrorWithMxStatus("Process setup failed", status);
  if (lp)
    launchpad_destroy(lp);
  return false;
}

bool LoadBinary(launchpad_t* lp, const string& binary_path) {
  FTL_DCHECK(lp);

  mx_status_t status =
      launchpad_elf_load(lp, launchpad_vmo_from_file(binary_path.c_str()));
  if (status != NO_ERROR) {
    util::LogErrorWithMxStatus("Could not load binary", status);
    return false;
  }

  status = launchpad_load_vdso(lp, MX_HANDLE_INVALID);
  if (status != NO_ERROR) {
    util::LogErrorWithMxStatus("Could not load vDSO", status);
    return false;
  }

  return true;
}

mx_handle_t GetProcessDebugHandle(launchpad_t* lp) {
  FTL_DCHECK(lp);

  // We use the mx_object_get_child syscall to obtain a debug-capable handle
  // to the process. For processes, the syscall expect the ID of the underlying
  // kernel object (koid, also passing for process id in Magenta).
  mx_handle_t process_handle = launchpad_get_process_handle(lp);
  FTL_DCHECK(process_handle);

  mx_info_handle_basic_t info;
  mx_ssize_t size = mx_object_get_info(process_handle, MX_INFO_HANDLE_BASIC,
                                       sizeof(info.rec), &info, sizeof(info));
  if (size != sizeof(info)) {
    if (size < 0) {
      util::LogErrorWithMxStatus("mx_object_get_info_failed", size);
    } else {
      FTL_LOG(ERROR) << "mx_object_get_info failed - expected size: "
                     << sizeof(info) << ", returned: " << size;
    }
    return MX_HANDLE_INVALID;
  }

  FTL_DCHECK(info.rec.type == MX_OBJ_TYPE_PROCESS);

  mx_handle_t debug_handle = mx_object_get_child(
      MX_HANDLE_INVALID, info.rec.koid, MX_RIGHT_SAME_RIGHTS);
  if (debug_handle < 0) {
    util::LogErrorWithMxStatus("mx_object_get_child failed", debug_handle);
    return MX_HANDLE_INVALID;
  }

  // TODO(armansito): Check that |debug_handle| has MX_RIGHT_DEBUG (this seems
  // not to be set by anything at the moment but eventully we should check)?

  // Syscalls shouldn't return MX_HANDLE_INVALID in the case of NO_ERROR.
  FTL_DCHECK(debug_handle != MX_HANDLE_INVALID);

  return debug_handle;
}

}  // namespace

Process::Process(const vector<string>& argv)
    : argv_(argv),
      launchpad_(nullptr),
      debug_handle_(MX_HANDLE_INVALID),
      started_(false) {
  FTL_DCHECK(argv_.size() > 0);
}

Process::~Process() {
  if (launchpad_)
    launchpad_destroy(launchpad_);
  if (debug_handle_ != MX_HANDLE_INVALID)
    mx_handle_close(debug_handle_);
}

bool Process::Initialize() {
  FTL_DCHECK(!launchpad_);
  FTL_DCHECK(debug_handle_ == MX_HANDLE_INVALID);

  if (!SetupLaunchpad(&launchpad_, argv_))
    return false;

  FTL_LOG(INFO) << "Process setup complete";

  if (!LoadBinary(launchpad_, argv_[0]))
    goto fail;

  FTL_LOG(INFO) << "Binary loaded";

  // Now we need a debug-capable handle of the process.
  debug_handle_ = GetProcessDebugHandle(launchpad_);
  if (debug_handle_ == MX_HANDLE_INVALID)
    goto fail;

  FTL_LOG(INFO) << "mx_debug handle obtained for process";

  return true;

fail:
  launchpad_destroy(launchpad_);
  launchpad_ = nullptr;
  return false;
}

bool Process::Attach() {
  // TODO(armansito): Implement.
  return true;
}

bool Process::Start() {
  FTL_DCHECK(launchpad_);
  FTL_DCHECK(debug_handle_ != MX_HANDLE_INVALID);
  // TODO(armansito): Assert that Attach has been called successfully once it's
  // been implemented.

  if (started_) {
    FTL_LOG(WARNING) << "Process already started";
    return false;
  }

  // launchpad_start returns a dup of the process handle (owned by
  // |launchpad_|), where the original handle is given to the child. We hae to
  // close the dup handle to avoid leaking it.
  mx_handle_t dup_handle = launchpad_start(launchpad_);
  if (dup_handle < 0) {
    util::LogErrorWithMxStatus("Failed to start inferior process", dup_handle);
    return false;
  }

  FTL_DCHECK(dup_handle != MX_HANDLE_INVALID);
  mx_handle_close(dup_handle);

  started_ = true;

  return true;
}

Thread* Process::FindThreadById(mx_koid_t thread_id) {
  FTL_DCHECK(debug_handle_ != MX_HANDLE_INVALID);
  if (thread_id == MX_HANDLE_INVALID) {
    FTL_LOG(WARNING) << "Invalid thread ID given: " << thread_id;
    return nullptr;
  }

  const auto iter = threads_.find(thread_id);
  if (iter != threads_.end())
    return iter->second.get();

  // Try to get a debug capable handle to the child of the current process with
  // a kernel object ID that matches |thread_id|.
  mx_status_t thread_debug_handle =
      mx_object_get_child(debug_handle_, thread_id, MX_RIGHT_SAME_RIGHTS);
  if (thread_debug_handle < 0) {
    util::LogErrorWithMxStatus("Could not obtain a debug handle to thread",
                               thread_debug_handle);
    return nullptr;
  }

  Thread* thread = new Thread(this, thread_debug_handle, thread_id);
  threads_[thread_id] = std::unique_ptr<Thread>(thread);
  return thread;
}

Thread* Process::PickOneThread() {
  FTL_DCHECK(debug_handle_ != MX_HANDLE_INVALID);

  // TODO(armansito): It's not ideal to refresh the entire thread list but this
  // is the most accurate way for now. When we have thread life-time events in
  // the future we can manage |threads_| using events and only partially refresh
  // it as needed.
  RefreshAllThreads();

  if (threads_.empty())
    return nullptr;

  return threads_.begin()->second.get();
}

bool Process::RefreshAllThreads() {
  FTL_DCHECK(debug_handle_ != MX_HANDLE_INVALID);

  // First get the thread count so that we can allocate an appropriately sized
  // buffer.
  mx_info_header_t hdr;
  mx_ssize_t result_size = mx_object_get_info(
      debug_handle_, MX_INFO_PROCESS_THREADS, 0, &hdr, sizeof(hdr));
  if (result_size < 0) {
    util::LogErrorWithMxStatus("Failed to get process thread info",
                               result_size);
    return false;
  }

  const size_t buffer_size =
      sizeof(mx_info_process_threads_t) +
      hdr.avail_count * sizeof(mx_record_process_thread_t);
  std::unique_ptr<uint8_t[]> buffer(new uint8_t[buffer_size]);
  result_size = mx_object_get_info(debug_handle_, MX_INFO_PROCESS_THREADS,
                                   sizeof(mx_record_process_thread_t),
                                   buffer.get(), buffer_size);
  if (result_size < 0) {
    util::LogErrorWithMxStatus("Failed to get process thread info",
                               result_size);
    return false;
  }

  // This comparison is OK (size_t vs mx_ssize_t) since |hdr.avail_count| is a
  // uint32_t. The cast is OK because we know that |result_size| is a natural
  // number here.
  FTL_DCHECK(buffer_size == static_cast<size_t>(result_size));

  mx_info_process_threads_t* thread_info =
      reinterpret_cast<mx_info_process_threads_t*>(buffer.get());
  ThreadMap new_threads;
  for (size_t i = 0; i < hdr.avail_count; ++i) {
    mx_koid_t thread_id = thread_info->rec[i].koid;
    mx_handle_t thread_debug_handle =
        mx_object_get_child(debug_handle_, thread_id, MX_RIGHT_SAME_RIGHTS);
    if (thread_debug_handle < 0) {
      util::LogErrorWithMxStatus("Could not obtain a debug handle to thread",
                                 thread_debug_handle);
      continue;
    }
    new_threads[thread_id] =
        std::make_unique<Thread>(this, thread_debug_handle, thread_id);
  }

  // Just clear the existing list and repopulate it.
  threads_ = std::move(new_threads);

  return true;
}

void Process::ForEachThread(const ThreadCallback& callback) {
  for (const auto& iter : threads_)
    callback(iter.second.get());
}

}  // namespace debugserver
