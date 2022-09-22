// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/console/commands/verb_watch.h"

#include "src/developer/debug/zxdb/client/breakpoint.h"
#include "src/developer/debug/zxdb/client/session.h"
#include "src/developer/debug/zxdb/console/command.h"
#include "src/developer/debug/zxdb/console/command_utils.h"
#include "src/developer/debug/zxdb/console/console.h"
#include "src/developer/debug/zxdb/console/output_buffer.h"
#include "src/developer/debug/zxdb/console/verbs.h"

namespace zxdb {

namespace {

const char kWatchShortHelp[] = "watch: Create a hardware write breakpoint on a variable.";
const char kWatchHelp[] =
    R"*(watch <expression>

  The "watch" command is an easier way to create a hardware data write
  breakpoint. It will stop the program when the given value changes.

  The expression is evaluated at the time the command is executed, and the
  address and size of the result are used to create a memory write breakpoint.
  The expression is not evaluated again. It is an alias for:

    break --type=write "* &(<expression>)"

  For control over more breakpoint settings, use the "break" command or edit the
  breakpoint settings after creation with "bp set". See "bp get" for the list of
  attributes that can be changed this way.

Gotchas

  The expression has a different meaning than the "break" command. The "break"
  command will evaluate an expression and will try to interpret the result as an
  address. In contrast, the "watch" command expects a value to watch and will
  implicitly take its address as the thing to watch.

  This is not the same thing as the more complicated GDB "watch" command: the
  expression will be evaluated only once at input time.

Examples

  watch i
      Breaks when the value of "i" changes.

  process 1 thread 2 watch i
      Breaks only on the given thread when the value of "i" changes.

  watch foo[5]->bar
      Evaluates the expression and sets a watchpoint at the address of "bar".
      It will NOT break if "foo[5]" changes to point to a different "bar".
)*";

void RunVerbWatch(const Command& cmd, fxl::RefPtr<CommandContext> cmd_context) {
  fxl::RefPtr<EvalContext> eval_context = GetEvalContextForCommand(cmd);

  BreakpointSettings settings;
  settings.type = BreakpointSettings::Type::kWrite;
  settings.scope = ExecutionScopeForCommand(cmd);

  auto data_provider = eval_context->GetDataProvider();
  Err err = EvalCommandExpression(
      cmd, "watch", eval_context, true, true,
      [settings, eval_context, cmd_context](ErrOrValue result) mutable {
        ConsoleContext* console_context = cmd_context->GetConsoleContext();
        if (!console_context)
          return;  // Nothing to do.
        if (result.has_error())
          return cmd_context->ReportError(result.err());

        // Validate the expression produced something with an address.
        const ExprValue& value = result.value();
        const ExprValueSource& source = value.source();
        if (source.type() != ExprValueSource::Type::kMemory) {
          cmd_context->ReportError(
              Err("This expression's value is stored in a %s location. Only values\n"
                  "stored in memory can be watched.\n"
                  "\n"
                  "The watch command will implicitly take the address of the result of the\n"
                  "expression. To set a breakpoint on a literal address you can do either:\n"
                  "\n"
                  "  watch *(uint32_t*)0x12345678\n"
                  "  break --type=write --size=4 0x12345678\n",
                  ExprValueSource::TypeToString(source.type())));
          return;
        }

        if (source.is_bitfield()) {
          cmd_context->ReportError(
              Err("This expression's result is a bitfield which can't be watched."));
          return;
        }

        // Size errors are very common if the object is too big. Catch those early before trying
        // to create a breakpoint.
        uint32_t size = static_cast<uint32_t>(value.data().size());
        if (Err err = BreakpointSettings::ValidateSize(console_context->session()->arch(),
                                                       settings.type, size);
            err.has_error()) {
          // Rewrite the error to list the size that this produced. Since "watch" implicitly gets
          // the size, the user may have no idea how much they requested.
          cmd_context->ReportError(Err("Attempting to watch a variable of size " +
                                       std::to_string(size) + ".\n\n" + err.msg()));
          return;
        }

        // Fill in the breakpoint location and set it.
        settings.locations.emplace_back(source.address());
        settings.byte_size = size;

        Breakpoint* breakpoint = console_context->session()->system().CreateNewBreakpoint();
        console_context->SetActiveBreakpoint(breakpoint);
        breakpoint->SetSettings(settings);

        // Created message.
        OutputBuffer out;
        out.Append("Created ");
        out.Append(FormatBreakpoint(console_context, breakpoint, true));
        cmd_context->Output(out);
      });
  if (err.has_error())
    cmd_context->ReportError(err);
}

}  // namespace

VerbRecord GetWatchVerbRecord() {
  return VerbRecord(&RunVerbWatch, {"watch"}, kWatchShortHelp, kWatchHelp,
                    CommandGroup::kBreakpoint);
}

}  // namespace zxdb
