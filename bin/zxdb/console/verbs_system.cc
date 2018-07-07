// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/console/verbs.h"

#include <iomanip>
#include <sstream>

#include "garnet/bin/zxdb/client/err.h"
#include "garnet/bin/zxdb/client/session.h"
#include "garnet/bin/zxdb/client/system.h"
#include "garnet/bin/zxdb/console/command.h"
#include "garnet/bin/zxdb/console/console.h"
#include "garnet/bin/zxdb/console/output_buffer.h"

namespace zxdb {

namespace {

void OutputProcessTreeRecord(const debug_ipc::ProcessTreeRecord& rec,
                             int indent, OutputBuffer* output) {
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

void OnListProcessesComplete(const Err& err,
                             debug_ipc::ProcessTreeReply reply) {
  OutputBuffer out;
  if (err.has_error())
    out.OutputErr(err);
  else
    OutputProcessTreeRecord(reply.root, 0, &out);
  Console::get()->Output(std::move(out));
}

const char kListProcessesShortHelp[] =
    "ps: Prints the process tree of the debugged system.";
const char kListProcessesHelp[] =
    R"(ps

Prints the process tree of the debugged system.)";
Err DoListProcesses(ConsoleContext* context, const Command& cmd) {
  context->session()->system().GetProcessTree(&OnListProcessesComplete);
  return Err();
}

}  // namespace

void AppendSystemVerbs(std::map<Verb, VerbRecord>* verbs) {
  (*verbs)[Verb::kListProcesses] =
      VerbRecord(&DoListProcesses, {"ps"}, kListProcessesShortHelp,
                 kListProcessesHelp, CommandGroup::kGeneral);
}

}  // namespace zxdb
