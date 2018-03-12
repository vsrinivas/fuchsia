// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/console/verbs.h"

#include <inttypes.h>

#include "garnet/bin/zxdb/client/err.h"
#include "garnet/bin/zxdb/client/session.h"
#include "garnet/bin/zxdb/client/process.h"
#include "garnet/bin/zxdb/client/target.h"
#include "garnet/bin/zxdb/console/command.h"
#include "garnet/bin/zxdb/console/command_utils.h"
#include "garnet/bin/zxdb/console/console.h"
#include "garnet/bin/zxdb/console/output_buffer.h"
#include "garnet/public/lib/fxl/strings/string_printf.h"

namespace zxdb {

namespace {

const char kRunShortHelp[] =
    "run / r: Run the program.";
const char kRunHelp[] =
    R"(run [ <program name> ]

  Alias: "r"

Examples

  run
  run chrome
  process 2 run
)";

Err DoRun(ConsoleContext* context, const Command& cmd) {
  // Only a process can be run.
  Err err = cmd.ValidateNouns({Noun::kProcess});
  if (err.has_error())
    return err;

  // TODO(brettw) figure out how argument passing should work. From a user
  // perspective it would be nicest to pass everything after "run" to the
  // app. But this means we can't have any switches to "run". LLDB requires
  // using "--" for this case to mark the end of switches.
  if (cmd.args().empty()) {
    // Use the args already set on the target.
    if (cmd.target()->GetArgs().empty())
      return Err("No program to run. Try \"run <program name>\".");
  } else {
    cmd.target()->SetArgs(cmd.args());
  }

  cmd.target()->Launch([](Target* target, const Err& err) {
    Console* console = Console::get();

    OutputBuffer out;
    out.Append(fxl::StringPrintf(
        "Process %d ", console->context().IdForTarget(target)));

    if (err.has_error()) {
      out.Append("launch failed.\n");
      out.OutputErr(err);
    } else {
      out.Append(fxl::StringPrintf(
          "launched with koid %" PRIu64 ".", target->GetProcess()->GetKoid()));
    }

    console->Output(std::move(out));
  });
  return Err();
}

}  // namespace

void AppendRunVerbs(std::map<Verb, VerbRecord>* verbs) {
  (*verbs)[Verb::kRun] =
      VerbRecord(&DoRun, {"run", "r"}, kRunShortHelp, kRunHelp);
}

}  // namespace zxdb
