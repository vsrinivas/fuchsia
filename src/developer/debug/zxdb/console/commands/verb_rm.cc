// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/console/commands/verb_rm.h"

#include "src/developer/debug/zxdb/client/filter.h"
#include "src/developer/debug/zxdb/client/session.h"
#include "src/developer/debug/zxdb/console/command_utils.h"
#include "src/developer/debug/zxdb/console/console.h"
#include "src/developer/debug/zxdb/console/console_context.h"
#include "src/developer/debug/zxdb/console/format_job.h"
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

job rm

  Removes the job. Any filters tied to this job will also be deleted.
)";

Err RunVerbRm(ConsoleContext* context, const Command& cmd) {
  // Require exactly one noun to be specified for the type of object.
  if (cmd.nouns().size() != 1u || !cmd.args().empty()) {
    return Err(
        "Use \"<object-type> <index> rm\" to delete an object.\n"
        "For example, \"filter 2 rm\".");
  }

  OutputBuffer description;
  switch (cmd.nouns().begin()->first) {
    case Noun::kFilter: {
      if (cmd.filter()) {
        description = FormatFilter(context, cmd.filter());
        context->session()->system().DeleteFilter(cmd.filter());
      } else {
        return Err("No filter to remove.");
      }
      break;
    }
    case Noun::kJob: {
      if (cmd.job_context()) {
        description = FormatJobContext(context, cmd.job_context());
        context->session()->system().DeleteJobContext(cmd.job_context());
      } else {
        return Err("No job to remove.");
      }
      break;
    }
    default: {
      std::string noun_name = GetNouns().find(cmd.nouns().begin()->first)->second.aliases[0];
      return Err("The \"rm\" command is not supported for \"%s\" objects.", noun_name.c_str());
    }
  }

  OutputBuffer out("Removed ");
  out.Append(std::move(description));
  Console::get()->Output(out);

  return Err();
}

}  // namespace

VerbRecord GetRmVerbRecord() {
  return VerbRecord(&RunVerbRm, {"rm"}, kRmShortHelp, kRmHelp, CommandGroup::kGeneral);
}

}  // namespace zxdb
