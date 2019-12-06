// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/client/breakpoint.h"
#include "src/developer/debug/zxdb/client/breakpoint_location.h"
#include "src/developer/debug/zxdb/client/breakpoint_settings.h"
#include "src/developer/debug/zxdb/client/frame.h"
#include "src/developer/debug/zxdb/client/session.h"
#include "src/developer/debug/zxdb/client/setting_schema_definition.h"
#include "src/developer/debug/zxdb/console/command.h"
#include "src/developer/debug/zxdb/console/command_utils.h"
#include "src/developer/debug/zxdb/console/console.h"
#include "src/developer/debug/zxdb/console/console_context.h"
#include "src/developer/debug/zxdb/console/format_context.h"
#include "src/developer/debug/zxdb/console/input_location_parser.h"
#include "src/developer/debug/zxdb/console/output_buffer.h"
#include "src/developer/debug/zxdb/console/verbs.h"
#include "src/developer/debug/zxdb/symbols/location.h"
#include "src/lib/fxl/strings/string_printf.h"

namespace zxdb {

namespace {

constexpr int kStopSwitch = 1;
constexpr int kEnableSwitch = 2;
constexpr int kTypeSwitch = 3;

// Validates that the current command has a breakpoint associated with it and no additional
// arguments. Used for enable/disable/clear that do one thing to a breakpoint
Err ValidateNoArgBreakpointModification(const Command& cmd, const char* command_name) {
  Err err = cmd.ValidateNouns({Noun::kBreakpoint});
  if (err.has_error())
    return err;

  // Expect no args. If an arg was specified, most likely they're trying to use GDB syntax of
  // e.g. "clear 2".
  if (cmd.args().size() > 0) {
    return Err(
        fxl::StringPrintf("\"%s\" takes no arguments. To specify an explicit "
                          "breakpoint to %s,\nuse \"bp <index> %s\"",
                          command_name, command_name, command_name));
  }

  if (!cmd.breakpoint()) {
    return Err(
        fxl::StringPrintf("There is no active breakpoint and no breakpoint was given.\n"
                          "Use \"bp <index> %s\" to specify one.\n",
                          command_name));
  }

  return Err();
}

// Callback for when updating a breakpoint is done. This will output the error or a success
// message describing the breakpoint.
//
// If non-null, the message_prefix is prepended to the description.
void CreateOrEditBreakpointComplete(fxl::WeakPtr<Breakpoint> breakpoint, const char* message_prefix,
                                    const Err& err) {
  if (!breakpoint)
    return;  // Do nothing if the breakpoint is gone.

  Console* console = Console::get();
  if (err.has_error()) {
    OutputBuffer out("Error setting breakpoint: ");
    out.Append(err);
    console->Output(out);
    return;
  }

  OutputBuffer out;
  if (message_prefix) {
    out.Append(message_prefix);
    out.Append(" ");
  }
  out.Append(FormatBreakpoint(&console->context(), breakpoint.get(), true));

  console->Output(out);
}

// Backend for setting attributes on a breakpoint from both creation and editing. The given
// breakpoint is specified if this is an edit, or is null if this is a creation.
Err CreateOrEditBreakpoint(ConsoleContext* context, const Command& cmd, Breakpoint* breakpoint,
                           CommandCallback callback) {
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
      return Err("--enabled switch requires either \"true\" or \"false\" values.");
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

  // Type.
  auto break_type = debug_ipc::BreakpointType::kSoftware;
  if (cmd.HasSwitch(kTypeSwitch)) {
    std::string type_str = cmd.GetSwitchValue(kTypeSwitch);
    if (type_str == "s" || type_str == "software") {
      break_type = debug_ipc::BreakpointType::kSoftware;
    } else if (type_str == "h" || type_str == "hardware") {
      break_type = debug_ipc::BreakpointType::kHardware;
    } else if (type_str == "w" || type_str == "watchpoint") {
      break_type = debug_ipc::BreakpointType::kWatchpoint;
    } else {
      return Err(fxl::StringPrintf("Unknown breakpoint type: %s", type_str.data()));
    }
  }
  settings.type = break_type;

  // Location.
  if (cmd.args().empty()) {
    if (!breakpoint) {
      // Creating a breakpoint with no location implicitly uses the current frame's current
      // location.
      if (!cmd.frame()) {
        return Err(ErrType::kInput,
                   "There isn't a current frame to take the breakpoint "
                   "location from.");
      }

      // Use the file/line of the frame if available. This is what a user will generally want to see
      // in the breakpoint list, and will persist across restarts. Fall back to an address
      // otherwise. Sometimes the file/line might not be what they want, though.
      const Location& frame_loc = cmd.frame()->GetLocation();
      if (frame_loc.has_symbols())
        settings.locations.emplace_back(frame_loc.file_line());
      else
        settings.locations.emplace_back(cmd.frame()->GetAddress());
    }
  } else if (cmd.args().size() == 1u) {
    Err err = ParseLocalInputLocation(cmd.frame(), cmd.args()[0], &settings.locations);
    if (err.has_error())
      return err;
  } else {
    return Err(ErrType::kInput,
               "Expecting only one arg for the location.\n"
               "Formats: <function>, <file>:<line#>, <line#>, or *<address>");
  }
  FXL_DCHECK(!settings.locations.empty());  // Should have filled something in.

  // Scope.
  settings.scope = ExecutionScopeForCommand(cmd);

  // Commit the changes.
  if (!breakpoint) {
    // New breakpoint.
    breakpoint = context->session()->system().CreateNewBreakpoint();
    context->SetActiveBreakpoint(breakpoint);
  }
  breakpoint->SetSettings(settings, [breakpoint = breakpoint->GetWeakPtr(),
                                     callback = std::move(callback)](const Err& err) mutable {
    CreateOrEditBreakpointComplete(std::move(breakpoint), nullptr, err);
    if (callback) {
      callback(err);
    }
  });

  return Err();
}

// break -------------------------------------------------------------------------------------------

const char kBreakShortHelp[] = "break / b: Create a breakpoint.";
const char kBreakHelp[] =
    R"(break <location>

  Alias: "b"

  Creates or modifies a breakpoint. Not to be confused with the "breakpoint" /
  "bp" noun which lists breakpoints and modifies the breakpoint context. See
  "help bp" for more.

  The new breakpoint will become the active breakpoint so future breakpoint
  commands will apply to it by default.

Location arguments

  Current frame's address (no input)
    break

)" LOCATION_ARG_HELP("break")
        R"(
  You can also specify the magic symbol "@main" to break on the process'
  entrypoint:
    break @main

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

  --type=[ (software|s) | (hardware|h) | (watchpoint|w) ]  (default: software)
  -t [ (software|s) | (hardware|h) | (watchpoint|w) ]

      Defines what kind of breakpoint to use. Hardware registers require support
      from the architecture and are limited in quantity. Keep this in mind when
      using breakpoints that will expand to several locations.

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

ELF PLT breakpoints for system calls

  Breakpoints can be set in the code in the ELF Procedure Linkage Table. This
  code is the tiny stub that the dynamic linker fixes up to resolve each
  function call imported from other ELF objects.

  This allows is setting breakpoints on system calls without using hardware
  breakpoints. The Zircon vDSO is mapped read-only which prevents the debugger
  from inserting hardware breakpoints. But each library's calls to vDSO
  functions goes through that library's PLT which is writable by the debugger.

  To indicate a PLT breakpoint, append "@plt" to the name of the imported
  function:

    [zxdb] break zx_debug_write@plt

  This will apply the breakpoint to every library's PLT entry for
  "zx_debug_write".

Breakpoints on overloaded functions

  If a named function has multiple overloads, the debugger will set a breakpoint
  on all of them. Specifying an individual overload by name is not supported
  (bug 41928).

  To refer to an individual overload, either refer to the location by file:line
  or by address. To get the addresses of each overload, use the command
  "sym-info FunctionName".

Other breakpoint commands

  "breakpoint" / "bp": List or select breakpoints.
  "clear": To delete breakpoints.
  "disable": Disable a breakpoint off without deleting it.
  "enable": Enable a previously-disabled breakpoint.

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

  break 32 --type h
      Break at line 23 of the file referenced by the current frame and use a
      hardware breakpoint.
)";
Err DoBreak(ConsoleContext* context, const Command& cmd, CommandCallback callback = nullptr) {
  Err err = cmd.ValidateNouns({Noun::kProcess, Noun::kThread, Noun::kFrame, Noun::kBreakpoint});
  if (err.has_error())
    return err;
  return CreateOrEditBreakpoint(context, cmd, nullptr, std::move(callback));
}

// hardware-breakpoint -----------------------------------------------------------------------------

const char kHardwareBreakpointShortHelp[] =
    "hardware-breakpoint / hb: Create a hardware breakpoint.";

const char kHardwareBreakpointHelp[] =
    R"(hardware-breakpoint <location>

  Alias: "hb"

  Creates or modifies a hardware breakpoint.
  This is a convenience shorthand for "break --type hardware <location>".
  See "help break" for more information.
)";

Err DoHardwareBreakpoint(ConsoleContext* context, const Command& cmd) {
  // We hack our way the command :)
  Command* cmd_ptr = const_cast<Command*>(&cmd);
  cmd_ptr->SetSwitch(kTypeSwitch, "hardware");
  return DoBreak(context, *cmd_ptr);
}

// clear -------------------------------------------------------------------------------------------

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
  if (Err err = ValidateNoArgBreakpointModification(cmd, "clear"); err.has_error())
    return err;

  OutputBuffer desc("Deleted ");
  desc.Append(FormatBreakpoint(context, cmd.breakpoint(), false));

  context->session()->system().DeleteBreakpoint(cmd.breakpoint());

  Console::get()->Output(desc);
  return Err();
}

// enable ------------------------------------------------------------------------------------------

const char kEnableShortHelp[] = "enable: Enable a breakpoint.";
const char kEnableHelp[] =
    R"(enable

  By itself, "enable" will enable the current active breakpoint. It is the
  opposite of "disable".

  It can be combined with an explicit breakpoint prefix to indicate a specific
  breakpoint to enable.

See also

  "help break": To create breakpoints.
  "help breakpoint": To manage the current breakpoint context.
  "help disable": To disable breakpoints.

Examples

  breakpoint 2 enable
  bp 2 enable
      Enable a specific breakpoint.

  enable
      Enable the current breakpoint.
)";
Err DoEnable(ConsoleContext* context, const Command& cmd) {
  if (Err err = ValidateNoArgBreakpointModification(cmd, "enable"); err.has_error())
    return err;

  BreakpointSettings settings = cmd.breakpoint()->GetSettings();
  settings.enabled = true;

  cmd.breakpoint()->SetSettings(
      settings, [weak_bp = cmd.breakpoint()->GetWeakPtr()](const Err& err) {
        CreateOrEditBreakpointComplete(std::move(weak_bp), "Enabled", err);
      });
  return Err();
}

// disable -----------------------------------------------------------------------------------------

const char kDisableShortHelp[] = "disable: Disable a breakpoint.";
const char kDisableHelp[] =
    R"(disable

  By itself, "disable" will disable the current active breakpoint. It is the
  opposite of "enable".

  It can be combined with an explicit breakpoint prefix to indicate a specific
  breakpoint to disable.

See also

  "help break": To create breakpoints.
  "help breakpoint": To manage the current breakpoint context.
  "help enable": To enable breakpoints.

Examples

  breakpoint 2 disable
  bp 2 disable
      Disable a specific breakpoint.

  disable
      Disable the current breakpoint.
)";
Err DoDisable(ConsoleContext* context, const Command& cmd) {
  if (Err err = ValidateNoArgBreakpointModification(cmd, "enable"); err.has_error())
    return err;

  BreakpointSettings settings = cmd.breakpoint()->GetSettings();
  settings.enabled = false;

  cmd.breakpoint()->SetSettings(
      settings, [weak_bp = cmd.breakpoint()->GetWeakPtr()](const Err& err) {
        CreateOrEditBreakpointComplete(std::move(weak_bp), "Disabled", err);
      });
  return Err();
}
// edit --------------------------------------------------------------------------------------------

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
Err DoEdit(ConsoleContext* context, const Command& cmd, CommandCallback callback = nullptr) {
  if (!cmd.HasNoun(Noun::kBreakpoint)) {
    // Edit requires an explicit "breakpoint" context so that in the future we can apply edit to
    // other nouns. I'm thinking any noun that can be created can have its switches modified via an
    // "edit" command that accepts the same settings.
    return Err(ErrType::kInput,
               "\"edit\" requires an explicit breakpoint context.\n"
               "Either \"breakpoint edit\" for the active breakpoint, or "
               "\"breakpoint <index> edit\" for an\nexplicit one.");
  }

  Err err = cmd.ValidateNouns({Noun::kProcess, Noun::kThread, Noun::kBreakpoint});
  if (err.has_error())
    return err;

  return CreateOrEditBreakpoint(context, cmd, cmd.breakpoint(), std::move(callback));
}

}  // namespace

void AppendBreakpointVerbs(std::map<Verb, VerbRecord>* verbs) {
  SwitchRecord enable_switch(kEnableSwitch, true, "enable", 'e');
  SwitchRecord stop_switch(kStopSwitch, true, "stop", 's');
  SwitchRecord type_switch(kTypeSwitch, true, "type", 't');

  VerbRecord break_record(&DoBreak, &CompleteInputLocation, {"break", "b"}, kBreakShortHelp,
                          kBreakHelp, CommandGroup::kBreakpoint);
  break_record.switches.push_back(enable_switch);
  break_record.switches.push_back(stop_switch);
  break_record.switches.push_back(type_switch);
  (*verbs)[Verb::kBreak] = std::move(break_record);

  // Note: if "edit" becomes more general than just for breakpoints, we'll want to change the
  // command category.
  VerbRecord edit_record(&DoEdit, {"edit", "ed"}, kEditShortHelp, kEditHelp,
                         CommandGroup::kBreakpoint);
  edit_record.switches.push_back(enable_switch);
  edit_record.switches.push_back(stop_switch);
  (*verbs)[Verb::kEdit] = std::move(edit_record);

  (*verbs)[Verb::kHardwareBreakpoint] =
      VerbRecord(&DoHardwareBreakpoint, {"hardware-breakpoint", "hb"}, kHardwareBreakpointShortHelp,
                 kHardwareBreakpointHelp, CommandGroup::kBreakpoint);

  (*verbs)[Verb::kClear] =
      VerbRecord(&DoClear, {"clear", "cl"}, kClearShortHelp, kClearHelp, CommandGroup::kBreakpoint);

  (*verbs)[Verb::kEnable] =
      VerbRecord(&DoEnable, {"enable"}, kEnableShortHelp, kEnableHelp, CommandGroup::kBreakpoint);
  (*verbs)[Verb::kDisable] = VerbRecord(&DoDisable, {"disable"}, kDisableShortHelp, kDisableHelp,
                                        CommandGroup::kBreakpoint);
}

}  // namespace zxdb
