// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

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

// system list-processes -------------------------------------------------------

void OutputProcessTreeRecord(const debug_ipc::ProcessTreeRecord& rec, int indent,
                             OutputBuffer* output) {
  std::ostringstream line;
  line << std::setw(indent * 2) << "";

  switch (rec.type) {
    case debug_ipc::ProcessTreeRecord::Type::kJob: line << 'j'; break;
    case debug_ipc::ProcessTreeRecord::Type::kProcess: line << 'p'; break;
    default: line << '?';
  }

  line << ": " << rec.koid << " " << rec.name << "\n";

  output->Append(line.str());
  for (const auto& child : rec.children)
    OutputProcessTreeRecord(child, indent + 1, output);
}

void OnListProcessesComplete(System* system, const Err& err,
                             debug_ipc::ProcessTreeReply reply) {
  OutputBuffer out;
  if (err.has_error())
    out.OutputErr(err);
  else
    OutputProcessTreeRecord(reply.root, 0, &out);
  Console::get()->Output(std::move(out));
}

const char kListProcessesHelp[] =
    R"(system list-processes

Aliases: "system ps", "ps"

Prints the process tree of the debugged system.)";
Err DoListProcesses(Session* session, const Command& cmd) {
  session->system().GetProcessTree(&OnListProcessesComplete);
  return Err();
}

}  // namespace

std::map<Verb, CommandRecord> GetSystemVerbs() {
  std::map<Verb, CommandRecord> map;
  map[Verb::kListProcesses] =
      CommandRecord(&DoListProcesses, kListProcessesHelp);
  return map;
}

}  // namespace zxdb
