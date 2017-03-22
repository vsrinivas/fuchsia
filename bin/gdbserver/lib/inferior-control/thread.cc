// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "thread.h"

#include <cinttypes>
#include <string>

#include <magenta/syscalls.h>
#include <magenta/syscalls/exception.h>

#include "lib/ftl/logging.h"
#include "lib/ftl/strings/string_printf.h"

#include "debugger-utils/util.h"

#include "arch.h"
#include "process.h"

namespace debugserver {

// static
const char* Thread::StateName(Thread::State state) {
#define CASE_TO_STR(x)   \
  case Thread::State::x: \
    return #x
  switch (state) {
    CASE_TO_STR(kNew);
    CASE_TO_STR(kStopped);
    CASE_TO_STR(kRunning);
    CASE_TO_STR(kStepping);
    CASE_TO_STR(kExiting);
    CASE_TO_STR(kGone);
    default:
      break;
  }
#undef CASE_TO_STR
  return "(unknown)";
}

Thread::Thread(Process* process, mx_handle_t handle, mx_koid_t id)
    : process_(process),
      handle_(handle),
      id_(id),
      state_(State::kNew),
      breakpoints_(this),
      weak_ptr_factory_(this) {
  FTL_DCHECK(process_);
  FTL_DCHECK(handle_ != MX_HANDLE_INVALID);
  FTL_DCHECK(id_ != MX_KOID_INVALID);

  registers_ = arch::Registers::Create(this);
  FTL_DCHECK(registers_.get());
}

Thread::~Thread() {
  FTL_VLOG(2) << "Destructing thread " << GetDebugName();

  Clear();
}

std::string Thread::GetName() const {
  return ftl::StringPrintf("%" PRId64 ".%" PRId64, process_->id(), id());
}

std::string Thread::GetDebugName() const {
  return ftl::StringPrintf("%" PRId64 ".%" PRId64 "(%" PRIx64 ".%" PRIx64 ")",
                           process_->id(), id(), process_->id(), id());
}

void Thread::set_state(State state) {
  FTL_DCHECK(state != State::kNew);
  state_ = state;
}

void Thread::Clear() {
  // We close the handle here so the o/s will release the thread.
  if (handle_ != MX_HANDLE_INVALID)
    mx_handle_close(handle_);
  handle_ = MX_HANDLE_INVALID;
}

ftl::WeakPtr<Thread> Thread::AsWeakPtr() {
  return weak_ptr_factory_.GetWeakPtr();
}

int Thread::GetGdbSignal() const {
  if (!exception_context_)
    return -1;

  return arch::ComputeGdbSignal(*exception_context_);
}

void Thread::OnException(const mx_excp_type_t type,
                         const mx_exception_context_t& context) {
  // TODO(dje): While having a pointer allows for a simple "do we have a
  // context" check, it might be simpler to just store the struct in the class.
  exception_context_.reset(new mx_exception_context_t);
  *exception_context_ = context;

  State prev_state = state_;
  set_state(State::kStopped);

  // If we were singlestepping turn it off.
  // If the user wants to try the singlestep again it must be re-requested.
  // If the thread has exited we may not be able to, and there's no point
  // anyway.
  if (prev_state == State::kStepping && type != MX_EXCP_THREAD_EXITING) {
    FTL_DCHECK(breakpoints_.SingleStepBreakpointInserted());
    if (!breakpoints_.RemoveSingleStepBreakpoint()) {
      FTL_LOG(ERROR) << "Unable to clear single-step bkpt";
    } else {
      FTL_VLOG(2) << "Single-step bkpt cleared";
    }
  }
}

bool Thread::Resume() {
  if (state() != State::kStopped && state() != State::kNew) {
    FTL_LOG(ERROR) << "Cannot resume a thread while in state: "
                   << StateName(state());
    return false;
  }

  // This is printed here before resuming the task so that this is always
  // printed before any subsequent exception report (which is read by another
  // thread).
  FTL_VLOG(2) << "Thread " << GetName() << " is now running";

  mx_status_t status = mx_task_resume(handle_, MX_RESUME_EXCEPTION);
  if (status < 0) {
    util::LogErrorWithMxStatus("Failed to resume thread", status);
    return false;
  }

  state_ = State::kRunning;
  return true;
}

void Thread::ResumeForExit() {
  switch (state()) {
    case State::kNew:
    case State::kStopped:
    case State::kExiting:
      break;
    default:
      FTL_DCHECK(false) << "unexpected state " << StateName(state());
      break;
  }

  FTL_VLOG(2) << "Thread " << GetName() << " is exiting";

  auto status = mx_task_resume(handle_, MX_RESUME_EXCEPTION);
  if (status < 0) {
    // This might fail if the process has been killed in the interim.
    // It shouldn't otherwise fail. Just log the failure, nothing else
    // we can do.
    mx_info_process_t info;
    auto info_status = mx_object_get_info(process()->handle(),
                                          MX_INFO_PROCESS, &info,
                                          sizeof(info), nullptr, nullptr);
    if (info_status != NO_ERROR)
      util::LogErrorWithMxStatus("error getting process info", info_status);
    if (info_status == NO_ERROR && info.exited) {
      FTL_VLOG(2) << "Process " << process()->GetName() << " exited too";
    } else {
      util::LogErrorWithMxStatus("Failed to resume thread for exit", status);
    }
  }

  set_state(State::kGone);
  Clear();
}

bool Thread::Step() {
  if (state() != State::kStopped) {
    FTL_LOG(ERROR) << "Cannot resume a thread while in state: "
                   << StateName(state());
    return false;
  }

  if (!registers_->RefreshGeneralRegisters()) {
    FTL_LOG(ERROR) << "Failed refreshing gregs";
    return false;
  }
  mx_vaddr_t pc = registers_->GetPC();

  if (!breakpoints_.InsertSingleStepBreakpoint(pc))
    return false;

  // This is printed here before resuming the task so that this is always
  // printed before any subsequent exception report (which is read by another
  // thread).
  FTL_LOG(INFO) << "Thread " << GetName() << " is now stepping";

  mx_status_t status = mx_task_resume(handle_, MX_RESUME_EXCEPTION);
  if (status < 0) {
    breakpoints_.RemoveSingleStepBreakpoint();
    util::LogErrorWithMxStatus("Failed to resume thread for step", status);
    return false;
  }

  state_ = State::kStepping;
  return true;
}

}  // namespace debugserver
