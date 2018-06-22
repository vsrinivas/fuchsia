// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/console/nouns.h"

#include <inttypes.h>
#include <algorithm>
#include <utility>

#include "garnet/bin/zxdb/client/breakpoint.h"
#include "garnet/bin/zxdb/client/err.h"
#include "garnet/bin/zxdb/client/process.h"
#include "garnet/bin/zxdb/client/session.h"
#include "garnet/bin/zxdb/client/system.h"
#include "garnet/bin/zxdb/client/thread.h"
#include "garnet/bin/zxdb/console/command.h"
#include "garnet/bin/zxdb/console/command_utils.h"
#include "garnet/bin/zxdb/console/console.h"
#include "garnet/bin/zxdb/console/console_context.h"
#include "garnet/bin/zxdb/console/format_table.h"
#include "garnet/bin/zxdb/console/output_buffer.h"
#include "garnet/bin/zxdb/console/string_util.h"
#include "garnet/public/lib/fxl/logging.h"
#include "garnet/public/lib/fxl/strings/string_printf.h"

namespace zxdb {

namespace {

void ListFrames(ConsoleContext* context, Thread* thread) {
  int active_frame_id = context->GetActiveFrameIdForThread(thread);

  OutputBuffer out;

  // This doesn't use table output since the format of the stack frames is
  // usually so unpredictable.
  const auto& frames = thread->GetFrames();
  if (frames.empty()) {
    out.Append("No stack frames.\n");
  } else {
    for (int i = 0; i < static_cast<int>(frames.size()); i++) {
      if (i == active_frame_id)
        out.Append(GetRightArrow() + " ");
      else
        out.Append("  ");
      out.Append(DescribeFrame(frames[i], i));
      out.Append("\n");
    }
  }
  Console::get()->Output(std::move(out));
}

void ScheduleListFrames(Thread* thread) {
  thread->SyncFrames(
      [thread]() { ListFrames(&Console::get()->context(), thread); });
}

// Returns true if processing should stop (either a frame command or an error),
// false to continue processing to the nex noun type.
bool HandleFrame(ConsoleContext* context, const Command& cmd, Err* err) {
  if (!cmd.HasNoun(Noun::kFrame))
    return false;

  if (!cmd.thread()) {
    *err = Err(ErrType::kInput, "There is no thread to have frames.");
    return true;
  }

  if (cmd.GetNounIndex(Noun::kFrame) == Command::kNoIndex) {
    // Just "frame", this lists available frames.
    if (cmd.thread()->HasAllFrames())
      ListFrames(context, cmd.thread());
    else
      ScheduleListFrames(cmd.thread());
    return true;
  }

  // Explicit index provided, this switches the current context. The thread
  // should be already resolved to a valid pointer if it was specified on the
  // command line (otherwise the command would have been rejected before here).
  FXL_DCHECK(cmd.frame());
  context->SetActiveFrameForThread(cmd.frame());
  // Setting the active thread also sets the active thread and target.
  context->SetActiveThreadForTarget(cmd.thread());
  context->SetActiveTarget(cmd.target());
  Console::get()->Output(DescribeFrame(
      cmd.frame(), context->GetActiveFrameIdForThread(cmd.thread())));
  return true;
}

// Prints the thread list for the given process to the console.
void ListThreads(ConsoleContext* context, Process* process) {
  std::vector<Thread*> threads = process->GetThreads();
  int active_thread_id =
      context->GetActiveThreadIdForTarget(process->GetTarget());

  // Sort by ID.
  std::vector<std::pair<int, Thread*>> id_threads;
  for (Thread* thread : threads)
    id_threads.push_back(std::make_pair(context->IdForThread(thread), thread));
  std::sort(id_threads.begin(), id_threads.end());

  std::vector<std::vector<std::string>> rows;
  for (const auto& pair : id_threads) {
    rows.emplace_back();
    std::vector<std::string>& row = rows.back();

    // "Current thread" marker.
    if (pair.first == active_thread_id)
      row.push_back(GetRightArrow());
    else
      row.emplace_back();

    row.push_back(fxl::StringPrintf("%d", pair.first));
    row.push_back(ThreadStateToString(pair.second->GetState()));
    row.push_back(fxl::StringPrintf("%" PRIu64, pair.second->GetKoid()));
    row.push_back(pair.second->GetName());
  }

  OutputBuffer out;
  FormatTable(
      {ColSpec(Align::kLeft), ColSpec(Align::kRight, 0, "#"),
       ColSpec(Align::kLeft, 0, "State"), ColSpec(Align::kRight, 0, "Koid"),
       ColSpec(Align::kLeft, 0, "Name")},
      rows, &out);
  Console::get()->Output(std::move(out));
}

// Updates the thread list from the debugged process and asynchronously prints
// the result. When the user lists threads, we really don't want to be
// misleading and show out-of-date thread names which the developer might be
// relying on. Therefore, force a sync of the thread list from the target
// (which should be fast) before displaying the thread list.
void ScheduleListThreads(Process* process) {
  // Since the Process issues the callback, it's OK to capture the pointer.
  process->SyncThreads(
      [process]() { ListThreads(&Console::get()->context(), process); });
}

// Returns true if processing should stop (either a thread command or an error),
// false to continue processing to the nex noun type.
bool HandleThread(ConsoleContext* context, const Command& cmd, Err* err) {
  if (!cmd.HasNoun(Noun::kThread))
    return false;

  Process* process = cmd.target()->GetProcess();
  if (!process) {
    *err = Err(ErrType::kInput, "Process not running, no threads.");
    return true;
  }

  if (cmd.GetNounIndex(Noun::kThread) == Command::kNoIndex) {
    // Just "thread" or "process 2 thread" specified, this lists available
    // threads.
    ScheduleListThreads(process);
    return true;
  }

  // Explicit index provided, this switches the current context. The thread
  // should be already resolved to a valid pointer if it was specified on the
  // command line (otherwise the command would have been rejected before here).
  FXL_DCHECK(cmd.thread());
  context->SetActiveThreadForTarget(cmd.thread());
  // Setting the active thread also sets the active target.
  context->SetActiveTarget(cmd.target());
  Console::get()->Output(DescribeThread(context, cmd.thread()));
  return true;
}

void ListProcesses(ConsoleContext* context) {
  auto targets = context->session()->system().GetTargets();

  int active_target_id = context->GetActiveTargetId();

  // Sort by ID.
  std::vector<std::pair<int, Target*>> id_targets;
  for (auto& target : targets)
    id_targets.push_back(std::make_pair(context->IdForTarget(target), target));
  std::sort(id_targets.begin(), id_targets.end());

  std::vector<std::vector<std::string>> rows;
  for (const auto& pair : id_targets) {
    rows.emplace_back();
    std::vector<std::string>& row = rows.back();

    // "Current process" marker (or nothing).
    if (pair.first == active_target_id)
      row.emplace_back(GetRightArrow());
    else
      row.emplace_back();

    // ID.
    row.push_back(fxl::StringPrintf("%d", pair.first));

    // State and koid (if running).
    row.push_back(TargetStateToString(pair.second->GetState()));
    if (pair.second->GetState() == Target::State::kRunning) {
      row.push_back(
          fxl::StringPrintf("%" PRIu64, pair.second->GetProcess()->GetKoid()));
    } else {
      row.emplace_back();
    }

    row.push_back(DescribeTargetName(pair.second));
  }

  OutputBuffer out;
  FormatTable(
      {ColSpec(Align::kLeft), ColSpec(Align::kRight, 0, "#"),
       ColSpec(Align::kLeft, 0, "State"), ColSpec(Align::kRight, 0, "Koid"),
       ColSpec(Align::kLeft, 0, "Name")},
      rows, &out);
  Console::get()->Output(std::move(out));
}

// Returns true if processing should stop (either a thread command or an error),
// false to continue processing to the nex noun type.
bool HandleProcess(ConsoleContext* context, const Command& cmd, Err* err) {
  if (!cmd.HasNoun(Noun::kProcess))
    return false;

  if (cmd.GetNounIndex(Noun::kProcess) == Command::kNoIndex) {
    // Just "process", this lists available processes.
    ListProcesses(context);
    return true;
  }

  // Explicit index provided, this switches the current context. The target
  // should be already resolved to a valid pointer if it was specified on the
  // command line (otherwise the command would have been rejected before here).
  FXL_DCHECK(cmd.target());
  context->SetActiveTarget(cmd.target());
  Console::get()->Output(DescribeTarget(context, cmd.target()));
  return true;
}

void ListBreakpoints(ConsoleContext* context) {
  auto breakpoints = context->session()->system().GetBreakpoints();
  if (breakpoints.empty()) {
    Console::get()->Output("No breakpoints.\n");
    return;
  }

  int active_breakpoint_id = context->GetActiveBreakpointId();

  // Sort by ID.
  std::vector<std::pair<int, Breakpoint*>> id_bp;
  for (auto& bp : breakpoints)
    id_bp.push_back(std::make_pair(context->IdForBreakpoint(bp), bp));
  std::sort(id_bp.begin(), id_bp.end());

  std::vector<std::vector<std::string>> rows;

  for (const auto& pair : id_bp) {
    rows.emplace_back();
    std::vector<std::string>& row = rows.back();

    // "Current breakpoint" marker.
    if (pair.first == active_breakpoint_id)
      row.emplace_back(GetRightArrow());
    else
      row.emplace_back();

    BreakpointSettings settings = pair.second->GetSettings();
    row.push_back(fxl::StringPrintf("%d", pair.first));
    row.push_back(BreakpointScopeToString(context, settings));
    row.push_back(BreakpointStopToString(settings.stop_mode));
    row.push_back(BreakpointEnabledToString(settings.enabled));
    row.push_back(DescribeInputLocation(settings.location));
  }

  OutputBuffer out;
  FormatTable(
      {ColSpec(Align::kLeft), ColSpec(Align::kRight, 0, "#"),
       ColSpec(Align::kLeft, 0, "Scope"), ColSpec(Align::kLeft, 0, "Stop"),
       ColSpec(Align::kLeft, 0, "Enabled"),
       ColSpec(Align::kLeft, 0, "Location")},
      rows, &out);
  Console::get()->Output(std::move(out));
}

// Returns true if breakpoint was specified (and therefore nothing else
// should be called. If breakpoint is specified but there was an error, *err
// will be set.
bool HandleBreakpoint(ConsoleContext* context, const Command& cmd, Err* err) {
  if (!cmd.HasNoun(Noun::kBreakpoint))
    return false;

  // With no verb, breakpoint can not be combined with any other noun. Saying
  // "process 2 breakpoint" doesn't make any sense.
  *err = cmd.ValidateNouns({Noun::kBreakpoint});
  if (err->has_error())
    return true;

  if (cmd.GetNounIndex(Noun::kBreakpoint) == Command::kNoIndex) {
    // Just "breakpoint", this lists available breakpoints.
    ListBreakpoints(context);
    return true;
  }

  // Explicit index provided, this switches the current context. The breakpoint
  // should be already resolved to a valid pointer if it was specified on the
  // command line (otherwise the command would have been rejected before here).
  FXL_DCHECK(cmd.breakpoint());
  context->SetActiveBreakpoint(cmd.breakpoint());
  Console::get()->Output(DescribeBreakpoint(context, cmd.breakpoint()));
  return true;
}

}  // namespace

Err ExecuteNoun(ConsoleContext* context, const Command& cmd) {
  Err result;

  if (HandleBreakpoint(context, cmd, &result))
    return result;

  // Work backwards in specificity (frame -> thread -> process).
  if (HandleFrame(context, cmd, &result))
    return result;
  if (HandleThread(context, cmd, &result))
    return result;
  if (HandleProcess(context, cmd, &result))
    return result;

  return result;
}

}  // namespace zxdb
