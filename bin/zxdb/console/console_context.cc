// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/console/console_context.h"

#include <inttypes.h>

#include "garnet/bin/zxdb/client/breakpoint.h"
#include "garnet/bin/zxdb/client/process.h"
#include "garnet/bin/zxdb/client/session.h"
#include "garnet/bin/zxdb/client/thread.h"
#include "garnet/bin/zxdb/console/command.h"
#include "garnet/bin/zxdb/console/command_utils.h"
#include "garnet/bin/zxdb/console/console.h"
#include "garnet/bin/zxdb/console/output_buffer.h"
#include "garnet/public/lib/fxl/logging.h"
#include "garnet/public/lib/fxl/strings/string_printf.h"

namespace zxdb {

ConsoleContext::ConsoleContext(Session* session) : session_(session) {
  session->system().AddObserver(this);

  // Pick up any previously created targets. This will normally just be the
  // default one.
  for (Target* target : session->system().GetTargets())
    DidCreateTarget(target);
}

ConsoleContext::~ConsoleContext() {
  session_->system().RemoveObserver(this);
}

int ConsoleContext::IdForTarget(const Target* target) const {
  const auto& found = target_to_id_.find(target);
  if (found == target_to_id_.end()) {
    FXL_NOTREACHED();
    return 0;
  }
  return found->second;
}

int ConsoleContext::IdForThread(const Thread* thread) const {
  const TargetRecord* record =
      const_cast<ConsoleContext*>(this)->GetTargetRecord(
          thread->GetProcess()->GetTarget());
  if (!record)
    return 0;

  auto found = record->thread_to_id.find(thread);
  if (found == record->thread_to_id.end()) {
    FXL_NOTREACHED();
    return 0;
  }
  return found->second;
}

int ConsoleContext::IdForBreakpoint(const Breakpoint* breakpoint) const {
  auto found = breakpoint_to_id_.find(breakpoint);
  if (found == breakpoint_to_id_.end()) {
    FXL_NOTREACHED();
    return 0;
  }
  return found->second;
}

void ConsoleContext::SetActiveTarget(const Target* target) {
  auto found = target_to_id_.find(target);
  if (found == target_to_id_.end()) {
    FXL_NOTREACHED();
    return;
  }
  active_target_id_ = found->second;
}

int ConsoleContext::GetActiveTargetId() const {
  return active_target_id_;
}

Target* ConsoleContext::GetActiveTarget() const {
  auto found = id_to_target_.find(active_target_id_);
  if (found == id_to_target_.end())
    return nullptr;
  return found->second.target;
}

void ConsoleContext::SetActiveThreadForTarget(const Thread* thread) {
  TargetRecord* record = GetTargetRecord(thread->GetProcess()->GetTarget());
  if (!record)
    return;

  auto found = record->thread_to_id.find(thread);
  if (found == record->thread_to_id.end()) {
    FXL_NOTREACHED();
    return;
  }
  record->active_thread_id = found->second;
}

int ConsoleContext::GetActiveThreadIdForTarget(const Target* target) {
  const TargetRecord* record = GetTargetRecord(target);
  if (!record) {
    FXL_NOTREACHED();
    return 0;
  }
  return record->active_thread_id;
}

void ConsoleContext::SetActiveBreakpoint(const Breakpoint* breakpoint) {
  int id = IdForBreakpoint(breakpoint);
  if (id != 0)
    active_breakpoint_id_ = id;
}

int ConsoleContext::GetActiveBreakpointId() const {
  return active_breakpoint_id_;
}

Breakpoint* ConsoleContext::GetActiveBreakpoint() const {
  if (active_breakpoint_id_ == 0)
    return nullptr;
  auto found = id_to_breakpoint_.find(active_breakpoint_id_);
  if (found == id_to_breakpoint_.end()) {
    FXL_NOTREACHED();
    return nullptr;
  }
  return found->second;
}

Err ConsoleContext::FillOutCommand(Command* cmd) {
  // Find the target/process.
  int target_id = cmd->GetNounIndex(Noun::kProcess);
  if (target_id == Command::kNoIndex) {
    // No index: use the active one (which should always exist).
    target_id = active_target_id_;
    cmd->set_target(id_to_target_[target_id].target);
    FXL_DCHECK(cmd->target());  // Default target should always exist.
  } else {
    // Explicit index given, look it up.
    auto found_target = id_to_target_.find(target_id);
    if (found_target == id_to_target_.end()) {
      return Err(ErrType::kInput, fxl::StringPrintf(
          "There is no process %d.", target_id));
    }
    cmd->set_target(found_target->second.target);
  }

  // The ID should have been validated above.
  TargetRecord* record = GetTargetRecord(target_id);
  FXL_DCHECK(record);

  // Find the thread.
  int thread_id = cmd->GetNounIndex(Noun::kThread);
  if (thread_id == Command::kNoIndex) {
    // No index: use the active one (though it may not exist if the program
    // isn't running, for example).
    if (cmd->HasNoun(Noun::kThread) && !record->active_thread_id) {
      return Err(ErrType::kInput,
          "There is no active thread.\n"
          "Use \"thread <index>\" to set the active one, or specify an "
          "explicit\none for a command via \"thread <index> <command>\".");
    }
    thread_id = record->active_thread_id;
    cmd->set_thread(record->id_to_thread[thread_id]);
  } else {
    // Explicit index given, look it up.
    auto found_thread = record->id_to_thread.find(thread_id);
    if (found_thread == record->id_to_thread.end()) {
      if (record->id_to_thread.empty()) {
        return Err(ErrType::kInput, fxl::StringPrintf(
            "There are no threads in process %d.", target_id));
      }
      return Err(ErrType::kInput, fxl::StringPrintf(
          "There is no thread %d in process %d.", thread_id, target_id));
    }
    cmd->set_thread(found_thread->second);
  }

  // Breakpoint.
  int breakpoint_id = cmd->GetNounIndex(Noun::kBreakpoint);
  if (breakpoint_id == Command::kNoIndex) {
    // No index: use the active one (which may not exist).
    if (cmd->HasNoun(Noun::kBreakpoint) && !active_breakpoint_id_) {
      return Err(ErrType::kInput,
          "There is no active breakpoint.\n"
          "Use \"breakpoint <index>\" to set the active one, or specify an "
          "explicit\none for a command via \"breakpoint <index> <command>\".");
    }
    breakpoint_id = active_breakpoint_id_;
    cmd->set_breakpoint(GetActiveBreakpoint());
  } else {
    // Explicit index given, look it up.
    auto found_breakpoint = id_to_breakpoint_.find(breakpoint_id);
    if (found_breakpoint == id_to_breakpoint_.end()) {
      return Err(ErrType::kInput,
                 fxl::StringPrintf("There is no breakpoint %d.",
                                   breakpoint_id));
    }
    cmd->set_breakpoint(found_breakpoint->second);
  }

  return Err();
}

void ConsoleContext::DidCreateTarget(Target* target) {
  target->AddObserver(this);

  int new_id = next_target_id_;
  next_target_id_++;

  TargetRecord record;
  record.target_id = new_id;
  record.target = target;

  id_to_target_[new_id] = std::move(record);
  target_to_id_[target] = new_id;

  // Set the active target only if there's none already.
  if (active_target_id_ == 0)
    active_target_id_ = new_id;
}

void ConsoleContext::WillDestroyTarget(Target* target) {
  target->RemoveObserver(this);

  TargetRecord* record = GetTargetRecord(target);
  if (!record) {
    FXL_NOTREACHED();
    return;
  }

  if (active_target_id_ == record->target_id) {
    // Need to update the default target ID.
    if (id_to_target_.empty()) {
      // This should only happen in the shutting-down case.
      active_target_id_ = 0;
    } else {
      // Just pick the first target to be the active one. It might be nice to
      // have an ordering of which one the user had selected previously in
      // case they're toggling between two.
      active_target_id_ = id_to_target_.begin()->first;
    }
  }

  // There should be no threads by the time we erase the target mapping.
  FXL_DCHECK(record->id_to_thread.empty());
  FXL_DCHECK(record->thread_to_id.empty());

  target_to_id_.erase(target);
  id_to_target_.erase(record->target_id);
  // *record is now invalid.
}

void ConsoleContext::DidCreateBreakpoint(Breakpoint* breakpoint) {
  int id = next_breakpoint_id_;
  next_breakpoint_id_++;

  id_to_breakpoint_[id] = breakpoint;
  breakpoint_to_id_[breakpoint] = id;
}

void ConsoleContext::WillDestroyBreakpoint(Breakpoint* breakpoint) {
  auto found_breakpoint = breakpoint_to_id_.find(breakpoint);
  if (found_breakpoint == breakpoint_to_id_.end()) {
    FXL_NOTREACHED();
    return;
  }
  int id = found_breakpoint->second;

  // Clear any active breakpoint if it's the deleted one.
  if (active_breakpoint_id_ == id)
    active_breakpoint_id_ = 0;

  id_to_breakpoint_.erase(id);
  breakpoint_to_id_.erase(found_breakpoint);

}

void ConsoleContext::DidCreateProcess(Target* target, Process* process) {
  TargetRecord* record = GetTargetRecord(target);
  if (!record) {
    FXL_NOTREACHED();
    return;
  }

  process->AddObserver(this);

  // Restart the thread ID counting when the process starts in case this
  // target was previously running (we want to restart numbering every time).
  record->next_thread_id = 1;
}

void ConsoleContext::DidDestroyProcess(Target* target, DestroyReason reason,
                                       int exit_code) {
  TargetRecord* record = GetTargetRecord(target);
  if (!record) {
    FXL_NOTREACHED();
    return;
  }

  Console* console = Console::get();
  std::string msg;
  switch (reason) {
    case TargetObserver::DestroyReason::kExit:
      msg = fxl::StringPrintf("Exited with code %d: ", exit_code);
      break;
    case TargetObserver::DestroyReason::kDetach:
      msg += "Detached: ";
      break;
    case TargetObserver::DestroyReason::kKill:
      msg += "Killed: ";
      break;
  }
  msg += DescribeTarget(this, target, false);

  console->Output(msg);
}

void ConsoleContext::DidCreateThread(Process* process, Thread* thread) {
  TargetRecord* record = GetTargetRecord(process->GetTarget());
  if (!record) {
    FXL_NOTREACHED();
    return;
  }

  thread->AddObserver(this);

  int thread_id = record->next_thread_id;
  record->next_thread_id++;

  record->id_to_thread[thread_id] = thread;
  record->thread_to_id[thread] = thread_id;

  // Only make a new thread the default if there is no current thread,
  // otherwise the context will be swapping out from under the user as the
  // program runs.
  if (record->active_thread_id == 0)
    record->active_thread_id = thread_id;
}

void ConsoleContext::WillDestroyThread(Process* process, Thread* thread) {
  TargetRecord* record = GetTargetRecord(process->GetTarget());
  if (!record) {
    FXL_NOTREACHED();
    return;
  }

  thread->RemoveObserver(this);

  auto found_thread_to_id = record->thread_to_id.find(thread);
  if (found_thread_to_id == record->thread_to_id.end()) {
    FXL_NOTREACHED();
    return;
  }
  int thread_id = found_thread_to_id->second;

  record->id_to_thread.erase(found_thread_to_id->second);
  record->thread_to_id.erase(found_thread_to_id);

  // Update the active thread if the currently active one is being deleted.
  if (thread_id == record->active_thread_id) {
    // Just pick the first thread to be the active one. It might be nice to
    // have an ordering of which one the user had selected previously in
    // case they're toggling between two.
    if (record->id_to_thread.empty()) {
      record->active_thread_id = 0;
    } else {
      record->active_thread_id = record->id_to_thread.begin()->first;
    }
  }
}

void ConsoleContext::OnThreadStopped(Thread* thread,
                                     debug_ipc::NotifyException::Type type,
                                     uint64_t address) {
  // Set this process and thread as active.
  SetActiveTarget(thread->GetProcess()->GetTarget());
  SetActiveThreadForTarget(thread);

  Console* console = Console::get();
  OutputBuffer out;
  out.Append(fxl::StringPrintf(
      "Thread stopped on %s exception @ 0x%" PRIx64 "\n",
      ExceptionTypeToString(type).c_str(), address));

  // Only print out the process when there's more than one.
  if (id_to_target_.size() > 1) {
    out.Append(DescribeTarget(this, thread->GetProcess()->GetTarget(), false));
    out.Append("\n");
  }

  out.Append(DescribeThread(this, thread, false));
  console->Output(std::move(out));
}

ConsoleContext::TargetRecord* ConsoleContext::GetTargetRecord(int target_id) {
  auto found_to_record = id_to_target_.find(target_id);
  if (found_to_record == id_to_target_.end()) {
    FXL_NOTREACHED();
    return nullptr;
  }
  return &found_to_record->second;
}

ConsoleContext::TargetRecord* ConsoleContext::GetTargetRecord(
    const Target* target) {
  auto found_to_id = target_to_id_.find(target);
  if (found_to_id == target_to_id_.end()) {
    FXL_NOTREACHED();
    return nullptr;
  }
  return GetTargetRecord(found_to_id->second);
}

}  // namespace zxdb
