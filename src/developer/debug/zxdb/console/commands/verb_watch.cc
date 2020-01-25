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

Err RunVerbWatch(ConsoleContext* context, const Command& cmd) {
  fxl::RefPtr<EvalContext> eval_context = GetEvalContextForCommand(cmd);

  BreakpointSettings settings;
  settings.type = BreakpointSettings::Type::kWrite;
  settings.scope = ExecutionScopeForCommand(cmd);

  auto data_provider = eval_context->GetDataProvider();
  return EvalCommandExpression(
      cmd, "watch", eval_context, true, [settings, eval_context](ErrOrValue result) mutable {
        Console* console = Console::get();
        if (result.has_error()) {
          console->Output(result.err());
          return;
        }

        // Validate the expression produced something with an address.
        const ExprValue& value = result.value();
        const ExprValueSource& source = value.source();
        if (source.type() != ExprValueSource::Type::kMemory) {
          console->Output(
              Err("This expression's value is stored in a %s location.\n"
                  "Only values stored in memory can be watched.",
                  ExprValueSource::TypeToString(source.type())));
          return;
        }

        if (source.is_bitfield()) {
          console->Output(Err("This expression's result is a bitfield which can't be watched."));
          return;
        }

        // Fill in the breakpoint location and set it.
        settings.locations.emplace_back(source.address());
        settings.byte_size = static_cast<uint32_t>(value.data().size());

        Breakpoint* breakpoint = console->context().session()->system().CreateNewBreakpoint();
        console->context().SetActiveBreakpoint(breakpoint);
        breakpoint->SetSettings(settings);

        // Created message.
        OutputBuffer out;
        out.Append("Created ");
        out.Append(FormatBreakpoint(&console->context(), breakpoint, true));
        console->Output(out);
      });
  return Err();
}

}  // namespace

VerbRecord GetWatchVerbRecord() {
  return VerbRecord(&RunVerbWatch, {"watch"}, kWatchShortHelp, kWatchHelp,
                    CommandGroup::kBreakpoint);
}

}  // namespace zxdb
