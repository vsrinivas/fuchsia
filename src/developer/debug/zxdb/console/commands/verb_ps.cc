// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/console/commands/verb_ps.h"

#include <iomanip>
#include <optional>
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

// Recursively filters the given process tree. All jobs and processes that contain the given filter
// string in their name are matched. These are added to the result, along with any parent job nodes
// required to get to the matched records.
std::optional<debug_ipc::ProcessTreeRecord> FilterProcessTree(
    const debug_ipc::ProcessTreeRecord& rec, const std::string& filter) {
  debug_ipc::ProcessTreeRecord result;

  for (const auto& child : rec.children) {
    if (auto matched_child = FilterProcessTree(child, filter))
      result.children.push_back(*matched_child);
  }

  // Return the node when it matches or any of its children do.
  if (rec.name.find(filter) != std::string::npos || !result.children.empty()) {
    result.type = rec.type;
    result.koid = rec.koid;
    result.name = rec.name;
    return result;
  }

  return std::nullopt;
}

void OnListProcessesComplete(const std::string& filter, const Err& err,
                             const debug_ipc::ProcessTreeReply& reply) {
  OutputBuffer out;
  if (err.has_error()) {
    out.Append(err);
  } else if (filter.empty()) {
    // Output everything.
    OutputProcessTreeRecord(reply.root, 0, &out);
  } else {
    // Filter the results.
    if (auto filtered = FilterProcessTree(reply.root, filter)) {
      OutputProcessTreeRecord(*filtered, 0, &out);
    } else {
      out.Append("No processes or jobs matching \"" + filter + "\".\n");
    }
  }
  Console::get()->Output(out);
}

const char kPsShortHelp[] = "ps: Prints the process tree of the debugged system.";
const char kPsHelp[] =
    R"(ps [ <filter-string> ]

  Prints the process tree of the debugged system.

  If a filter-string is provided only jobs and processes whose names contain the
  given case-sensitive substring. It does not support regular expressions.

  Jobs are annotated with "j: <job koid>"
  Processes are annotated with "p: <process koid>")";

Err RunVerbPs(ConsoleContext* context, const Command& cmd) {
  std::string filter_string;
  if (!cmd.args().empty())
    filter_string = cmd.args()[0];

  context->session()->system().GetProcessTree(
      [filter_string](const Err& err, debug_ipc::ProcessTreeReply reply) {
        OnListProcessesComplete(filter_string, err, reply);
      });
  return Err();
}

}  // namespace

VerbRecord GetPsVerbRecord() {
  VerbRecord record(&RunVerbPs, {"ps"}, kPsShortHelp, kPsHelp, CommandGroup::kGeneral);
  record.param_type = VerbRecord::kOneParam;  // Allow spaces in the filter string.
  return record;
}

}  // namespace zxdb
