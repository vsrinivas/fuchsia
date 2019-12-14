// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/console/commands/verb_ps.h"

#include <iomanip>
#include <sstream>

#include "src/developer/debug/zxdb/client/session.h"
#include "src/developer/debug/zxdb/console/command.h"
#include "src/developer/debug/zxdb/console/console.h"
#include "src/developer/debug/zxdb/console/output_buffer.h"
#include "src/developer/debug/zxdb/console/verbs.h"

namespace zxdb {

namespace {

void OutputProcessTreeRecord(const debug_ipc::ProcessTreeRecord& rec, int indent,
                             OutputBuffer* output) {
  std::ostringstream line;
  line << std::setw(indent * 2) << "";

  switch (rec.type) {
    case debug_ipc::ProcessTreeRecord::Type::kJob:
      line << 'j';
      break;
    case debug_ipc::ProcessTreeRecord::Type::kProcess:
      line << 'p';
      break;
    default:
      line << '?';
  }

  line << ": " << rec.koid << " " << rec.name << "\n";

  output->Append(line.str());
  for (const auto& child : rec.children)
    OutputProcessTreeRecord(child, indent + 1, output);
}

void OnListProcessesComplete(const Err& err, debug_ipc::ProcessTreeReply reply) {
  OutputBuffer out;
  if (err.has_error())
    out.Append(err);
  else
    OutputProcessTreeRecord(reply.root, 0, &out);
  Console::get()->Output(out);
}

const char kPsShortHelp[] = "ps: Prints the process tree of the debugged system.";
const char kPsHelp[] =
    R"(ps

  Prints the process tree of the debugged system.

  Jobs are annotated with "j: <job koid>"
  Processes are annotated with "p: <process koid>")";

Err RunVerbPs(ConsoleContext* context, const Command& cmd) {
  context->session()->system().GetProcessTree(&OnListProcessesComplete);
  return Err();
}

}  // namespace

VerbRecord GetPsVerbRecord() {
  return VerbRecord(&RunVerbPs, {"ps"}, kPsShortHelp, kPsHelp, CommandGroup::kGeneral);
}

}  // namespace zxdb
