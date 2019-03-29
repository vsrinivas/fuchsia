// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <inttypes.h>

#include "garnet/bin/zxdb/expr/expr.h"
#include "garnet/bin/zxdb/expr/symbol_eval_context.h"
#include "garnet/bin/zxdb/symbols/code_block.h"
#include "garnet/bin/zxdb/symbols/function.h"
#include "garnet/bin/zxdb/symbols/location.h"
#include "garnet/bin/zxdb/symbols/variable.h"
#include "src/lib/fxl/strings/string_printf.h"
#include "src/developer/debug/shared/message_loop.h"
#include "src/developer/debug/zxdb/client/finish_thread_controller.h"
#include "src/developer/debug/zxdb/client/frame.h"
#include "src/developer/debug/zxdb/client/process.h"
#include "src/developer/debug/zxdb/client/register.h"
#include "src/developer/debug/zxdb/client/session.h"
#include "src/developer/debug/zxdb/client/step_over_thread_controller.h"
#include "src/developer/debug/zxdb/client/step_thread_controller.h"
#include "src/developer/debug/zxdb/client/thread.h"
#include "src/developer/debug/zxdb/client/until_thread_controller.h"
#include "src/developer/debug/zxdb/common/err.h"
#include "src/developer/debug/zxdb/console/command.h"
#include "src/developer/debug/zxdb/console/command_utils.h"
#include "src/developer/debug/zxdb/console/console.h"
#include "src/developer/debug/zxdb/console/format_frame.h"
#include "src/developer/debug/zxdb/console/format_register.h"
#include "src/developer/debug/zxdb/console/format_table.h"
#include "src/developer/debug/zxdb/console/format_value.h"
#include "src/developer/debug/zxdb/console/format_value_process_context_impl.h"
#include "src/developer/debug/zxdb/console/input_location_parser.h"
#include "src/developer/debug/zxdb/console/output_buffer.h"
#include "src/developer/debug/zxdb/console/verbs.h"

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

// If the system has at least one running process, returns true. If not,
// returns false and sets the err.
//
// When doing global things like System::Continue(), it will succeed if there
// are no running programs (it will successfully continue all 0 processes).
// This is confusing to the user so this function is used to check first.
bool VerifySystemHasRunningProcess(System* system, Err* err) {
  for (const Target* target : system->GetTargets()) {
    if (target->GetProcess())
      return true;
  }
  *err = Err("No processes are running.");
  return false;
}

// Populates the formatting options with the given command's switches.
Err GetFormatExprValueOptions(const Command& cmd,
                              FormatExprValueOptions* options) {
  // Verbosity.
  if (cmd.HasSwitch(kForceAllTypes))
    options->verbosity = FormatExprValueOptions::Verbosity::kAllTypes;
  else if (cmd.HasSwitch(kVerboseFormat))
    options->verbosity = FormatExprValueOptions::Verbosity::kMedium;
  else
    options->verbosity = FormatExprValueOptions::Verbosity::kMinimal;

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
  static constexpr std::pair<int, FormatExprValueOptions::NumFormat>
      kFormats[kFormatCount] = {
          {kForceNumberChar, FormatExprValueOptions::NumFormat::kChar},
          {kForceNumberUnsigned, FormatExprValueOptions::NumFormat::kUnsigned},
          {kForceNumberSigned, FormatExprValueOptions::NumFormat::kSigned},
          {kForceNumberHex, FormatExprValueOptions::NumFormat::kHex}};

  int num_type_overrides = 0;
  for (const auto& cur : kFormats) {
    if (cmd.HasSwitch(cur.first)) {
      num_type_overrides++;
      options->num_format = cur.second;
    }
  }

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

// backtrace -------------------------------------------------------------------

const char kBacktraceShortHelp[] = "backtrace / bt: Print a backtrace.";
const char kBacktraceHelp[] =
    R"(backtrace / bt

  Prints a backtrace of the selected thread. This is an alias for "frame -v".

  To see less information, use "frame" or just "f".

Arguments

  -t
  --types
      Include all type information for function parameters.

Examples

  t 2 bt
  thread 2 backtrace
)";
Err DoBacktrace(ConsoleContext* context, const Command& cmd) {
  Err err = cmd.ValidateNouns({Noun::kProcess, Noun::kThread});
  if (err.has_error())
    return err;

  if (!cmd.thread())
    return Err("There is no thread to have frames.");

  // TODO(brettw) this should share formatting options and parsing with the
  // printing commands.
  bool show_params = cmd.HasSwitch(kForceAllTypes);
  OutputFrameList(cmd.thread(), show_params, true);
  return Err();
}

// continue --------------------------------------------------------------------

const char kContinueShortHelp[] =
    "continue / c: Continue a suspended thread or process.";
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

// down ------------------------------------------------------------------------

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
Err DoDown(ConsoleContext* context, const Command& cmd) {
  Err err = AssertStoppedThreadCommand(context, cmd, true, "down");
  if (err.has_error())
    return err;

  auto id = context->GetActiveFrameIdForThread(cmd.thread());

  if (id < 0) {
    return Err("Cannot find current frame.");
  }

  if (id == 0) {
    return Err("At bottom of stack.");
  }

  if (cmd.thread()->GetStack().size() == 0) {
    return Err("No stack frames.");
  }

  id -= 1;

  context->SetActiveFrameIdForThread(cmd.thread(), id);
  FormatFrameAsync(context, cmd.target(), cmd.thread(),
                   cmd.thread()->GetStack()[id]);

  return Err();
}

// up --------------------------------------------------------------------------

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
  Err err = AssertStoppedThreadCommand(context, cmd, true, "up");
  if (err.has_error())
    return err;

  auto id = context->GetActiveFrameIdForThread(cmd.thread());

  if (id < 0) {
    return Err("Cannot find current frame.");
  }

  if (cmd.thread()->GetStack().size() == 0) {
    return Err("No stack frames.");
  }

  id += 1;

  auto cb = [context, cmd, id](const Err& err) {
    Err got;

    if (!err.has_error() &&
        static_cast<size_t>(id) >= cmd.thread()->GetStack().size()) {
      got = Err("At top of stack.");
    } else {
      got = err;
    }

    if (got.has_error()) {
      Console::get()->Output(got);
    } else {
      context->SetActiveFrameIdForThread(cmd.thread(), id);
      FormatFrameAsync(context, cmd.target(), cmd.thread(),
                       cmd.thread()->GetStack()[id]);
    }
  };

  if (cmd.thread()->GetStack().has_all_frames()) {
    cb(Err());
  } else {
    cmd.thread()->GetStack().SyncFrames(std::move(cb));
  }

  return Err();
}

// finish ----------------------------------------------------------------------

const char kFinishShortHelp[] =
    "finish / fi: Finish execution of a stack frame.";
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
  // This command allows "frame" which AssertStoppedThreadCommand doesn't,
  // so pass "false" to disabled noun checking and manually check ourselves.
  Err err = AssertStoppedThreadCommand(context, cmd, false, "finish");
  if (err.has_error())
    return err;
  err = cmd.ValidateNouns({Noun::kProcess, Noun::kThread, Noun::kFrame});
  if (err.has_error())
    return err;

  Stack& stack = cmd.thread()->GetStack();
  size_t frame_index;
  if (auto found_frame_index = stack.IndexForFrame(cmd.frame()))
    frame_index = *found_frame_index;
  else
    return Err("Internal error, frame not found in current thread.");

  auto controller =
      std::make_unique<FinishThreadController>(stack, frame_index);
  cmd.thread()->ContinueWith(std::move(controller), [](const Err& err) {
    if (err.has_error())
      Console::get()->Output(err);
  });
  return Err();
}

// jump ------------------------------------------------------------------------

const char kJumpShortHelp[] =
    "jump / jmp: Set the instruction pointer to a different address.";
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
  Err err = AssertStoppedThreadCommand(context, cmd, false, "jump");
  if (err.has_error())
    return err;

  if (cmd.args().size() != 1)
    return Err("The 'jump' command requires one argument for the location.");

  InputLocation input_location;
  err = ParseInputLocation(cmd.frame(), cmd.args()[0], &input_location);
  if (err.has_error())
    return err;

  Location location;
  err = ResolveUniqueInputLocation(cmd.target()->GetProcess()->GetSymbols(),
                                   input_location, true, &location);
  if (err.has_error())
    return err;

  cmd.thread()->JumpTo(
      location.address(),
      [thread = cmd.thread()->GetWeakPtr()](const Err& err) {
        Console* console = Console::get();
        if (err.has_error()) {
          console->Output(err);
        } else if (thread) {
          // Reset the current stack frame to the top to reflect the location
          // the user has just jumped to.
          console->context().SetActiveFrameIdForThread(thread.get(), 0);

          // Tell the user where they are.
          console->context().OutputThreadContext(
              thread.get(), debug_ipc::NotifyException::Type::kNone, {});
        }
      });

  return Err();
}

// locals ----------------------------------------------------------------------

const char kLocalsShortHelp[] =
    "locals: Print local variables and function args.";
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
  // Don't have AssertStoppedThreadCommand check nouns because we additionally
  // allow "frame", which we manually validate below.
  Err err = AssertStoppedThreadCommand(context, cmd, false, "locals");
  if (err.has_error())
    return err;
  err = cmd.ValidateNouns({Noun::kProcess, Noun::kThread, Noun::kFrame});
  if (err.has_error())
    return err;
  if (!cmd.frame())
    return Err("There isn't a current frame to read locals from.");

  const Location& location = cmd.frame()->GetLocation();
  if (!location.symbol())
    return Err("There is no symbol information for the frame.");
  const Function* function = location.symbol().Get()->AsFunction();
  if (!function)
    return Err("Symbols are corrupt.");

  // Find the innermost lexical block for the current IP.
  const CodeBlock* block = function->GetMostSpecificChild(
      location.symbol_context(), location.address());
  if (!block)
    return Err("There is no symbol information for the current IP.");

  // Walk upward in the hierarchy to collect local variables until hitting a
  // function. Using the map allows collecting only the innermost version of
  // a given name, and sorts them as we go.
  std::map<std::string, const Variable*> vars;
  while (block) {
    for (const auto& lazy_var : block->variables()) {
      const Variable* var = lazy_var.Get()->AsVariable();
      if (!var)
        continue;  // Symbols are corrupt.
      const std::string& name = var->GetAssignedName();
      if (vars.find(name) == vars.end())
        vars[name] = var;  // New one.
    }

    if (block == function)
      break;
    block = block->parent().Get()->AsCodeBlock();
  }

  // Add function parameters. Don't overwrite existing names in case of
  // duplicates to duplicate the shadowing rules of the language.
  for (const auto& param : function->parameters()) {
    const Variable* var = param.Get()->AsVariable();
    if (!var)
      continue;  // Symbols are corrupt.
    const std::string& name = var->GetAssignedName();
    if (vars.find(name) == vars.end())
      vars[name] = var;  // New one.
  }

  if (vars.empty()) {
    Console::get()->Output("No local variables in scope.");
    return Err();
  }

  FormatExprValueOptions options;
  err = GetFormatExprValueOptions(cmd, &options);
  if (err.has_error())
    return err;

  auto helper = fxl::MakeRefCounted<FormatValue>(
      std::make_unique<FormatValueProcessContextImpl>(cmd.target()));
  for (const auto& pair : vars) {
    helper->AppendVariable(location.symbol_context(),
                           cmd.frame()->GetSymbolDataProvider(), pair.second,
                           options);
    helper->Append(OutputBuffer("\n"));
  }
  helper->Complete([helper](OutputBuffer out) { Console::get()->Output(out); });
  return Err();
}

// next ------------------------------------------------------------------------

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

  auto controller =
      std::make_unique<StepOverThreadController>(StepMode::kSourceLine);
  cmd.thread()->ContinueWith(std::move(controller), [](const Err& err) {
    if (err.has_error())
      Console::get()->Output(err);
  });
  return Err();
}

// nexti -----------------------------------------------------------------------

const char kNextiShortHelp[] =
    "nexti / ni: Single-step over one machine instruction.";
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

  auto controller =
      std::make_unique<StepOverThreadController>(StepMode::kInstruction);
  cmd.thread()->ContinueWith(std::move(controller), [](const Err& err) {
    if (err.has_error())
      Console::get()->Output(err);
  });
  return Err();
}

// pause -----------------------------------------------------------------------

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
Err DoPause(ConsoleContext* context, const Command& cmd) {
  Err err = cmd.ValidateNouns({Noun::kProcess, Noun::kThread});
  if (err.has_error())
    return err;

  if (cmd.HasNoun(Noun::kThread)) {
    cmd.thread()->Pause();
  } else if (cmd.HasNoun(Noun::kProcess)) {
    Process* process = cmd.target()->GetProcess();
    if (!process)
      return Err("Process not running, can't pause.");
    process->Pause();
  } else {
    if (!VerifySystemHasRunningProcess(&context->session()->system(), &err))
      return err;
    context->session()->system().Pause();
  }

  return Err();
}

// print -----------------------------------------------------------------------

const char kPrintShortHelp[] = "print / p: Print a variable or expression.";
const char kPrintHelp[] =
    R"(print <expression>

  Alias: p

  Evaluates a simple expression or variable name and prints the result.

  The expression is evaluated by default in the currently selected thread and
  stack frame. You can override this with "frame <x> print ...".

Arguments

)" FORMAT_VALUE_SWITCHES
    R"(
Expressions

  The expression evaluator understands the following C/C++ things:

    - Identifiers

    - Struct and class member access: . ->

    - Array access (for native arrays): [ <expression> ]

    - Create or dereference pointers: & *

    - Precedence: ( <expression> )

  Not supported: function calls, overloaded operators, casting.

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
  Err err = AssertStoppedThreadCommand(context, cmd, false, "print");
  if (err.has_error())
    return err;
  err = cmd.ValidateNouns({Noun::kProcess, Noun::kThread, Noun::kFrame});
  if (err.has_error())
    return err;
  if (!cmd.frame())
    return Err("There isn't a current frame for printing context.");

  // This takes one expression that may have spaces, so concatenate everything
  // the command parser has split apart back into one thing.
  //
  // If we run into limitations of this, we should add a "don't parse the args"
  // flag to the command record.
  std::string expr;
  for (const auto& cur : cmd.args()) {
    if (!expr.empty())
      expr.push_back(' ');
    expr += cur;
  }

  if (expr.empty())
    return Err("Usage: print <expression>\nSee \"help print\" for more.");

  FormatExprValueOptions options;
  err = GetFormatExprValueOptions(cmd, &options);
  if (err.has_error())
    return err;

  auto data_provider = cmd.frame()->GetSymbolDataProvider();
  auto formatter = fxl::MakeRefCounted<FormatValue>(
      std::make_unique<FormatValueProcessContextImpl>(cmd.target()));

  EvalExpression(
      expr, cmd.frame()->GetExprEvalContext(),
      [formatter, options, data_provider](const Err& err, ExprValue value) {
        if (err.has_error()) {
          Console::get()->Output(err);
        } else {
          formatter->AppendValue(data_provider, value, options);
          // Bind the formatter to keep it in scope across this
          // async call.
          formatter->Complete(
              [formatter](OutputBuffer out) { Console::get()->Output(out); });
        }
      });

  return Err();
}

// step ------------------------------------------------------------------------

const char kStepShortHelp[] =
    "step / s: Step one source line, going into subroutines.";
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
    // Step over a single line.
    auto controller =
        std::make_unique<StepThreadController>(StepMode::kSourceLine);
    controller->set_stop_on_no_symbols(cmd.HasSwitch(kStepIntoUnsymbolized));
    cmd.thread()->ContinueWith(std::move(controller), std::move(completion));
  } else if (cmd.args().size() == 1) {
    // Step into a specific named subroutine. This uses the "step over"
    // controller with a special condition.
    if (cmd.HasSwitch(kStepIntoUnsymbolized)) {
      return Err(
          "The --unsymbolized switch is not compatible with a named "
          "subroutine to step\ninto.");
    }
    auto controller =
        std::make_unique<StepOverThreadController>(StepMode::kSourceLine);
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

// stepi -----------------------------------------------------------------------

const char kStepiShortHelp[] =
    "stepi / si: Single-step a thread one machine instruction.";
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

// regs ------------------------------------------------------------------------

using debug_ipc::RegisterCategory;

const char kRegsShortHelp[] =
    "regs / rg: Show the current registers for a thread.";
const char kRegsHelp[] =
    R"(regs [(--category|-c)=<category>] [(--extended|-e)] [<regexp>]

  Alias: "rg"

  Shows the current registers for a thread. The thread must be stopped.
  By default the general purpose registers will be shown, but more can be
  configures through switches.

  NOTE: The values are displayed in the endianess of the target architecture.
        The interpretation of which bits are the MSB will vary across different
        endianess.

Arguments

  --category=<category> | -c <category>
      Which categories if registers to show.
      The following options can be set:

      - general: Show general purpose registers.
      - fp: Show floating point registers.
      - vector: Show vector registers.
      - debug: Show debug registers (eg. The DR registers on x86).
      - all: Show all the categories available.

      NOTE: not all categories exist within all architectures. For example,
            ARM64's fp category doesn't have any registers.

  --extended | -e
      Enables more verbose flag decoding. This will enable more information
      that is not normally useful for everyday debugging. This includes
      information such as the system level flags within the RFLAGS register for
      x86.

  <regexp>
      Case insensitive regular expression. Any register that matches will be
      shown. Uses POSIX Extended Regular Expression syntax. If not specified, it
      will match all registers.

Examples

  regs
  thread 4 regs --category=vector
  process 2 thread 1 regs -c all v*
)";

// Switches
constexpr int kRegsCategoriesSwitch = 1;
constexpr int kRegsExtendedSwitch = 2;

void OnRegsComplete(const Err& cmd_err, const RegisterSet& register_set,
                    FormatRegisterOptions options) {
  Console* console = Console::get();
  if (cmd_err.has_error()) {
    console->Output(cmd_err);
    return;
  }

  options.arch = register_set.arch();

  FilteredRegisterSet filtered_set;
  Err err = FilterRegisters(options, register_set, &filtered_set);
  if (!err.ok()) {
    console->Output(err);
    return;
  }

  OutputBuffer out;
  err = FormatRegisters(options, filtered_set, &out);
  if (!err.ok()) {
    console->Output(err);
    return;
  }
  console->Output(out);
}

Err DoRegs(ConsoleContext* context, const Command& cmd) {
  Err err = AssertStoppedThreadCommand(context, cmd, true, "regs");
  if (err.has_error())
    return err;

  // When empty, we print all the registers.
  std::string regex_filter;
  if (!cmd.args().empty()) {
    // We expect only one name.
    if (cmd.args().size() > 1u) {
      return Err("Only one register regular expression filter expected.");
    }
    regex_filter = cmd.args().front();
  }

  // General purpose are the default.
  std::vector<RegisterCategory::Type> cats_to_show = {
      RegisterCategory::Type::kGeneral};
  if (cmd.HasSwitch(kRegsCategoriesSwitch)) {
    auto option = cmd.GetSwitchValue(kRegsCategoriesSwitch);
    if (option == "all") {
      cats_to_show = {
          debug_ipc::RegisterCategory::Type::kGeneral,
          debug_ipc::RegisterCategory::Type::kFP,
          debug_ipc::RegisterCategory::Type::kVector,
          debug_ipc::RegisterCategory::Type::kDebug,
      };
    } else if (option == "general") {
      cats_to_show = {RegisterCategory::Type::kGeneral};
    } else if (option == "fp") {
      cats_to_show = {RegisterCategory::Type::kFP};
    } else if (option == "vector") {
      cats_to_show = {RegisterCategory::Type::kVector};
    } else if (option == "debug") {
      cats_to_show = {RegisterCategory::Type::kDebug};
    } else {
      return Err(fxl::StringPrintf("Unknown category: %s", option.c_str()));
    }
  }

  // Parse the switches
  FormatRegisterOptions options;
  options.categories = cats_to_show;
  options.extended = cmd.HasSwitch(kRegsExtendedSwitch);
  options.filter_regexp = std::move(regex_filter);

  // We pass the given register name to the callback
  auto regs_cb = [options = std::move(options)](const Err& err,
                                                const RegisterSet& registers) {
    OnRegsComplete(err, registers, std::move(options));
  };

  cmd.thread()->ReadRegisters(std::move(cats_to_show), regs_cb);
  return Err();
}

// until -----------------------------------------------------------------------

const char kUntilShortHelp[] =
    "until / u: Runs a thread until a location is reached.";
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
  // The validation on this is a bit tricky. Most uses apply to the current
  // thread and take some implicit information from the current frame (which
  // requires the thread be stopped). But when doing a process-wide one, don't
  // require a currently stopped thread unless it's required to compute the
  // location.
  InputLocation location;
  if (cmd.args().empty()) {
    // No args means use the current location.
    if (!cmd.frame()) {
      return Err(ErrType::kInput,
                 "There isn't a current frame to take the location from.");
    }
    location = InputLocation(cmd.frame()->GetAddress());
  } else if (cmd.args().size() == 1) {
    // One arg = normal location (ParseInputLocation can handle null frames).
    Err err = ParseInputLocation(cmd.frame(), cmd.args()[0], &location);
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
  if (cmd.HasNoun(Noun::kProcess) && !cmd.HasNoun(Noun::kThread) &&
      !cmd.HasNoun(Noun::kFrame)) {
    // Process-wide ("process until ...").
    err = AssertRunningTarget(context, "until", cmd.target());
    if (err.has_error())
      return err;
    cmd.target()->GetProcess()->ContinueUntil(location, callback);
  } else {
    // Thread-specific.
    err = cmd.ValidateNouns({Noun::kProcess, Noun::kThread, Noun::kFrame});
    if (err.has_error())
      return err;

    err = AssertStoppedThreadCommand(context, cmd, false, "until");
    if (err.has_error())
      return err;

    auto controller = std::make_unique<UntilThreadController>(location);
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
  const std::vector<SwitchRecord> format_switches{
      force_types,
      SwitchRecord(kVerboseFormat, false, "verbose", 'v'),
      SwitchRecord(kForceNumberChar, false, "", 'c'),
      SwitchRecord(kForceNumberSigned, false, "", 'd'),
      SwitchRecord(kForceNumberUnsigned, false, "", 'u'),
      SwitchRecord(kForceNumberHex, false, "", 'x'),
      SwitchRecord(kMaxArraySize, true, "max-array")};

  VerbRecord backtrace(&DoBacktrace, {"backtrace", "bt"}, kBacktraceShortHelp,
                       kBacktraceHelp, CommandGroup::kQuery);
  backtrace.switches = {force_types};
  (*verbs)[Verb::kBacktrace] = std::move(backtrace);

  (*verbs)[Verb::kContinue] =
      VerbRecord(&DoContinue, {"continue", "cont", "c"}, kContinueShortHelp,
                 kContinueHelp, CommandGroup::kStep, SourceAffinity::kSource);
  (*verbs)[Verb::kFinish] =
      VerbRecord(&DoFinish, {"finish", "fi"}, kFinishShortHelp, kFinishHelp,
                 CommandGroup::kStep);
  (*verbs)[Verb::kJump] = VerbRecord(&DoJump, {"jump", "jmp"}, kJumpShortHelp,
                                     kJumpHelp, CommandGroup::kStep);

  // locals
  VerbRecord locals(&DoLocals, {"locals"}, kLocalsShortHelp, kLocalsHelp,
                    CommandGroup::kQuery);
  locals.switches = format_switches;
  (*verbs)[Verb::kLocals] = std::move(locals);

  (*verbs)[Verb::kNext] =
      VerbRecord(&DoNext, {"next", "n"}, kNextShortHelp, kNextHelp,
                 CommandGroup::kStep, SourceAffinity::kSource);
  (*verbs)[Verb::kNexti] =
      VerbRecord(&DoNexti, {"nexti", "ni"}, kNextiShortHelp, kNextiHelp,
                 CommandGroup::kAssembly, SourceAffinity::kAssembly);
  (*verbs)[Verb::kPause] =
      VerbRecord(&DoPause, {"pause", "pa"}, kPauseShortHelp, kPauseHelp,
                 CommandGroup::kProcess);

  // print
  VerbRecord print(&DoPrint, {"print", "p"}, kPrintShortHelp, kPrintHelp,
                   CommandGroup::kQuery);
  print.switches = format_switches;
  (*verbs)[Verb::kPrint] = std::move(print);

  // regs
  SwitchRecord regs_categories(kRegsCategoriesSwitch, true, "category", 'c');
  SwitchRecord regs_extended(kRegsExtendedSwitch, false, "extended", 'e');
  VerbRecord regs(&DoRegs, {"regs", "rg"}, kRegsShortHelp, kRegsHelp,
                  CommandGroup::kAssembly);
  regs.switches.push_back(std::move(regs_categories));
  regs.switches.push_back(std::move(regs_extended));
  (*verbs)[Verb::kRegs] = std::move(regs);

  // step
  SwitchRecord step_force(kStepIntoUnsymbolized, false, "unsymbolized", 'u');
  VerbRecord step(&DoStep, {"step", "s"}, kStepShortHelp, kStepHelp,
                  CommandGroup::kStep, SourceAffinity::kSource);
  step.switches.push_back(step_force);
  (*verbs)[Verb::kStep] = std::move(step);

  (*verbs)[Verb::kStepi] =
      VerbRecord(&DoStepi, {"stepi", "si"}, kStepiShortHelp, kStepiHelp,
                 CommandGroup::kAssembly, SourceAffinity::kAssembly);
  (*verbs)[Verb::kUntil] = VerbRecord(&DoUntil, {"until", "u"}, kUntilShortHelp,
                                      kUntilHelp, CommandGroup::kStep);

  // Stack navigation
  (*verbs)[Verb::kDown] = VerbRecord(&DoDown, {"down"}, kDownShortHelp,
                                     kDownHelp, CommandGroup::kGeneral);
  (*verbs)[Verb::kUp] =
      VerbRecord(&DoUp, {"up"}, kUpShortHelp, kUpHelp, CommandGroup::kGeneral);
}

}  // namespace zxdb
