// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "thread.h"

#include <string>

#include <magenta/syscalls.h>
#include <magenta/syscalls/exception.h>

#include "lib/ftl/logging.h"

#include "util.h"

namespace debugserver {
namespace {

std::string ThreadStateToString(Thread::State state) {
#define CASE_TO_STR(x) \
  case x:              \
    return #x
  switch (state) {
    CASE_TO_STR(Thread::State::kNew);
    CASE_TO_STR(Thread::State::kGone);
    CASE_TO_STR(Thread::State::kStopped);
    CASE_TO_STR(Thread::State::kRunning);
    default:
      break;
  }
#undef CASE_TO_STR
  return "(unknown)";
}

}  // namespace

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

int Thread::GetGdbSignal() const {
  if (!exception_context_)
    return -1;

  return arch::ComputeGdbSignal(*exception_context_);
}

void Thread::SetExceptionContext(const mx_exception_context_t& context) {
  exception_context_.reset(new mx_exception_context_t);
  *exception_context_ = context;
}

bool Thread::Resume() {
  if (state() != State::kStopped && state() != State::kNew) {
    FTL_LOG(ERROR) << "Cannot resume a thread while in state: "
                   << ThreadStateToString(state());
    return false;
  }

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
