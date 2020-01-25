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
#include "src/developer/debug/zxdb/console/string_util.h"
#include "src/developer/debug/zxdb/console/verbs.h"
#include "src/developer/debug/zxdb/symbols/location.h"
#include "src/lib/fxl/strings/string_printf.h"

namespace zxdb {

namespace {

constexpr int kSizeSwitch = 1;
constexpr int kStopSwitch = 2;
constexpr int kDisabledSwitch = 3;
constexpr int kTypeSwitch = 4;
constexpr int kOneShotSwitch = 5;

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

// General output for Callback for when updating a breakpoint is done. This will output a
// description of the breakpoint with a type-specific prefix.
void CreateOrEditBreakpointComplete(Breakpoint* breakpoint, const char* message_prefix) {
  Console* console = Console::get();

  OutputBuffer out;
  out.Append(message_prefix);
  out.Append(" ");
  out.Append(FormatBreakpoint(&console->context(), breakpoint, true));

  console->Output(out);
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

)" LOCATION_ARG_HELP("break") LOCATION_EXPRESSION_HELP("break")
        R"(  You can also specify the magic symbol "@main" to break on the process'
  entrypoint:
      break @main

Options

  --disabled
  -d
      Creates the breakpoint as initially disabled. Otherwise, it will be
      enabled.

  --one-shot
  -o
      Creates a one-shot breakpoint. One-shot breakpoints are automatically
      deleted after they are hit once.

  --size=<byte-size>
  -s <byte-size>
      Size in bytes for hardware write and read-write breakpoints. This will
      default to 4 if unspecified. Not valid for hardware or software execution
      breakpoints. The address will need to be aligned to an even multiple of
      its size.

  --stop=[ all | process | thread | none ]
  -p [ all | process | thread | none ]
      Controls what execution is stopped when the breakpoint is hit. By
      default all threads of all debugged process will be stopped ("all") when
      a breakpoint is hit. But it's possible to only stop the threads of the
      current process ("process") or the thread that hit the breakpoint
      ("thread").

      If "none" is specified, any threads hitting the breakpoint will
      immediately resume, but the hit count will continue to accumulate.

  --type=<type>
  -t <type>
      The type of the breakpoint. Defaults to "software". Possible values are:

)" BREAKPOINT_TYPE_HELP("      ")
            R"(
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

Editing breakpoint attributes

  Individual breakpoint attributes can be accessed with the "get" and "set"
  commands. To list all attributes on the current breakpoint:

    bp get

  To get a specific value along with help for what the setting means, give the
  specific attribute:

    bp get stop

  And to set the attribute:

    bp set stop = thread

Other breakpoint commands

  "breakpoint" / "bp": List or select breakpoints.
  "clear": To delete breakpoints.
  "disable": Disable a breakpoint without deleting it.
  "enable": Enable a previously-disabled breakpoint.
  "watch": Create a hardware write breakpoint.

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

  break --type h 23
      Break at line 23 of the file referenced by the current frame and use a
      hardware breakpoint.
)";
Err DoBreak(ConsoleContext* context, const Command& cmd, CommandCallback cb) {
  Err err = cmd.ValidateNouns({Noun::kProcess, Noun::kThread, Noun::kFrame, Noun::kBreakpoint});
  if (err.has_error())
    return err;

  // Get existing settings (or defaults for new one).
  BreakpointSettings settings;

  // Disabled flag.
  if (cmd.HasSwitch(kDisabledSwitch))
    settings.enabled = false;

  // One-shot.
  if (cmd.HasSwitch(kOneShotSwitch))
    settings.one_shot = true;

  // Stop mode.
  if (cmd.HasSwitch(kStopSwitch)) {
    auto stop_mode = BreakpointSettings::StringToStopMode(cmd.GetSwitchValue(kStopSwitch));
    if (!stop_mode) {
      return Err(
          "--%s requires \"%s\", \"%s\", \"%s\", or \"%s\".", ClientSettings::Breakpoint::kStopMode,
          ClientSettings::Breakpoint::kStopMode_All, ClientSettings::Breakpoint::kStopMode_Process,
          ClientSettings::Breakpoint::kStopMode_Thread, ClientSettings::Breakpoint::kStopMode_None);
    }
    settings.stop_mode = *stop_mode;
  }

  // Type.
  settings.type = BreakpointSettings::Type::kSoftware;
  if (cmd.HasSwitch(kTypeSwitch)) {
    if (auto opt_type = BreakpointSettings::StringToType(cmd.GetSwitchValue(kTypeSwitch)))
      settings.type = *opt_type;
    else
      return Err("Unknown breakpoint type.");
  }

  // Size. Track if this is set or not si we can change the default based on the expression result.
  bool has_explicit_size = false;
  if (cmd.HasSwitch(kSizeSwitch)) {
    has_explicit_size = true;

    if (!BreakpointSettings::TypeHasSize(settings.type))
      return Err("Breakpoint size is only supported for write and read-write breakpoints.");
    if (Err err = StringToUint32(cmd.GetSwitchValue(kSizeSwitch), &settings.byte_size);
        err.has_error())
      return err;
  } else if (BreakpointSettings::TypeHasSize(settings.type)) {
    settings.byte_size = 4;  // Default size.
  }

  // Scope.
  settings.scope = ExecutionScopeForCommand(cmd);

  // Location.
  if (cmd.args().size() > 1u) {
    return Err(ErrType::kInput,
               "Expecting only one arg for the location.\n"
               "Formats: <function>, <file>:<line#>, <line#>, or *<expression>");
  }

  if (cmd.args().empty()) {
    // Creating a breakpoint with no location implicitly uses the current frame's current
    // location.
    if (!cmd.frame()) {
      return Err(ErrType::kInput,
                 "There isn't a current frame to take the breakpoint location from.");
    }

    // Use the file/line of the frame if available. This is what a user will generally want to see
    // in the breakpoint list, and will persist across restarts. Fall back to an address
    // otherwise. Sometimes the file/line might not be what they want, though.
    const Location& frame_loc = cmd.frame()->GetLocation();
    if (frame_loc.has_symbols())
      settings.locations.emplace_back(frame_loc.file_line());
    else
      settings.locations.emplace_back(cmd.frame()->GetAddress());

    // New breakpoint.
    Breakpoint* breakpoint = context->session()->system().CreateNewBreakpoint();
    context->SetActiveBreakpoint(breakpoint);

    breakpoint->SetSettings(settings);
    CreateOrEditBreakpointComplete(breakpoint, "Created");
    if (cb)
      cb(err);
    return Err();
  }

  // Parse the given input location in args[0]. This may require async evaluation.
  Location cur_location;
  if (cmd.frame())
    cur_location = cmd.frame()->GetLocation();

  EvalLocalInputLocation(
      GetEvalContextForCommand(cmd), cur_location, cmd.args()[0],
      [settings, has_explicit_size, cb = std::move(cb)](ErrOr<std::vector<InputLocation>> locs,
                                                        std::optional<uint32_t> expr_size) mutable {
        if (locs.has_error()) {
          Console::get()->Output(locs.err());
          if (cb)
            cb(locs.err());
          return;
        }

        // New breakpoint.
        ConsoleContext* context = &Console::get()->context();
        Breakpoint* breakpoint = context->session()->system().CreateNewBreakpoint();
        context->SetActiveBreakpoint(breakpoint);

        if (BreakpointSettings::TypeHasSize(settings.type) && !has_explicit_size && expr_size) {
          // Input expression has a size we should default to.
          settings.byte_size = *expr_size;
        }

        settings.locations = locs.take_value();
        breakpoint->SetSettings(settings);
        CreateOrEditBreakpointComplete(breakpoint, "Created");
        if (cb)
          cb(Err());
      });

  return Err();
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

  It is an alias for:

    bp set enabled = true

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

  cmd.breakpoint()->SetSettings(settings);
  CreateOrEditBreakpointComplete(cmd.breakpoint(), "Enabled");
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

  It is an alias for:

    bp set enabled = false

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

  cmd.breakpoint()->SetSettings(settings);
  CreateOrEditBreakpointComplete(cmd.breakpoint(), "Disabled");
  return Err();
}

}  // namespace

void AppendBreakpointVerbs(std::map<Verb, VerbRecord>* verbs) {
  SwitchRecord disabled_switch(kDisabledSwitch, false, "disabled", 'd');
  SwitchRecord one_shot_switch(kOneShotSwitch, false, ClientSettings::Breakpoint::kOneShot, 'o');
  SwitchRecord size_switch(kSizeSwitch, true, ClientSettings::Breakpoint::kSize, 's');
  SwitchRecord stop_switch(kStopSwitch, true, ClientSettings::Breakpoint::kStopMode, 'p');
  SwitchRecord type_switch(kTypeSwitch, true, "type", 't');

  VerbRecord break_record(&DoBreak, &CompleteInputLocation, {"break", "b"}, kBreakShortHelp,
                          kBreakHelp, CommandGroup::kBreakpoint);
  break_record.switches.push_back(disabled_switch);
  break_record.switches.push_back(one_shot_switch);
  break_record.switches.push_back(size_switch);
  break_record.switches.push_back(stop_switch);
  break_record.switches.push_back(type_switch);
  (*verbs)[Verb::kBreak] = std::move(break_record);

  (*verbs)[Verb::kClear] =
      VerbRecord(&DoClear, {"clear", "cl"}, kClearShortHelp, kClearHelp, CommandGroup::kBreakpoint);

  (*verbs)[Verb::kEnable] =
      VerbRecord(&DoEnable, {"enable"}, kEnableShortHelp, kEnableHelp, CommandGroup::kBreakpoint);
  (*verbs)[Verb::kDisable] = VerbRecord(&DoDisable, {"disable"}, kDisableShortHelp, kDisableHelp,
                                        CommandGroup::kBreakpoint);
}

}  // namespace zxdb
