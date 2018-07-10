// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/console/nouns.h"

#include <inttypes.h>
#include <algorithm>
#include <utility>

#include "garnet/bin/zxdb/client/breakpoint.h"
#include "garnet/bin/zxdb/client/err.h"
#include "garnet/bin/zxdb/client/frame.h"
#include "garnet/bin/zxdb/client/process.h"
#include "garnet/bin/zxdb/client/session.h"
#include "garnet/bin/zxdb/client/symbols/location.h"
#include "garnet/bin/zxdb/client/system.h"
#include "garnet/bin/zxdb/client/thread.h"
#include "garnet/bin/zxdb/console/command.h"
#include "garnet/bin/zxdb/console/command_utils.h"
#include "garnet/bin/zxdb/console/console.h"
#include "garnet/bin/zxdb/console/console_context.h"
#include "garnet/bin/zxdb/console/format_frame.h"
#include "garnet/bin/zxdb/console/format_table.h"
#include "garnet/bin/zxdb/console/output_buffer.h"
#include "garnet/bin/zxdb/console/string_util.h"
#include "garnet/public/lib/fxl/logging.h"
#include "garnet/public/lib/fxl/strings/string_printf.h"

namespace zxdb {

namespace {

constexpr int kVerboseSwitch = 1;

// Frames ----------------------------------------------------------------------

const char kFrameShortHelp[] = "frame / f: Select or list stack frames.";
const char kFrameHelp[] =
    R"(frame [ -v ] [ <id> [ <command> ... ] ]

  Selects or lists stack frames. Stack frames are only available for threads
  that are stopped. Selecting or listing frames for running threads will
  fail.

  By itself, "frame" will list the stack frames in the current thread.

  With an ID following it ("frame 3"), selects that frame as the current
  active frame. This frame will apply by default for subsequent commands.

  With an ID and another command following it ("frame 3 print"), modifies the
  frame for that command only. This allows interrogating stack frames
  regardless of which is the active one.

Options

  --verbose | -v
      Show more information in the frame list. This is valid when listing
      frames only.

Examples

  f
  frame
  f -v
  frame -v
    Lists all stack frames in the current thread.

  f 1
  frame 1
    Selects frame 1 to be the active frame in the current thread.

  process 2 thread 1 frame 3
    Selects the specified process, thread, and frame.
)";

// Returns true if processing should stop (either a frame command or an error),
// false to continue processing to the nex noun type.
bool HandleFrameNoun(ConsoleContext* context, const Command& cmd, Err* err) {
  Console* console = Console::get();
  if (!cmd.HasNoun(Noun::kFrame))
    return false;

  if (!cmd.thread()) {
    *err = Err(ErrType::kInput, "There is no thread to have frames.");
    return true;
  }

  if (cmd.GetNounIndex(Noun::kFrame) == Command::kNoIndex) {
    // Just "frame", this lists available frames.
    OutputFrameList(cmd.thread(), cmd.HasSwitch(kVerboseSwitch));
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

  OutputBuffer out;
  FormatFrame(cmd.frame(), &out, true, context->GetActiveFrameIdForThread(cmd.thread()));
  console->Output(out);
  return true;
}

// Threads ---------------------------------------------------------------------

const char kThreadShortHelp[] = "thread / t: Select or list threads.";
const char kThreadHelp[] =
    R"(thread [ <id> [ <command> ... ] ]

  Selects or lists threads.

  By itself, "thread" will list the threads in the current process.

  With an ID following it ("thread 3"), selects that thread as the current
  active thread. This thread will apply by default for subsequent commands
  (like "step").

  With an ID and another command following it ("thread 3 step"), modifies the
  thread for that command only. This allows stepping or interrogating threads
  regardless of which is the active one.

Examples

  t
  thread
      Lists all threads in the current process.

  t 1
  thread 1
      Selects thread 1 to be the active thread in the current process.

  process 2 thread 1
      Selects process 2 as the active process and thread 1 within it as the
      active thread.

  process 2 thread
      Lists all threads in process 2.

  thread 1 step
      Steps thread 1 in the current process, regardless of the active thread.

  process 2 thread 1 step
      Steps thread 1 in process 2, regardless of the active process or thread.
)";

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
bool HandleThreadNoun(ConsoleContext* context, const Command& cmd, Err* err) {
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

// Processes -------------------------------------------------------------------

const char kProcessShortHelp[] =
    "process / pr: Select or list process contexts.";
const char kProcessHelp[] =
    R"(process [ <id> [ <command> ... ] ]

  Alias: "pr"

  Selects or lists process contexts.

  By itself, "process" will list available process contexts with their IDs. New
  process contexts can be created with the "new" command. This list of debugger
  contexts is different than the list of processes on the target system (use
  "ps" to list all running processes, and "attach" to attach a context to a
  running process).

  With an ID following it ("process 3"), selects that process context as the
  current active context. This context will apply by default for subsequent
  commands (like "run").

  With an ID and another command following it ("process 3 run"), modifies the
  process context for that command only. This allows running, pausing, etc.
  processes regardless of which is the active one.

Examples

  pr
  process
      Lists all process contexts.

  pr 2
  process 2
      Sets process context 2 as the active one.

  pr 2 r
  process 2 run
      Runs process context 2, regardless of the active one.
)";

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
bool HandleProcessNoun(ConsoleContext* context, const Command& cmd, Err* err) {
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

// Breakpoints -----------------------------------------------------------------

const char kBreakpointShortHelp[] =
    "breakpoint / bp: Select or list breakpoints.";
const char kBreakpointHelp[] =
    R"(breakpoint [ <id> [ <command> ... ] ]

  Alias: "bp"

  Selects or lists breakpoints. Not to be confused with the "break" / "b"
  command which creates new breakpoints. See "help break" for more.

  By itself, "breakpoint" or "bp" will list all breakpoints with their IDs.

  With an ID following it ("breakpoint 3"), selects that breakpoint as the
  current active breakpoint. This breakpoint will apply by default for
  subsequent breakpoint commands (like "clear" or "edit").

  With an ID and another command following it ("breakpoint 2 clear"), modifies
  the breakpoint context for that command only. This allows modifying
  breakpoints regardless of the active one.

Examples

  bp
  breakpoint
      Lists all breakpoints.

  bp 2
  breakpoint 2
      Sets breakpoint 2 as the active one.

  bp 2 cl
  breakpoint 2 clear
      Clears breakpoint 2.
)";

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
bool HandleBreakpointNoun(ConsoleContext* context, const Command& cmd, Err* err) {
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

  if (HandleBreakpointNoun(context, cmd, &result))
    return result;

  // Work backwards in specificity (frame -> thread -> process).
  if (HandleFrameNoun(context, cmd, &result))
    return result;
  if (HandleThreadNoun(context, cmd, &result))
    return result;
  if (HandleProcessNoun(context, cmd, &result))
    return result;

  return result;
}

void AppendNouns(std::map<Noun, NounRecord>* nouns) {
  (*nouns)[Noun::kBreakpoint] =
      NounRecord({"breakpoint", "bp"}, kBreakpointShortHelp, kBreakpointHelp,
                 CommandGroup::kBreakpoint);

  (*nouns)[Noun::kFrame] = NounRecord({"frame", "f"}, kFrameShortHelp,
                                      kFrameHelp, CommandGroup::kQuery);

  (*nouns)[Noun::kThread] = NounRecord({"thread", "t"}, kThreadShortHelp,
                                       kThreadHelp, CommandGroup::kProcess);
  (*nouns)[Noun::kProcess] = NounRecord({"process", "pr"}, kProcessShortHelp,
                                        kProcessHelp, CommandGroup::kProcess);
}

const std::vector<SwitchRecord>& GetNounSwitches() {
  static std::vector<SwitchRecord> switches;
  if (switches.empty()) {
    switches.emplace_back(kVerboseSwitch, false, "verbose", 'v');
  }
  return switches;
}

}  // namespace zxdb
