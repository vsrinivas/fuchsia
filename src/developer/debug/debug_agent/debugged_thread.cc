// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/debug_agent/debugged_thread.h"

#include <inttypes.h>
#include <lib/syslog/cpp/macros.h>
#include <zircon/status.h>
#include <zircon/syscalls/debug.h>
#include <zircon/syscalls/exception.h>

#include <memory>

#include "src/developer/debug/debug_agent/arch.h"
#include "src/developer/debug/debug_agent/debug_agent.h"
#include "src/developer/debug/debug_agent/debugged_process.h"
#include "src/developer/debug/debug_agent/hardware_breakpoint.h"
#include "src/developer/debug/debug_agent/process_breakpoint.h"
#include "src/developer/debug/debug_agent/software_breakpoint.h"
#include "src/developer/debug/debug_agent/unwind.h"
#include "src/developer/debug/debug_agent/watchpoint.h"
#include "src/developer/debug/ipc/agent_protocol.h"
#include "src/developer/debug/ipc/message_reader.h"
#include "src/developer/debug/ipc/message_writer.h"
#include "src/developer/debug/shared/logging/logging.h"
#include "src/developer/debug/shared/platform_message_loop.h"
#include "src/developer/debug/shared/stream_buffer.h"
#include "src/developer/debug/shared/zx_status.h"
#include "src/lib/fxl/strings/string_printf.h"

namespace debug_agent {

namespace {

// Used to have better context upon reading the debug logs.
std::string ThreadPreamble(const DebuggedThread* thread) {
  return fxl::StringPrintf("[Pr: %lu (%s), T: %lu] ", thread->process()->koid(),
                           thread->process()->process_handle().GetName().c_str(), thread->koid());
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

DebuggedThread::DebuggedThread(DebugAgent* debug_agent, DebuggedProcess* process,
                               std::unique_ptr<ThreadHandle> handle,
                               ThreadCreationOption creation_option,
                               std::unique_ptr<ExceptionHandle> exception)
    : thread_handle_(std::move(handle)),
      debug_agent_(debug_agent),
      process_(process),
      exception_handle_(std::move(exception)),
      weak_factory_(this) {
  switch (creation_option) {
    case ThreadCreationOption::kRunningKeepRunning:
      // do nothing
      break;
    case ThreadCreationOption::kSuspendedKeepSuspended:
      break;
    case ThreadCreationOption::kSuspendedShouldRun:
      InternalResumeException();
      break;
  }
}

DebuggedThread::~DebuggedThread() = default;

fxl::WeakPtr<DebuggedThread> DebuggedThread::GetWeakPtr() { return weak_factory_.GetWeakPtr(); }

void DebuggedThread::OnException(std::unique_ptr<ExceptionHandle> exception_handle) {
  exception_handle_ = std::move(exception_handle);

  debug_ipc::ExceptionType type = exception_handle_->GetType(*thread_handle_);

  std::optional<GeneralRegisters> regs = thread_handle_->GetGeneralRegisters();
  if (!regs) {
    // This can happen, for example, if the thread was killed during the time the exception message
    // was waiting to be delivered to us.
    FX_LOGS(WARNING) << "Could not read registers from thread.";
    return;
  }

  DEBUG_LOG(Thread) << ThreadPreamble(this) << "Exception @ 0x" << std::hex << regs->ip()
                    << std::dec << ": " << debug_ipc::ExceptionTypeToString(type);

  debug_ipc::NotifyException exception{};
  exception.type = type;
  exception.exception = thread_handle_->GetExceptionRecord();

  switch (type) {
    case debug_ipc::ExceptionType::kSingleStep:
      return HandleSingleStep(&exception, *regs);
    case debug_ipc::ExceptionType::kSoftwareBreakpoint:
      return HandleSoftwareBreakpoint(&exception, *regs);
    case debug_ipc::ExceptionType::kHardwareBreakpoint:
      return HandleHardwareBreakpoint(&exception, *regs);
    case debug_ipc::ExceptionType::kWatchpoint:
      return HandleWatchpoint(&exception, *regs);
    case debug_ipc::ExceptionType::kNone:
    case debug_ipc::ExceptionType::kLast:
      break;
    // TODO(donosoc): Should synthetic be general or invalid?
    case debug_ipc::ExceptionType::kSynthetic:
    default:
      return HandleGeneralException(&exception, *regs);
  }

  FX_NOTREACHED() << "Invalid exception notification type: "
                  << debug_ipc::ExceptionTypeToString(type);

  // The exception was unhandled, so we close it so that the system can run its course. The
  // destructor would've done it anyway, but being explicit helps readability.
  exception_handle_ = nullptr;
}

void DebuggedThread::ResumeFromException() {
  if (in_exception() && current_breakpoint_) {
    // Resuming from a breakpoint hit. Going over a breakpoint requires removing the breakpoint,
    // single-stepping the thread, and putting the breakpoint back.
    DEBUG_LOG(Thread) << ThreadPreamble(this) << "Stepping over breakpoint: 0x" << std::hex
                      << current_breakpoint_->address();

    // BeginStepOver() will takes responsibility for resuming the exception at the proper time.
    current_breakpoint_->BeginStepOver(this);
  } else {
    // Normal exception resumption.
    InternalResumeException();
  }
}

void DebuggedThread::HandleSingleStep(debug_ipc::NotifyException* exception,
                                      const GeneralRegisters& regs) {
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
    // NOTE: It's important to resume the exception *after* telling the breakpoint we are done going
    //       over it. This is because in the case that there are no other threads queued (the normal
    //       case), it produces a window between resuming the exception and suspending the thread
    //       to reinstall the breakpointer, which could make the thread miss the exception. By
    //       keeping the exception until *after* the breakpoint has been told to step over, we
    //       ensure that any installs have already occured and thus the thread won't miss any
    //       breakpoints.
    current_breakpoint_->EndStepOver(this);
    current_breakpoint_ = nullptr;

    InternalResumeException();
    return;
  }

  if (!debug_ipc::ResumeRequest::MakesStep(run_mode_)) {
    // This could be due to a race where the user was previously single stepping and then requested
    // a continue or forward before the single stepping completed. It could also be a breakpoint
    // that was deleted while in the process of single-stepping over it. In both cases, the least
    // confusing thing is to resume automatically (since forwarding the single step exception to the
    // debugged program makes no sense).
    DEBUG_LOG(Thread) << ThreadPreamble(this) << "Single step without breakpoint. Continuing.";
    ResumeFromException();
    return;
  }

  // When stepping in a range, automatically continue as long as we're still in range.
  if (run_mode_ == debug_ipc::ResumeRequest::How::kStepInRange &&
      regs.ip() >= step_in_range_begin_ && regs.ip() < step_in_range_end_) {
    DEBUG_LOG(Thread) << ThreadPreamble(this) << "Stepping in range. Continuing.";
    ResumeFromException();
    return;
  }

  DEBUG_LOG(Thread) << ThreadPreamble(this) << "Expected single step. Notifying.";
  SendExceptionNotification(exception, regs);
}

void DebuggedThread::HandleGeneralException(debug_ipc::NotifyException* exception,
                                            const GeneralRegisters& regs) {
  auto strategy = exception_handle_->GetStrategy();
  if (strategy.is_error()) {
    FX_LOGS(WARNING) << "Failed to determine current exception strategy: "
                     << zx_status_get_string(strategy.error_value());
    return;
  }

  debug_ipc::ExceptionStrategy applied = strategy.value();
  bool handle_now = true;

  // If the strategy is first-chance, then this is the first that we've seen this exception.
  // Further, if the applied strategy for this type is second-chance, update and handle it
  // accordingly.
  auto applicable_strategy = debug_agent_->GetExceptionStrategy(exception->type);
  if (strategy.value() == debug_ipc::ExceptionStrategy::kFirstChance &&
      applicable_strategy == debug_ipc::ExceptionStrategy::kSecondChance) {
    if (auto status = exception_handle_->SetStrategy(applicable_strategy); status != ZX_OK) {
      FX_LOGS(WARNING) << "Failed to apply default exception strategy: "
                       << zx_status_get_string(status);
      return;
    }
    applied = applicable_strategy;
    handle_now = false;
  }

  DEBUG_LOG(Thread) << ThreadPreamble(this)
                    << "Exception strategy: " << debug_ipc::ExceptionStrategyToString(applied);

  if (handle_now) {
    exception->exception.strategy = applied;
    SendExceptionNotification(exception, regs);
  }
}

void DebuggedThread::HandleSoftwareBreakpoint(debug_ipc::NotifyException* exception,
                                              GeneralRegisters& regs) {
  auto on_stop = UpdateForSoftwareBreakpoint(regs, exception->hit_breakpoints);
  switch (on_stop) {
    case OnStop::kIgnore:
      return;
    case OnStop::kNotify:
      SendExceptionNotification(exception, regs);
      return;
    case OnStop::kResume: {
      // We mark the thread as within an exception
      ResumeFromException();
      return;
    }
  }

  FX_NOTREACHED() << "Invalid OnStop.";
}

void DebuggedThread::HandleHardwareBreakpoint(debug_ipc::NotifyException* exception,
                                              GeneralRegisters& regs) {
  uint64_t breakpoint_address = arch::BreakpointInstructionForHardwareExceptionAddress(regs.ip());
  if (HardwareBreakpoint* found_bp = process_->FindHardwareBreakpoint(breakpoint_address)) {
    UpdateForHitProcessBreakpoint(debug_ipc::BreakpointType::kHardware, found_bp,
                                  exception->hit_breakpoints);
    // Note: may have deleted found_bp.
  } else {
    // Hit a hw debug exception that doesn't belong to any ProcessBreakpoint. This is probably a
    // race between the removal and the exception handler.
    regs.set_ip(breakpoint_address);
  }
  SendExceptionNotification(exception, regs);
}

void DebuggedThread::HandleWatchpoint(debug_ipc::NotifyException* exception,
                                      const GeneralRegisters& regs) {
  std::optional<DebugRegisters> debug_regs = thread_handle_->GetDebugRegisters();
  if (!debug_regs) {
    DEBUG_LOG(Thread) << "Could not load debug registers to handle watchpoint.";
    return;
  }

  std::optional<WatchpointInfo> hit = debug_regs->DecodeHitWatchpoint();
  if (!hit) {
    // When no watchpoint matches this watchpoint, send the exception notification and let the
    // debugger frontend handle the exception.
    DEBUG_LOG(Thread) << "Could not find watchpoint.";
    SendExceptionNotification(exception, regs);
    return;
  }

  DEBUG_LOG(Thread) << "Found watchpoint hit at 0x" << std::hex << hit->range.ToString()
                    << " on slot " << std::dec << hit->slot;

  // Comparison is by the base of the address range.
  Watchpoint* watchpoint = process_->FindWatchpoint(hit->range);
  if (!watchpoint) {
    DEBUG_LOG(Thread) << "Could not find watchpoint for range " << hit->range.ToString();
    SendExceptionNotification(exception, regs);
    return;
  }

  // TODO(donosoc): Plumb in R/RW types.
  UpdateForHitProcessBreakpoint(watchpoint->Type(), watchpoint, exception->hit_breakpoints);
  // The ProcessBreakpoint could'be been deleted, so we cannot use it anymore.
  watchpoint = nullptr;
  SendExceptionNotification(exception, regs);
}

void DebuggedThread::SendExceptionNotification(debug_ipc::NotifyException* exception,
                                               const GeneralRegisters& regs) {
  exception->thread = GetThreadRecord(debug_ipc::ThreadRecord::StackAmount::kMinimal, regs);

  // Keep the thread suspended for the client.

  // TODO(brettw) suspend other threads in the process and other debugged processes as desired.

  LogExceptionNotification(FROM_HERE, this, *exception);

  // Send notification.
  debug_ipc::MessageWriter writer;
  debug_ipc::WriteNotifyException(*exception, &writer);
  debug_agent_->stream()->Write(writer.MessageComplete());
}

void DebuggedThread::ClientResume(const debug_ipc::ResumeRequest& request) {
  DEBUG_LOG(Thread) << ThreadPreamble(this)
                    << "Resuming. Run mode: " << debug_ipc::ResumeRequest::HowToString(request.how)
                    << ", Range: [" << request.range_begin << ", " << request.range_end << ").";

  run_mode_ = request.how;
  step_in_range_begin_ = request.range_begin;
  step_in_range_end_ = request.range_end;

  ResumeFromException();
  if (client_suspend_handle_) {
    // Normally the single-step flat is set by the exception resumption code, but if we're resuming
    // from a pause that will do nothing so set here.
    SetSingleStepForRunMode();
    client_suspend_handle_.reset();
  }
}

void DebuggedThread::InternalResumeException() {
  if (!in_exception()) {
    DEBUG_LOG(Thread) << ThreadPreamble(this)
                      << "Resuming from exception but there is no exception, skipping.";
    return;
  }

  SetSingleStepForRunMode();

  if (run_mode_ == debug_ipc::ResumeRequest::How::kForwardAndContinue) {
    DEBUG_LOG(Thread) << ThreadPreamble(this) << "Resuming from exception (second chance).";
    zx_status_t status =
        exception_handle_->SetStrategy(debug_ipc::ExceptionStrategy::kSecondChance);
    if (status != ZX_OK) {
      DEBUG_LOG(Thread) << ThreadPreamble(this) << "Failed to set exception as second-chance: "
                        << zx_status_get_string(status);
    }
  } else {
    DEBUG_LOG(Thread) << ThreadPreamble(this) << "Resuming from exception (handled).";
    zx_status_t status = exception_handle_->SetState(ZX_EXCEPTION_STATE_HANDLED);
    if (status != ZX_OK) {
      DEBUG_LOG(Thread) << ThreadPreamble(this)
                        << "Failed to set exception as handled: " << zx_status_get_string(status);
    }
  }
  exception_handle_ = nullptr;
}

void DebuggedThread::ClientSuspend(bool synchronous) {
  if (!client_suspend_handle_)
    client_suspend_handle_ = thread_handle_->Suspend();

  // Even if there was already a client_suspend, the previous suspend could have been asynchronous
  // and still pending. When a synchronous suspend is requested make sure we honor that the thread
  // is suspended before returning. WaitForSuspension() should be relatively inexpensive if the
  // thread is already suspended.
  if (synchronous)
    thread_handle_->WaitForSuspension(DefaultSuspendDeadline());
}

std::unique_ptr<SuspendHandle> DebuggedThread::InternalSuspend(bool synchronous) {
  auto suspend_handle = thread_handle_->Suspend();
  if (synchronous)
    thread_handle_->WaitForSuspension(DefaultSuspendDeadline());
  return suspend_handle;
}

zx::time DebuggedThread::DefaultSuspendDeadline() {
  // Various events and environments can cause suspensions to take a long time, so this needs to
  // be a relatively long time. We don't generally expect error cases that take infinitely long so
  // there isn't much downside of a long timeout.
  return zx::deadline_after(zx::msec(100));
}

// Note that everything in this function is racy because the thread state can change at any time,
// even while processing an exception (an external program can kill it out from under us).
debug_ipc::ThreadRecord DebuggedThread::GetThreadRecord(
    debug_ipc::ThreadRecord::StackAmount stack_amount, std::optional<GeneralRegisters> regs) const {
  debug_ipc::ThreadRecord record = thread_handle_->GetThreadRecord(process_->koid());

  // Unwind the stack if requested. This requires the registers which are available when suspended
  // or blocked in an exception.
  if ((record.state == debug_ipc::ThreadRecord::State::kSuspended ||
       (record.state == debug_ipc::ThreadRecord::State::kBlocked &&
        record.blocked_reason == debug_ipc::ThreadRecord::BlockedReason::kException)) &&
      stack_amount != debug_ipc::ThreadRecord::StackAmount::kNone) {
    // Only record this when we actually attempt to query the stack.
    record.stack_amount = stack_amount;

    // The registers are required, fetch them if the caller didn't provide.
    if (!regs)
      regs = thread_handle_->GetGeneralRegisters();  // Note this could still fail.

    if (regs) {
      // Minimal stacks are 2 (current frame and calling one). Full stacks max out at 256 to prevent
      // edge cases, especially around corrupted stacks.
      uint32_t max_stack_depth =
          stack_amount == debug_ipc::ThreadRecord::StackAmount::kMinimal ? 2 : 256;

      UnwindStack(process_->process_handle(), process_->module_list(), thread_handle(), *regs,
                  max_stack_depth, &record.frames);
    }
  } else {
    // Didn't bother querying the stack.
    record.stack_amount = debug_ipc::ThreadRecord::StackAmount::kNone;
  }
  return record;
}

std::vector<debug_ipc::Register> DebuggedThread::ReadRegisters(
    const std::vector<debug_ipc::RegisterCategory>& cats_to_get) const {
  return thread_handle_->ReadRegisters(cats_to_get);
}

std::vector<debug_ipc::Register> DebuggedThread::WriteRegisters(
    const std::vector<debug_ipc::Register>& regs) {
  std::vector<debug_ipc::Register> written = thread_handle_->WriteRegisters(regs);

  // If we're updating the instruction pointer directly, current state is no longer valid.
  // Specifically, if we're currently on a breakpoint, we have to now know the fact that we're no
  // longer in a breakpoint.
  //
  // This is necessary to avoid the single-stepping logic that the thread does when resuming from
  // a breakpoint.
  bool rip_change = false;
  debug_ipc::RegisterID rip_id =
      GetSpecialRegisterID(arch::GetCurrentArch(), debug_ipc::SpecialRegisterType::kIP);
  for (const debug_ipc::Register& reg : regs) {
    if (reg.id == rip_id) {
      rip_change = true;
      break;
    }
  }
  if (rip_change)
    current_breakpoint_ = nullptr;

  return written;
}

void DebuggedThread::SendThreadNotification() const {
  DEBUG_LOG(Thread) << ThreadPreamble(this) << "Sending starting notification.";
  debug_ipc::NotifyThread notify;
  notify.record = GetThreadRecord(debug_ipc::ThreadRecord::StackAmount::kMinimal);

  debug_ipc::MessageWriter writer;
  debug_ipc::WriteNotifyThread(debug_ipc::MsgHeader::Type::kNotifyThreadStarting, notify, &writer);
  debug_agent_->stream()->Write(writer.MessageComplete());
}

void DebuggedThread::WillDeleteProcessBreakpoint(ProcessBreakpoint* bp) {
  if (current_breakpoint_ == bp)
    current_breakpoint_ = nullptr;
}

DebuggedThread::OnStop DebuggedThread::UpdateForSoftwareBreakpoint(
    GeneralRegisters& regs, std::vector<debug_ipc::BreakpointStats>& hit_breakpoints) {
  // Get the correct address where the CPU is after hitting a breakpoint (this is
  // architecture-specific).
  uint64_t breakpoint_address = regs.ip() - arch::kExceptionOffsetForSoftwareBreakpoint;

  if (SoftwareBreakpoint* found_bp = process_->FindSoftwareBreakpoint(breakpoint_address)) {
    LogHitBreakpoint(FROM_HERE, this, found_bp, breakpoint_address);

    FixSoftwareBreakpointAddress(found_bp, regs);

    switch (process_->HandleSpecialBreakpoint(found_bp)) {
      case DebuggedProcess::SpecialBreakpointResult::kContinue: {
        DEBUG_LOG(Thread) << ThreadPreamble(this) << "Loader breakpoint, internally resuming.";
        current_breakpoint_ = found_bp;
        return OnStop::kResume;
      }
      case DebuggedProcess::SpecialBreakpointResult::kKeepSuspended: {
        DEBUG_LOG(Thread) << ThreadPreamble(this) << "Loader breakpoint, keeping stopped.";
        current_breakpoint_ = found_bp;
        return OnStop::kIgnore;
      }
      case DebuggedProcess::SpecialBreakpointResult::kNotSpecial:
        break;
    }

    // When hitting a breakpoint, we need to check if indeed this exception should apply to this
    // thread or not.
    if (!found_bp->ShouldHitThread(koid())) {
      DEBUG_LOG(Thread) << ThreadPreamble(this) << "SW Breakpoint not for me. Ignoring.";
      // The way to go over is to step over the breakpoint as one would over a resume.
      current_breakpoint_ = found_bp;
      return OnStop::kResume;
    }

    UpdateForHitProcessBreakpoint(debug_ipc::BreakpointType::kSoftware, found_bp, hit_breakpoints);
    // Note: may have deleted found_bp!
  } else if (IsBreakpointInstructionAtAddress(breakpoint_address)) {
    // Hit a software breakpoint that doesn't correspond to any current breakpoint.
    //
    // The breakpoint is a hardcoded instruction in the program code. In this case we want to
    // continue from the following instruction since the breakpoint instruction will never go
    // away.
    regs.set_ip(arch::NextInstructionForSoftwareExceptionAddress(regs.ip()));
    thread_handle_->SetGeneralRegisters(regs);

    switch (process_->HandleSpecialBreakpoint(found_bp)) {
      case DebuggedProcess::SpecialBreakpointResult::kContinue: {
        DEBUG_LOG(Thread) << ThreadPreamble(this)
                          << "Hardcoded loader breakpoint, internally resuming.";
        return OnStop::kResume;
      }
      case DebuggedProcess::SpecialBreakpointResult::kKeepSuspended: {
        DEBUG_LOG(Thread) << ThreadPreamble(this)
                          << "Hardcoded loader breakpoint, keeping stopped.";
        current_breakpoint_ = found_bp;
        return OnStop::kIgnore;
      }
      case DebuggedProcess::SpecialBreakpointResult::kNotSpecial:
        break;
    }
  } else {
    // Not a breakpoint instruction. Probably the breakpoint instruction used to be ours but its
    // removal raced with the exception handler. Resume from the instruction that used to be the
    // breakpoint.
    DEBUG_LOG(Thread) << ThreadPreamble(this) << "Hit non debugger SW breakpoint on 0x" << std::hex
                      << breakpoint_address;
    regs.set_ip(breakpoint_address);

    // Don't automatically continue execution here. A race for this should be unusual and maybe
    // something weird happened that caused an exception we're not set up to handle. Err on the
    // side of telling the user about the exception.
  }

  return OnStop::kNotify;
}

void DebuggedThread::FixSoftwareBreakpointAddress(ProcessBreakpoint* process_breakpoint,
                                                  GeneralRegisters& regs) {
  // When the program hits one of our breakpoints, set the IP back to the exact address that
  // triggered the breakpoint. When the thread resumes, this is the address that it will resume
  // from (after putting back the original instruction), and will be what the client wants to
  // display to the user.
  regs.set_ip(process_breakpoint->address());
  thread_handle_->SetGeneralRegisters(regs);
}

void DebuggedThread::UpdateForHitProcessBreakpoint(
    debug_ipc::BreakpointType exception_type, ProcessBreakpoint* process_breakpoint,
    std::vector<debug_ipc::BreakpointStats>& hit_breakpoints) {
  current_breakpoint_ = process_breakpoint;

  process_breakpoint->OnHit(exception_type, &hit_breakpoints);

  // Delete any one-shot breakpoints. Since there can be multiple Breakpoints (some one-shot, some
  // not) referring to the current ProcessBreakpoint, this operation could delete the
  // ProcessBreakpoint or it could not. If it does, our observer will be told and
  // current_breakpoint_ will be cleared.
  for (const auto& stats : hit_breakpoints) {
    if (stats.should_delete)
      process_->debug_agent()->RemoveBreakpoint(stats.id);
  }
}

bool DebuggedThread::IsBreakpointInstructionAtAddress(uint64_t address) const {
  arch::BreakInstructionType instruction = 0;
  size_t bytes_read = 0;
  if (process_->process_handle().ReadMemory(address, &instruction, sizeof(instruction),
                                            &bytes_read) != ZX_OK ||
      bytes_read != sizeof(instruction))
    return false;
  return arch::IsBreakpointInstruction(instruction);
}

void DebuggedThread::SetSingleStepForRunMode() {
  // When we're single-stepping over a breakpoint, that overrides the user run mode.
  thread_handle_->SetSingleStep(stepping_over_breakpoint_ ||
                                debug_ipc::ResumeRequest::MakesStep(run_mode_));
}

}  // namespace debug_agent
