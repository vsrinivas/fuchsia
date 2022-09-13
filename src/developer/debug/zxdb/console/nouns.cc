// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/console/nouns.h"

#include <inttypes.h>
#include <lib/syslog/cpp/macros.h>

#include <algorithm>
#include <utility>

#include "src/developer/debug/zxdb/client/breakpoint.h"
#include "src/developer/debug/zxdb/client/breakpoint_location.h"
#include "src/developer/debug/zxdb/client/filter.h"
#include "src/developer/debug/zxdb/client/frame.h"
#include "src/developer/debug/zxdb/client/process.h"
#include "src/developer/debug/zxdb/client/session.h"
#include "src/developer/debug/zxdb/client/setting_schema_definition.h"
#include "src/developer/debug/zxdb/client/system.h"
#include "src/developer/debug/zxdb/client/target.h"
#include "src/developer/debug/zxdb/client/thread.h"
#include "src/developer/debug/zxdb/common/err.h"
#include "src/developer/debug/zxdb/console/command.h"
#include "src/developer/debug/zxdb/console/command_utils.h"
#include "src/developer/debug/zxdb/console/console.h"
#include "src/developer/debug/zxdb/console/console_context.h"
#include "src/developer/debug/zxdb/console/format_filter.h"
#include "src/developer/debug/zxdb/console/format_frame.h"
#include "src/developer/debug/zxdb/console/format_location.h"
#include "src/developer/debug/zxdb/console/format_node_console.h"
#include "src/developer/debug/zxdb/console/format_table.h"
#include "src/developer/debug/zxdb/console/format_target.h"
#include "src/developer/debug/zxdb/console/output_buffer.h"
#include "src/developer/debug/zxdb/console/string_util.h"
#include "src/developer/debug/zxdb/symbols/location.h"
#include "src/lib/fxl/strings/string_printf.h"

namespace zxdb {

namespace {

constexpr int kForceTypes = 1;
constexpr int kVerboseSwitch = 2;
constexpr int kRawOutput = 3;

// Frames ------------------------------------------------------------------------------------------

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

  -r
  --raw
      Expands frames that were collapsed by the "pretty" stack formatter.

  -t
  --types
      Include all type information for function parameters.

  -v
  --verbose
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

// Returns true if processing should stop (either a frame command or an error), false to continue
// processing to the next noun type.
bool HandleFrameNoun(ConsoleContext* console_context, const Command& cmd,
                     fxl::RefPtr<CommandContext>& cmd_context) {
  if (!cmd.HasNoun(Noun::kFrame))
    return false;

  if (!cmd.thread()) {
    cmd_context->ReportError(Err(ErrType::kInput, "There is no thread to have frames."));
    return true;
  }

  FormatStackOptions opts;

  if (!cmd.HasSwitch(kRawOutput))
    opts.pretty_stack = console_context->pretty_stack_manager();

  opts.frame.loc = FormatLocationOptions(cmd.target());
  opts.frame.loc.func.name.elide_templates = true;
  opts.frame.loc.func.name.bold_last = true;
  opts.frame.loc.func.name.enable_pretty = true;
  opts.frame.loc.func.params = cmd.HasSwitch(kForceTypes) ? FormatFunctionNameOptions::kParamTypes
                                                          : FormatFunctionNameOptions::kElideParams;

  opts.frame.variable.verbosity = cmd.HasSwitch(kForceTypes)
                                      ? ConsoleFormatOptions::Verbosity::kAllTypes
                                      : ConsoleFormatOptions::Verbosity::kMinimal;
  opts.frame.variable.pointer_expand_depth = 1;
  opts.frame.variable.max_depth = 4;

  if (cmd.GetNounIndex(Noun::kFrame) == Command::kNoIndex) {
    // Just "frame", this lists available frames.
    opts.frame.detail = FormatFrameOptions::kSimple;
    if (cmd.HasSwitch(kVerboseSwitch)) {
      opts.frame.loc.func.name.elide_templates = false;
      opts.frame.loc.func.name.enable_pretty = false;
      opts.frame.loc.func.params = FormatFunctionNameOptions::kParamTypes;
    }

    // Always force update the stack. Various things can have changed and when the user requests
    // a stack we want to be sure things are correct.
    cmd_context->Output(FormatStack(cmd.thread(), true, opts));
    return true;
  }

  // Explicit index provided, this switches the current context. The thread should be already
  // resolved to a valid pointer if it was specified on the command line (otherwise the command
  // would have been rejected before here).
  FX_DCHECK(cmd.frame());
  console_context->SetActiveFrameForThread(cmd.frame());
  // Setting the active frame also sets the active thread and target.
  console_context->SetActiveThreadForTarget(cmd.thread());
  console_context->SetActiveTarget(cmd.target());

  cmd_context->Output(FormatFrame(cmd.frame(), opts.frame, -1));
  return true;
}

// Filters -----------------------------------------------------------------------------------------

const char kFilterShortHelp[] = "filter: Select or list process filters.";
const char kFilterHelp[] =
    R"(filter [ <id> [ <command> ... ] ]

  Selects or lists process filters. Process filters allow you to attach to
  processes that spawn in the system as soon as they spawn. You can use "attach"
  to create a new filter.

More info

    • Create a filter with "attach <pattern>". See "help attach" for more.

    • Change a filter's pattern with "filter [X] set pattern = <newvalue>"
      (where [X] is the index of the filter from the "filter" command).

    • Delete a filter with "filter [X] rm".

Examples

  filter
      Lists all filters.

  filter 1
      Selects filter 1 to be the active filter.

  filter 3 set pattern = foo
      Update filter 3 to attach to processes named "foo".

  filter 4 rm
      Removes filter 4.
)";

// Returns true if processing should stop (either a filter command or an error), false to continue
// processing to the next noun type.
bool HandleFilterNoun(ConsoleContext* console_context, const Command& cmd,
                      fxl::RefPtr<CommandContext>& cmd_context) {
  if (!cmd.HasNoun(Noun::kFilter))
    return false;

  if (Err err = cmd.ValidateNouns({Noun::kFilter}); err.has_error()) {
    cmd_context->ReportError(err);
    return true;
  }

  if (cmd.GetNounIndex(Noun::kFilter) == Command::kNoIndex) {
    // Just "filter", this lists available filters.
    cmd_context->Output(FormatFilterList(console_context));
    return true;
  }

  FX_DCHECK(cmd.filter());
  console_context->SetActiveFilter(cmd.filter());
  cmd_context->Output(FormatFilter(console_context, cmd.filter()));
  return true;
}

// Threads -----------------------------------------------------------------------------------------

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
void ListThreads(fxl::RefPtr<CommandContext> cmd_context, Process* process) {
  if (!cmd_context->console())
    return;
  ConsoleContext* console_context = &cmd_context->console()->context();

  std::vector<Thread*> threads = process->GetThreads();
  int active_thread_id = console_context->GetActiveThreadIdForTarget(process->GetTarget());

  // Sort by ID.
  std::vector<std::pair<int, Thread*>> id_threads;
  for (Thread* thread : threads)
    id_threads.push_back(std::make_pair(console_context->IdForThread(thread), thread));
  std::sort(id_threads.begin(), id_threads.end());

  std::vector<std::vector<std::string>> rows;
  for (const auto& pair : id_threads) {
    rows.emplace_back();
    std::vector<std::string>& row = rows.back();

    // "Current thread" marker.
    if (pair.first == active_thread_id)
      row.push_back(GetCurrentRowMarker());
    else
      row.emplace_back();

    row.push_back(std::to_string(pair.first));
    row.push_back(ThreadStateToString(pair.second->GetState(), pair.second->GetBlockedReason()));
    row.push_back(std::to_string(pair.second->GetKoid()));
    row.push_back(pair.second->GetName());
  }

  OutputBuffer out;
  FormatTable({ColSpec(Align::kLeft), ColSpec(Align::kRight, 0, "#", 0, Syntax::kSpecial),
               ColSpec(Align::kLeft, 0, "state"), ColSpec(Align::kRight, 0, "koid"),
               ColSpec(Align::kLeft, 0, "name")},
              rows, &out);
  cmd_context->Output(out);
}

// Updates the thread list from the debugged process and asynchronously prints the result. When the
// user lists threads, we really don't want to be misleading and show out-of-date thread names which
// the developer might be relying on. Therefore, force a sync of the thread list from the target
// (which should be fast) before displaying the thread list.
void ScheduleListThreads(fxl::RefPtr<CommandContext>& cmd_context, Process* process) {
  // Since the Process issues the callback, it's OK to capture the pointer.
  process->SyncThreads([cmd_context, process]() { ListThreads(cmd_context, process); });
}

// Returns true if processing should stop (either a thread command or an error), false to continue
// processing to the nex noun type.
bool HandleThreadNoun(ConsoleContext* console_context, const Command& cmd,
                      fxl::RefPtr<CommandContext>& cmd_context) {
  if (!cmd.HasNoun(Noun::kThread))
    return false;

  Process* process = cmd.target()->GetProcess();
  if (!process) {
    cmd_context->ReportError(Err(ErrType::kInput, "Process not running, no threads."));
    return true;
  }

  if (cmd.GetNounIndex(Noun::kThread) == Command::kNoIndex) {
    // Just "thread" or "process 2 thread" specified, this lists available threads.
    ScheduleListThreads(cmd_context, process);
    return true;
  }

  // Explicit index provided, this switches the current context. The thread should be already
  // resolved to a valid pointer if it was specified on the command line (otherwise the command
  // would have been rejected before here).
  FX_DCHECK(cmd.thread());
  console_context->SetActiveThreadForTarget(cmd.thread());
  // Setting the active thread also sets the active target.
  console_context->SetActiveTarget(cmd.target());
  cmd_context->Output(FormatThread(console_context, cmd.thread()));
  return true;
}

// Processes ---------------------------------------------------------------------------------------

const char kProcessShortHelp[] = "process / pr: Select or list process contexts.";
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

// Returns true if processing should stop (either a thread command or an error), false to continue
// processing to the next noun type.
bool HandleProcessNoun(ConsoleContext* console_context, const Command& cmd,
                       fxl::RefPtr<CommandContext>& cmd_context) {
  if (!cmd.HasNoun(Noun::kProcess))
    return false;

  if (cmd.GetNounIndex(Noun::kProcess) == Command::kNoIndex) {
    // Just "process", this lists available processes.
    cmd_context->Output(FormatTargetList(console_context));
    return true;
  }

  // Explicit index provided, this switches the current context. The target should be already
  // resolved to a valid pointer if it was specified on the command line (otherwise the command
  // would have been rejected before here).
  FX_DCHECK(cmd.target());
  console_context->SetActiveTarget(cmd.target());
  cmd_context->Output(FormatTarget(console_context, cmd.target()));
  return true;
}

// Global ------------------------------------------------------------------------------------------

const char kGlobalShortHelp[] = "global / gl: Global override for commands.";
const char kGlobalHelp[] =
    R"("global <command> ...

  Alias: "gl"

  The "global" noun allows explicitly scoping a command to the global scope
  as opposed to a process or thread.
)";

bool HandleGlobalNoun(ConsoleContext* console_context, const Command& cmd,
                      fxl::RefPtr<CommandContext>& cmd_context) {
  if (!cmd.HasNoun(Noun::kGlobal))
    return false;

  cmd_context->ReportError(
      Err("\"global\" only makes sense when applied to a verb, for example \"global get\"."));
  return true;
}

// Breakpoints -------------------------------------------------------------------------------------

const char kBreakpointShortHelp[] = "breakpoint / bp: Select or list breakpoints.";
const char kBreakpointHelp[] =
    R"(breakpoint [ <id> [ <command> ... ] ]

  Alias: "bp"

  Selects or lists breakpoints. Not to be confused with the "break" / "b"
  command which creates new breakpoints. See "help break" for more.

  By itself, "breakpoint" or "bp" will list all breakpoints with their IDs.

  With an ID following it ("breakpoint 3"), selects that breakpoint as the
  current active breakpoint. This breakpoint will apply by default for
  subsequent breakpoint commands like "clear".

  With an ID and another command following it ("breakpoint 2 clear"), modifies
  the breakpoint context for that command only. This allows modifying
  breakpoints regardless of the active one.

Options

  -v
  --verbose
      When listing breakpoints, show information on each address that the
      breakpoint applies to. A symbolic breakpoint can apply to many processes
      and can expand to more than one address in a process.

Other breakpoint commands

  "break": Create a breakpoint.
  "clear": Delete a breakpoint.
  "disable": Disable a breakpoint off without deleting it.
  "enable": Enable a previously-disabled breakpoint.

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

void ListBreakpoints(ConsoleContext* console_context, fxl::RefPtr<CommandContext>& cmd_context,
                     bool include_locations) {
  auto breakpoints = console_context->session()->system().GetBreakpoints();
  if (breakpoints.empty()) {
    cmd_context->Output("No breakpoints.\n");
    return;
  }

  // The size is normally not applicable since most breakpoints are software. Hide the size for
  // clarity unless there is a hardware breakpoint.
  bool include_size = false;
  // The hit_mult is normally 1. Hide it unless there's a breakpoint with a different value.
  bool include_hit_mult = false;
  for (const auto& bp : breakpoints) {
    if (BreakpointSettings::TypeHasSize(bp->GetSettings().type)) {
      include_size = true;
    }
    if (bp->GetSettings().hit_mult != 1) {
      include_hit_mult = true;
    }
  }

  int active_breakpoint_id = console_context->GetActiveBreakpointId();

  // Sort by ID.
  std::map<int, Breakpoint*> id_bp;
  for (auto& bp : breakpoints)
    id_bp[console_context->IdForBreakpoint(bp)] = bp;

  std::vector<std::vector<OutputBuffer>> rows;
  for (const auto& pair : id_bp) {
    std::vector<OutputBuffer>& row = rows.emplace_back();

    // "Current breakpoint" marker.
    if (pair.first == active_breakpoint_id)
      row.emplace_back(GetCurrentRowMarker());
    else
      row.emplace_back();

    BreakpointSettings settings = pair.second->GetSettings();
    auto matched_locs = pair.second->GetLocations();

    row.push_back(OutputBuffer(Syntax::kSpecial, std::to_string(pair.first)));
    row.emplace_back(ExecutionScopeToString(console_context, settings.scope));
    row.emplace_back(BreakpointSettings::StopModeToString(settings.stop_mode));
    if (settings.enabled) {
      row.emplace_back("true");
    } else {
      row.emplace_back(Syntax::kError, "false");
    }
    row.emplace_back(BreakpointSettings::TypeToString(settings.type));

    if (include_size) {
      if (BreakpointSettings::TypeHasSize(settings.type))
        row.emplace_back(std::to_string(settings.byte_size));
      else
        row.emplace_back(Syntax::kComment, "n/a");
    }

    if (include_hit_mult) {
      row.emplace_back(std::to_string(settings.hit_mult));
    }

    if (matched_locs.empty()) {
      row.emplace_back(Syntax::kWarning, "pending");
      // It's confusing to show a hit_count for pending breakpoints, which happens
      // when a process is killed and locations are cleared.
      row.emplace_back();
    } else {
      row.emplace_back(std::to_string(matched_locs.size()));
      row.emplace_back(std::to_string(pair.second->GetStats().hit_count));
    }

    row.push_back(FormatInputLocations(settings.locations));

    if (include_locations) {
      for (const auto& loc : matched_locs) {
        std::vector<OutputBuffer>& loc_row = rows.emplace_back();

        loc_row.resize(2);  // Empty columns.
        Process* process = loc->GetProcess();

        FormatLocationOptions opts(process->GetTarget());
        opts.always_show_addresses = true;  // So the disambiguation is always unique.

        OutputBuffer out(GetBullet() + " ");
        out.Append(FormatLocation(loc->GetLocation(), opts));

        loc_row.push_back(out);
      }
    }
  }

  std::vector<ColSpec> col_specs{ColSpec(Align::kLeft),
                                 ColSpec(Align::kRight, 0, "#", 0, Syntax::kSpecial),
                                 ColSpec(Align::kLeft, 0, ClientSettings::Breakpoint::kScope),
                                 ColSpec(Align::kLeft, 0, ClientSettings::Breakpoint::kStopMode),
                                 ColSpec(Align::kLeft, 0, ClientSettings::Breakpoint::kEnabled),
                                 ColSpec(Align::kLeft, 0, ClientSettings::Breakpoint::kType)};
  if (include_size)
    col_specs.emplace_back(Align::kRight, 0, ClientSettings::Breakpoint::kSize);
  if (include_hit_mult)
    col_specs.emplace_back(Align::kRight, 0, ClientSettings::Breakpoint::kHitMult);
  col_specs.emplace_back(Align::kRight, 0, "#addrs");
  col_specs.emplace_back(Align::kRight, 0, "hit-count");
  col_specs.emplace_back(Align::kLeft, 0, ClientSettings::Breakpoint::kLocation);

  OutputBuffer out;
  FormatTable(col_specs, rows, &out);
  cmd_context->Output(out);
}

// Returns true if breakpoint was specified (and therefore nothing else should be called. If
// breakpoint is specified but there was an error, *err will be set.
bool HandleBreakpointNoun(ConsoleContext* console_context, const Command& cmd,
                          fxl::RefPtr<CommandContext>& cmd_context) {
  if (!cmd.HasNoun(Noun::kBreakpoint))
    return false;

  // With no verb, breakpoint can not be combined with any other noun. Saying "process 2 breakpoint"
  // doesn't make any sense.
  if (Err err = cmd.ValidateNouns({Noun::kBreakpoint}); err.has_error()) {
    cmd_context->ReportError(err);
    return true;
  }

  if (cmd.GetNounIndex(Noun::kBreakpoint) == Command::kNoIndex) {
    // Just "breakpoint", this lists available breakpoints. The verbose switch expands each
    // individual breakpoint location.
    bool include_locations = cmd.HasSwitch(kVerboseSwitch);
    ListBreakpoints(console_context, cmd_context, include_locations);
    return true;
  }

  // Explicit index provided, this switches the current context. The breakpoint should be already
  // resolved to a valid pointer if it was specified on the command line (otherwise the command
  // would have been rejected before here).
  FX_DCHECK(cmd.breakpoint());
  console_context->SetActiveBreakpoint(cmd.breakpoint());
  cmd_context->Output(FormatBreakpoint(console_context, cmd.breakpoint(), true));
  return true;
}

// Symbol Servers ----------------------------------------------------------------------------------

const char kSymServerShortHelp[] = "sym-server: Select or list symbol servers.";
const char kSymServerHelp[] =
    R"(sym-server [ <id> [ <command> ... ] ]

  Selects or lists symbol servers.

  By itself, "sym-server" will list all symbol servers with their IDs.

  With an ID following it ("sym-server 3"), selects that symbol server as the
  current active symbol server. This symbol server will apply by default for
  subsequent symbol server commands (like "auth" or "rm").

  With an ID and another command following it ("sym-server 2 auth"), applys the
  command to that symbol server.

Examples

  sym-server
      Lists all symbol servers.

  sym-server 2
      Sets symbol server 2 as the active one.

  sym-server 2 auth
      Authenticates with symbol server 2.
)";

OutputBuffer SymbolServerStateToColorString(SymbolServer::State state) {
  switch (state) {
    case SymbolServer::State::kInitializing:
      return OutputBuffer(Syntax::kComment, "Initializing");
    case SymbolServer::State::kAuth:
      return OutputBuffer(Syntax::kHeading, "Authenticating");
    case SymbolServer::State::kBusy:
      return OutputBuffer(Syntax::kComment, "Busy");
    case SymbolServer::State::kReady:
      return OutputBuffer(Syntax::kHeading, "Ready");
    case SymbolServer::State::kUnreachable:
      return OutputBuffer(Syntax::kError, "Unreachable");
  }
}

void ListSymbolServers(ConsoleContext* console_context, fxl::RefPtr<CommandContext>& cmd_context) {
  std::vector<SymbolServer*> symbol_servers =
      console_context->session()->system().GetSymbolServers();
  int active_symbol_server_id = console_context->GetActiveSymbolServerId();

  // Sort by ID.
  std::vector<std::pair<int, SymbolServer*>> id_symbol_servers;
  for (SymbolServer* symbol_server : symbol_servers) {
    id_symbol_servers.push_back(
        std::make_pair(console_context->IdForSymbolServer(symbol_server), symbol_server));
  }
  std::sort(id_symbol_servers.begin(), id_symbol_servers.end());

  std::vector<std::vector<OutputBuffer>> rows;
  for (const auto& [id, server] : id_symbol_servers) {
    rows.emplace_back();
    std::vector<OutputBuffer>& row = rows.back();

    // "Current symbol_server" marker.
    if (id == active_symbol_server_id)
      row.emplace_back(GetCurrentRowMarker());
    else
      row.emplace_back();

    row.emplace_back(std::to_string(id));
    row.emplace_back(server->name());
    row.emplace_back(SymbolServerStateToColorString(server->state()));

    if (server->error_log().empty()) {
      continue;
    }

    rows.emplace_back();
    std::vector<OutputBuffer>& line = rows.back();

    line.emplace_back("");
    line.emplace_back("");
    line.emplace_back(Syntax::kError, server->error_log().back());
  }

  OutputBuffer out;
  FormatTable({ColSpec(Align::kLeft), ColSpec(Align::kRight, 0, "#", 0, Syntax::kSpecial),
               ColSpec(Align::kLeft, 0, "URL"), ColSpec(Align::kLeft, 0, "State")},
              rows, &out);
  cmd_context->Output(out);
}

bool HandleSymbolServerNoun(ConsoleContext* console_context, const Command& cmd,
                            fxl::RefPtr<CommandContext>& cmd_context) {
  if (!cmd.HasNoun(Noun::kSymServer))
    return false;

  // sym-server only makes sense by itself. It doesn't make sense with any other nouns.
  if (Err err = cmd.ValidateNouns({Noun::kSymServer}); err.has_error()) {
    cmd_context->ReportError(err);
    return true;
  }

  if (cmd.GetNounIndex(Noun::kSymServer) == Command::kNoIndex) {
    // Just "breakpoint", this lists available breakpoints.
    ListSymbolServers(console_context, cmd_context);
    return true;
  }

  // Explicit index provided, this switches the current context. The symbol server should be already
  // resolved to a valid pointer if it was specified on the command line (otherwise the command
  // would have been rejected before here).
  FX_DCHECK(cmd.sym_server());
  console_context->SetActiveSymbolServer(cmd.sym_server());

  OutputBuffer out;
  out.Append(cmd.sym_server()->name() + " - ");
  out.Append(SymbolServerStateToColorString(cmd.sym_server()->state()));
  out.Append("\n");

  auto& error_log = cmd.sym_server()->error_log();
  auto iter = error_log.begin();
  if (error_log.size() > 10) {
    iter += error_log.size() - 10;
    out.Append("  ... " + std::to_string(error_log.size() - 10) + " more ...\n");
  }

  for (; iter != error_log.end(); iter++) {
    out.Append("  " + *iter + "\n", TextForegroundColor::kRed);
  }

  Console::get()->Output(out);
  return true;
}

}  // namespace

NounRecord::NounRecord() = default;
NounRecord::NounRecord(std::initializer_list<std::string> aliases, const char* short_help,
                       const char* help, CommandGroup command_group)
    : aliases(aliases), short_help(short_help), help(help), command_group(command_group) {}
NounRecord::~NounRecord() = default;

const std::map<Noun, NounRecord>& GetNouns() {
  static std::map<Noun, NounRecord> all_nouns;
  if (all_nouns.empty()) {
    AppendNouns(&all_nouns);

    // Everything but Noun::kNone (= 0) should be in the map.
    FX_DCHECK(all_nouns.size() == static_cast<size_t>(Noun::kLast) - 1)
        << "You need to update the noun lookup table for additions to Nouns.";
  }
  return all_nouns;
}

std::string NounToString(Noun n) {
  const auto& nouns = GetNouns();
  auto found = nouns.find(n);
  if (found == nouns.end())
    return std::string();
  return found->second.aliases[0];
}

const std::map<std::string, Noun>& GetStringNounMap() {
  static std::map<std::string, Noun> map;
  if (map.empty()) {
    // Build up the reverse-mapping from alias to verb enum.
    for (const auto& noun_pair : GetNouns()) {
      for (const auto& alias : noun_pair.second.aliases)
        map[alias] = noun_pair.first;
    }
  }
  return map;
}

void ExecuteNoun(const Command& cmd, fxl::RefPtr<CommandContext> cmd_context) {
  // Extract a non-weak ConsoleContext pointer. The noun implementations all deal with the console
  // context (because they're listing and modifying the default item) and are almost all
  // synchronous. This prevents those implementations from having to deal with the weak pointer in
  // the CommandContext themselves.
  if (!cmd_context->console())
    return;
  ConsoleContext* console_context = &cmd_context->console()->context();

  if (HandleBreakpointNoun(console_context, cmd, cmd_context))
    return;
  if (HandleFilterNoun(console_context, cmd, cmd_context))
    return;

  // Work backwards in specificity (frame -> thread -> process).
  if (HandleFrameNoun(console_context, cmd, cmd_context))
    return;
  if (HandleThreadNoun(console_context, cmd, cmd_context))
    return;
  if (HandleProcessNoun(console_context, cmd, cmd_context))
    return;
  if (HandleSymbolServerNoun(console_context, cmd, cmd_context))
    return;
  if (HandleGlobalNoun(console_context, cmd, cmd_context))
    return;
}

void AppendNouns(std::map<Noun, NounRecord>* nouns) {
  // If non-kNone, the "command groups" on the noun will cause the help for that noun to additionall
  // appear under that section (people expect the "thread" command to appear in the process
  // section).
  (*nouns)[Noun::kBreakpoint] = NounRecord({"breakpoint", "bp"}, kBreakpointShortHelp,
                                           kBreakpointHelp, CommandGroup::kBreakpoint);

  (*nouns)[Noun::kFrame] =
      NounRecord({"frame", "f"}, kFrameShortHelp, kFrameHelp, CommandGroup::kQuery);

  (*nouns)[Noun::kThread] =
      NounRecord({"thread", "t"}, kThreadShortHelp, kThreadHelp, CommandGroup::kProcess);
  (*nouns)[Noun::kProcess] =
      NounRecord({"process", "pr"}, kProcessShortHelp, kProcessHelp, CommandGroup::kProcess);
  (*nouns)[Noun::kGlobal] =
      NounRecord({"global", "gl"}, kGlobalShortHelp, kGlobalHelp, CommandGroup::kNone);
  (*nouns)[Noun::kSymServer] =
      NounRecord({"sym-server"}, kSymServerShortHelp, kSymServerHelp, CommandGroup::kSymbol);
  (*nouns)[Noun::kFilter] =
      NounRecord({"filter"}, kFilterShortHelp, kFilterHelp, CommandGroup::kProcess);
}

const std::vector<SwitchRecord>& GetNounSwitches() {
  static std::vector<SwitchRecord> switches;
  if (switches.empty()) {
    switches.emplace_back(kRawOutput, false, "raw", 'r');
    switches.emplace_back(kForceTypes, false, "types", 't');
    switches.emplace_back(kVerboseSwitch, false, "verbose", 'v');
  }
  return switches;
}

}  // namespace zxdb
