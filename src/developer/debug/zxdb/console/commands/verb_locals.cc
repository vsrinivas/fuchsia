// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/console/commands/verb_locals.h"

#include "src/developer/debug/zxdb/client/frame.h"
#include "src/developer/debug/zxdb/console/command.h"
#include "src/developer/debug/zxdb/console/command_utils.h"
#include "src/developer/debug/zxdb/console/console.h"
#include "src/developer/debug/zxdb/console/output_buffer.h"
#include "src/developer/debug/zxdb/console/print_command_utils.h"
#include "src/developer/debug/zxdb/console/verbs.h"
#include "src/developer/debug/zxdb/symbols/function.h"
#include "src/developer/debug/zxdb/symbols/variable.h"

namespace zxdb {

namespace {

const char kLocalsShortHelp[] = "locals: Print local variables and function args.";
const char kLocalsHelp[] =
    R"(locals

  Prints all local variables and the current function's arguments. By default
  it will print the variables for the currently selected stack frame.

  You can override the stack frame with the "frame" noun to get the locals
  for any specific stack frame of thread.

Arguments

)" PRINT_COMMAND_SWITCH_HELP
    R"(
Examples

  locals
      Prints locals and args for the current stack frame.

  f 4 locals
  frame 4 locals
  thread 2 frame 3 locals
      Prints locals for a specific stack frame.

  f 4 locals -t
      Prints locals with types.
)";
void RunVerbLocals(const Command& cmd, fxl::RefPtr<CommandContext> cmd_context) {
  if (Err err =
          AssertStoppedThreadWithFrameCommand(cmd_context->GetConsoleContext(), cmd, "locals");
      err.has_error())
    return cmd_context->ReportError(err);

  const Location& location = cmd.frame()->GetLocation();
  if (!location.symbol())
    return cmd_context->ReportError(Err("There is no symbol information for the frame."));
  const Function* function = location.symbol().Get()->As<Function>();
  if (!function)
    return cmd_context->ReportError(Err("Symbols are corrupt."));

  // Walk upward from the innermost lexical block for the current IP to collect local variables.
  // Using the map allows collecting only the innermost version of a given name, and sorts them as
  // we go.
  //
  // Need owning variable references to copy data out.
  //
  // Note that this does NOT skip "artificial" variables. In the standard these are marked as
  // compiler-generated and we should probably just skip them. The exception is for "this"
  // variables which we do want to show.
  //
  // Be aware that as of this writing there is Clang bug https://bugs.llvm.org/show_bug.cgi?id=49565
  // which marks the artifical flag on structured bindings incorrectly:
  //
  //   auto [a, b] = GetSomePair();
  //
  // It generates an unnamed std::pair variable without the DW_AT_artificial tag, and "a" and "b"
  // varialbles WITH the artificial tag. This is backwards from what one would expect and how GCC
  // encodes this (the internal generated variable should be marked artificial, and the ones the
  // user named should not be).
  //
  // Our behavior of showing artificial variables but hiding unnamed ones worked around this bug.
  // It's not clear what other cases in C++ there might be for artificial variables.
  std::map<std::string, fxl::RefPtr<Variable>> vars;
  VisitLocalBlocks(function->GetMostSpecificChild(location.symbol_context(), location.address()),
                   [&vars](const CodeBlock* block) {
                     for (const auto& lazy_var : block->variables()) {
                       const Variable* var = lazy_var.Get()->As<Variable>();
                       if (!var)
                         continue;  // Symbols are corrupt.

                       const std::string& name = var->GetAssignedName();
                       if (name.empty())
                         continue;

                       if (vars.find(name) == vars.end())
                         vars[name] = RefPtrTo(var);  // New one.
                     }
                     return VisitResult::kContinue;
                   });

  // Add function parameters. Don't overwrite existing names in case of duplicates to duplicate the
  // shadowing rules of the language.
  for (const auto& param : function->parameters()) {
    const Variable* var = param.Get()->As<Variable>();
    if (!var)
      continue;  // Symbols are corrupt.

    const std::string& name = var->GetAssignedName();
    if (!name.empty() && vars.find(name) == vars.end())
      vars[name] = RefPtrTo(var);  // New one.
  }

  if (vars.empty()) {
    cmd_context->Output("No local variables in scope.");
    return;
  }

  ErrOr<ConsoleFormatOptions> options = GetPrintCommandFormatOptions(cmd);
  if (options.has_error())
    return cmd_context->ReportError(options.err());

  auto output = fxl::MakeRefCounted<AsyncOutputBuffer>();
  for (const auto& pair : vars) {
    output->Append(FormatVariableForConsole(pair.second.get(), options.value(),
                                            cmd.frame()->GetEvalContext()));
    output->Append("\n");
  }
  output->Complete();
  cmd_context->Output(output);
}

}  // namespace

VerbRecord GetLocalsVerbRecord() {
  VerbRecord locals(&RunVerbLocals, {"locals"}, kLocalsShortHelp, kLocalsHelp,
                    CommandGroup::kQuery);
  AppendPrintCommandSwitches(&locals);
  return locals;
}

}  // namespace zxdb
