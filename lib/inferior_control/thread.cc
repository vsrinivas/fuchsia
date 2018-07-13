// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "thread.h"

#include <cinttypes>
#include <string>

#include <zircon/syscalls.h>
#include <zircon/syscalls/exception.h>

#include "lib/fxl/logging.h"
#include "lib/fxl/strings/string_printf.h"

#include "garnet/lib/debugger_utils/util.h"

#include "arch.h"
#include "process.h"

namespace inferior_control {

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

Thread::Thread(Process* process, zx_handle_t handle, zx_koid_t id)
    : process_(process),
      handle_(handle),
      id_(id),
      state_(State::kNew),
      breakpoints_(this),
      weak_ptr_factory_(this) {
  FXL_DCHECK(process_);
  FXL_DCHECK(handle_ != ZX_HANDLE_INVALID);
  FXL_DCHECK(id_ != ZX_KOID_INVALID);

  registers_ = Registers::Create(this);
  FXL_DCHECK(registers_.get());
}

Thread::~Thread() {
  FXL_VLOG(2) << "Destructing thread " << GetDebugName();

  Clear();
}

std::string Thread::GetName() const {
  return fxl::StringPrintf("%" PRId64 ".%" PRId64, process_->id(), id());
}

std::string Thread::GetDebugName() const {
  return fxl::StringPrintf("%" PRId64 ".%" PRId64 "(%" PRIx64 ".%" PRIx64 ")",
                           process_->id(), id(), process_->id(), id());
}

void Thread::set_state(State state) {
  FXL_DCHECK(state != State::kNew);
  state_ = state;
}

bool Thread::IsLive() const {
  switch (state_) {
    case State::kNew:
    case State::kStopped:
    case State::kRunning:
    case State::kStepping:
      return true;
    default:
      return false;
  }
}

void Thread::Clear() {
  // We close the handle here so the o/s will release the thread.
  if (handle_ != ZX_HANDLE_INVALID)
    zx_handle_close(handle_);
  handle_ = ZX_HANDLE_INVALID;
}

fxl::WeakPtr<Thread> Thread::AsWeakPtr() {
  return weak_ptr_factory_.GetWeakPtr();
}

GdbSignal Thread::GetGdbSignal() const {
  if (!exception_context_) {
    // TODO(dje): kNone may be a better value to return here.
    return GdbSignal::kUnsupported;
  }

  return ComputeGdbSignal(*exception_context_);
}

void Thread::OnException(const zx_excp_type_t type,
                         const zx_exception_context_t& context) {
  // TODO(dje): While having a pointer allows for a simple "do we have a
  // context" check, it might be simpler to just store the struct in the class.
  exception_context_.reset(new zx_exception_context_t);
  *exception_context_ = context;

  State prev_state = state_;
  set_state(State::kStopped);

  // If we were singlestepping turn it off.
  // If the user wants to try the singlestep again it must be re-requested.
  // If the thread has exited we may not be able to, and there's no point
  // anyway.
  if (prev_state == State::kStepping && type != ZX_EXCP_THREAD_EXITING) {
    FXL_DCHECK(breakpoints_.SingleStepBreakpointInserted());
    if (!breakpoints_.RemoveSingleStepBreakpoint()) {
      FXL_LOG(ERROR) << "Unable to clear single-step bkpt";
    } else {
      FXL_VLOG(2) << "Single-step bkpt cleared";
    }
  }
}

bool Thread::Resume() {
  if (state() != State::kStopped && state() != State::kNew) {
    FXL_LOG(ERROR) << "Cannot resume a thread while in state: "
                   << StateName(state());
    return false;
  }

  // This is printed here before resuming the task so that this is always
  // printed before any subsequent exception report (which is read by another
  // thread).
  FXL_VLOG(2) << "Thread " << GetName() << " is now running";

  zx_status_t status = zx_task_resume(handle_, ZX_RESUME_EXCEPTION);
  if (status < 0) {
    FXL_LOG(ERROR) << "Failed to resume thread: "
                   << debugger_utils::ZxErrorString(status);
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
      FXL_DCHECK(false) << "unexpected state " << StateName(state());
      break;
  }

  FXL_VLOG(2) << "Thread " << GetName() << " is exiting";

  auto status = zx_task_resume(handle_, ZX_RESUME_EXCEPTION);
  if (status < 0) {
    // This might fail if the process has been killed in the interim.
    // It shouldn't otherwise fail. Just log the failure, nothing else
    // we can do.
    zx_info_process_t info;
    auto info_status =
        zx_object_get_info(process()->handle(), ZX_INFO_PROCESS, &info,
                           sizeof(info), nullptr, nullptr);
    if (info_status != ZX_OK) {
      FXL_LOG(ERROR) << "error getting process info: "
                     << debugger_utils::ZxErrorString(info_status);
    }
    if (info_status == ZX_OK && info.exited) {
      FXL_VLOG(2) << "Process " << process()->GetName() << " exited too";
    } else {
      FXL_LOG(ERROR) << "Failed to resume thread for exit: "
                     << debugger_utils::ZxErrorString(status);
    }
  }

  set_state(State::kGone);
  Clear();
}

bool Thread::Step() {
  if (state() != State::kStopped) {
    FXL_LOG(ERROR) << "Cannot resume a thread while in state: "
                   << StateName(state());
    return false;
  }

  if (!registers_->RefreshGeneralRegisters()) {
    FXL_LOG(ERROR) << "Failed refreshing gregs";
    return false;
  }
  zx_vaddr_t pc = registers_->GetPC();

  if (!breakpoints_.InsertSingleStepBreakpoint(pc))
    return false;

  // This is printed here before resuming the task so that this is always
  // printed before any subsequent exception report (which is read by another
  // thread).
  FXL_LOG(INFO) << "Thread " << GetName() << " is now stepping";

  zx_status_t status = zx_task_resume(handle_, ZX_RESUME_EXCEPTION);
  if (status < 0) {
    breakpoints_.RemoveSingleStepBreakpoint();
    FXL_LOG(ERROR) << "Failed to resume thread for step: "
                   << debugger_utils::ZxErrorString(status);
    return false;
  }

  state_ = State::kStepping;
  return true;
}

}  // namespace inferior_control
