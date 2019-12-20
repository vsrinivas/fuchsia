// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/debug_agent/debugged_thread.h"

#include <inttypes.h>
#include <lib/zx/clock.h>
#include <zircon/status.h>
#include <zircon/syscalls/debug.h>
#include <zircon/syscalls/exception.h>

#include <memory>

#include "src/developer/debug/debug_agent/arch.h"
#include "src/developer/debug/debug_agent/debug_agent.h"
#include "src/developer/debug/debug_agent/debugged_process.h"
#include "src/developer/debug/debug_agent/hardware_breakpoint.h"
#include "src/developer/debug/debug_agent/object_provider.h"
#include "src/developer/debug/debug_agent/process_breakpoint.h"
#include "src/developer/debug/debug_agent/process_info.h"
#include "src/developer/debug/debug_agent/software_breakpoint.h"
#include "src/developer/debug/debug_agent/unwind.h"
#include "src/developer/debug/debug_agent/watchpoint.h"
#include "src/developer/debug/ipc/agent_protocol.h"
#include "src/developer/debug/ipc/message_reader.h"
#include "src/developer/debug/ipc/message_writer.h"
#include "src/developer/debug/shared/logging/logging.h"
#include "src/developer/debug/shared/message_loop_target.h"
#include "src/developer/debug/shared/stream_buffer.h"
#include "src/developer/debug/shared/zx_status.h"
#include "src/lib/fxl/logging.h"
#include "src/lib/fxl/strings/string_printf.h"

namespace debug_agent {

namespace {

bool IsBlockedOnException(const zx::thread& thread) {
  zx_info_thread info;
  return thread.get_info(ZX_INFO_THREAD, &info, sizeof(info), nullptr, nullptr) == ZX_OK &&
         info.state == ZX_THREAD_STATE_BLOCKED_EXCEPTION;
}

// Used to have better context upon reading the debug logs.
std::string ThreadPreamble(const DebuggedThread* thread) {
  return fxl::StringPrintf("[Pr: %lu (%s), T: %lu] ", thread->process()->koid(),
                           thread->process()->name().c_str(), thread->koid());
}

// TODO(donosoc): Move this to a more generic place (probably shared) where it
//                can be used by other code.
const char* ExceptionTypeToString(uint32_t type) {
  switch (type) {
    case ZX_EXCP_GENERAL:
      return "ZX_EXCP_GENERAL";
    case ZX_EXCP_FATAL_PAGE_FAULT:
      return "ZX_EXCP_FATAL_PAGE_FAULT";
    case ZX_EXCP_UNDEFINED_INSTRUCTION:
      return "ZX_EXCP_UNDEFINED_INSTRUCTION";
    case ZX_EXCP_SW_BREAKPOINT:
      return "ZX_EXCP_SW_BREAKPOINT";
    case ZX_EXCP_HW_BREAKPOINT:
      return "ZX_EXCP_HW_BREAKPOINT";
    case ZX_EXCP_UNALIGNED_ACCESS:
      return "ZX_EXCP_UNALIGNED_ACCESS";
    default:
      break;
  }

  return "<unknown>";
}

void LogHitBreakpoint(debug_ipc::FileLineFunction location, const DebuggedThread* thread,
                      ProcessBreakpoint* process_breakpoint, uint64_t address) {
  if (!debug_ipc::IsDebugModeActive())
    return;

  std::stringstream ss;
  ss << ThreadPreamble(thread) << "Hit SW breakpoint on 0x" << std::hex << address << " for: ";
  for (Breakpoint* breakpoint : process_breakpoint->breakpoints()) {
    ss << breakpoint->settings().name << ", ";
  }

  DEBUG_LOG_WITH_LOCATION(Thread, location) << ss.str();
}

void LogExceptionNotification(debug_ipc::FileLineFunction location, const DebuggedThread* thread,
                              const debug_ipc::NotifyException& exception) {
  if (!debug_ipc::IsDebugModeActive())
    return;

  std::stringstream ss;
  ss << ThreadPreamble(thread) << "Notifying exception "
     << debug_ipc::ExceptionTypeToString(exception.type) << ". ";
  ss << "Breakpoints hit: ";
  int count = 0;
  for (auto& bp : exception.hit_breakpoints) {
    if (count > 0)
      ss << ", ";

    ss << bp.id;
    if (bp.should_delete)
      ss << " (delete)";
  }

  DEBUG_LOG_WITH_LOCATION(Thread, location) << ss.str();
}

}  // namespace

// DebuggedThread::SuspendToken --------------------------------------------------------------------

DebuggedThread::SuspendToken::SuspendToken(DebuggedThread* thread) : thread_(thread->GetWeakPtr()) {
  thread->IncreaseSuspend();
}

DebuggedThread::SuspendToken::~SuspendToken() {
  if (!thread_)
    return;
  thread_->DecreaseSuspend();
}

// DebuggedThread ----------------------------------------------------------------------------------

DebuggedThread::DebuggedThread(DebugAgent* debug_agent, CreateInfo&& create_info)
    : koid_(create_info.koid),
      handle_(std::move(create_info.handle)),
      debug_agent_(debug_agent),
      process_(create_info.process),
      exception_handle_(std::move(create_info.exception)),
      arch_provider_(std::move(create_info.arch_provider)),
      object_provider_(std::move(create_info.object_provider)),
      weak_factory_(this) {
  FXL_DCHECK(object_provider_);
  FXL_DCHECK(arch_provider_);

  switch (create_info.creation_option) {
    case ThreadCreationOption::kRunningKeepRunning:
      // do nothing
      break;
    case ThreadCreationOption::kSuspendedKeepSuspended:
      break;
    case ThreadCreationOption::kSuspendedShouldRun:
      ResumeException();
      break;
  }
}

DebuggedThread::~DebuggedThread() = default;

fxl::WeakPtr<DebuggedThread> DebuggedThread::GetWeakPtr() { return weak_factory_.GetWeakPtr(); }

void DebuggedThread::OnException(zx::exception exception_handle,
                                 zx_exception_info_t exception_info) {
  exception_handle_ = std::move(exception_handle);

  debug_ipc::NotifyException exception;
  exception.type = arch_provider_->DecodeExceptionType(*this, exception_info.type);

  DEBUG_LOG(Thread) << ThreadPreamble(this)
                    << "Exception: " << ExceptionTypeToString(exception_info.type) << " -> "
                    << debug_ipc::ExceptionTypeToString(exception.type);

  zx_thread_state_general_regs regs;
  zx_status_t status = arch_provider_->ReadGeneralState(handle_, &regs);
  if (status != ZX_OK) {
    FXL_LOG(WARNING) << "Could not read registers from thread: " << zx_status_get_string(status);
    return;
  }

  switch (exception.type) {
    case debug_ipc::ExceptionType::kSingleStep:
      return HandleSingleStep(&exception, &regs);
    case debug_ipc::ExceptionType::kSoftware:
      return HandleSoftwareBreakpoint(&exception, &regs);
    case debug_ipc::ExceptionType::kHardware:
      return HandleHardwareBreakpoint(&exception, &regs);
    case debug_ipc::ExceptionType::kWatchpoint:
      return HandleWatchpoint(&exception, &regs);
    case debug_ipc::ExceptionType::kNone:
    case debug_ipc::ExceptionType::kLast:
      break;
    // TODO(donosoc): Should synthetic be general or invalid?
    case debug_ipc::ExceptionType::kSynthetic:
    default:
      return HandleGeneralException(&exception, &regs);
  }

  FXL_NOTREACHED() << "Invalid exception notification type: "
                   << debug_ipc::ExceptionTypeToString(exception.type);

  // The exception was unhandled, so we close it so that the system can run its
  // course. The destructor would've done it anyway, but being explicit helps
  // readability.
  exception_handle_.reset();
}

void DebuggedThread::HandleSingleStep(debug_ipc::NotifyException* exception,
                                      zx_thread_state_general_regs* regs) {
  if (current_breakpoint_) {
    DEBUG_LOG(Thread) << ThreadPreamble(this) << "Ending single stepped over 0x" << std::hex
                      << current_breakpoint_->address();
    // Getting here means that the thread is done stepping over a breakpoint.
    // Depending on whether others threads are stepping over the breakpoints, this thread might be
    // suspended (waiting for other threads to step over).
    // This means that we cannot resume from suspension here, as the breakpoint is owning the
    // thread "run-lifetime".
    //
    // We can, though, resume from the exception, as effectively we already handled the single-step
    // exception, so there is no more need to keep the thread in an excepted state. The suspend
    // handle will take care of keeping the thread stopped.
    //
    // NOTE: It's important to resume the exception *before* telling the breakpoint we are done
    //       going over it, as it may call ResumeForRunMode, which could then again attempt to step
    //       over it.
    SetSingleStep(run_mode_ != debug_ipc::ResumeRequest::How::kContinue);
    ResumeException();
    current_breakpoint_->EndStepOver(this);
    current_breakpoint_ = nullptr;
    return;
  }

  if (run_mode_ == debug_ipc::ResumeRequest::How::kContinue) {
    // This could be due to a race where the user was previously single
    // stepping and then requested a continue before the single stepping
    // completed. It could also be a breakpoint that was deleted while
    // in the process of single-stepping over it. In both cases, the
    // least confusing thing is to resume automatically.
    DEBUG_LOG(Thread) << ThreadPreamble(this) << "Single step without breakpoint. Continuing.";
    ResumeForRunMode();
    return;
  }

  // When stepping in a range, automatically continue as long as we're
  // still in range.
  if (run_mode_ == debug_ipc::ResumeRequest::How::kStepInRange &&
      *arch_provider_->IPInRegs(regs) >= step_in_range_begin_ &&
      *arch_provider_->IPInRegs(regs) < step_in_range_end_) {
    DEBUG_LOG(Thread) << ThreadPreamble(this) << "Stepping in range. Continuing.";
    ResumeForRunMode();
    return;
  }

  DEBUG_LOG(Thread) << ThreadPreamble(this) << "Expected single step. Notifying.";
  SendExceptionNotification(exception, regs);
}

void DebuggedThread::HandleGeneralException(debug_ipc::NotifyException* exception,
                                            zx_thread_state_general_regs* regs) {
  SendExceptionNotification(exception, regs);
}

void DebuggedThread::HandleSoftwareBreakpoint(debug_ipc::NotifyException* exception,
                                              zx_thread_state_general_regs* regs) {
  auto on_stop = UpdateForSoftwareBreakpoint(regs, &exception->hit_breakpoints);
  switch (on_stop) {
    case OnStop::kIgnore:
      return;
    case OnStop::kNotify:
      SendExceptionNotification(exception, regs);
      return;
    case OnStop::kResume: {
      // We mark the thread as within an exception
      ResumeForRunMode();
      return;
    }
  }

  FXL_NOTREACHED() << "Invalid OnStop.";
}

void DebuggedThread::HandleHardwareBreakpoint(debug_ipc::NotifyException* exception,
                                              zx_thread_state_general_regs* regs) {
  uint64_t breakpoint_address = arch_provider_->BreakpointInstructionForHardwareExceptionAddress(
      *arch_provider_->IPInRegs(regs));
  HardwareBreakpoint* found_bp = process_->FindHardwareBreakpoint(breakpoint_address);
  if (found_bp) {
    UpdateForHitProcessBreakpoint(debug_ipc::BreakpointType::kHardware, found_bp, regs,
                                  &exception->hit_breakpoints);
  } else {
    // Hit a hw debug exception that doesn't belong to any ProcessBreakpoint.
    // This is probably a race between the removal and the exception handler.
    *arch_provider_->IPInRegs(regs) = breakpoint_address;
  }

  // The ProcessBreakpoint could've been deleted if it was a one-shot, so must
  // not be derefereced below this.
  found_bp = nullptr;
  SendExceptionNotification(exception, regs);
}

void DebuggedThread::HandleWatchpoint(debug_ipc::NotifyException* exception,
                                      zx_thread_state_general_regs* regs) {
  auto [range, slot] = arch_provider_->InstructionForWatchpointHit(*this);
  DEBUG_LOG(Thread) << "Found watchpoint hit at 0x" << std::hex << range.ToString() << " on slot "
                    << std::dec << slot;

  // If no process matches this watchpoint, we send the exception notification and let the client
  // handle the exception.
  if (slot == -1) {
    DEBUG_LOG(Thread) << "Could not find watchpoint.";
    SendExceptionNotification(exception, regs);
    return;
  }

  // Comparison is by the base of the address range.
  Watchpoint* watchpoint = process_->FindWatchpoint(range);
  if (!watchpoint) {
    DEBUG_LOG(Thread) << "Could not find watchpoint for range " << range.ToString();
    SendExceptionNotification(exception, regs);
    return;
  }

  // TODO(donosoc): Plumb in R/RW types.
  UpdateForHitProcessBreakpoint(debug_ipc::BreakpointType::kWrite, watchpoint, regs,
                                &exception->hit_breakpoints);
  // The ProcessBreakpoint could'be been deleted, so we cannot use it anymore.
  watchpoint = nullptr;
  SendExceptionNotification(exception, regs);
}

void DebuggedThread::SendExceptionNotification(debug_ipc::NotifyException* exception,
                                               zx_thread_state_general_regs* regs) {
  FillThreadRecord(debug_ipc::ThreadRecord::StackAmount::kMinimal, regs, &exception->thread);

  // Keep the thread suspended for the client.

  // TODO(brettw) suspend other threads in the process and other debugged
  // processes as desired.

  LogExceptionNotification(FROM_HERE, this, *exception);

  // Send notification.
  debug_ipc::MessageWriter writer;
  debug_ipc::WriteNotifyException(*exception, &writer);
  debug_agent_->stream()->Write(writer.MessageComplete());
}

void DebuggedThread::Resume(const debug_ipc::ResumeRequest& request) {
  DEBUG_LOG(Thread) << ThreadPreamble(this)
                    << "Resuming. Run mode: " << debug_ipc::ResumeRequest::HowToString(request.how)
                    << ", Range: [" << request.range_begin << ", " << request.range_end << ").";

  run_mode_ = request.how;
  step_in_range_begin_ = request.range_begin;
  step_in_range_end_ = request.range_end;

  ResumeForRunMode();
}

void DebuggedThread::ResumeException() {
  // We need to mark that this token is correctly handled before closing it.
  if (exception_handle_.is_valid()) {
    DEBUG_LOG(Thread) << ThreadPreamble(this) << "Resuming exception handle.";
    uint32_t state = ZX_EXCEPTION_STATE_HANDLED;
    exception_handle_.set_property(ZX_PROP_EXCEPTION_STATE, &state, sizeof(state));
  }
  exception_handle_.reset();
}

void DebuggedThread::ResumeSuspension() {
  if (local_suspend_token_) {
    DEBUG_LOG(Thread) << ThreadPreamble(this) << "Resuming suspend token.";
  }
  local_suspend_token_.reset();
}

bool DebuggedThread::Suspend(bool synchronous) {
  if (local_suspend_token_)
    return false;
  local_suspend_token_ = RefCountedSuspend(synchronous);

  // If there is only one count, we know that this was the token that did the suspension.
  return suspend_count_ == 1;
}

std::unique_ptr<DebuggedThread::SuspendToken> DebuggedThread::RefCountedSuspend(bool synchronous) {
  auto token = std::unique_ptr<SuspendToken>(new SuspendToken(this));

  if (synchronous)
    WaitForSuspension(DefaultSuspendDeadline());
  return token;
}

zx::time DebuggedThread::DefaultSuspendDeadline() {
  // Various events and environments can cause suspensions to take a long time,
  // so this needs to be a relatively long time. We don't generally expect
  // error cases that take infinitely long so there isn't much downside of a
  // long timeout.
  return zx::deadline_after(zx::msec(100));
}

bool DebuggedThread::WaitForSuspension(zx::time deadline) {
  // This function is complex because a thread in an exception state can't be
  // suspended (ZX-3772). Delivery of exceptions are queued on the
  // exception port so our cached state may be stale, and exceptions can also
  // race with our suspend call.
  //
  // To manually stress-test this code, write a one-line infinite loop:
  //   volatile bool done = false;
  //   while (!done) {}
  // and step over it with "next". This will cause an infinite flood of
  // single-step exceptions as fast as the debugger can process them. Pausing
  // after doing the "next" will trigger a suspension and is more likely to
  // race with an exception.

  // If an exception happens before the suspend does, we'll never get the
  // suspend signal and will end up waiting for the entire timeout just to be
  // able to tell the difference between suspended and exception. To avoid
  // waiting for a long timeout to tell the difference, wait for short timeouts
  // multiple times.
  auto poll_time = zx::msec(10);
  zx_status_t status = ZX_OK;
  do {
    // Always check the thread state from the kernel because of queue described
    // above.
    if (IsBlockedOnException(handle_))
      return true;

    zx_signals_t observed;
    status = handle_.wait_one(ZX_THREAD_SUSPENDED, zx::deadline_after(poll_time), &observed);
    if (status == ZX_OK && (observed & ZX_THREAD_SUSPENDED))
      return true;

  } while (status == ZX_ERR_TIMED_OUT && zx::clock::get_monotonic() < deadline);
  return false;
}

void DebuggedThread::FillThreadRecord(debug_ipc::ThreadRecord::StackAmount stack_amount,
                                      const zx_thread_state_general_regs* optional_regs,
                                      debug_ipc::ThreadRecord* record) const {
  record->process_koid = process_->koid();
  record->thread_koid = koid();
  record->name = object_provider_->NameForObject(handle_);

  // State (running, blocked, etc.).
  zx_info_thread info;
  if (arch_provider_->GetInfo(handle_, ZX_INFO_THREAD, &info, sizeof(info)) == ZX_OK) {
    record->state = ThreadStateToEnums(info.state, &record->blocked_reason);
  } else {
    FXL_NOTREACHED();
    record->state = debug_ipc::ThreadRecord::State::kDead;
  }

  // The registers are available when suspended or blocked in an exception.
  if ((info.state == ZX_THREAD_STATE_SUSPENDED ||
       info.state == ZX_THREAD_STATE_BLOCKED_EXCEPTION) &&
      stack_amount != debug_ipc::ThreadRecord::StackAmount::kNone) {
    // Only record this when we actually attempt to query the stack.
    record->stack_amount = stack_amount;

    // The registers are required, fetch them if the caller didn't provide.
    zx_thread_state_general_regs queried_regs;  // Storage for fetched regs.
    zx_thread_state_general_regs* regs = nullptr;
    if (!optional_regs) {
      if (arch_provider_->ReadGeneralState(handle_, &queried_regs) == ZX_OK)
        regs = &queried_regs;
    } else {
      // We don't change the values here but *InRegs below returns mutable
      // references so we need a mutable pointer.
      regs = const_cast<zx_thread_state_general_regs*>(optional_regs);
    }

    if (regs) {
      // Minimal stacks are 2 (current frame and calling one). Full stacks max
      // out at 256 to prevent edge cases, especially around corrupted stacks.
      uint32_t max_stack_depth =
          stack_amount == debug_ipc::ThreadRecord::StackAmount::kMinimal ? 2 : 256;

      UnwindStack(process_->handle(), process_->dl_debug_addr(), handle_, *regs, max_stack_depth,
                  &record->frames);
    }
  } else {
    // Didn't bother querying the stack.
    record->stack_amount = debug_ipc::ThreadRecord::StackAmount::kNone;
    record->frames.clear();
  }
}

void DebuggedThread::ReadRegisters(const std::vector<debug_ipc::RegisterCategory>& cats_to_get,
                                   std::vector<debug_ipc::Register>* out) const {
  out->clear();
  for (const auto& cat_type : cats_to_get) {
    zx_status_t status = arch_provider_->ReadRegisters(cat_type, handle_, out);
    if (status != ZX_OK) {
      DEBUG_LOG(Thread) << "Could not get register state for category: "
                        << debug_ipc::RegisterCategoryToString(cat_type);
    }
  }
}

zx_status_t DebuggedThread::WriteRegisters(const std::vector<debug_ipc::Register>& regs,
                                           std::vector<debug_ipc::Register>* written) {
  bool rip_change = false;
  debug_ipc::RegisterID rip_id =
      GetSpecialRegisterID(arch_provider_->GetArch(), debug_ipc::SpecialRegisterType::kIP);

  // Figure out which registers will change.
  std::map<debug_ipc::RegisterCategory, std::vector<debug_ipc::Register>> categories;
  for (const debug_ipc::Register& reg : regs) {
    auto cat_type = debug_ipc::RegisterIDToCategory(reg.id);
    if (cat_type == debug_ipc::RegisterCategory::kNone) {
      FXL_LOG(WARNING) << "Attempting to change register without category: "
                       << RegisterIDToString(reg.id);
      continue;
    }

    // We are changing the RIP, meaning that we're not going to jump over a breakpoint.
    if (reg.id == rip_id)
      rip_change = true;

    categories[cat_type].push_back(reg);
  }

  for (const auto& [cat_type, cat_regs] : categories) {
    FXL_DCHECK(cat_type != debug_ipc::RegisterCategory::kNone);
    zx_status_t res = arch_provider_->WriteRegisters(cat_type, cat_regs, &handle_);
    if (res != ZX_OK) {
      FXL_LOG(WARNING) << "Could not write category "
                       << debug_ipc::RegisterCategoryToString(cat_type) << ": "
                       << debug_ipc::ZxStatusToString(res);
    }

    res = arch_provider_->ReadRegisters(cat_type, handle_, written);
    if (res != ZX_OK) {
      FXL_LOG(WARNING) << "Could not read category "
                       << debug_ipc::RegisterCategoryToString(cat_type) << ": "
                       << debug_ipc::ZxStatusToString(res);
    }
  }

  // If the debug agent wrote to the thread IP directly, then current state is no longer valid.
  // Specifically, if we're currently on a breakpoint, we have to now know the fact that we're no
  // longer in a breakpoint.
  //
  // This is necessary to avoid the single-stepping logic that the thread does when resuming from a
  // breakpoint.
  if (rip_change)
    current_breakpoint_ = nullptr;

  return ZX_OK;
}

void DebuggedThread::SendThreadNotification() const {
  DEBUG_LOG(Thread) << ThreadPreamble(this) << "Sending starting notification.";
  debug_ipc::NotifyThread notify;
  FillThreadRecord(debug_ipc::ThreadRecord::StackAmount::kMinimal, nullptr, &notify.record);

  debug_ipc::MessageWriter writer;
  debug_ipc::WriteNotifyThread(debug_ipc::MsgHeader::Type::kNotifyThreadStarting, notify, &writer);
  debug_agent_->stream()->Write(writer.MessageComplete());
}

void DebuggedThread::WillDeleteProcessBreakpoint(ProcessBreakpoint* bp) {
  if (current_breakpoint_ == bp)
    current_breakpoint_ = nullptr;
}

DebuggedThread::OnStop DebuggedThread::UpdateForSoftwareBreakpoint(
    zx_thread_state_general_regs* regs, std::vector<debug_ipc::BreakpointStats>* hit_breakpoints) {
  // Get the correct address where the CPU is after hitting a breakpoint
  // (this is architecture specific).
  uint64_t breakpoint_address = arch_provider_->BreakpointInstructionForSoftwareExceptionAddress(
      *arch_provider_->IPInRegs(regs));

  SoftwareBreakpoint* found_bp = process_->FindSoftwareBreakpoint(breakpoint_address);
  if (found_bp) {
    LogHitBreakpoint(FROM_HERE, this, found_bp, breakpoint_address);

    FixSoftwareBreakpointAddress(found_bp, regs);

    // When hitting a breakpoint, we need to check if indeed this exception
    // should apply to this thread or not.
    if (!found_bp->ShouldHitThread(koid())) {
      DEBUG_LOG(Thread) << ThreadPreamble(this) << "SW Breakpoint not for me. Ignoring.";
      // The way to go over is to step over the breakpoint as one would over a resume.
      current_breakpoint_ = found_bp;
      return OnStop::kResume;
    }

    UpdateForHitProcessBreakpoint(debug_ipc::BreakpointType::kSoftware, found_bp, regs,
                                  hit_breakpoints);

    // The found_bp could have been deleted if it was a one-shot, so must
    // not be dereferenced below this.
    found_bp = nullptr;
  } else {
    // Hit a software breakpoint that doesn't correspond to any current
    // breakpoint.
    if (arch_provider_->IsBreakpointInstruction(process_->handle(), breakpoint_address)) {
      // The breakpoint is a hardcoded instruction in the program code. In
      // this case we want to continue from the following instruction since
      // the breakpoint instruction will never go away.
      *arch_provider_->IPInRegs(regs) = arch_provider_->NextInstructionForSoftwareExceptionAddress(
          *arch_provider_->IPInRegs(regs));
      zx_status_t status = arch_provider_->WriteGeneralState(handle_, *regs);
      if (status != ZX_OK) {
        fprintf(stderr, "Warning: could not update IP on thread, error = %d.",
                static_cast<int>(status));
      }

      if (!process_->dl_debug_addr() && process_->RegisterDebugState()) {
        DEBUG_LOG(Thread) << ThreadPreamble(this) << "Found ld.so breakpoint. Sending modules.";
        // This breakpoint was the explicit breakpoint ld.so executes to notify us that the loader
        // is ready (see DebuggerProcess::RegisterDebugState).
        //
        // Send the current module list and silently keep this thread stopped. The client will
        // explicitly resume this thread when it's ready to continue (it will need to load symbols
        // for the modules and may need to set breakpoints based on them).
        std::vector<uint64_t> paused_threads;
        paused_threads.push_back(koid());
        process_->SendModuleNotification(std::move(paused_threads));
        return OnStop::kIgnore;
      }
    } else {
      DEBUG_LOG(Thread) << ThreadPreamble(this) << "Hit non debugger SW breakpoint on 0x"
                        << std::hex << breakpoint_address;
      // Not a breakpoint instruction. Probably the breakpoint instruction
      // used to be ours but its removal raced with the exception handler.
      // Resume from the instruction that used to be the breakpoint.
      *arch_provider_->IPInRegs(regs) = breakpoint_address;

      // Don't automatically continue execution here. A race for this should
      // be unusual and maybe something weird happened that caused an
      // exception we're not set up to handle. Err on the side of telling the
      // user about the exception.
    }
  }
  return OnStop::kNotify;
}

void DebuggedThread::FixSoftwareBreakpointAddress(ProcessBreakpoint* process_breakpoint,
                                                  zx_thread_state_general_regs* regs) {
  // When the program hits one of our breakpoints, set the IP back to the exact address that
  // triggered the breakpoint. When the thread resumes, this is the address that it will resume
  // from (after putting back the original instruction), and will be what the client wants to
  // display to the user.
  *arch_provider_->IPInRegs(regs) = process_breakpoint->address();
  zx_status_t status = arch_provider_->WriteGeneralState(handle_, *regs);
  if (status != ZX_OK) {
    fprintf(stderr, "Warning: could not update IP on thread, error = %d.",
            static_cast<int>(status));
  }
}

void DebuggedThread::UpdateForHitProcessBreakpoint(
    debug_ipc::BreakpointType exception_type, ProcessBreakpoint* process_breakpoint,
    zx_thread_state_general_regs* regs, std::vector<debug_ipc::BreakpointStats>* hit_breakpoints) {
  current_breakpoint_ = process_breakpoint;

  process_breakpoint->OnHit(exception_type, hit_breakpoints);

  // Delete any one-shot breakpoints. Since there can be multiple Breakpoints
  // (some one-shot, some not) referring to the current ProcessBreakpoint,
  // this operation could delete the ProcessBreakpoint or it could not. If it
  // does, our observer will be told and current_breakpoint_ will be cleared.
  for (const auto& stats : *hit_breakpoints) {
    if (stats.should_delete)
      process_->debug_agent()->RemoveBreakpoint(stats.id);
  }
}

void DebuggedThread::ResumeForRunMode() {
  // We check if we're set to currently step over a breakpoint. If so we need to do some special
  // handling, as going over a breakpoint is always a single-step operation.
  // After that we can continue according to the set run-mode.
  if (IsInException() && current_breakpoint_) {
    DEBUG_LOG(Thread) << ThreadPreamble(this) << "Stepping over breakpoint: 0x" << std::hex
                      << current_breakpoint_->address();
    SetSingleStep(true);
    current_breakpoint_->BeginStepOver(this);

    // In this case, the breakpoint takes control of the thread lifetime and has already set the
    // thread to resume.
    return;
  }

  // We're not handling the special "step over a breakpoint case". This is the normal resume case.
  // This could've been triggered by an internal resume (eg. triggered by a breakpoint), so we need
  // to check if the client actually wants this thread to resume.
  if (client_state_ == ClientState::kPaused)
    return;

  // All non-continue resumptions require single stepping.
  SetSingleStep(run_mode_ != debug_ipc::ResumeRequest::How::kContinue);
  ResumeException();
  ResumeSuspension();
}

void DebuggedThread::SetSingleStep(bool single_step) {
  arch_provider_->WriteSingleStep(handle_, single_step);
}

const char* DebuggedThread::ClientStateToString(ClientState client_state) {
  switch (client_state) {
    case ClientState::kRunning:
      return "Running";
    case ClientState::kPaused:
      return "Paused";
  }

  FXL_NOTREACHED();
  return "<unknown>";
}

void DebuggedThread::IncreaseSuspend() {
  suspend_count_++;

  // We only need to keep one suspend token around.
  if (ref_counted_suspend_token_.is_valid())
    return;

  zx_status_t status = handle_.suspend(&ref_counted_suspend_token_);
  if (status != ZX_OK) {
    DEBUG_LOG(Thread) << ThreadPreamble(this)
                      << "Could not suspend: " << zx_status_get_string(status);
  }
}

void DebuggedThread::DecreaseSuspend() {
  suspend_count_--;
  FXL_DCHECK(suspend_count_ >= 0);
  if (suspend_count_ > 0)
    return;
  ref_counted_suspend_token_.reset();
}

}  // namespace debug_agent
