// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/console/verbs.h"

#include <sstream>

#include "garnet/bin/zxdb/client/err.h"
#include "garnet/bin/zxdb/client/session.h"
#include "garnet/bin/zxdb/client/process.h"
#include "garnet/bin/zxdb/client/target.h"
#include "garnet/bin/zxdb/console/command.h"
#include "garnet/bin/zxdb/console/command_utils.h"
#include "garnet/bin/zxdb/console/console.h"
#include "garnet/bin/zxdb/console/output_buffer.h"

namespace zxdb {

namespace {

const char kRunShortHelp[] =
    "run / r: Run the program.";
const char kRunHelp[] =
    R"(run

    Alias: "r"
    )";

Err DoRun(Session* session, const Command& cmd) {
  Target* target = session->system().GetActiveTarget();
  target->args().resize(1);
  target->args()[0] = "/boot/bin/ps";
  target->Launch([](Target* target, const Err& err) {
    std::ostringstream line;
    line << "Process " << target->target_id();

    OutputBuffer out;
    if (err.has_error()) {
      line << " launch failed.\n";
      out.Append(line.str());
      out.OutputErr(err);
    } else {
      line << " launched with koid " << target->process()->koid() << ".";
      out.Append(line.str());
    }

    Console::get()->Output(std::move(out));
  });
  return Err();
}

}  // namespace

void AppendRunVerbs(std::map<Verb, VerbRecord>* verbs) {
  (*verbs)[Verb::kRun] =
      VerbRecord(&DoRun, {"run", "r"}, kRunShortHelp, kRunHelp);
}

}  // namespace zxdb
