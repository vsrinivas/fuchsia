// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <inttypes.h>

#include "src/developer/debug/shared/message_loop.h"
#include "src/developer/debug/zxdb/client/finish_thread_controller.h"
#include "src/developer/debug/zxdb/client/frame.h"
#include "src/developer/debug/zxdb/client/process.h"
#include "src/developer/debug/zxdb/client/session.h"
#include "src/developer/debug/zxdb/client/setting_schema_definition.h"
#include "src/developer/debug/zxdb/client/step_into_specific_thread_controller.h"
#include "src/developer/debug/zxdb/client/step_into_thread_controller.h"
#include "src/developer/debug/zxdb/client/step_over_thread_controller.h"
#include "src/developer/debug/zxdb/client/substatement.h"
#include "src/developer/debug/zxdb/client/thread.h"
#include "src/developer/debug/zxdb/client/until_thread_controller.h"
#include "src/developer/debug/zxdb/common/err.h"
#include "src/developer/debug/zxdb/console/command.h"
#include "src/developer/debug/zxdb/console/command_utils.h"
#include "src/developer/debug/zxdb/console/commands/verb_steps.h"
#include "src/developer/debug/zxdb/console/console.h"
#include "src/developer/debug/zxdb/console/format_frame.h"
#include "src/developer/debug/zxdb/console/format_location.h"
#include "src/developer/debug/zxdb/console/format_node_console.h"
#include "src/developer/debug/zxdb/console/format_register.h"
#include "src/developer/debug/zxdb/console/format_table.h"
#include "src/developer/debug/zxdb/console/format_target.h"
#include "src/developer/debug/zxdb/console/input_location_parser.h"
#include "src/developer/debug/zxdb/console/output_buffer.h"
#include "src/developer/debug/zxdb/console/string_util.h"
#include "src/developer/debug/zxdb/console/verbs.h"
#include "src/developer/debug/zxdb/expr/eval_context_impl.h"
#include "src/developer/debug/zxdb/expr/expr.h"
#include "src/developer/debug/zxdb/symbols/code_block.h"
#include "src/developer/debug/zxdb/symbols/function.h"
#include "src/developer/debug/zxdb/symbols/location.h"
#include "src/developer/debug/zxdb/symbols/process_symbols.h"
#include "src/developer/debug/zxdb/symbols/variable.h"
#include "src/developer/debug/zxdb/symbols/visit_scopes.h"
#include "src/lib/fxl/strings/string_printf.h"

namespace zxdb {

namespace {

constexpr int kStepIntoUnsymbolized = 1;
constexpr int kVerboseFormat = 2;
constexpr int kForceAllTypes = 3;
constexpr int kForceNumberChar = 4;
constexpr int kForceNumberSigned = 5;
constexpr int kForceNumberUnsigned = 6;
constexpr int kForceNumberHex = 7;
constexpr int kMaxArraySize = 8;
constexpr int kRawOutput = 9;
constexpr int kVerboseBacktrace = 10;

// If the system has at least one running process, returns true. If not, returns false and sets the
// err.
//
// When doing global things like System::Continue(), it will succeed if there are no running
// programs (it will successfully continue all 0 processes). This is confusing to the user so this
// function is used to check first.
bool VerifySystemHasRunningProcess(System* system, Err* err) {
  for (const Target* target : system->GetTargets()) {
    if (target->GetProcess())
      return true;
  }
  *err = Err("No processes are running.");
  return false;
}

// Populates the formatting options with the given command's switches.
Err GetConsoleFormatOptions(const Command& cmd, ConsoleFormatOptions* options) {
  // These defaults currently don't have exposed options. A pointer expand depth of one allows
  // local variables and "this" to be expanded without expanding anything else. Often pointed-to
  // classes are less useful and can be very large.
  options->pointer_expand_depth = 1;
  options->max_depth = 16;

  // All current users of this want the smart form.
  //
  // This keeps the default wrap columns at 80. We can consider querying the actual console width.
  // But very long lines start putting many struct members on the same line which gets increasingly
  // difficult to read. 80 columns feels reasonably close to how much you can take in at once.
  //
  // Note also that this doesn't stricly wrap the output to 80 columns. Long type names or values
  // will still use the full width and will be wrapped by the console. This wrapping only affects
  // the splitting of items across lines.
  options->wrapping = ConsoleFormatOptions::Wrapping::kSmart;

  // Verbosity.
  if (cmd.HasSwitch(kForceAllTypes))
    options->verbosity = ConsoleFormatOptions::Verbosity::kAllTypes;
  else if (cmd.HasSwitch(kVerboseFormat))
    options->verbosity = ConsoleFormatOptions::Verbosity::kMedium;
  else
    options->verbosity = ConsoleFormatOptions::Verbosity::kMinimal;

  // Array size.
  if (cmd.HasSwitch(kMaxArraySize)) {
    int size = 0;
    Err err = StringToInt(cmd.GetSwitchValue(kMaxArraySize), &size);
    if (err.has_error())
      return err;
    options->max_array_size = static_cast<uint32_t>(size);
  }

  // Mapping from command-line parameter to format enum.
  constexpr size_t kFormatCount = 4;
  static constexpr std::pair<int, ConsoleFormatOptions::NumFormat> kFormats[kFormatCount] = {
      {kForceNumberChar, ConsoleFormatOptions::NumFormat::kChar},
      {kForceNumberUnsigned, ConsoleFormatOptions::NumFormat::kUnsigned},
      {kForceNumberSigned, ConsoleFormatOptions::NumFormat::kSigned},
      {kForceNumberHex, ConsoleFormatOptions::NumFormat::kHex}};

  int num_type_overrides = 0;
  for (const auto& cur : kFormats) {
    if (cmd.HasSwitch(cur.first)) {
      num_type_overrides++;
      options->num_format = cur.second;
    }
  }

  // Disable pretty-printing.
  if (cmd.HasSwitch(kRawOutput))
    options->enable_pretty_printing = false;

  if (num_type_overrides > 1)
    return Err("More than one type override (-c, -d, -u, -x) specified.");
  return Err();
}

#define FORMAT_VALUE_SWITCHES                                                 \
  "  --max-array=<number>\n"                                                  \
  "      Specifies the maximum array size to print. By default this is\n"     \
  "      256. Specifying large values will slow things down and make the\n"   \
  "      output harder to read, but the default is sometimes insufficient.\n" \
  "      This also applies to strings.\n"                                     \
  "\n"                                                                        \
  "  -r\n"                                                                    \
  "  --raw\n"                                                                 \
  "      Bypass pretty-printers and show the raw type information.\n"         \
  "\n"                                                                        \
  "  -t\n"                                                                    \
  "  --types\n"                                                               \
  "      Force type printing on. The type of every value printed will be\n"   \
  "      explicitly shown. Implies -v.\n"                                     \
  "\n"                                                                        \
  "  -v\n"                                                                    \
  "  --verbose\n"                                                             \
  "      Don't elide type names. Show reference addresses and pointer\n"      \
  "      types.\n"                                                            \
  "\n"                                                                        \
  "Number formatting options\n"                                               \
  "\n"                                                                        \
  "  Force numeric values to be of specific types with these options:\n"      \
  "\n"                                                                        \
  "  -c  Character\n"                                                         \
  "  -d  Signed decimal\n"                                                    \
  "  -u  Unsigned decimal\n"                                                  \
  "  -x  Unsigned hexadecimal\n"

// backtrace ---------------------------------------------------------------------------------------

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
Err DoBacktrace(ConsoleContext* context, const Command& cmd) {
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

// continue ----------------------------------------------------------------------------------------

const char kContinueShortHelp[] = "continue / c: Continue a suspended thread or process.";
const char kContinueHelp[] =
    R"(continue / c

  When a thread is stopped at an exception or a breakpoint, "continue" will
  continue execution.

  See "pause" to stop a running thread or process.

  The behavior will depend upon the context specified.

  - By itself, "continue" will continue all threads of all processes that are
    currently stopped.

  - When a process is specified ("process 2 continue" for an explicit process
    or "process continue" for the current process), only the threads in that
    process will be continued. Other debugged processes currently stopped will
    remain so.

  - When a thread is specified ("thread 1 continue" for an explicit thread
    or "thread continue" for the current thread), only that thread will be
    continued. Other threads in that process and other processes currently
    stopped will remain so.

  TODO(brettw) it might be nice to have a --other flag that would continue
  all threads other than the specified one (which the user might want to step
  while everything else is going).

Examples

  c
  continue
      Continue all processes and threads.

  pr c
  process continue
  process 4 continue
      Continue all threads of a process (the current process is implicit if
      no process index is specified).

  t c
  thread continue
  pr 2 t 4 c
  process 2 thread 4 continue
      Continue only one thread (the current process and thread are implicit
      if no index is specified).
)";
Err DoContinue(ConsoleContext* context, const Command& cmd) {
  Err err = cmd.ValidateNouns({Noun::kProcess, Noun::kThread});
  if (err.has_error())
    return err;

  if (cmd.HasNoun(Noun::kThread)) {
    cmd.thread()->Continue();
  } else if (cmd.HasNoun(Noun::kProcess)) {
    Process* process = cmd.target()->GetProcess();
    if (!process)
      return Err("Process not running, can't continue.");
    process->Continue();
  } else {
    if (!VerifySystemHasRunningProcess(&context->session()->system(), &err))
      return err;
    context->session()->system().Continue();
  }

  return Err();
}

// down --------------------------------------------------------------------------------------------

const char kDownShortHelp[] = "down: Move down the stack";
const char kDownHelp[] =
    R"(down

  Switch the active frame to the one below (forward in time from) the current.

Examples

  down
      Move one frame down the stack

  t 1 down
      Move down the stack on thread 1
)";

// Shows the given frame for when it changes. This encapsulates the formatting options.
void OutputFrameInfoForChange(const Frame* frame, int id) {
  FormatFrameOptions opts;
  opts.loc.func.name.elide_templates = true;
  opts.loc.func.name.bold_last = true;
  opts.loc.func.params = FormatFunctionNameOptions::kElideParams;

  opts.variable.verbosity = ConsoleFormatOptions::Verbosity::kMinimal;
  opts.variable.pointer_expand_depth = 1;
  opts.variable.max_depth = 4;

  Console::get()->Output(FormatFrame(frame, opts, id));
}

Err DoDown(ConsoleContext* context, const Command& cmd) {
  if (Err err = AssertStoppedThreadCommand(context, cmd, true, "down"); err.has_error())
    return err;

  auto id = context->GetActiveFrameIdForThread(cmd.thread());
  if (id < 0)
    return Err("Cannot find current frame.");

  if (id == 0)
    return Err("At bottom of stack.");

  if (cmd.thread()->GetStack().size() == 0)
    return Err("No stack frames.");

  id -= 1;

  context->SetActiveFrameIdForThread(cmd.thread(), id);
  OutputFrameInfoForChange(cmd.thread()->GetStack()[id], id);
  return Err();
}

// up ----------------------------------------------------------------------------------------------

const char kUpShortHelp[] = "up: Move up the stack";
const char kUpHelp[] =
    R"(up

  Switch the active frame to the one above (backward in time from) the current.

Examples

  up
      Move one frame up the stack

  t 1 up
      Move up the stack on thread 1
)";
Err DoUp(ConsoleContext* context, const Command& cmd) {
  if (Err err = AssertStoppedThreadCommand(context, cmd, true, "up"); err.has_error())
    return err;

  // This computes the frame index from the callback in case the user does "up" faster than an
  // async stack request can complete. Doing the new index computation from the callback ensures
  // that all commands are executed.
  auto on_has_frames = [weak_thread = cmd.thread()->GetWeakPtr()](const Err& err) {
    if (!weak_thread) {
      Console::get()->Output(Err("Thread destroyed."));
      return;
    }
    const Thread* thread = weak_thread.get();

    ConsoleContext* context = &Console::get()->context();
    auto id = context->GetActiveFrameIdForThread(thread);
    if (id < 0 || thread->GetStack().size() == 0) {
      Console::get()->Output(Err("No current frame."));
      return;
    }

    id += 1;

    if (static_cast<size_t>(id) >= thread->GetStack().size()) {
      Console::get()->Output(Err("At top of stack."));
      return;
    }

    context->SetActiveFrameIdForThread(thread, id);
    OutputFrameInfoForChange(thread->GetStack()[id], id);
  };

  if (cmd.thread()->GetStack().has_all_frames()) {
    on_has_frames(Err());
  } else {
    cmd.thread()->GetStack().SyncFrames(std::move(on_has_frames));
  }

  return Err();
}

// finish ------------------------------------------------------------------------------------------

const char kFinishShortHelp[] = "finish / fi: Finish execution of a stack frame.";
const char kFinishHelp[] =
    R"(finish / fi

  Alias: "fi"

  Resume thread execution until the selected stack frame returns. This means
  that the current function call will execute normally until it finished.

  See also "until".

Examples

  fi
  finish
      Exit the currently selected stack frame (see "frame").

  pr 1 t 4 fi
  process 1 thead 4 finish
      Applies "finish" to process 1, thread 4.

  f 2 fi
  frame 2 finish
      Exit frame 2, leaving program execution in what was frame 3. Try also
      "frame 3 until" which will do the same thing when the function is not
      recursive.
)";
Err DoFinish(ConsoleContext* context, const Command& cmd) {
  Err err = AssertStoppedThreadWithFrameCommand(context, cmd, "finish");
  if (err.has_error())
    return err;

  Stack& stack = cmd.thread()->GetStack();
  size_t frame_index;
  if (auto found_frame_index = stack.IndexForFrame(cmd.frame()))
    frame_index = *found_frame_index;
  else
    return Err("Internal error, frame not found in current thread.");

  auto controller = std::make_unique<FinishThreadController>(stack, frame_index);
  cmd.thread()->ContinueWith(std::move(controller), [](const Err& err) {
    if (err.has_error())
      Console::get()->Output(err);
  });
  return Err();
}

// jump --------------------------------------------------------------------------------------------

const char kJumpShortHelp[] = "jump / jmp: Set the instruction pointer to a different address.";
const char kJumpHelp[] =
    R"(jump <location>

  Alias: "jmp"

  Sets the instruction pointer of the thread to the given address. It does not
  continue execution. You can "step" or "continue" from the new location.

  You are responsible for what this means semantically since one can't
  generally change the instruction flow and expect things to work.

Location arguments

)" LOCATION_ARG_HELP("jump");
Err DoJump(ConsoleContext* context, const Command& cmd) {
  Err err = AssertStoppedThreadCommand(context, cmd, true, "jump");
  if (err.has_error())
    return err;

  if (cmd.args().size() != 1)
    return Err("The 'jump' command requires one argument for the location.");

  Location location;
  err = ResolveUniqueInputLocation(cmd.target()->GetProcess()->GetSymbols(), cmd.frame(),
                                   cmd.args()[0], true, &location);
  if (err.has_error())
    return err;

  cmd.thread()->JumpTo(location.address(), [thread = cmd.thread()->GetWeakPtr()](const Err& err) {
    Console* console = Console::get();
    if (err.has_error()) {
      console->Output(err);
    } else if (thread) {
      // Reset the current stack frame to the top to reflect the location the user has just jumped
      // to.
      console->context().SetActiveFrameIdForThread(thread.get(), 0);

      // Tell the user where they are.
      console->context().OutputThreadContext(thread.get(), debug_ipc::ExceptionType::kNone, {});
    }
  });

  return Err();
}

// locals ------------------------------------------------------------------------------------------

const char kLocalsShortHelp[] = "locals: Print local variables and function args.";
const char kLocalsHelp[] =
    R"(locals

  Prints all local variables and the current function's arguments. By default
  it will print the variables for the currently selected stack frame.

  You can override the stack frame with the "frame" noun to get the locals
  for any specific stack frame of thread.

Arguments

)" FORMAT_VALUE_SWITCHES
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
Err DoLocals(ConsoleContext* context, const Command& cmd) {
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

  ConsoleFormatOptions options;
  if (Err err = GetConsoleFormatOptions(cmd, &options); err.has_error())
    return err;

  auto output = fxl::MakeRefCounted<AsyncOutputBuffer>();
  for (const auto& pair : vars) {
    output->Append(
        FormatVariableForConsole(pair.second.get(), options, cmd.frame()->GetEvalContext()));
    output->Append("\n");
  }
  output->Complete();
  Console::get()->Output(std::move(output));
  return Err();
}

// next --------------------------------------------------------------------------------------------

const char kNextShortHelp[] = "next / n: Single-step over one source line.";
const char kNextHelp[] =
    R"(next / n

  When a thread is stopped, "next" will execute one source line, stepping over
  subroutine call instructions, and stop the thread again. If the thread is
  running it will issue an error.

  By default, "next" will operate on the current thread. If a thread context
  is given, the specified thread will be single-stepped. You can't single-step
  a process.

  See also "step" to step into subroutine calls or "nexti" to step machine
  instructions.

Examples

  n
  next
      Step the current thread.

  t 2 n
  thread 2 next
      Steps thread 2 in the current process.

  pr 3 n
  process 3 next
      Steps the current thread in process 3 (regardless of which process is
      the current process).

  pr 3 t 2 n
  process 3 thread 2 next
      Steps thread 2 in process 3.
)";
Err DoNext(ConsoleContext* context, const Command& cmd) {
  Err err = AssertStoppedThreadCommand(context, cmd, true, "next");
  if (err.has_error())
    return err;

  auto controller = std::make_unique<StepOverThreadController>(StepMode::kSourceLine);
  cmd.thread()->ContinueWith(std::move(controller), [](const Err& err) {
    if (err.has_error())
      Console::get()->Output(err);
  });
  return Err();
}

// nexti -----------------------------------------------------------------------

const char kNextiShortHelp[] = "nexti / ni: Single-step over one machine instruction.";
const char kNextiHelp[] =
    R"(nexti / ni

  When a thread is stopped, "nexti" will execute one machine instruction,
  stepping over subroutine call instructions, and stop the thread again.
  If the thread is running it will issue an error.

  Only machine call instructions ("call" on x86 and "bl" on ARM) will be
  stepped over with this command. This is not the only way to do a subroutine
  call, as code can manually set up a call frame and jump. These jumps will not
  count as a call and this command will step into the resulting frame.

  By default, "nexti" will operate on the current thread. If a thread context
  is given, the specified thread will be single-stepped. You can't single-step
  a process.

  See also "stepi" to step into subroutine calls.

Examples

  ni
  nexti
      Step the current thread.

  t 2 ni
  thread 2 nexti
      Steps thread 2 in the current process.

  pr 3 ni
  process 3 nexti
      Steps the current thread in process 3 (regardless of which process is
      the current process).

  pr 3 t 2 ni
  process 3 thread 2 nexti
      Steps thread 2 in process 3.
)";
Err DoNexti(ConsoleContext* context, const Command& cmd) {
  Err err = AssertStoppedThreadCommand(context, cmd, true, "nexti");
  if (err.has_error())
    return err;

  auto controller = std::make_unique<StepOverThreadController>(StepMode::kInstruction);
  cmd.thread()->ContinueWith(std::move(controller), [](const Err& err) {
    if (err.has_error())
      Console::get()->Output(err);
  });
  return Err();
}

// pause -------------------------------------------------------------------------------------------

const char kPauseShortHelp[] = "pause / pa: Pause a thread or process.";
const char kPauseHelp[] =
    R"(pause / pa

  When a thread or process is running, "pause" will stop execution so state
  can be inspected or the thread single-stepped.

  See "continue" to resume a paused thread or process.

  The behavior will depend upon the context specified.

  - By itself, "pause" will pause all threads of all processes that are
    currently running.

  - When a process is specified ("process 2 pause" for an explicit process
    or "process pause" for the current process), only the threads in that
    process will be paused. Other debugged processes currently running will
    remain so.

  - When a thread is specified ("thread 1 pause" for an explicit thread
    or "thread pause" for the current thread), only that thread will be
    paused. Other threads in that process and other processes currently
    running will remain so.

  TODO(brettw) it might be nice to have a --other flag that would pause
  all threads other than the specified one.

Examples

  pa
  pause
      Pause all processes and threads.

  pr pa
  process pause
  process 4 pause
      Pause all threads of a process (the current process is implicit if
      no process index is specified).

  t pa
  thread pause
  pr 2 t 4 pa
  process 2 thread 4 pause
      Pause only one thread (the current process and thread are implicit
      if no index is specified).
)";

Err PauseThread(ConsoleContext* context, Thread* thread) {
  // Only save the thread (for printing source info) if it's the current thread.
  Target* target = thread->GetProcess()->GetTarget();
  bool show_source =
      context->GetActiveTarget() == target && context->GetActiveThreadForTarget(target) == thread;

  thread->Pause([weak_thread = thread->GetWeakPtr(), show_source]() {
    if (!weak_thread)
      return;

    Console* console = Console::get();
    if (show_source) {
      // Output the full source location.
      console->context().OutputThreadContext(weak_thread.get(), debug_ipc::ExceptionType::kNone,
                                             {});

    } else {
      // Not current, just output the one-line description.
      console->Output("Paused " + DescribeThread(&console->context(), weak_thread.get()));
    }
  });

  return Err();
}

// Source information on this thread will be printed out on completion. The current thread may be
// null.
Err PauseTarget(ConsoleContext* context, Target* target, Thread* current_thread) {
  Process* process = target->GetProcess();
  if (!process)
    return Err("Process not running, can't pause.");

  // Only save the thread (for printing source info) if it's the current thread.
  fxl::WeakPtr<Thread> weak_thread;
  if (current_thread && context->GetActiveTarget() == target &&
      context->GetActiveThreadForTarget(target) == current_thread)
    weak_thread = current_thread->GetWeakPtr();

  process->Pause([weak_process = process->GetWeakPtr(), weak_thread]() {
    if (!weak_process)
      return;
    Console* console = Console::get();
    OutputBuffer out("Paused");
    out.Append(FormatTarget(&console->context(), weak_process->GetTarget()));
    console->Output(out);

    if (weak_thread) {
      // Thread is current, show current location.
      console->context().OutputThreadContext(weak_thread.get(), debug_ipc::ExceptionType::kNone,
                                             {});
    }
  });
  return Err();
}

// Source information on this thread will be printed out on completion. The current thread may be
// null.
Err PauseSystem(System* system, Thread* current_thread) {
  Err err;
  if (!VerifySystemHasRunningProcess(system, &err))
    return err;

  fxl::WeakPtr<Thread> weak_thread;
  if (current_thread)
    weak_thread = current_thread->GetWeakPtr();

  system->Pause([weak_system = system->GetWeakPtr(), weak_thread]() {
    // Provide messaging about the system pause.
    if (!weak_system)
      return;
    OutputBuffer out;
    Console* console = Console::get();

    // Collect the status of all running processes.
    int paused_process_count = 0;
    for (const Target* target : weak_system->GetTargets()) {
      if (const Process* process = target->GetProcess()) {
        paused_process_count++;
        out.Append(" " + GetBullet() + " ");
        out.Append(FormatTarget(&console->context(), target));
        out.Append("\n");
      }
    }
    // Skip the process list if there's only one and we're showing the thread info below. Otherwise
    // the one thing paused is duplicated twice and this is the most common case.
    if (paused_process_count > 1 || !weak_thread) {
      console->Output("Paused:\n");
      console->Output(out);
      console->Output("\n");
    }

    // Follow with the source context of the current thread if there is one.
    if (weak_thread) {
      console->context().OutputThreadContext(weak_thread.get(), debug_ipc::ExceptionType::kNone,
                                             {});
    }
  });
  return Err();
}

Err DoPause(ConsoleContext* context, const Command& cmd) {
  Err err = cmd.ValidateNouns({Noun::kProcess, Noun::kThread});
  if (err.has_error())
    return err;

  if (cmd.HasNoun(Noun::kThread))
    return PauseThread(context, cmd.thread());
  if (cmd.HasNoun(Noun::kProcess))
    return PauseTarget(context, cmd.target(), cmd.thread());
  return PauseSystem(&context->session()->system(), cmd.thread());
}

// print -------------------------------------------------------------------------------------------

const char kPrintShortHelp[] = "print / p: Print a variable or expression.";
const char kPrintHelp[] =
    R"(print <expression>

  Alias: p

  Evaluates a simple expression or variable name and prints the result.

  The expression is evaluated by default in the currently selected thread and
  stack frame. You can override this with "frame <x> print ...".

  👉 See "help expressions" for how to write expressions.

Arguments

)" FORMAT_VALUE_SWITCHES
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
Err DoPrint(ConsoleContext* context, const Command& cmd) {
  // This will work in any context, but the data that's available will vary depending on whether
  // there's a stopped thread, a process, or nothing.
  fxl::RefPtr<EvalContext> eval_context = GetEvalContextForCommand(cmd);

  ConsoleFormatOptions options;
  Err err = GetConsoleFormatOptions(cmd, &options);
  if (err.has_error())
    return err;

  auto data_provider = eval_context->GetDataProvider();
  return EvalCommandExpression(
      cmd, "print", eval_context, false, [options, eval_context](ErrOrValue value) {
        if (value.has_error())
          Console::get()->Output(value.err());
        else
          Console::get()->Output(FormatValueForConsole(value.value(), options, eval_context));
      });
}

// step --------------------------------------------------------------------------------------------

const char kStepShortHelp[] = "step / s: Step one source line, going into subroutines.";
const char kStepHelp[] =
    R"(step [ <function-fragment> ]

  Alias: "s"

  When a thread is stopped, "step" will execute one source line and stop the
  thread again. This will follow execution into subroutines. If the thread is
  running it will issue an error.

  By default, "step" will single-step the current thread. If a thread context
  is given, the specified thread will be stepped. You can't step a process.
  Other threads in the process will be unchanged so will remain running or
  stopped.

  If the thread ends up in a new function, that function's prologue will be
  automatically skipped before the operation complets. An option to control
  whether this happens can be added in the future if desired.

  See also "stepi".

Stepping into specific functions

  If provided, the parameter will specify a specific function call to step
  into.

  The string will be matched against the symbol names of subroutines called
  directly from the current line. Execution will stop if the function name
  contains this fragment, and automatically complete that function call
  otherwise.

Arguments

  --unsymbolized | -u
      Force stepping into functions with no symbols. Normally "step" will
      skip over library calls or thunks with no symbols. This option allows
      one to step into these unsymbolized calls.

Examples

  s
  step
      Step the current thread.

  t 2 s
  thread 2 step
      Steps thread 2 in the current process.

  s Pri
      Steps into a function with the substring "Pri" anywhere in its name. If
      you have a complex line such as:

        Print(GetFoo(), std::string("bar");

      The "s Pri" command will step over the GetFoo() and std::string() calls,
      and leave execution at the beginning of the "Print" subroutine.
)";
Err DoStep(ConsoleContext* context, const Command& cmd) {
  Err err = AssertStoppedThreadCommand(context, cmd, true, "step");
  if (err.has_error())
    return err;

  // All controllers do this on completion.
  auto completion = [](const Err& err) {
    if (err.has_error())
      Console::get()->Output(err);
  };

  if (cmd.args().empty()) {
    // Step for a single line.
    auto controller = std::make_unique<StepIntoThreadController>(StepMode::kSourceLine);
    controller->set_stop_on_no_symbols(cmd.HasSwitch(kStepIntoUnsymbolized));
    cmd.thread()->ContinueWith(std::move(controller), std::move(completion));
  } else if (cmd.args().size() == 1) {
    // Step into a specific named subroutine. This uses the "step over" controller with a special
    // condition.
    if (cmd.HasSwitch(kStepIntoUnsymbolized)) {
      return Err(
          "The --unsymbolized switch is not compatible with a named "
          "subroutine to step\ninto.");
    }
    auto controller = std::make_unique<StepOverThreadController>(StepMode::kSourceLine);
    controller->set_subframe_should_stop_callback(
        [substr = cmd.args()[0]](const Frame* frame) -> bool {
          const Symbol* symbol = frame->GetLocation().symbol().Get();
          if (!symbol)
            return false;  // Unsymbolized location, continue.
          return symbol->GetFullName().find(substr) != std::string::npos;
        });
    cmd.thread()->ContinueWith(std::move(controller), std::move(completion));
  } else {
    return Err("Too many arguments for 'step'.");
  }

  return Err();
}

// stepi -------------------------------------------------------------------------------------------

const char kStepiShortHelp[] = "stepi / si: Single-step a thread one machine instruction.";
const char kStepiHelp[] =
    R"(stepi / si

  When a thread is stopped, "stepi" will execute one machine instruction and
  stop the thread again. If the thread is running it will issue an error.

  By default, "stepi" will single-step the current thread. If a thread context
  is given, the specified thread will be single-stepped. You can't single-step
  a process.

  See also "nexti" to step over subroutine calls.

Examples

  si
  stepi
      Step the current thread.

  t 2 si
  thread 2 stepi
      Steps thread 2 in the current process.

  pr 3 si
  process 3 stepi
      Steps the current thread in process 3 (regardless of which process is
      the current process).

  pr 3 t 2 si
  process 3 thread 2 stepi
      Steps thread 2 in process 3.
)";
Err DoStepi(ConsoleContext* context, const Command& cmd) {
  Err err = AssertStoppedThreadCommand(context, cmd, true, "stepi");
  if (err.has_error())
    return err;

  cmd.thread()->StepInstruction();
  return Err();
}

// regs --------------------------------------------------------------------------------------------

using debug_ipc::RegisterCategory;

const char kRegsShortHelp[] = "regs / rg: Show the current registers for a thread.";
const char kRegsHelp[] =
    R"(regs [(--category|-c)=<category>] [(--extended|-e)] [<regexp>]

  Alias: "rg"

  Shows the current registers for a stack frame. The thread must be stopped.
  By default the general purpose registers will be shown, but more can be
  configures through switches.

  When the frame is not the topmost stack frame, the regsiters shown will be
  only those saved on the stack. The values will reflect the value of the
  registers at the time that stack frame was active. To get the current CPU
  registers, run "regs" on frame 0.

Category selection arguments

  -a
  --all
      Prints all register categories.

  -g
  --general  (default)
      Prints the general CPU registers.

  -f
  --float
      Prints the dedicated floating-point registers but most users will want
      --vector instead. 64-bit ARM uses vector registers for floating
      point and has no separate floating-point registers. Almost all x64 code
      also uses vector registers for floating-point computations.

  -v
  --vector
      Prints the vector registers. These will be displayed in a table according
      to the current "vector-format" setting (use "get vector-format" for
      the current value and options, and "set vector-format <new-value>" to set).

      Note that the vector register table will be displayed with the low values
      on the right side, which is the opposite order that the expression
      evaluator (which treats them as arrays) displays them.

  -d
  --debug
      Prints the debug registers.

  -e
  --extended
      Enables more verbose flag decoding. This will enable more information
      that is not normally useful for everyday debugging. This includes
      information such as the system level flags within the RFLAGS register for
      x86.

Reading and writing individual registers

  The "regs" command only shows full categories of registers. If you want to see
  individual ones or modify them, use the expression system:

    [zxdb] print $rax
    41

    [zxdb] print -x $rbx      # Use -x for hex formatting.
    0x7cc6120190

    [zxdb] print $xmm3
    {0.0, 3.14159}      # Note [0] index is first in contrast to the table view!

    [zxdb] print $xmm3[0]
    3.14159

  The print command can also be used to set register values:

    [zxdb] print $rax = 42
    42

  The "$" may be omitted for registers if there is no collision with program
  variables.

Examples

  regs
  thread 4 regs -v
  process 2 thread 1 regs --all
  frame 2 regs
)";

// Switches
constexpr int kRegsAllSwitch = 1;
constexpr int kRegsGeneralSwitch = 2;
constexpr int kRegsFloatingPointSwitch = 3;
constexpr int kRegsVectorSwitch = 4;
constexpr int kRegsDebugSwitch = 5;
constexpr int kRegsExtendedSwitch = 6;

void OnRegsComplete(const Err& cmd_err, const std::vector<debug_ipc::Register>& registers,
                    const FormatRegisterOptions& options, bool top_stack_frame) {
  Console* console = Console::get();
  if (cmd_err.has_error()) {
    console->Output(cmd_err);
    return;
  }

  if (registers.empty()) {
    if (top_stack_frame) {
      console->Output("No matching registers.");
    } else {
      console->Output("No matching registers saved with this non-topmost stack frame.");
    }
    return;
  }

  // Always output warning first if needed. If the filtering fails it could be because the register
  // wasn't saved.
  if (!top_stack_frame) {
    OutputBuffer warning_out;
    warning_out.Append(Syntax::kWarning, GetExclamation());
    warning_out.Append(" Stack frame is not topmost. Only saved registers will be available.\n");
    console->Output(warning_out);
  }

  OutputBuffer out;
  out.Append(Syntax::kComment,
             "    (Use \"print $registername\" to show a single one, or\n"
             "     \"print $registername = newvalue\" to set.)\n\n");
  out.Append(FormatRegisters(options, registers));

  console->Output(out);
}

// When we request more than one category of registers, this collects all of them and keeps track
// of how many callbacks are remaining.
struct RegisterCollector {
  Err err;  // Most recent error from all callbacks, if any.
  std::vector<debug_ipc::Register> registers;
  int remaining_callbacks = 0;

  // Parameters to OnRegsComplete().
  FormatRegisterOptions options;
  bool top_stack_frame;
};

Err DoRegs(ConsoleContext* context, const Command& cmd) {
  using debug_ipc::RegisterCategory;

  Err err = AssertStoppedThreadWithFrameCommand(context, cmd, "regs");
  if (err.has_error())
    return err;

  FormatRegisterOptions options;
  options.arch = cmd.thread()->session()->arch();

  std::string vec_fmt = cmd.target()->settings().GetString(ClientSettings::Target::kVectorFormat);
  if (auto found = StringToVectorRegisterFormat(vec_fmt))
    options.vector_format = *found;

  if (!cmd.args().empty())
    return Err("\"regs\" takes no arguments. To show an individual register, use \"print\".");

  bool top_stack_frame = (cmd.frame() == cmd.thread()->GetStack()[0]);

  // General purpose are the default. Other categories can only be shown for the top stack frame
  // since they require reading from the current CPU state.
  std::set<RegisterCategory> category_set;
  if (cmd.HasSwitch(kRegsAllSwitch)) {
    category_set.insert(RegisterCategory::kGeneral);
    category_set.insert(RegisterCategory::kFloatingPoint);
    category_set.insert(RegisterCategory::kVector);
    category_set.insert(RegisterCategory::kDebug);
  }
  if (cmd.HasSwitch(kRegsGeneralSwitch))
    category_set.insert(RegisterCategory::kGeneral);
  if (cmd.HasSwitch(kRegsFloatingPointSwitch))
    category_set.insert(RegisterCategory::kFloatingPoint);
  if (cmd.HasSwitch(kRegsVectorSwitch))
    category_set.insert(RegisterCategory::kVector);
  if (cmd.HasSwitch(kRegsDebugSwitch))
    category_set.insert(RegisterCategory::kDebug);

  // Default to "general" if no categories specified.
  if (category_set.empty())
    category_set.insert(RegisterCategory::kGeneral);

  options.extended = cmd.HasSwitch(kRegsExtendedSwitch);

  if (category_set.size() == 1 && *category_set.begin() == RegisterCategory::kGeneral) {
    // Any available general registers should be available synchronously.
    auto* regs = cmd.frame()->GetRegisterCategorySync(debug_ipc::RegisterCategory::kGeneral);
    FXL_DCHECK(regs);
    OnRegsComplete(Err(), *regs, options, top_stack_frame);
  } else {
    auto collector = std::make_shared<RegisterCollector>();
    collector->remaining_callbacks = static_cast<int>(category_set.size());
    collector->options = std::move(options);
    collector->top_stack_frame = top_stack_frame;

    for (auto category : category_set) {
      cmd.frame()->GetRegisterCategoryAsync(
          category, [collector](const Err& err, const std::vector<debug_ipc::Register>& new_regs) {
            // Save the new registers.
            collector->registers.insert(collector->registers.end(), new_regs.begin(),
                                        new_regs.end());

            // Save the error. Just keep the most recent error if there are multiple.
            if (err.has_error())
              collector->err = err;

            FXL_DCHECK(collector->remaining_callbacks > 0);
            collector->remaining_callbacks--;
            if (collector->remaining_callbacks == 0) {
              OnRegsComplete(collector->err, collector->registers, collector->options,
                             collector->top_stack_frame);
            }
          });
    }
  }

  return Err();
}

// until -------------------------------------------------------------------------------------------

const char kUntilShortHelp[] = "until / u: Runs a thread until a location is reached.";
const char kUntilHelp[] =
    R"(until <location>

  Alias: "u"

  Continues execution of a thread or a process until a given location is
  reached. You could think of this command as setting an implicit one-shot
  breakpoint at the given location and continuing execution.

  Normally this operation will apply only to the current thread. To apply to
  all threads in a process, use "process until" (see the examples below).

  See also "finish".

Location arguments

  Current frame's address (no input)
    until

)" LOCATION_ARG_HELP("until")
        R"(
Examples

  u
  until
      Runs until the current frame's location is hit again. This can be useful
      if the current code is called in a loop to advance to the next iteration
      of the current code.

  f 1 u
  frame 1 until
      Runs until the given frame's location is hit. Since frame 1 is
      always the current function's calling frame, this command will normally
      stop when the current function returns. The exception is if the code
      in the calling function is called recursively from the current location,
      in which case the next invocation will stop ("until" does not match
      stack frames on break). See "finish" for a stack-aware version.

  u 24
  until 24
      Runs the current thread until line 24 of the current frame's file.

  until foo.cc:24
      Runs the current thread until the given file/line is reached.

  thread 2 until 24
  process 1 thread 2 until 24
      Runs the specified thread until line 24 is reached. When no filename is
      given, the specified thread's currently selected frame will be used.

  u MyClass::MyFunc
  until MyClass::MyFunc
      Runs the current thread until the given function is called.

  pr u MyClass::MyFunc
  process until MyClass::MyFunc
      Continues all threads of the current process, stopping the next time any
      of them call the function.
)";
Err DoUntil(ConsoleContext* context, const Command& cmd) {
  Err err;

  // Decode the location.
  //
  // The validation on this is a bit tricky. Most uses apply to the current thread and take some
  // implicit information from the current frame (which requires the thread be stopped). But when
  // doing a process-wide one, don't require a currently stopped thread unless it's required to
  // compute the location.
  std::vector<InputLocation> locations;
  if (cmd.args().empty()) {
    // No args means use the current location.
    if (!cmd.frame()) {
      return Err(ErrType::kInput, "There isn't a current frame to take the location from.");
    }
    locations.emplace_back(cmd.frame()->GetAddress());
  } else if (cmd.args().size() == 1) {
    // One arg = normal location (this function can handle null frames).
    Err err = ParseLocalInputLocation(cmd.frame(), cmd.args()[0], &locations);
    if (err.has_error())
      return err;
  } else {
    return Err(ErrType::kInput,
               "Expecting zero or one arg for the location.\n"
               "Formats: <function>, <file>:<line#>, <line#>, or *<address>");
  }

  auto callback = [](const Err& err) {
    if (err.has_error())
      Console::get()->Output(err);
  };

  // Dispatch the request.
  if (cmd.HasNoun(Noun::kProcess) && !cmd.HasNoun(Noun::kThread) && !cmd.HasNoun(Noun::kFrame)) {
    // Process-wide ("process until ...").
    err = AssertRunningTarget(context, "until", cmd.target());
    if (err.has_error())
      return err;
    cmd.target()->GetProcess()->ContinueUntil(locations, callback);
  } else {
    // Thread-specific.
    err = AssertStoppedThreadWithFrameCommand(context, cmd, "until");
    if (err.has_error())
      return err;

    auto controller = std::make_unique<UntilThreadController>(std::move(locations));
    cmd.thread()->ContinueWith(std::move(controller), [](const Err& err) {
      if (err.has_error())
        Console::get()->Output(err);
    });
  }
  return Err();
}

}  // namespace

void AppendThreadVerbs(std::map<Verb, VerbRecord>* verbs) {
  // Shared options for value printing.
  SwitchRecord force_types(kForceAllTypes, false, "types", 't');
  SwitchRecord raw(kRawOutput, false, "raw", 'r');
  const std::vector<SwitchRecord> format_switches{
      force_types,
      raw,
      SwitchRecord(kVerboseFormat, false, "verbose", 'v'),
      SwitchRecord(kForceNumberChar, false, "", 'c'),
      SwitchRecord(kForceNumberSigned, false, "", 'd'),
      SwitchRecord(kForceNumberUnsigned, false, "", 'u'),
      SwitchRecord(kForceNumberHex, false, "", 'x'),
      SwitchRecord(kMaxArraySize, true, "max-array")};

  // backtrace
  VerbRecord backtrace(&DoBacktrace, {"backtrace", "bt"}, kBacktraceShortHelp, kBacktraceHelp,
                       CommandGroup::kQuery);
  backtrace.switches = {force_types, raw, SwitchRecord(kVerboseBacktrace, false, "verbose", 'v')};

  (*verbs)[Verb::kBacktrace] = std::move(backtrace);

  (*verbs)[Verb::kContinue] =
      VerbRecord(&DoContinue, {"continue", "cont", "c"}, kContinueShortHelp, kContinueHelp,
                 CommandGroup::kStep, SourceAffinity::kSource);
  (*verbs)[Verb::kFinish] =
      VerbRecord(&DoFinish, {"finish", "fi"}, kFinishShortHelp, kFinishHelp, CommandGroup::kStep);
  (*verbs)[Verb::kJump] = VerbRecord(&DoJump, &CompleteInputLocation, {"jump", "jmp"},
                                     kJumpShortHelp, kJumpHelp, CommandGroup::kStep);

  // locals
  VerbRecord locals(&DoLocals, {"locals"}, kLocalsShortHelp, kLocalsHelp, CommandGroup::kQuery);
  locals.switches = format_switches;
  (*verbs)[Verb::kLocals] = std::move(locals);

  (*verbs)[Verb::kNext] = VerbRecord(&DoNext, {"next", "n"}, kNextShortHelp, kNextHelp,
                                     CommandGroup::kStep, SourceAffinity::kSource);
  (*verbs)[Verb::kNexti] = VerbRecord(&DoNexti, {"nexti", "ni"}, kNextiShortHelp, kNextiHelp,
                                      CommandGroup::kAssembly, SourceAffinity::kAssembly);
  (*verbs)[Verb::kPause] =
      VerbRecord(&DoPause, {"pause", "pa"}, kPauseShortHelp, kPauseHelp, CommandGroup::kProcess);

  // print
  VerbRecord print(&DoPrint, {"print", "p"}, kPrintShortHelp, kPrintHelp, CommandGroup::kQuery);
  print.switches = format_switches;
  print.param_type = VerbRecord::kOneParam;
  (*verbs)[Verb::kPrint] = std::move(print);

  // regs
  VerbRecord regs(&DoRegs, {"regs", "rg"}, kRegsShortHelp, kRegsHelp, CommandGroup::kAssembly);
  regs.switches.emplace_back(kRegsAllSwitch, false, "all", 'a');
  regs.switches.emplace_back(kRegsGeneralSwitch, false, "general", 'g');
  regs.switches.emplace_back(kRegsFloatingPointSwitch, false, "float", 'f');
  regs.switches.emplace_back(kRegsVectorSwitch, false, "vector", 'v');
  regs.switches.emplace_back(kRegsDebugSwitch, false, "debug", 'd');
  regs.switches.emplace_back(kRegsExtendedSwitch, false, "extended", 'e');
  (*verbs)[Verb::kRegs] = std::move(regs);

  // step
  SwitchRecord step_force(kStepIntoUnsymbolized, false, "unsymbolized", 'u');
  VerbRecord step(&DoStep, {"step", "s"}, kStepShortHelp, kStepHelp, CommandGroup::kStep,
                  SourceAffinity::kSource);
  step.switches.push_back(step_force);
  (*verbs)[Verb::kStep] = std::move(step);

  (*verbs)[Verb::kStepi] = VerbRecord(&DoStepi, {"stepi", "si"}, kStepiShortHelp, kStepiHelp,
                                      CommandGroup::kAssembly, SourceAffinity::kAssembly);
  (*verbs)[Verb::kSteps] = GetStepsVerbRecord();
  (*verbs)[Verb::kUntil] = VerbRecord(&DoUntil, &CompleteInputLocation, {"until", "u"},
                                      kUntilShortHelp, kUntilHelp, CommandGroup::kStep);

  // Stack navigation
  (*verbs)[Verb::kDown] =
      VerbRecord(&DoDown, {"down"}, kDownShortHelp, kDownHelp, CommandGroup::kGeneral);
  (*verbs)[Verb::kUp] = VerbRecord(&DoUp, {"up"}, kUpShortHelp, kUpHelp, CommandGroup::kGeneral);
}

}  // namespace zxdb
