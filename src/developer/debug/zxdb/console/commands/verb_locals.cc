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
Err RunVerbLocals(ConsoleContext* context, const Command& cmd) {
  if (Err err = AssertStoppedThreadWithFrameCommand(context, cmd, "locals"); err.has_error())
    return err;

  const Location& location = cmd.frame()->GetLocation();
  if (!location.symbol())
    return Err("There is no symbol information for the frame.");
  const Function* function = location.symbol().Get()->AsFunction();
  if (!function)
    return Err("Symbols are corrupt.");

  // Walk upward from the innermost lexical block for the current IP to collect local variables.
  // Using the map allows collecting only the innermost version of a given name, and sorts them as
  // we go.
  //
  // Need owning variable references to copy data out.
  std::map<std::string, fxl::RefPtr<Variable>> vars;
  VisitLocalBlocks(function->GetMostSpecificChild(location.symbol_context(), location.address()),
                   [&vars](const CodeBlock* block) {
                     for (const auto& lazy_var : block->variables()) {
                       const Variable* var = lazy_var.Get()->AsVariable();
                       if (!var)
                         continue;  // Symbols are corrupt.

                       if (var->artificial())
                         continue;  // Skip compiler-generated symbols.

                       const std::string& name = var->GetAssignedName();
                       if (vars.find(name) == vars.end())
                         vars[name] = RefPtrTo(var);  // New one.
                     }
                     return VisitResult::kContinue;
                   });

  // Add function parameters. Don't overwrite existing names in case of duplicates to duplicate the
  // shadowing rules of the language.
  for (const auto& param : function->parameters()) {
    const Variable* var = param.Get()->AsVariable();
    if (!var)
      continue;  // Symbols are corrupt.

    // Here we do not exclude artificial parameters. "this" will be marked as artificial and we want
    // to include it. We could special-case the object pointer and exclude the rest, but there's not
    // much other use for compiler-generated parameters for now.

    const std::string& name = var->GetAssignedName();
    if (vars.find(name) == vars.end())
      vars[name] = RefPtrTo(var);  // New one.
  }

  if (vars.empty()) {
    Console::get()->Output("No local variables in scope.");
    return Err();
  }

  ErrOr<ConsoleFormatOptions> options = GetPrintCommandFormatOptions(cmd);
  if (options.has_error())
    return options.err();

  auto output = fxl::MakeRefCounted<AsyncOutputBuffer>();
  for (const auto& pair : vars) {
    output->Append(FormatVariableForConsole(pair.second.get(), options.value(),
                                            cmd.frame()->GetEvalContext()));
    output->Append("\n");
  }
  output->Complete();
  Console::get()->Output(std::move(output));
  return Err();
}

}  // namespace

VerbRecord GetLocalsVerbRecord() {
  VerbRecord locals(&RunVerbLocals, {"locals"}, kLocalsShortHelp, kLocalsHelp,
                    CommandGroup::kQuery);
  AppendPrintCommandSwitches(&locals);
  return locals;
}

}  // namespace zxdb
