// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/debug_agent/debugged_thread.h"

#include <inttypes.h>
#include <zircon/syscalls/debug.h>
#include <zircon/syscalls/exception.h>
#include <memory>

#include "src/lib/fxl/logging.h"
#include "src/developer/debug/debug_agent/arch.h"
#include "src/developer/debug/debug_agent/debug_agent.h"
#include "src/developer/debug/debug_agent/debugged_process.h"
#include "src/developer/debug/debug_agent/process_breakpoint.h"
#include "src/developer/debug/debug_agent/process_info.h"
#include "src/developer/debug/debug_agent/unwind.h"
#include "src/developer/debug/ipc/agent_protocol.h"
#include "src/developer/debug/ipc/message_reader.h"
#include "src/developer/debug/ipc/message_writer.h"
#include "src/developer/debug/shared/logging/logging.h"
#include "src/developer/debug/shared/message_loop_target.h"
#include "src/developer/debug/shared/stream_buffer.h"
#include "src/developer/debug/shared/zx_status.h"

namespace debug_agent {

namespace {

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

}  // namespace

DebuggedThread::DebuggedThread(DebuggedProcess* process, zx::thread thread,
                               zx_koid_t koid, ThreadCreationOption option)
    : debug_agent_(process->debug_agent()),
      process_(process),
      thread_(std::move(thread)),
      koid_(koid) {
  switch (option) {
    case ThreadCreationOption::kRunningKeepRunning:
      // do nothing
      break;
    case ThreadCreationOption::kSuspendedKeepSuspended:
      suspend_reason_ = SuspendReason::kException;
      break;
    case ThreadCreationOption::kSuspendedShouldRun:
      debug_ipc::MessageLoopTarget::Current()->ResumeFromException(koid,
                                                                   thread_, 0);
  }
}

DebuggedThread::~DebuggedThread() {}

void DebuggedThread::OnException(uint32_t type) {
  suspend_reason_ = SuspendReason::kException;

  debug_ipc::NotifyException notify;
  notify.type = arch::ArchProvider::Get().DecodeExceptionType(*this, type);

  DEBUG_LOG(Thread) << "Thread " << koid_ << ": Received exception "
                    << ExceptionTypeToString(type) << ", interpreted as "
                    << debug_ipc::NotifyException::TypeToString(notify.type);

  if (current_breakpoint_) {
    // The current breakpoint is set only when stopped at a breakpoint or when
    // single-stepping over one. We're not going to get an exception for a
    // thread when stopped, so hitting this exception means the breakpoint is
    // done being stepped over. The breakpoint will tell us if the exception
    // was from a normal completion of the breakpoint step, or whether
    // something else went wrong while stepping.
    bool completes_bp_step =
        current_breakpoint_->BreakpointStepHasException(koid_, notify.type);
    current_breakpoint_ = nullptr;
    if (completes_bp_step &&
        run_mode_ == debug_ipc::ResumeRequest::How::kContinue) {
      // This step was an internal thing to step over the breakpoint in
      // service of continuing from a breakpoint. Transparently resume the
      // thread since the client didn't request the step. The step
      // (non-continue) cases will be handled below in the normal flow since
      // we just finished a step.
      ResumeForRunMode();
      return;
    }
    // Something else went wrong while stepping (the instruction with the
    // breakpoint could have crashed). Fall through to dispatching the
    // exception to the client.
    current_breakpoint_ = nullptr;
  }

  zx_thread_state_general_regs regs;
  thread_.read_state(ZX_THREAD_STATE_GENERAL_REGS, &regs, sizeof(regs));

  switch (type) {
    case ZX_EXCP_SW_BREAKPOINT:
      notify.type = debug_ipc::NotifyException::Type::kSoftware;
      if (UpdateForSoftwareBreakpoint(&regs, &notify.hit_breakpoints) ==
          OnStop::kIgnore)
        return;
      break;
    case ZX_EXCP_HW_BREAKPOINT: {
      if (notify.type == debug_ipc::NotifyException::Type::kSingleStep) {
        if (run_mode_ == debug_ipc::ResumeRequest::How::kContinue) {
          // This could be due to a race where the user was previously single
          // stepping and then requested a continue before the single stepping
          // completed. It could also be a breakpoint that was deleted while
          // in the process of single-stepping over it. In both cases, the
          // least confusing thing is to resume automatically.
          ResumeForRunMode();
          return;
        }

        // When stepping in a range, automatically continue as long as we're
        // still in range.
        if (run_mode_ == debug_ipc::ResumeRequest::How::kStepInRange &&
            *arch::ArchProvider::Get().IPInRegs(&regs) >=
                step_in_range_begin_ &&
            *arch::ArchProvider::Get().IPInRegs(&regs) < step_in_range_end_) {
          ResumeForRunMode();
          return;
        }
      } else if (notify.type == debug_ipc::NotifyException::Type::kHardware) {
        if (UpdateForHardwareBreakpoint(&regs, &notify.hit_breakpoints) ==
            OnStop::kIgnore)
          return;
      } else {
        FXL_NOTREACHED() << "Unexpected hw exception type: "
                         << static_cast<uint32_t>(notify.type);
      }
      break;
    }
    default:
      notify.type = debug_ipc::NotifyException::Type::kGeneral;
      break;
  }

  notify.process_koid = process_->koid();
  FillThreadRecord(debug_ipc::ThreadRecord::StackAmount::kMinimal, &regs,
                   &notify.thread);

  // Send notification.
  debug_ipc::MessageWriter writer;
  debug_ipc::WriteNotifyException(notify, &writer);
  debug_agent_->stream()->Write(writer.MessageComplete());

  // Keep the thread suspended for the client.

  // TODO(brettw) suspend other threads in the process and other debugged
  // processes as desired.
}

bool DebuggedThread::Pause() {
  if (suspend_reason_ == SuspendReason::kNone) {
    if (thread_.suspend(&suspend_token_) == ZX_OK) {
      suspend_reason_ = SuspendReason::kOther;
      return true;
    }
  }
  return false;
}

void DebuggedThread::Resume(const debug_ipc::ResumeRequest& request) {
  run_mode_ = request.how;
  step_in_range_begin_ = request.range_begin;
  step_in_range_end_ = request.range_end;

  ResumeForRunMode();
}

void DebuggedThread::FillThreadRecord(
    debug_ipc::ThreadRecord::StackAmount stack_amount,
    const zx_thread_state_general_regs* optional_regs,
    debug_ipc::ThreadRecord* record) const {
  debug_agent::FillThreadRecord(process_->process(), process_->dl_debug_addr(),
                                thread_, stack_amount, optional_regs, record);
}

void DebuggedThread::ReadRegisters(
    const std::vector<debug_ipc::RegisterCategory::Type>& cats_to_get,
    std::vector<debug_ipc::RegisterCategory>* out) const {
  out->clear();
  for (const auto& cat_type : cats_to_get) {
    auto& cat = out->emplace_back();
    cat.type = cat_type;
    zx_status_t status = arch::ArchProvider::Get().ReadRegisters(
        cat_type, thread_, &cat.registers);
    if (status != ZX_OK) {
      out->pop_back();
      FXL_LOG(ERROR) << "Could not get register state for category: "
                     << debug_ipc::RegisterCategory::TypeToString(cat_type);
    }
  }
}

zx_status_t DebuggedThread::WriteRegisters(
    const std::vector<debug_ipc::Register>& regs) {
  // We use a map to keep track of which categories will change.
  std::map<debug_ipc::RegisterCategory::Type, debug_ipc::RegisterCategory>
      categories;

  bool rip_change = false;
  debug_ipc::RegisterID rip_id = GetSpecialRegisterID(
      arch::ArchProvider::Get().GetArch(), debug_ipc::SpecialRegisterType::kIP);

  // We append each register to the correct category to be changed.
  for (const debug_ipc::Register& reg : regs) {
    auto cat_type = debug_ipc::RegisterCategory::RegisterIDToCategory(reg.id);
    if (cat_type == debug_ipc::RegisterCategory::Type::kNone) {
      FXL_LOG(WARNING) << "Attempting to change register without category: "
                       << RegisterIDToString(reg.id);
      continue;
    }

    // We are changing the RIP, meaning that we're not going to jump over a
    // breakpoint.
    if (reg.id == rip_id)
      rip_change = true;

    auto& category = categories[cat_type];
    category.type = cat_type;
    category.registers.push_back(reg);
  }

  for (const auto& [cat_type, cat] : categories) {
    FXL_DCHECK(cat_type != debug_ipc::RegisterCategory::Type::kNone);
    zx_status_t res = arch::ArchProvider::Get().WriteRegisters(cat, &thread_);
    if (res != ZX_OK) {
      FXL_LOG(WARNING) << "Could not write category "
                       << debug_ipc::RegisterCategory::TypeToString(cat_type)
                       << ": " << debug_ipc::ZxStatusToString(res);
    }
  }
  // If the debug agent wrote to the thread IP directly, then current state is
  // no longer valid. Specifically, if we're currently on a breakpoint, we have
  // to now know the fact that we're no longer in a breakpoint.
  //
  // This is necessary to avoid the single-stepping logic that the thread does
  // when resuming from a breakpoint.
  current_breakpoint_ = nullptr;
  return ZX_OK;
}

void DebuggedThread::SendThreadNotification() const {
  debug_ipc::ThreadRecord record;
  FillThreadRecord(debug_ipc::ThreadRecord::StackAmount::kMinimal, nullptr,
                   &record);

  debug_ipc::NotifyThread notify;
  notify.process_koid = process_->koid();
  notify.record = record;

  debug_ipc::MessageWriter writer;
  debug_ipc::WriteNotifyThread(
      debug_ipc::MsgHeader::Type::kNotifyThreadStarting, notify, &writer);
  debug_agent_->stream()->Write(writer.MessageComplete());
}

void DebuggedThread::WillDeleteProcessBreakpoint(ProcessBreakpoint* bp) {
  if (current_breakpoint_ == bp)
    current_breakpoint_ = nullptr;
}

DebuggedThread::OnStop DebuggedThread::UpdateForSoftwareBreakpoint(
    zx_thread_state_general_regs* regs,
    std::vector<debug_ipc::BreakpointStats>* hit_breakpoints) {
  uint64_t breakpoint_address =
      arch::ArchProvider::Get()
          .BreakpointInstructionForSoftwareExceptionAddress(
              *arch::ArchProvider::Get().IPInRegs(regs));

  ProcessBreakpoint* found_bp =
      process_->FindProcessBreakpointForAddr(breakpoint_address);
  if (found_bp) {
    // Our software breakpoint.
    UpdateForHitProcessBreakpoint(debug_ipc::BreakpointType::kSoftware,
                                  found_bp, regs, hit_breakpoints);

    // The found_bp could have been deleted if it was a one-shot, so must
    // not be dereferenced below this.
    found_bp = nullptr;
  } else {
    // Hit a software breakpoint that doesn't correspond to any current
    // breakpoint.
    if (arch::ArchProvider::Get().IsBreakpointInstruction(process_->process(),
                                                          breakpoint_address)) {
      // The breakpoint is a hardcoded instruction in the program code. In
      // this case we want to continue from the following instruction since
      // the breakpoint instruction will never go away.
      *arch::ArchProvider::Get().IPInRegs(regs) =
          arch::ArchProvider::Get().NextInstructionForSoftwareExceptionAddress(
              *arch::ArchProvider::Get().IPInRegs(regs));
      zx_status_t status =
          thread_.write_state(ZX_THREAD_STATE_GENERAL_REGS, regs,
                              sizeof(zx_thread_state_general_regs));
      if (status != ZX_OK) {
        fprintf(stderr, "Warning: could not update IP on thread, error = %d.",
                static_cast<int>(status));
      }

      if (!process_->dl_debug_addr() && process_->RegisterDebugState()) {
        // This breakpoint was the explicit breakpoint ld.so executes to
        // notify us that the loader is ready. Send the current module list
        // and silently keep this thread stopped. The client will explicitly
        // resume this thread when it's ready to continue (it will need to
        // load symbols for the modules and may need to set breakpoints based
        // on them).
        std::vector<uint64_t> paused_threads;
        paused_threads.push_back(koid());
        process_->SendModuleNotification(std::move(paused_threads));
        return OnStop::kIgnore;
      }
    } else {
      // Not a breakpoint instruction. Probably the breakpoint instruction
      // used to be ours but its removal raced with the exception handler.
      // Resume from the instruction that used to be the breakpoint.
      *arch::ArchProvider::Get().IPInRegs(regs) = breakpoint_address;

      // Don't automatically continue execution here. A race for this should
      // be unusual and maybe something weird happened that caused an
      // exception we're not set up to handle. Err on the side of telling the
      // user about the exception.
    }
  }
  return OnStop::kSendNotification;
}

DebuggedThread::OnStop DebuggedThread::UpdateForHardwareBreakpoint(
    zx_thread_state_general_regs* regs,
    std::vector<debug_ipc::BreakpointStats>* hit_breakpoints) {
  uint64_t breakpoint_address =
      arch::ArchProvider::Get()
          .BreakpointInstructionForHardwareExceptionAddress(
              *arch::ArchProvider::Get().IPInRegs(regs));
  ProcessBreakpoint* found_bp =
      process_->FindProcessBreakpointForAddr(breakpoint_address);
  if (!found_bp) {
    // Hit a hw debug exception that doesn't belong to any ProcessBreakpoint.
    // This is probably a race between the removal and the exception handler.

    // Send a notification.
    *arch::ArchProvider::Get().IPInRegs(regs) = breakpoint_address;
    return OnStop::kSendNotification;
  }

  UpdateForHitProcessBreakpoint(debug_ipc::BreakpointType::kHardware, found_bp,
                                regs, hit_breakpoints);

  // The ProcessBreakpoint could've been deleted if it was a one-shot, so must
  // not be derefereced below this.
  found_bp = nullptr;
  return OnStop::kSendNotification;
}

void DebuggedThread::UpdateForHitProcessBreakpoint(
    debug_ipc::BreakpointType exception_type,
    ProcessBreakpoint* process_breakpoint, zx_thread_state_general_regs* regs,
    std::vector<debug_ipc::BreakpointStats>* hit_breakpoints) {
  current_breakpoint_ = process_breakpoint;

  process_breakpoint->OnHit(exception_type, hit_breakpoints);

  // When the program hits one of our breakpoints, set the IP back to
  // the exact address that triggered the breakpoint. When the thread
  // resumes, this is the address that it will resume from (after
  // putting back the original instruction), and will be what the client
  // wants to display to the user.
  *arch::ArchProvider::Get().IPInRegs(regs) = process_breakpoint->address();
  zx_status_t status = thread_.write_state(
      ZX_THREAD_STATE_GENERAL_REGS, regs, sizeof(zx_thread_state_general_regs));
  if (status != ZX_OK) {
    fprintf(stderr, "Warning: could not update IP on thread, error = %d.",
            static_cast<int>(status));
  }

  // Delete any one-shot breakpoints. Since there can be multiple Breakpoints
  // (some one-shot, some not) referring to the current ProcessBreakpoint,
  // this operation could delete the ProcessBreakpoint or it could not. If it
  // does, our observer will be told and current_breakpoint_ will be cleared.
  for (const auto& stats : *hit_breakpoints) {
    if (stats.should_delete)
      process_->debug_agent()->RemoveBreakpoint(stats.breakpoint_id);
  }
}

void DebuggedThread::ResumeForRunMode() {
  // If we jumped, once we resume we reset the status.
  if (suspend_reason_ == SuspendReason::kException) {
    // Note: we could have a valid suspend token here in addition to the
    // exception if the suspension races with the delivery of the exception.
    if (current_breakpoint_) {
      // Going over a breakpoint always requires a single-step first. Then we
      // continue according to run_mode_.
      SetSingleStep(true);
      current_breakpoint_->BeginStepOver(koid_);
    } else {
      // All non-continue resumptions require single stepping.
      SetSingleStep(run_mode_ != debug_ipc::ResumeRequest::How::kContinue);
    }
    suspend_reason_ = SuspendReason::kNone;

    zx_status_t status =
        debug_ipc::MessageLoopTarget::Current()->ResumeFromException(
            koid_, thread_, 0);
    FXL_DCHECK(status == ZX_OK)
        << "Expected ZX_OK, got " << debug_ipc::ZxStatusToString(status);
  } else if (suspend_reason_ == SuspendReason::kOther) {
    // A breakpoint should only be current when it was hit which will be
    // caused by an exception.
    FXL_DCHECK(!current_breakpoint_);

    // All non-continue resumptions require single stepping.
    SetSingleStep(run_mode_ != debug_ipc::ResumeRequest::How::kContinue);

    // The suspend token is holding the thread suspended, releasing it will
    // resume (if nobody else has the thread suspended).
    suspend_reason_ = SuspendReason::kNone;
    FXL_DCHECK(suspend_token_.is_valid());
    suspend_token_.reset();
  }
}

void DebuggedThread::SetSingleStep(bool single_step) {
  zx_thread_state_single_step_t value = single_step ? 1 : 0;
  // This could fail for legitimate reasons, like the process could have just
  // closed the thread.
  thread_.write_state(ZX_THREAD_STATE_SINGLE_STEP, &value, sizeof(value));
}

const char* DebuggedThread::SuspendReasonToString(SuspendReason reason) {
  switch (reason) {
    case SuspendReason::kNone:
      return "None";
    case SuspendReason::kException:
      return "Exception";
    case SuspendReason::kOther:
      return "Other";
  }
  FXL_NOTREACHED();
  return "";
}

}  // namespace debug_agent
