// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/console/console_context.h"

#include <inttypes.h>

#include "garnet/bin/zxdb/client/breakpoint.h"
#include "garnet/bin/zxdb/client/frame.h"
#include "garnet/bin/zxdb/client/process.h"
#include "garnet/bin/zxdb/client/session.h"
#include "garnet/bin/zxdb/client/symbols/location.h"
#include "garnet/bin/zxdb/client/thread.h"
#include "garnet/bin/zxdb/console/command.h"
#include "garnet/bin/zxdb/console/command_utils.h"
#include "garnet/bin/zxdb/console/console.h"
#include "garnet/bin/zxdb/console/format_context.h"
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
  // Unregister for all observers.
  session_->system().RemoveObserver(this);

  for (auto& target_pair : id_to_target_) {
    target_pair.second.target->RemoveObserver(this);

    Process* process = target_pair.second.target->GetProcess();
    if (process)
      process->RemoveObserver(this);

    for (auto& thread_pair : target_pair.second.id_to_thread)
      thread_pair.second.thread->RemoveObserver(this);
  }
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

int ConsoleContext::IdForFrame(const Frame* frame) const {
  // Find the frame in the thread's backtrace. We don't have to worry about
  // whether the frames have been synced, since if there is a frame here,
  // we know it's present in the thread's list.
  Thread* thread = frame->GetThread();
  auto frames = thread->GetFrames();
  for (size_t i = 0; i < frames.size(); i++) {
    if (frames[i] == frame)
      return static_cast<int>(i);
  }
  FXL_NOTREACHED();  // Should have found the frame.
  return 0;
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

int ConsoleContext::GetActiveTargetId() const { return active_target_id_; }

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

void ConsoleContext::SetActiveFrameForThread(const Frame* frame) {
  ThreadRecord* record = GetThreadRecord(frame->GetThread());
  if (!record) {
    FXL_NOTREACHED();
    return;
  }
  record->active_frame_id = IdForFrame(frame);
}

int ConsoleContext::GetActiveFrameIdForThread(const Thread* thread) {
  ThreadRecord* record = GetThreadRecord(thread);
  if (!record) {
    FXL_NOTREACHED();
    return 0;
  }

  // Should be a valid frame index in the thread (or no frames and == 0).
  FXL_DCHECK(
      (thread->GetFrames().empty() && record->active_frame_id == 0) ||
      (record->active_frame_id >= 0 &&
       record->active_frame_id < static_cast<int>(thread->GetFrames().size())));
  return record->active_frame_id;
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

SourceAffinity ConsoleContext::GetSourceAffinityForThread(
    const Thread* thread) const {
  const ThreadRecord* record = GetThreadRecord(thread);
  if (!record)
    return SourceAffinity::kSource;
  return record->source_affinity;
}

void ConsoleContext::SetSourceAffinityForThread(
    const Thread* thread, SourceAffinity source_affinity) {
  if (source_affinity == SourceAffinity::kNone)
    return;  // Don't change anything, previous command still stands.

  ThreadRecord* record = GetThreadRecord(thread);
  if (!record)
    return;
  record->source_affinity = source_affinity;
}

Err ConsoleContext::FillOutCommand(Command* cmd) const {
  // Target.
  const TargetRecord* target_record = nullptr;
  Err result = FillOutTarget(cmd, &target_record);
  if (result.has_error())
    return result;

  // Thread.
  const ThreadRecord* thread_record = nullptr;
  result = FillOutThread(cmd, target_record, &thread_record);
  if (result.has_error())
    return result;

  // Frame.
  result = FillOutFrame(cmd, thread_record);
  if (result.has_error())
    return result;

  // Breakpoint.
  result = FillOutBreakpoint(cmd);
  if (result.has_error())
    return result;

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

void ConsoleContext::DidTryToLoadSymbolMapping(bool ids_loaded,
                                               const std::string& msg) {
  Console* console = Console::get();
  console->Output(msg);
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

void ConsoleContext::WillDestroyProcess(Target* target, Process* process,
                                        DestroyReason reason, int exit_code) {
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
  msg += DescribeTarget(this, target);

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

  record->id_to_thread[thread_id].thread = thread;
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

void ConsoleContext::OnSymbolLoadFailure(Process* process, const Err& err) {
  Console::get()->Output(err);
}

// For comparison, GDB's printout for a breakpoint hit is:
//
//   Breakpoint 1, main () at eraseme.c:4
//   4         printf("Hello\n");
//
// And LLDB's is:
//
//   * thread #1: tid = 33767, 0x000055555555463e a.out`main + 4 at
//   eraseme.c:4, name = 'a.out', stop reason = breakpoint 1.1
//       frame #0: 0x000055555555463e a.out`main + 4 at eraseme.c:4
//      1    #include <stdio.h>
//      2
//      3    int main() {
//   -> 4    printf("Hello\n");
//      5    return 1;
//      6  }
//
// When stepping, GDB prints out only the 2nd line with source info, and LLDB
// prints out the whole thing with "step over" for "stop reason".
void ConsoleContext::OnThreadStopped(
    Thread* thread, debug_ipc::NotifyException::Type type,
    std::vector<fxl::WeakPtr<Breakpoint>> hit_breakpoints) {
  // Set this process and thread as active.
  Target* target = thread->GetProcess()->GetTarget();
  SetActiveTarget(target);
  SetActiveThreadForTarget(thread);

  Console* console = Console::get();
  OutputBuffer out;

  // Only print out the process when there's more than one.
  if (id_to_target_.size() > 1)
    out.Append(fxl::StringPrintf("Process %d ", IdForTarget(target)));
  out.Append(fxl::StringPrintf("Thread %d stopped ", IdForThread(thread)));

  // Skip the exception reason for the debugger breakpoints because they
  // mostly add noise.
  if (!hit_breakpoints.empty()) {
    out.Append(DescribeHitBreakpoints(hit_breakpoints));
  } else if (type != debug_ipc::NotifyException::Type::kHardware &&
             type != debug_ipc::NotifyException::Type::kSoftware) {
    // Show exception type for non-debug exceptions. Most debug exceptions
    // are generated by the debugger internally so add noise.
    out.Append(fxl::StringPrintf("on %s exception ",
                                 ExceptionTypeToString(type).c_str()));
  }

  // Frame (current position will always be frame 0).
  const auto& frames = thread->GetFrames();
  FXL_DCHECK(!frames.empty());
  const Location& location = frames[0]->GetLocation();
  out.Append("at ");
  out.Append(DescribeLocation(location, false));
  if (location.file_line().file().empty()) {
    out.Append(" (no symbol info)\n");
  } else {
    out.Append("\n");
  }
  console->Output(std::move(out));
  Err err = OutputSourceContext(thread->GetProcess(), location,
                                GetSourceAffinityForThread(thread));
  if (err.has_error())
    console->Output(err);
}

void ConsoleContext::OnThreadFramesInvalidated(Thread* thread) {
  ThreadRecord* record = GetThreadRecord(thread);
  if (!record) {
    FXL_NOTREACHED();
    return;
  }

  // Reset the active frame.
  record->active_frame_id = 0;
}

ConsoleContext::TargetRecord* ConsoleContext::GetTargetRecord(int target_id) {
  return const_cast<TargetRecord*>(
      const_cast<const ConsoleContext*>(this)->GetTargetRecord(target_id));
}

const ConsoleContext::TargetRecord* ConsoleContext::GetTargetRecord(
    int target_id) const {
  auto found_to_record = id_to_target_.find(target_id);
  if (found_to_record == id_to_target_.end()) {
    FXL_NOTREACHED();
    return nullptr;
  }
  return &found_to_record->second;
}

ConsoleContext::TargetRecord* ConsoleContext::GetTargetRecord(
    const Target* target) {
  return const_cast<TargetRecord*>(
      const_cast<const ConsoleContext*>(this)->GetTargetRecord(target));
}

const ConsoleContext::TargetRecord* ConsoleContext::GetTargetRecord(
    const Target* target) const {
  auto found_to_id = target_to_id_.find(target);
  if (found_to_id == target_to_id_.end()) {
    FXL_NOTREACHED();
    return nullptr;
  }
  return GetTargetRecord(found_to_id->second);
}

ConsoleContext::ThreadRecord* ConsoleContext::GetThreadRecord(
    const Thread* thread) {
  // Share implementation with the non-const version.
  return const_cast<ThreadRecord*>(
      const_cast<const ConsoleContext*>(this)->GetThreadRecord(thread));
}

const ConsoleContext::ThreadRecord* ConsoleContext::GetThreadRecord(
    const Thread* thread) const {
  const TargetRecord* target_record =
      GetTargetRecord(thread->GetProcess()->GetTarget());
  if (!target_record) {
    FXL_NOTREACHED();
    return nullptr;
  }

  auto found_thread_to_id = target_record->thread_to_id.find(thread);
  if (found_thread_to_id == target_record->thread_to_id.end()) {
    FXL_NOTREACHED();
    return nullptr;
  }
  int thread_id = found_thread_to_id->second;

  auto found_id_to_thread = target_record->id_to_thread.find(thread_id);
  if (found_thread_to_id == target_record->thread_to_id.end()) {
    FXL_NOTREACHED();
    return nullptr;
  }
  return &found_id_to_thread->second;
}

Err ConsoleContext::FillOutTarget(
    Command* cmd, TargetRecord const** out_target_record) const {
  int target_id = cmd->GetNounIndex(Noun::kProcess);
  if (target_id == Command::kNoIndex) {
    // No index: use the active one (which should always exist).
    target_id = active_target_id_;
    auto found_target = id_to_target_.find(target_id);
    FXL_DCHECK(found_target != id_to_target_.end());
    cmd->set_target(found_target->second.target);

    FXL_DCHECK(cmd->target());  // Default target should always exist.
    *out_target_record = GetTargetRecord(target_id);
    return Err();
  }

  // Explicit index given, look it up.
  auto found_target = id_to_target_.find(target_id);
  if (found_target == id_to_target_.end()) {
    return Err(ErrType::kInput,
               fxl::StringPrintf("There is no process %d.", target_id));
  }
  cmd->set_target(found_target->second.target);
  *out_target_record = GetTargetRecord(target_id);
  return Err();
}

Err ConsoleContext::FillOutThread(
    Command* cmd, const TargetRecord* target_record,
    ThreadRecord const** out_thread_record) const {
  int thread_id = cmd->GetNounIndex(Noun::kThread);
  const ThreadRecord* thread_record = nullptr;
  if (thread_id == Command::kNoIndex) {
    // No thread specified, use the default one.
    thread_id = target_record->active_thread_id;
    auto found_thread = target_record->id_to_thread.find(thread_id);
    if (found_thread == target_record->id_to_thread.end()) {
      // When there are no threads, the active thread ID will be 0 and that's
      // fine. But if it's nonzero, the thread should always be valid.
      FXL_DCHECK(thread_id == 0);
    } else {
      thread_record = &found_thread->second;
      cmd->set_thread(thread_record->thread);
    }
    *out_thread_record = thread_record;
    return Err();
  }

  // Explicit index given, look it up.
  auto found_thread = target_record->id_to_thread.find(thread_id);
  if (found_thread == target_record->id_to_thread.end()) {
    if (target_record->id_to_thread.empty()) {
      return Err(ErrType::kInput, "There are no threads in the process.");
    }
    return Err(
        ErrType::kInput,
        fxl::StringPrintf("There is no thread %d in the process.", thread_id));
  }

  thread_record = &found_thread->second;
  cmd->set_thread(thread_record->thread);
  *out_thread_record = thread_record;
  return Err();
}

Err ConsoleContext::FillOutFrame(Command* cmd,
                                 const ThreadRecord* thread_record) const {
  int frame_id = cmd->GetNounIndex(Noun::kFrame);
  if (frame_id == Command::kNoIndex) {
    // No index: use the active one (if any).
    if (thread_record) {
      const auto& frames = thread_record->thread->GetFrames();
      frame_id = thread_record->active_frame_id;
      if (frame_id < static_cast<int>(frames.size())) {
        cmd->set_frame(frames[frame_id]);
      } else {
        // If the active frame doesn't point to a valid frame, it should
        // always be 0.
        FXL_DCHECK(frame_id == 0);
      }
    }
    return Err();
  }

  // Frame index specified, use it.
  if (!thread_record)
    return Err(ErrType::kInput, "There is no thread to have frames.");

  const auto& frames = thread_record->thread->GetFrames();
  if (frame_id < static_cast<int>(frames.size())) {
    cmd->set_frame(frames[frame_id]);
    return Err();
  }

  // Invalid frame specified. The full backtrace list is populated on
  // demand. It could be if the frames aren't synced for the thread we
  // could delay processing this command and get the frames, but we're not
  // set up to do that (this function is currently synchronous). Instead
  // if we detect the list isn't populated and the user requested one
  // that's out-of-range, request they manually sync the list.
  //
  // Check for the presence of one frame to indicate that the thread is in
  // a state to have frames at all (stopped). There will always be the
  // topmost frame in this case. If the thread is running there will be no
  // frames.
  if (frames.size() == 1 && !thread_record->thread->HasAllFrames()) {
    return Err(ErrType::kInput,
               "The frames for this thread haven't been synced.\n"
               "Use \"frame\" to list the frames before selecting one to "
               "populate the frame list.");
  }
  return Err(ErrType::kInput,
             "Invalid frame index.\n"
             "Use \"frame\" to list available ones.");
}

Err ConsoleContext::FillOutBreakpoint(Command* cmd) const {
  int breakpoint_id = cmd->GetNounIndex(Noun::kBreakpoint);
  if (breakpoint_id == Command::kNoIndex) {
    // No index: use the active one (which may not exist).
    cmd->set_breakpoint(GetActiveBreakpoint());
    return Err();
  }

  // Explicit index given, look it up.
  auto found_breakpoint = id_to_breakpoint_.find(breakpoint_id);
  if (found_breakpoint == id_to_breakpoint_.end()) {
    return Err(ErrType::kInput,
               fxl::StringPrintf("There is no breakpoint %d.", breakpoint_id));
  }
  cmd->set_breakpoint(found_breakpoint->second);
  return Err();
}

std::string ConsoleContext::DescribeHitBreakpoints(
    const std::vector<fxl::WeakPtr<Breakpoint>>& hits) const {
  // Do two passes since some of the weak pointers may be gone.
  std::vector<int> ids;
  for (const auto& hit : hits) {
    if (hit)
      ids.push_back(IdForBreakpoint(hit.get()));
  }

  if (ids.empty())
    return std::string();
  if (ids.size() == 1)
    return fxl::StringPrintf("on breakpoint %d ", ids[0]);

  std::string result("on breakpoints");
  for (size_t i = 0; i < ids.size(); i++) {
    result += fxl::StringPrintf(" %d", ids[i]);
    if (i < ids.size() - 1)
      result.push_back(',');
  }
  result.push_back(' ');
  return result;
}

}  // namespace zxdb
