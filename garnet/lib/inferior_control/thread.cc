// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>

#include <src/lib/fxl/logging.h>
#include <src/lib/fxl/strings/string_printf.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/exception.h>

#include "garnet/lib/debugger_utils/breakpoints.h"
#include "garnet/lib/debugger_utils/threads.h"
#include "garnet/lib/debugger_utils/util.h"

#include "arch.h"
#include "process.h"
#include "server.h"
#include "thread.h"

namespace inferior_control {

// static
const char* Thread::StateName(Thread::State state) {
#define CASE_TO_STR(x)   \
  case Thread::State::x: \
    return #x
  switch (state) {
    CASE_TO_STR(kNew);
    CASE_TO_STR(kInException);
    CASE_TO_STR(kSuspended);
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

  name_ = debugger_utils::GetObjectName(handle_);

  registers_ = Registers::Create(this);
  FXL_DCHECK(registers_.get());
}

Thread::~Thread() {
  FXL_VLOG(4) << "Destructing thread " << GetDebugName();

  Clear();
}

std::string Thread::GetName() const {
  if (name_.empty()) {
    return fxl::StringPrintf("%lu.%lu", process_->id(), id_);
  }
  return fxl::StringPrintf("%lu.%lu(%s)",
                           process_->id(), id_, name_.c_str());
}

std::string Thread::GetDebugName() const {
  if (name_.empty()) {
    return fxl::StringPrintf("%lu.%lu(%lx.%lx)",
                             process_->id(), id_, process_->id(), id_);
  }
  return fxl::StringPrintf("%lu.%lu(%lx.%lx)(%s)",
                           process_->id(), id_, process_->id(), id_,
                           name_.c_str());
}

void Thread::set_state(State state) {
  FXL_DCHECK(state != State::kNew);
  state_ = state;
}

bool Thread::IsLive() const {
  switch (state_) {
    case State::kNew:
    case State::kInException:
    case State::kSuspended:
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

zx_handle_t Thread::GetExceptionPortHandle() {
  return process_->server()->exception_port_handle();
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
  set_state(State::kInException);

  // If we were singlestepping turn it off.
  // If the user wants to try the singlestep again it must be re-requested.
  // If the thread has exited we may not be able to, and there's no point
  // anyway.
  if (prev_state == State::kStepping && type != ZX_EXCP_THREAD_EXITING) {
    FXL_DCHECK(breakpoints_.SingleStepBreakpointInserted());
    if (!breakpoints_.RemoveSingleStepBreakpoint()) {
      FXL_LOG(ERROR) << "Unable to clear single-step bkpt for thread "
                     << GetName();
    } else {
      FXL_VLOG(4) << "Single-step bkpt cleared for thread "
                  << GetDebugName();
    }
  }

  FXL_VLOG(2) << ExceptionToString(type, context);
}

void Thread::OnTermination() {
  set_state(State::kGone);
  process_->delegate()->OnThreadTermination(this);
  FXL_VLOG(2) << SignalsToString(ZX_THREAD_TERMINATED);
}

void Thread::OnSuspension() {
  set_state(State::kSuspended);
  process_->delegate()->OnThreadSuspension(this);
  FXL_VLOG(2) << SignalsToString(ZX_THREAD_SUSPENDED);
}

void Thread::OnResumption() {
  set_state(State::kRunning);
  process_->delegate()->OnThreadResumption(this);
  FXL_VLOG(2) << SignalsToString(ZX_THREAD_RUNNING);
}

void Thread::OnSignal(zx_signals_t signals) {
    if (signals & ZX_THREAD_TERMINATED) {
      OnTermination();
      return;
    }

    switch (signals & (ZX_THREAD_SUSPENDED | ZX_THREAD_RUNNING)) {
    case 0:
      break;
    case ZX_THREAD_SUSPENDED:
      OnSuspension();
      break;
    case ZX_THREAD_RUNNING:
      OnResumption();
      break;
    case ZX_THREAD_SUSPENDED | ZX_THREAD_RUNNING: {
      // These signals can get folded together.
      uint32_t state = debugger_utils::GetThreadOsState(handle_);
      switch (ZX_THREAD_STATE_BASIC(state)) {
      case ZX_THREAD_STATE_RUNNING:
        OnResumption();
        break;
      case ZX_THREAD_STATE_SUSPENDED:
        OnSuspension();
        break;
      case ZX_THREAD_STATE_BLOCKED:
        // If we're blocked in a syscall or some such we're still running
        // as far as we're concerned.
        OnResumption();
        break;
      case ZX_THREAD_STATE_DYING:
      case ZX_THREAD_STATE_DEAD:
        // These are handled elsewhere, e.g., on receipt of ZX_THREAD_TERMINATED.
        // But if we were suspended we no longer are.
        if (state_ == State::kSuspended) {
          // The transition to kExiting or kGone is handled elsewhere.
          // Here we just process the fact that we got SUSPENDED|RUNNING signals.
          OnResumption();
        }
        break;
      default:
        FXL_NOTREACHED();
        break;
      }
      break;
    }
    }
}

bool Thread::TryNext(zx_handle_t eport) {
  if (state() != State::kInException && state() != State::kNew) {
    FXL_LOG(ERROR) << "Cannot try-next thread " << GetName()
                   << " while in state " << StateName(state());
    return false;
  }

  FXL_VLOG(4) << "Thread " << GetDebugName()
              << ": trying next exception handler";

  zx_status_t status =
      zx_task_resume_from_exception(handle_, eport, ZX_RESUME_TRY_NEXT);
  if (status < 0) {
    FXL_LOG(ERROR) << "Failed to try-next thread "
                   << GetName() << ": "
                   << debugger_utils::ZxErrorString(status);
    return false;
  }

  return true;
}

bool Thread::ResumeFromException(zx_handle_t eport) {
  if (state() != State::kInException && state() != State::kNew) {
    FXL_LOG(ERROR) << "Cannot resume thread " << GetName()
                   << " while in state: " << StateName(state());
    return false;
  }

  // This is printed here before resuming the task so that this is always
  // printed before any subsequent exception report (which is read by another
  // thread).
  FXL_VLOG(4) << "Resuming thread " << GetDebugName() << " after an exception";

  zx_status_t status =
      zx_task_resume_from_exception(handle_, eport, 0);
  if (status < 0) {
    FXL_LOG(ERROR) << "Failed to resume thread " << GetName() << ": "
                   << debugger_utils::ZxErrorString(status);
    return false;
  }

  state_ = State::kRunning;
  return true;
}

bool Thread::ResumeAfterSoftwareBreakpointInstruction(zx_handle_t eport) {
  FXL_DCHECK(state() == State::kInException);
  if (!registers_->RefreshGeneralRegisters()) {
    return false;
  }
  zx_vaddr_t pc = registers_->GetPC();
  zx_vaddr_t new_pc = debugger_utils::IncrementPcAfterBreak(pc);
  FXL_VLOG(4) << "Changing pc 0x" << std::hex << pc << " -> 0x" << new_pc;
  registers_->SetPC(new_pc);
  if (!registers_->WriteGeneralRegisters()) {
    return false;
  }
  if (!ResumeFromException(eport)) {
    return false;
  }
  return true;
}

void Thread::ResumeForExit(zx_handle_t eport) {
  switch (state()) {
    case State::kNew:
    case State::kInException:
    case State::kExiting:
      break;
    default:
      FXL_DCHECK(false) << "unexpected state " << StateName(state());
      break;
  }

  FXL_VLOG(4) << "Thread " << GetDebugName() << " is exiting";

  auto status =
      zx_task_resume_from_exception(handle_, eport, 0);
  if (status < 0) {
    // This might fail if the process has been killed in the interim.
    // It shouldn't otherwise fail. Just log the failure, nothing else
    // we can do.
    zx_info_process_t info;
    auto info_status =
        zx_object_get_info(process()->process().get(), ZX_INFO_PROCESS, &info,
                           sizeof(info), nullptr, nullptr);
    if (info_status != ZX_OK) {
      FXL_LOG(ERROR) << "Error getting process info for thread "
                     << GetName() << ": "
                     << debugger_utils::ZxErrorString(info_status);
    }
    if (info_status == ZX_OK && info.exited) {
      FXL_VLOG(4) << "Process " << process()->GetName() << " exited too";
    } else {
      FXL_LOG(ERROR) << "Failed to resume thread " << GetName()
                     << " for exit: "
                     << debugger_utils::ZxErrorString(status);
    }
  }

  set_state(State::kGone);
  Clear();
}

bool Thread::RequestSuspend() {
  FXL_DCHECK(!suspend_token_);

  switch (state_) {
    case State::kGone:
      FXL_VLOG(2) << "Thread " << GetDebugName() << " is not live";
      return false;
    default:
      break;
  }

  FXL_LOG(INFO) << "Suspending thread " << id_;

  FXL_DCHECK(handle_ != ZX_HANDLE_INVALID);
  auto status = zx_task_suspend(handle_, suspend_token_.reset_and_get_address());
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to suspend thread " << GetName() << ": "
                   << debugger_utils::ZxErrorString(status);
    return false;
  }

  return true;
}

void Thread::ResumeFromSuspension() {
  FXL_DCHECK(suspend_token_);
  suspend_token_.reset();
}

bool Thread::Step() {
  if (state() != State::kInException) {
    FXL_LOG(ERROR) << "Cannot step thread " << GetName() << " while in state "
                   << StateName(state());
    return false;
  }

  if (!registers_->RefreshGeneralRegisters()) {
    FXL_LOG(ERROR) << "Failed refreshing gregs for thread " << GetName();
    return false;
  }
  zx_vaddr_t pc = registers_->GetPC();

  if (!breakpoints_.InsertSingleStepBreakpoint(pc))
    return false;

  // This is printed here before resuming the task so that this is always
  // printed before any subsequent exception report (which is read by another
  // thread).
  FXL_LOG(INFO) << "Thread " << GetName() << " is now stepping";

  // TODO(dje): Handle stopped by suspension.
  zx_status_t status =
      zx_task_resume_from_exception(handle_, GetExceptionPortHandle(), 0);
  if (status < 0) {
    breakpoints_.RemoveSingleStepBreakpoint();
    FXL_LOG(ERROR) << "Failed to resume thread " << GetName() << " for step: "
                   << debugger_utils::ZxErrorString(status);
    return false;
  }

  state_ = State::kStepping;
  return true;
}

zx_status_t Thread::GetExceptionReport(zx_exception_report_t* report) const {
  zx_status_t status =
    zx_object_get_info(handle_, ZX_INFO_THREAD_EXCEPTION_REPORT,
                       report, sizeof(*report), nullptr, nullptr);
  // This could fail if the process terminates before we get a chance to
  // look at it.
  if (status == ZX_ERR_BAD_STATE) {
    // The signal notifying us the thread/process death may not have been
    // processed yet, so get the thread's state directly.
    zx_thread_state_t state = debugger_utils::GetThreadOsState(handle_);
    if (state != ZX_THREAD_STATE_DEAD) {
      FXL_LOG(WARNING) << "No exception report for thread " << id_;
    }
  }

  return status;
}

void Thread::Dump() {
  if (state_ == State::kInException ||
      state_ == State::kSuspended) {
    FXL_LOG(INFO) << "Thread " << GetDebugName() << " dump";
    debugger_utils::DumpThread(process()->process().get(), handle(),
                               state_ == State::kInException);
  } else {
    FXL_LOG(INFO) << "Thread " << id_ << " not stopped, skipping dump";
  }
}

std::string Thread::ExceptionToString(
    zx_excp_type_t type, const zx_exception_context_t& context) const {
  std::string description = fxl::StringPrintf(
    "Thread %s: received exception %s",
    GetDebugName().c_str(),
    debugger_utils::ExceptionNameAsString(type).c_str());

  if (ZX_EXCP_IS_ARCH(type)) {
    Registers* regs = registers();
    if (regs->RefreshGeneralRegisters()) {
      zx_vaddr_t pc = regs->GetPC();
      description += fxl::StringPrintf(", @PC 0x%lx", pc);
    }
  }

  return description;
}

std::string Thread::SignalsToString(zx_signals_t signals) const {
  std::string description;
  if (signals & ZX_THREAD_RUNNING)
    description += ", running";
  if (signals & ZX_THREAD_SUSPENDED)
    description += ", suspended";
  if (signals & ZX_THREAD_TERMINATED)
    description += ", terminated";
  zx_signals_t mask =
      (ZX_THREAD_RUNNING | ZX_THREAD_SUSPENDED | ZX_THREAD_TERMINATED);
  if (signals & ~mask)
    description += fxl::StringPrintf(", unknown (0x%x)", signals & ~mask);
  if (description.length() == 0)
    description = ", none";
  return fxl::StringPrintf("Thread %s got signals: %s",
                           GetDebugName().c_str(), description.c_str() + 2);
}

}  // namespace inferior_control
