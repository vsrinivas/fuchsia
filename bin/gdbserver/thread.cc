// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "thread.h"

#include <magenta/syscalls.h>
#include <magenta/syscalls/exception.h>

#include "lib/ftl/logging.h"

#include "util.h"

namespace debugserver {

Thread::Thread(Process* process, mx_handle_t debug_handle, mx_koid_t thread_id)
    : process_(process),
      debug_handle_(debug_handle),
      thread_id_(thread_id),
      state_(State::kNew),
      weak_ptr_factory_(this) {
  FTL_DCHECK(process_);
  FTL_DCHECK(debug_handle_ != MX_HANDLE_INVALID);
  FTL_DCHECK(thread_id_ != MX_KOID_INVALID);

  registers_ = arch::Registers::Create(this);
  FTL_DCHECK(registers_.get());
}

Thread::~Thread() {
  mx_handle_close(debug_handle_);
}

ftl::WeakPtr<Thread> Thread::AsWeakPtr() {
  return weak_ptr_factory_.GetWeakPtr();
}

bool Thread::Resume() {
  mx_status_t status = mx_task_resume(debug_handle_, MX_RESUME_EXCEPTION);
  if (status < 0) {
    util::LogErrorWithMxStatus("Failed to resume thread", status);
    return false;
  }

  state_ = State::kRunning;
  FTL_LOG(INFO) << "Thread (tid = " << thread_id_ << ") is running";

  return true;
}

}  // namespace debugserver
