// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/console/commands/verb_regs.h"

#include <set>

#include "src/developer/debug/zxdb/client/frame.h"
#include "src/developer/debug/zxdb/client/session.h"
#include "src/developer/debug/zxdb/client/setting_schema_definition.h"
#include "src/developer/debug/zxdb/client/thread.h"
#include "src/developer/debug/zxdb/console/command.h"
#include "src/developer/debug/zxdb/console/command_utils.h"
#include "src/developer/debug/zxdb/console/console.h"
#include "src/developer/debug/zxdb/console/format_register.h"
#include "src/developer/debug/zxdb/console/output_buffer.h"
#include "src/developer/debug/zxdb/console/string_util.h"
#include "src/developer/debug/zxdb/console/verbs.h"

namespace zxdb {

namespace {

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

Err RunVerbRegs(ConsoleContext* context, const Command& cmd) {
  using debug_ipc::RegisterCategory;

  if (Err err = AssertStoppedThreadWithFrameCommand(context, cmd, "regs"); err.has_error())
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
          category, true,
          [collector](const Err& err, const std::vector<debug_ipc::Register>& new_regs) {
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

}  // namespace

VerbRecord GetRegsVerbRecord() {
  // regs
  VerbRecord regs(&RunVerbRegs, {"regs", "rg"}, kRegsShortHelp, kRegsHelp, CommandGroup::kAssembly);
  regs.switches.emplace_back(kRegsAllSwitch, false, "all", 'a');
  regs.switches.emplace_back(kRegsGeneralSwitch, false, "general", 'g');
  regs.switches.emplace_back(kRegsFloatingPointSwitch, false, "float", 'f');
  regs.switches.emplace_back(kRegsVectorSwitch, false, "vector", 'v');
  regs.switches.emplace_back(kRegsDebugSwitch, false, "debug", 'd');
  regs.switches.emplace_back(kRegsExtendedSwitch, false, "extended", 'e');
  return regs;
}

}  // namespace zxdb
