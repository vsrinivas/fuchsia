// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/console/commands/verb_steps.h"

#include "src/developer/debug/zxdb/client/frame.h"
#include "src/developer/debug/zxdb/client/process.h"
#include "src/developer/debug/zxdb/client/step_into_specific_thread_controller.h"
#include "src/developer/debug/zxdb/client/substatement.h"
#include "src/developer/debug/zxdb/client/target.h"
#include "src/developer/debug/zxdb/client/thread.h"
#include "src/developer/debug/zxdb/console/command.h"
#include "src/developer/debug/zxdb/console/command_utils.h"
#include "src/developer/debug/zxdb/console/console.h"
#include "src/developer/debug/zxdb/console/format_location.h"
#include "src/developer/debug/zxdb/console/output_buffer.h"
#include "src/developer/debug/zxdb/console/verbs.h"
#include "src/developer/debug/zxdb/symbols/process_symbols.h"
#include "src/lib/fxl/strings/string_printf.h"

namespace zxdb {

namespace {

const char kStepsShortHelp[] = "steps / ss: Step into specific call.";
const char kStepsHelp[] =
    R"(steps / ss: Step into specific call.

  Alias: ss

  Finds function calls from the current source line and interactively asks which
  one to step into. Execution will stop at the beginning of the selected
  function.

  The function calls are presented in execution order from the current line
  after the current instruction pointer. If the selected function call is not
  executed due to program logic, execution will stop before the first executed
  instruction immediately following it.

Examples

  [zxdb] ss
    1 Controller::GetLaunchTime()
    2 LaunchRocket()
  Step into specific: 2
)";

// This command is executed in three phases, each separated by an async step.
//
//  1. RunVerbSteps: Request identification of the substatements for the current line.
//
// ---- Async while memory is being fetched from the target.
//
//  2. RunStepsWithSubstatements: Once the call information has been collected, interpret it and
//     start the prompt.
//
// ---- Async while the user enters their selection.
//
//  3. CompleteSteps: Actually do the step given the selected item.

void CompleteSteps(Thread* thread, TargetPointer ip, const std::vector<AddressRange>& ranges,
                   const std::string& one_based_index_str,
                   fxl::RefPtr<CommandContext> cmd_context) {
  // Validate that the thread hasn't changed since we prompted.
  if (thread->GetStack().empty() || ip != thread->GetStack()[0]->GetAddress()) {
    return cmd_context->ReportError(
        Err("Thread continued in the background, giving up on \"steps\" command."));
  }

  if (one_based_index_str == "q")
    return;  // Don't do anything for "quit".

  size_t chosen_index = 0;  // One-based!
  if (sscanf(one_based_index_str.c_str(), "%zu", &chosen_index) != 1 || chosen_index == 0 ||
      chosen_index > ranges.size()) {
    // The prompt should have validated the input but we double-check.
    FX_NOTREACHED();
    cmd_context->ReportError(Err("Bad selected index."));
    return;
  }

  auto controller = std::make_unique<StepIntoSpecificThreadController>(
      ranges[chosen_index - 1], fit::defer_callback([cmd_context]() {}));
  thread->ContinueWith(std::move(controller), [cmd_context](const Err& err) {
    if (err.has_error())
      cmd_context->ReportError(err);
  });
}

void RunVerbSteps(const Command& cmd, fxl::RefPtr<CommandContext> cmd_context) {
  if (Err err = AssertStoppedThreadWithFrameCommand(cmd_context->GetConsoleContext(), cmd, "steps");
      err.has_error())
    return cmd_context->ReportError(err);

  Process* process = cmd.target()->GetProcess();
  GetSubstatementCallsForLine(process, cmd.frame()->GetLocation(),
                              [weak_thread = cmd.thread()->GetWeakPtr(), cmd_context](
                                  const Err& err, std::vector<SubstatementCall> calls) {
                                if (!weak_thread) {
                                  cmd_context->ReportError(Err("Thread terminated."));
                                } else if (err.has_error()) {
                                  cmd_context->ReportError(err);
                                } else {
                                  RunVerbStepsWithSubstatements(weak_thread.get(), calls,
                                                                cmd_context);
                                }
                              });
}

}  // namespace

VerbRecord GetStepsVerbRecord() {
  return VerbRecord(&RunVerbSteps, {"steps", "ss"}, kStepsShortHelp, kStepsHelp,
                    CommandGroup::kStep, SourceAffinity::kSource);
}

void RunVerbStepsWithSubstatements(Thread* thread, std::vector<SubstatementCall> calls,
                                   fxl::RefPtr<CommandContext> cmd_context) {
  Console* console = cmd_context->console();
  if (!console)
    return;  // Console gone, nothing to do.

  if (thread->GetStack().empty()) {
    cmd_context->Output("Can't step non-suspended thread.");
    return;
  }

  if (calls.empty()) {
    cmd_context->Output("No function calls from this line.");
    return;
  }

  TargetPointer ip = thread->GetStack()[0]->GetAddress();

  ProcessSymbols* symbols = thread->GetProcess()->GetSymbols();

  FormatLocationOptions format_opts;
  format_opts.func.params = FormatFunctionNameOptions::kElideParams;
  format_opts.func.name.show_global_qual = false;
  format_opts.func.name.elide_templates = true;
  format_opts.func.name.bold_last = true;
  format_opts.show_file_line = false;

  // Collects step over ranges for each option.
  std::vector<AddressRange> ranges;

  line_input::ModalPromptOptions prompt_opts;
  OutputBuffer message;
  for (size_t i = 0; i < calls.size(); i++) {
    if (calls[i].call_addr < ip)
      continue;  // Skip anything already past.

    ranges.emplace_back(ip, calls[i].call_addr);

    std::string index_str = fxl::StringPrintf("%zu", ranges.size());
    prompt_opts.options.push_back(index_str);  // Tell the prompt this is a valid option.
    message.Append(Syntax::kSpecial, fxl::StringPrintf("%3s ", index_str.c_str()));

    if (calls[i].call_dest) {
      // Provide a symbol name for the call destination.
      auto locs = symbols->ResolveInputLocation(InputLocation(*calls[i].call_dest));
      FX_DCHECK(locs.size() == 1);  // Should always get one result for an address lookup.
      message.Append(FormatLocation(locs[0], format_opts));
    } else {
      // Indirect calls won't have a call address.
      message.Append("«Indirect or virtual function call, no name available.»");
    }
    message.Append("\n");
  }

  if (ranges.empty()) {
    message.Append("Already past all calls on this line.\n");
    cmd_context->Output(message);
    return;
  }

  // Allow "q" to quit.
  prompt_opts.options.push_back("q");
  message.Append(Syntax::kSpecial, "  q");
  message.Append("uit\n");
  prompt_opts.cancel_option = "q";

  // Single-digit entry doesn't require <Enter>.
  prompt_opts.require_enter = calls.size() >= 10;

  console->ModalGetOption(
      prompt_opts, std::move(message), "> ",
      [weak_thread = thread->GetWeakPtr(), ip, ranges, cmd_context](const std::string& input) {
        if (!weak_thread)
          return;
        CompleteSteps(weak_thread.get(), ip, ranges, input, cmd_context);
      });
}

}  // namespace zxdb
