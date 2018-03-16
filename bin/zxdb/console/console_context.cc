// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/console/console_context.h"

#include <inttypes.h>

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
  for (Target* target : session->system().GetAllTargets())
    DidCreateTarget(target);
}

ConsoleContext::~ConsoleContext() {
  session_->system().RemoveObserver(this);
}

int ConsoleContext::IdForTarget(Target* target) const {
  const auto& found = target_to_id_.find(target);
  if (found == target_to_id_.end()) {
    FXL_NOTREACHED();
    return 0;
  }
  return found->second;
}

int ConsoleContext::IdForThread(Thread* thread) const {
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

void ConsoleContext::SetActiveTarget(Target* target) {
  auto found = target_to_id_.find(target);
  if (found == target_to_id_.end()) {
    FXL_NOTREACHED();
    return;
  }
  active_target_id_ = found->second;
}

int ConsoleContext::GetActiveTargetId() {
  return active_target_id_;
}

Target* ConsoleContext::GetActiveTarget() {
  auto found = id_to_target_.find(active_target_id_);
  if (found == id_to_target_.end())
    return nullptr;
  return found->second.target;
}

void ConsoleContext::SetActiveThreadForTarget(Thread* thread) {
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

int ConsoleContext::GetActiveThreadIdForTarget(Target* target) {
  TargetRecord* record = GetTargetRecord(target);
  if (!record) {
    FXL_NOTREACHED();
    return 0;
  }
  return record->active_thread_id;
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
    thread_id = record->active_thread_id;
    if (thread_id > 0) {
      cmd->set_thread(record->id_to_thread[thread_id]);
      FXL_DCHECK(cmd->thread());  // Should have been validated above.
    }
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

void ConsoleContext::DidChangeTargetState(Target* target,
                                          Target::State old_state) {
  TargetRecord* record = GetTargetRecord(target);
  if (!record) {
    FXL_NOTREACHED();
    return;
  }

  // When starting a new process, register for notifications.
  Target::State new_state = target->GetState();
  if (new_state == Target::State::kRunning) {
    target->GetProcess()->AddObserver(this);

    // Restart the thread ID counting when the process starts in case this
    // target was previously running (we want to restart numbering every time).
    record->next_thread_id = 1;
  }

  if (new_state == Target::State::kStopped &&
      old_state == Target::State::kRunning) {
    // Only print info for running->stopped transitions. The launch
    // callback will be called for succeeded and failed starts
    // (starting->stopped and starting->running) with the appropriate error
    // or information.
    Console* console = Console::get();
    OutputBuffer out;
    out.Append(DescribeTarget(&console->context(), target, false));
    out.Append(fxl::StringPrintf(": exit code %" PRId64 ".",
                                 target->GetLastReturnCode()));
    console->Output(std::move(out));
  }
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

void ConsoleContext::OnThreadStopped(Thread* thread) {
  // Set this process and thread as active.
  SetActiveTarget(thread->GetProcess()->GetTarget());
  SetActiveThreadForTarget(thread);

  Console* console = Console::get();
  OutputBuffer out;
  out.Append(DescribeThread(&console->context(), thread, false));
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

ConsoleContext::TargetRecord* ConsoleContext::GetTargetRecord(Target* target) {
  auto found_to_id = target_to_id_.find(target);
  if (found_to_id == target_to_id_.end()) {
    FXL_NOTREACHED();
    return nullptr;
  }
  return GetTargetRecord(found_to_id->second);
}

}  // namespace zxdb
