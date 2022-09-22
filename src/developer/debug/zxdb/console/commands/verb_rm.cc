// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/console/commands/verb_rm.h"

#include "src/developer/debug/zxdb/client/filter.h"
#include "src/developer/debug/zxdb/client/session.h"
#include "src/developer/debug/zxdb/console/command_utils.h"
#include "src/developer/debug/zxdb/console/console.h"
#include "src/developer/debug/zxdb/console/console_context.h"
#include "src/developer/debug/zxdb/console/format_filter.h"
#include "src/developer/debug/zxdb/console/format_target.h"
#include "src/developer/debug/zxdb/console/verbs.h"

namespace zxdb {

namespace {

const char kRmShortHelp[] = "rm: Remove a debugger object.";
const char kRmHelp[] =
    R"(<object-type> [ <object-id> ] rm

  Removes the given object. Specify an explicit object id ("filter 2 rm") to
  remove that object, or omit it ("filter rm") to remove the current one (if
  there is one). To see a list of available objects and their IDs, use the
  object type by itself ("filter").

filter rm

  Removes the filter.

process rm
pr rm
pr 2 rm

  Removes the process. The process should be disconnected first.

breakpoint rm
breakpoint 2 rm
bp rm

  Removes the breakpoint. This is equivalent to "clear".
)";

void RunVerbRm(const Command& cmd, fxl::RefPtr<CommandContext> cmd_context) {
  // Require exactly one noun to be specified for the type of object.
  if (cmd.nouns().size() != 1u || !cmd.args().empty()) {
    return cmd_context->ReportError(
        Err("Use \"<object-type> [ <index> ] rm\" to delete an object.\n"
            "For example, \"filter 2 rm\"."));
  }

  // Guaranteed non-null since this function was called synchronously.
  ConsoleContext* console_context = cmd_context->GetConsoleContext();

  OutputBuffer description;
  switch (cmd.nouns().begin()->first) {
    case Noun::kFilter: {
      if (cmd.filter()) {
        description = FormatFilter(console_context, cmd.filter());
        console_context->session()->system().DeleteFilter(cmd.filter());
      } else {
        return cmd_context->ReportError(Err("No filter to remove."));
      }
      break;
    }
    case Noun::kProcess: {
      // Commands are guaranteed to have targets.
      description = FormatTarget(console_context, cmd.target());
      if (Err err = console_context->session()->system().DeleteTarget(cmd.target());
          err.has_error())
        return cmd_context->ReportError(err);
      break;
    }
    case Noun::kBreakpoint: {
      if (cmd.breakpoint()) {
        description = FormatBreakpoint(console_context, cmd.breakpoint(), false);
        console_context->session()->system().DeleteBreakpoint(cmd.breakpoint());
      } else {
        return cmd_context->ReportError(Err("No breakpoint to remove."));
      }
      break;
    }
    default: {
      std::string noun_name = GetNouns().find(cmd.nouns().begin()->first)->second.aliases[0];
      return cmd_context->ReportError(
          Err("The \"rm\" command is not supported for \"%s\" objects.", noun_name.c_str()));
    }
  }

  OutputBuffer out("Removed ");
  out.Append(std::move(description));
  cmd_context->Output(out);
}

}  // namespace

VerbRecord GetRmVerbRecord() {
  return VerbRecord(&RunVerbRm, {"rm"}, kRmShortHelp, kRmHelp, CommandGroup::kGeneral);
}

}  // namespace zxdb
