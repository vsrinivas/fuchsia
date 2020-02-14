// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/console/commands/verb_backtrace.h"

#include "src/developer/debug/zxdb/console/command.h"
#include "src/developer/debug/zxdb/console/console.h"
#include "src/developer/debug/zxdb/console/format_frame.h"
#include "src/developer/debug/zxdb/console/format_location.h"
#include "src/developer/debug/zxdb/console/format_node_console.h"
#include "src/developer/debug/zxdb/console/output_buffer.h"
#include "src/developer/debug/zxdb/console/verbs.h"

namespace zxdb {

namespace {

constexpr int kForceAllTypes = 1;
constexpr int kRawOutput = 2;
constexpr int kVerboseBacktrace = 3;

const char kBacktraceShortHelp[] = "backtrace / bt: Print a backtrace.";
const char kBacktraceHelp[] =
    R"(backtrace / bt

  Prints a backtrace of the thread, including function parameters.

  To see just function names and line numbers, use "frame" or just "f".

Arguments

  -r
  --raw
      Expands frames that were collapsed by the "pretty" stack formatter.

  -t
  --types
      Include all type information for function parameters.

  -v
  --verbose
      Include extra stack frame information:
       • Full template lists and function parameter types.
       • Instruction pointer.
       • Stack pointer.
       • Stack frame base pointer.

Examples

  t 2 bt
  thread 2 backtrace
)";

Err RunVerbBacktrace(ConsoleContext* context, const Command& cmd) {
  if (Err err = cmd.ValidateNouns({Noun::kProcess, Noun::kThread}); err.has_error())
    return err;

  if (!cmd.thread())
    return Err("There is no thread to have frames.");

  FormatStackOptions opts;

  if (!cmd.HasSwitch(kRawOutput))
    opts.pretty_stack = context->pretty_stack_manager();

  opts.frame.loc = FormatLocationOptions(cmd.target());
  opts.frame.loc.show_params = cmd.HasSwitch(kForceAllTypes);
  opts.frame.loc.func.name.elide_templates = true;
  opts.frame.loc.func.name.bold_last = true;
  opts.frame.loc.func.params = FormatFunctionNameOptions::kElideParams;

  opts.frame.detail = FormatFrameOptions::kParameters;
  if (cmd.HasSwitch(kVerboseBacktrace)) {
    opts.frame.detail = FormatFrameOptions::kVerbose;
    opts.frame.loc.func.name.elide_templates = false;
    opts.frame.loc.func.params = FormatFunctionNameOptions::kParamTypes;
  }

  // These are minimal since there is often a lot of data.
  opts.frame.variable.verbosity = ConsoleFormatOptions::Verbosity::kMinimal;
  opts.frame.variable.verbosity = cmd.HasSwitch(kForceAllTypes)
                                      ? ConsoleFormatOptions::Verbosity::kAllTypes
                                      : ConsoleFormatOptions::Verbosity::kMinimal;
  opts.frame.variable.pointer_expand_depth = 1;
  opts.frame.variable.max_depth = 3;

  // Always force update the stack. Various things can have changed and when the user requests
  // a stack we want to be sure things are correct.
  Console::get()->Output(FormatStack(cmd.thread(), true, opts));
  return Err();
}

}  // namespace

VerbRecord GetBacktraceVerbRecord() {
  VerbRecord backtrace(&RunVerbBacktrace, {"backtrace", "bt"}, kBacktraceShortHelp, kBacktraceHelp,
                       CommandGroup::kQuery);
  SwitchRecord force_types(kForceAllTypes, false, "types", 't');
  SwitchRecord raw(kRawOutput, false, "raw", 'r');
  SwitchRecord verbose(kVerboseBacktrace, false, "verbose", 'v');
  backtrace.switches = {force_types, raw, verbose};

  return backtrace;
}

}  // namespace zxdb
