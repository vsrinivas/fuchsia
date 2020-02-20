// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/console/commands/verb_print.h"

#include "src/developer/debug/zxdb/console/command.h"
#include "src/developer/debug/zxdb/console/command_utils.h"
#include "src/developer/debug/zxdb/console/console.h"
#include "src/developer/debug/zxdb/console/output_buffer.h"
#include "src/developer/debug/zxdb/console/print_command_utils.h"
#include "src/developer/debug/zxdb/console/verbs.h"

namespace zxdb {

namespace {

const char kPrintShortHelp[] = "print / p: Print a variable or expression.";
const char kPrintHelp[] =
    R"(print <expression>

  Alias: p

  Evaluates a simple expression or variable name and prints the result.

  The expression is evaluated by default in the currently selected thread and
  stack frame. You can override this with "frame <x> print ...".

  👉 See "help expressions" for how to write expressions.

Arguments

)" PRINT_COMMAND_SWITCH_HELP
    R"(

Examples

  p foo
  print foo
      Print a variable

  p *foo->bar
  print &foo.bar[2]
      Deal with structs and arrays.

  f 2 p -t foo
  frame 2 print -t foo
  thread 1 frame 2 print -t foo
      Print a variable with types in the context of a specific stack frame.
)";

Err RunVerbPrint(ConsoleContext* context, const Command& cmd) {
  // This will work in any context, but the data that's available will vary depending on whether
  // there's a stopped thread, a process, or nothing.
  fxl::RefPtr<EvalContext> eval_context = GetEvalContextForCommand(cmd);

  ErrOr<ConsoleFormatOptions> options = GetPrintCommandFormatOptions(cmd);
  if (options.has_error())
    return options.err();

  auto data_provider = eval_context->GetDataProvider();
  return EvalCommandExpression(
      cmd, "print", eval_context, false, false,
      [options = options.value(), eval_context](ErrOrValue value) {
        if (value.has_error())
          Console::get()->Output(value.err());
        else
          Console::get()->Output(FormatValueForConsole(value.value(), options, eval_context));
      });
}

}  // namespace

VerbRecord GetPrintVerbRecord() {
  VerbRecord print(&RunVerbPrint, {"print", "p"}, kPrintShortHelp, kPrintHelp,
                   CommandGroup::kQuery);
  AppendPrintCommandSwitches(&print);
  print.param_type = VerbRecord::kOneParam;
  return print;
}

}  // namespace zxdb
