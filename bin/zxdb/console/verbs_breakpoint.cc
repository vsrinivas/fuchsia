// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/console/verbs.h"
#include "garnet/bin/zxdb/client/breakpoint.h"
#include "garnet/bin/zxdb/client/breakpoint_location.h"
#include "garnet/bin/zxdb/client/breakpoint_settings.h"
#include "garnet/bin/zxdb/client/frame.h"
#include "garnet/bin/zxdb/client/session.h"
#include "garnet/bin/zxdb/client/symbols/location.h"
#include "garnet/bin/zxdb/console/command.h"
#include "garnet/bin/zxdb/console/command_utils.h"
#include "garnet/bin/zxdb/console/console.h"
#include "garnet/bin/zxdb/console/console_context.h"
#include "garnet/bin/zxdb/console/format_context.h"
#include "garnet/bin/zxdb/console/input_location_parser.h"
#include "garnet/bin/zxdb/console/output_buffer.h"

namespace zxdb {

namespace {

constexpr int kStopSwitch = 1;
constexpr int kEnableSwitch = 2;

// Callback for when updating a breakpoint is done.
void CreateOrEditBreakpointComplete(fxl::WeakPtr<Breakpoint> breakpoint,
                                    const Err& err) {
  if (!breakpoint)
    return;  // Do nothing if the breakpoint is gone.

  Console* console = Console::get();
  if (err.has_error()) {
    OutputBuffer out;
    out.Append("Error setting breakpoint: ");
    out.OutputErr(err);
    console->Output(std::move(out));
    return;
  }

  auto locs = breakpoint->GetLocations();
  if (locs.empty()) {
    // When the breakpoint resolved to nothing, warn the user, they may have
    // made a typo.
    OutputBuffer out;
    out.Append(DescribeBreakpoint(&console->context(), breakpoint.get()));
    out.Append(Syntax::kWarning, "\nPending");
    out.Append(": No matches for location, it will be pending library loads.");
    console->Output(std::move(out));
    return;
  }

  // Successfully wrote the breakpoint.
  OutputBuffer out;
  out.Append(DescribeBreakpoint(&console->context(), breakpoint.get()));
  out.Append("\n");

  // There is a question of what to show the breakpoint enabled state. The
  // breakpoint has a main enabled bit and each location (it can apply to more
  // than one address -- think templates and inlined functions) within that
  // breakpoint has its own. But each location normally resolves to the same
  // source code location so we can't practically show the individual
  // location's enabled state separately.
  //
  // For simplicity, just base it on the main enabled bit. Most people won't
  // use location-specific enabling anyway.
  //
  // Ignore errors from printing the source, it doesn't matter that much.
  FormatBreakpointContext(
      locs[0]->GetLocation(),
      breakpoint->session()->system().GetSymbols()->build_dir(),
      breakpoint->GetSettings().enabled, &out);
  console->Output(std::move(out));
}

// Backend for setting attributes on a breakpoint from both creation and
// editing. The given breakpoint is specified if this is an edit, or is null
// if this is a creation.
Err CreateOrEditBreakpoint(ConsoleContext* context, const Command& cmd,
                           Breakpoint* breakpoint) {
  // Get existing settings (or defaults for new one).
  BreakpointSettings settings;
  if (breakpoint)
    settings = breakpoint->GetSettings();

  // Enable flag.
  if (cmd.HasSwitch(kEnableSwitch)) {
    std::string enable_str = cmd.GetSwitchValue(kEnableSwitch);
    if (enable_str == "true") {
      settings.enabled = true;
    } else if (enable_str == "false") {
      settings.enabled = false;
    } else {
      return Err(
          "--enabled switch requires either \"true\" or \"false\" values.");
    }
  }

  // Stop mode.
  if (cmd.HasSwitch(kStopSwitch)) {
    std::string stop_str = cmd.GetSwitchValue(kStopSwitch);
    if (stop_str == "all") {
      settings.stop_mode = BreakpointSettings::StopMode::kAll;
    } else if (stop_str == "process") {
      settings.stop_mode = BreakpointSettings::StopMode::kProcess;
    } else if (stop_str == "thread") {
      settings.stop_mode = BreakpointSettings::StopMode::kThread;
    } else if (stop_str == "none") {
      settings.stop_mode = BreakpointSettings::StopMode::kNone;
    } else {
      return Err(
          "--stop switch requires \"all\", \"process\", \"thread\", "
          "or \"none\".");
    }
  }

  // Location.
  if (cmd.args().empty()) {
    if (!breakpoint) {
      // Creating a breakpoint with no location implicitly uses the current
      // frame's current location.
      if (!cmd.frame()) {
        return Err(ErrType::kInput,
                   "There isn't a current frame to take the breakpoint "
                   "location from.");
      }

      // Use the file/line of the frame if available. This is what a user will
      // generally want to see in the breakpoint list, and will persist across
      // restarts. Fall back to an address otherwise. Sometimes the file/line
      // might not be what they want, though.
      const Location& frame_loc = cmd.frame()->GetLocation();
      if (frame_loc.has_symbols())
        settings.location = InputLocation(frame_loc.file_line());
      else
        settings.location = InputLocation(cmd.frame()->GetAddress());
    }
  } else if (cmd.args().size() == 1u) {
    Err err =
        ParseInputLocation(cmd.frame(), cmd.args()[0], &settings.location);
    if (err.has_error())
      return err;
  } else {
    return Err(ErrType::kInput,
               "Expecting only one arg for the location.\n"
               "Formats: <function>, <file>:<line#>, <line#>, or *<address>");
  }

  // Scope.
  if (cmd.HasNoun(Noun::kThread)) {
    settings.scope = BreakpointSettings::Scope::kThread;
    settings.scope_thread = cmd.thread();
    settings.scope_target = cmd.target();
  } else if (cmd.HasNoun(Noun::kProcess) ||
             settings.location.type == InputLocation::Type::kAddress) {
    settings.scope = BreakpointSettings::Scope::kTarget;
    settings.scope_thread = nullptr;
    settings.scope_target = cmd.target();
  }
  // TODO(brettw) We don't have a "system" noun so there's no way to express
  // converting a process- or thread-specific breakpoint to a global one.
  // A system noun should be added and, if specified, this code should
  // convert to a global breakpoint.

  // Commit the changes.
  if (!breakpoint) {
    // New breakpoint.
    breakpoint = context->session()->system().CreateNewBreakpoint();
    context->SetActiveBreakpoint(breakpoint);
  }
  breakpoint->SetSettings(
      settings, [breakpoint = breakpoint->GetWeakPtr()](const Err& err) {
        CreateOrEditBreakpointComplete(std::move(breakpoint), err);
      });

  return Err();
}

// break -----------------------------------------------------------------------

const char kBreakShortHelp[] = "break / br: Create a breakpoint.";
const char kBreakHelp[] =
    R"("break <location>

  Alias: "b"

  Creates or modifies a breakpoint. Not to be confused the the "breakpoint" /
  "bp" noun which lists breakpoints and modifies the breakpoint context. See
  "help bp" for more.

  The new breakpoint will become the active breakpoint so future breakpoint
  commands will apply to it by default.

Location arguments

  Current frame's address (no input)
    break

)" LOCATION_ARG_HELP("break")
        R"(
Options

  --enable=[ true | false ]
  -e [ true | false ]

      Controls whether the breakpoint is enabled or disabled. A disabled
      breakpoint is never hit and hit counts are not incremented, but its
      settings are preserved. Defaults to enabled (true).

  --stop=[ all | process | thread | none ]
  -s [ all | process | thread | none ]

      Controls what execution is stopped when the breakpoint is hit. By
      default all threads of all debugged process will be stopped ("all") when
      a breakpoint is hit. But it's possible to only stop the threads of the
      current process ("process") or the thread that hit the breakpoint
      ("thread").

      If "none" is specified, any threads hitting the breakpoint will
      immediately resume, but the hit count will continue to accumulate.

Scoping to processes and threads

  Explicit context can be provided to scope a breakpoint to a single process
  or a single thread. To do this, provide that process or thread as context
  before the break command:

    t 1 b *0x614a19837
    thread 1 break *0x614a19837
        Breaks on only this thread in the current process.

    pr 2 b *0x614a19837
    process 2 break *0x614a19837
        Breaks on all threads in the given process.

  When the thread of a thread-scoped breakpoint is destroyed, the breakpoint
  will be converted to a disabled process-scoped breakpoint. When the process
  context of a process-scoped breakpoint is destroyed, the breakpoint will be
  converted to a disabled global breakpoint.

See also

  "help breakpoint": To list or select breakpoints.
  "help clear": To delete breakpoints.

Examples

  break
      Set a breakpoint at the current frame's address.

  frame 1 break
      Set a breakpoint at the specified frame's address. Since frame 1 is
      always the current function's calling frame, this command will set a
      breakpoint at the current function's return.

  break MyClass::MyFunc
      Breakpoint in all processes that have a function with this name.

  break *0x123c9df
      Process-specific breakpoint at the given address.

  process 3 break MyClass::MyFunc
      Process-specific breakpoint at the given function.

  thread 1 break foo.cpp:34
      Thread-specific breakpoint at the give file/line.

  break 23
      Break at line 23 of the file referenced by the current frame.

  frame 3 break 23
      Break at line 23 of the file referenced by frame 3.
)";
Err DoBreak(ConsoleContext* context, const Command& cmd) {
  Err err = cmd.ValidateNouns(
      {Noun::kProcess, Noun::kThread, Noun::kFrame, Noun::kBreakpoint});
  if (err.has_error())
    return err;
  return CreateOrEditBreakpoint(context, cmd, nullptr);
}

// clear -----------------------------------------------------------------------

const char kClearShortHelp[] = "clear / cl: Clear a breakpoint.";
const char kClearHelp[] =
    R"(clear

  Alias: "cl"

  By itself, "clear" will delete the current active breakpoint.

  Clear a named breakpoint by specifying the breakpoint context for the
  command. Unlike GDB, the context comes first, so instead of "clear 2" to
  clear breakpoint #2, use "breakpoint 2 clear" (or "bp 2 cl" for short).

See also

  "help break": To create breakpoints.
  "help breakpoint": To manage the current breakpoint context.

Examples

  breakpoint 2 clear
  bp 2 cl
  clear
  cl
)";
Err DoClear(ConsoleContext* context, const Command& cmd) {
  Err err = cmd.ValidateNouns({Noun::kBreakpoint});
  if (err.has_error())
    return err;

  // Expect no args. If an arg was specified, most likely they're trying to
  // use GDB syntax of "clear 2".
  if (cmd.args().size() > 0) {
    return Err(
        "\"clear\" takes no arguments. To specify an explicit "
        "breakpoint to clear,\nuse \"breakpoint <index> clear\" or "
        "\"bp <index> cl\" for short.");
  }

  if (!cmd.breakpoint()) {
    return Err(
        "There is no active breakpoint and no breakpoint was given.\n"
        "Use \"breakpoint <index> clear\" to specify one.\n");
  }

  std::string desc = DescribeBreakpoint(context, cmd.breakpoint());
  context->session()->system().DeleteBreakpoint(cmd.breakpoint());
  Console::get()->Output("Deleted " + desc);
  return Err();
}

// edit ------------------------------------------------------------------------

const char kEditShortHelp[] = "edit / ed: Edit a breakpoint.";
const char kEditHelp[] =
    R"(edit

  Alias: "ed"

  Edits an existing breakpoint.  Edit requires an explicit context. The only
  context currently supported is "breakpoint". Specify an explicit breakpoint
  with the "breakpoint"/"bp" noun and its index:

    bp 4 ed ...
    breakpoint 4 edit ...

  Or use the active breakpoint by omitting the index:

    bp ed ...
    breakpoint edit ...

  The parameters accepted are any parameters accepted by the "break" command.
  Specified parameters will overwrite the existing settings. If a location is
  specified, the breakpoint will be moved, if a location is not specified, its
  location will be unchanged.

  The active breakpoint will not be changed.

See also

  "help break": To create breakpoints.
  "help breakpoint": To list and select the active breakpoint.

Examples

  bp 2 ed --enable=false
  breakpoint 2 edit --enable=false
      Disable breakpoint 2.

  bp ed --stop=thread
  breakpoint edit --stop=thread
      Make the active breakpoint stop only the thread that triggered it.

  pr 1 t 6 bp 7 b 0x614a19837
  process 1 thread 6 breakpoint 7 edit 0x614a19837
      Modifies breakpoint 7 to only break in process 1, thread 6 at the
      given address.
)";
Err DoEdit(ConsoleContext* context, const Command& cmd) {
  if (!cmd.HasNoun(Noun::kBreakpoint)) {
    // Edit requires an explicit "breakpoint" context so that in the future we
    // can apply edit to other nouns. I'm thinking any noun that can be created
    // can have its switches modified via an "edit" command that accepts the
    // same settings.
    return Err(ErrType::kInput,
               "\"edit\" requires an explicit breakpoint context.\n"
               "Either \"breakpoint edit\" for the active breakpoint, or "
               "\"breakpoint <index> edit\" for an\nexplicit one.");
  }

  Err err =
      cmd.ValidateNouns({Noun::kProcess, Noun::kThread, Noun::kBreakpoint});
  if (err.has_error())
    return err;

  return CreateOrEditBreakpoint(context, cmd, cmd.breakpoint());
}

}  // namespace

void AppendBreakpointVerbs(std::map<Verb, VerbRecord>* verbs) {
  SwitchRecord enable_switch(kEnableSwitch, true, "enable", 'e');
  SwitchRecord stop_switch(kStopSwitch, true, "stop", 's');

  VerbRecord break_record(&DoBreak, {"break", "b"}, kBreakShortHelp,
                          kBreakHelp);
  break_record.switches.push_back(enable_switch);
  break_record.switches.push_back(stop_switch);
  (*verbs)[Verb::kBreak] = break_record;

  VerbRecord edit_record(&DoEdit, {"edit", "ed"}, kEditShortHelp, kEditHelp);
  edit_record.switches.push_back(enable_switch);
  edit_record.switches.push_back(stop_switch);
  (*verbs)[Verb::kEdit] = edit_record;

  (*verbs)[Verb::kClear] =
      VerbRecord(&DoClear, {"clear", "cl"}, kClearShortHelp, kClearHelp);
}

}  // namespace zxdb
