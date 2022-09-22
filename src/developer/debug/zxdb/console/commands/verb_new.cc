// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/console/commands/verb_new.h"

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

const char kNewShortHelp[] = "new: Create a new debugger object.";
const char kNewHelp[] =
    R"(<object-type> new

  Creates a new object of type <object-type>.

filter new

  A filter looks for process launches matching a pattern and automatically
  attaches to them. Most often, filters are created with the "attach <filter>"
  command. See "help filter" and "help attach" for more.

  [zxdb] filter new
  Filter 2 type=unset

process new

  A process context holds settings (binary name, command line arguments, etc.)
  and possibly a running process. The new context will have no associated
  process and can then be run or attached.

    [zxdb] process new
    Process 2 [Not running]
    [zxdb] attach 22860
    Attached Process 2 [Running] koid=22860 foobar.cmx
)";

void RunVerbNew(const Command& cmd, fxl::RefPtr<CommandContext> cmd_context) {
  // Require exactly one noun to be specified for the type of object.
  if (cmd.nouns().size() != 1u || !cmd.args().empty()) {
    return cmd_context->ReportError(
        Err("Use \"<object-type> new\" to create a new object of <object-type>.\n"
            "For example, \"process new\"."));
  }

  // Guaranteed non-null since this is called synchronously.
  ConsoleContext* console_context = cmd_context->GetConsoleContext();

  switch (cmd.nouns().begin()->first) {
    case Noun::kFilter: {
      Filter* new_filter = console_context->session()->system().CreateNewFilter();
      console_context->SetActiveFilter(new_filter);
      cmd_context->Output(FormatFilter(console_context, new_filter));
      break;
    }
    case Noun::kProcess: {
      Target* new_target = console_context->session()->system().CreateNewTarget(cmd.target());
      console_context->SetActiveTarget(new_target);
      cmd_context->Output(FormatTarget(console_context, new_target));
      break;
    }
    case Noun::kBreakpoint: {
      // Creates a disabled-by-default breakpoint with no settings. This isn't very useful but
      // we do this for symmetry.
      Breakpoint* new_breakpoint = console_context->session()->system().CreateNewBreakpoint();
      console_context->SetActiveBreakpoint(new_breakpoint);
      cmd_context->Output(FormatBreakpoint(console_context, new_breakpoint, false));
      break;
    }
    default: {
      std::string noun_name = GetNouns().find(cmd.nouns().begin()->first)->second.aliases[0];
      return cmd_context->ReportError(
          Err("The \"new\" command is not supported for \"%s\" objects.", noun_name.c_str()));
    }
  }
}

}  // namespace

VerbRecord GetNewVerbRecord() {
  return VerbRecord(&RunVerbNew, {"new"}, kNewShortHelp, kNewHelp, CommandGroup::kGeneral);
}

}  // namespace zxdb
