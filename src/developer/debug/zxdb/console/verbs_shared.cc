// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

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

// new ---------------------------------------------------------------------------------------------

const char kNewShortHelp[] = "new: Create a new debugger object.";
const char kNewHelp[] =
    R"(<object-type> [ <reference-object-id> ] new

  Creates a new object of type <object-type>.

  The settings from the current object will be cloned. If an explicit object
  index is specified ("process 2 new"), the new one will clone the given one.
  The new object will be the active one of that type.

filter new

  A filter looks for process launches matching a pattern and automatically
  attaches to them. Most often, filters are created with the "attach <filter>"
  command. See "help filter" and "help attach" for more.

  [zxdb] filter new
  Filter 2  FIXME

job new

  A job context holds settings (filters, etc.) and possibly a running job. The
  new context will have no associated job and can then be run or attached.
  Attach a job context with a job on the target system with "attach-job <koid>".

    [zxdb] job new
    Job 2 [Not attached]
    [zxdb] job 2 attach-job 1960
    Job 2 [Attached] koid=1960 
  
process new

  A process context holds settings (binary name, command line arguments, etc.)
  and possibly a running process. The new context will have no associated
  process and can then be run or attached.

    [zxdb] process new
    Process 2 [Not running]
    [zxdb] attach 22860
    Attached Process 2 [Running] koid=22860 foobar.cmx
)";
Err DoNew(ConsoleContext* context, const Command& cmd) {
  // Require exactly one noun to be specified for the type of object.
  if (cmd.nouns().size() != 1u || !cmd.args().empty()) {
    return Err(
        "Use \"<object-type> new\" to create a new object of <object-type>.\n"
        "For example, \"process new\".");
  }

  Console* console = Console::get();

  switch (cmd.nouns().begin()->first) {
    case Noun::kFilter: {
      Filter* new_filter = context->session()->system().CreateNewFilter();
      if (cmd.filter()) {
        // Clone existing filter's settings.
        new_filter->SetJob(cmd.filter()->job());
        new_filter->SetPattern(cmd.filter()->pattern());
      }
      context->SetActiveFilter(new_filter);
      console->Output(FormatFilter(context, new_filter));
      break;
    }
    case Noun::kJob: {
      JobContext* new_job_context = context->session()->system().CreateNewJobContext();
      context->SetActiveJobContext(new_job_context);
      console->Output(FormatJobContext(context, new_job_context));
      break;
    }
    case Noun::kProcess: {
      Target* new_target = context->session()->system().CreateNewTarget(cmd.target());
      context->SetActiveTarget(new_target);
      console->Output(FormatTarget(context, new_target));
      break;
    }
    default: {
      std::string noun_name = GetNouns().find(cmd.nouns().begin()->first)->second.aliases[0];
      return Err("The \"new\" command is not supported for \"%s\" objects.", noun_name.c_str());
    }
  }

  return Err();
}

// rm --------------------------------------------------------------------------

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
Err DoRm(ConsoleContext* context, const Command& cmd) {
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

void AppendSharedVerbs(std::map<Verb, VerbRecord>* verbs) {
  (*verbs)[Verb::kNew] =
      VerbRecord(&DoNew, {"new"}, kNewShortHelp, kNewHelp, CommandGroup::kGeneral);
  (*verbs)[Verb::kRm] = VerbRecord(&DoRm, {"rm"}, kRmShortHelp, kRmHelp, CommandGroup::kGeneral);
}

}  // namespace zxdb
