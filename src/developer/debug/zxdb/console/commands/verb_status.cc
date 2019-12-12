// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/console/commands/verb_status.h"

#include "lib/fit/defer.h"
#include "src/developer/debug/zxdb/client/remote_api.h"
#include "src/developer/debug/zxdb/client/session.h"
#include "src/developer/debug/zxdb/console/command.h"
#include "src/developer/debug/zxdb/console/console.h"
#include "src/developer/debug/zxdb/console/console_context.h"
#include "src/developer/debug/zxdb/console/format_job.h"
#include "src/developer/debug/zxdb/console/format_table.h"
#include "src/developer/debug/zxdb/console/format_target.h"
#include "src/developer/debug/zxdb/console/output_buffer.h"
#include "src/lib/fxl/strings/string_printf.h"

namespace zxdb {

namespace {

const char kStatusShortHelp[] = "status: Show debugger status.";
const char kStatusHelp[] = R"(status: Show debugger status.

  Shows information on the current connection, process, thread, etc. along
  with suggestions on what to do.
)";

Err RunVerbStatus(ConsoleContext* context, const Command& cmd, CommandCallback cb = nullptr) {
  OutputBuffer out;
  out.Append(GetConnectionStatus(context->session()));
  out.Append("\n");

  if (!context->session()->IsConnected()) {
    Console::get()->Output(std::move(out));
    return Err();
  }

  out.Append(GetJobStatus(context));
  out.Append("\n");
  out.Append(GetProcessStatus(context));
  out.Append("\n");

  // Attempt to get the agent's state.
  context->session()->remote_api()->Status(
      {}, [out = std::move(out), session = context->session()->GetWeakPtr(), cb = std::move(cb)](
              const Err& err, debug_ipc::StatusReply reply) mutable {
        // Call the callback if applicable.
        Err return_err = err;
        auto defer = fit::defer([&return_err, cb = std::move(cb)]() mutable {
          if (cb)
            cb(return_err);
        });

        if (!session) {
          return_err = Err("No session found.");
          return;
        }

        if (err.has_error()) {
          return_err = err;
          return;
        }

        // Append the Limbo state.
        out.Append(GetLimboStatus(reply.limbo));

        Console::get()->Output(std::move(out));
      });

  return Err();
}

OutputBuffer FormatProcessRecords(std::vector<debug_ipc::ProcessRecord> records, int indent) {
  // Sort by name.
  std::sort(records.begin(), records.end(),
            [](const debug_ipc::ProcessRecord& lhs, const debug_ipc::ProcessRecord& rhs) {
              return lhs.process_name < rhs.process_name;
            });

  std::string indent_str(indent, ' ');
  std::vector<std::vector<std::string>> rows;

  for (const debug_ipc::ProcessRecord& record : records) {
    auto& row = rows.emplace_back();
    row.reserve(4);

    row.push_back(indent_str);
    row.push_back(fxl::StringPrintf("%" PRIu64, record.process_koid));
    row.push_back(record.process_name);
  }

  OutputBuffer out;
  FormatTable(
      {ColSpec(Align::kLeft), ColSpec(Align::kRight, 0, "Koid"), ColSpec(Align::kLeft, 0, "Name")},
      rows, &out);

  return out;
}

}  // namespace

VerbRecord GetStatusVerbRecord() {
  return VerbRecord(&RunVerbStatus, {"status", "stat", "wtf"}, kStatusShortHelp, kStatusHelp,
                    CommandGroup::kGeneral);
}

OutputBuffer GetConnectionStatus(const Session* session) {
  OutputBuffer result;
  result.Append(Syntax::kHeading, "Connection\n");
  if (session->is_minidump()) {
    result.Append(Syntax::kHeading, "  Opened minidump: ");
    result.Append(session->minidump_path() + "\n");
  } else if (session->IsConnected()) {
    result.Append(fxl::StringPrintf("  Connected to '%s' on port %d.\n",
                                    session->connected_host().c_str(), session->connected_port()));
  } else {
    result.Append(
        "  Not connected. You can type these commands (see also \"help "
        "<command>\").\n\n");
    result.Append(Syntax::kHeading, "  connect");
    result.Append(R"( <host+port>
     Connects to a debug agent running on a remote system on the given port.
     However, most users will use a debug command from their environment to
     automatically run the debug_agent and connect the zxdb frontend to it
     (e.g. "fx debug"). See your environment's documentation.

)");
    result.Append(Syntax::kHeading, "  opendump");
    result.Append(R"( <local filename>
    Opens a local file containing a crash dump for analysis.

)");
    result.Append(Syntax::kHeading, "  quit");
    result.Append(R"(
    Have a nice day.
)");
  }

  return result;
}

OutputBuffer GetJobStatus(ConsoleContext* context) {
  OutputBuffer result;
  result.Append(Syntax::kHeading, "Jobs\n");

  auto jobs = context->session()->system().GetJobContexts();
  int attached_count = 0;
  for (const auto& j : jobs) {
    if (j->GetState() == JobContext::State::kAttached)
      attached_count++;
  }

  result.Append(
      fxl::StringPrintf("  Attached to %d job(s) (jobs are nodes in the Zircon process tree). "
                        "Processes\n  launched in attached jobs can be caught and debugged via "
                        "\"attach\" filters.\n  See \"help job\" and \"help attach\". The "
                        "debugger has these:\n",
                        attached_count));
  result.Append(FormatJobList(context, 2));

  return result;
}

OutputBuffer GetProcessStatus(ConsoleContext* context) {
  OutputBuffer result;
  result.Append(Syntax::kHeading, "Processes\n");

  auto targets = context->session()->system().GetTargets();
  int attached_count = 0;
  for (const auto& t : targets) {
    if (t->GetState() == Target::State::kRunning)
      attached_count++;
  }

  result.Append(
      fxl::StringPrintf("  Attached to %d process(es). The debugger has these:\n", attached_count));
  result.Append(FormatTargetList(context, 2));

  return result;
}

OutputBuffer GetLimboStatus(const std::vector<debug_ipc::ProcessRecord>& limbo) {
  OutputBuffer result;

  result.Append(Syntax::kHeading, "Processes waiting on exception\n");
  if (limbo.empty()) {
    result.Append("  No processes waiting on exception.");
  } else {
    result.Append(fxl::StringPrintf("  %zu process(es) waiting on exception.\n", limbo.size()));
    result.Append(
        "  Run \"attach <KOID>\" to load them into zxdb or \"detach <KOID>\" to free them back "
        "into "
        "the system.\n");

    result.Append(FormatProcessRecords(limbo, 2));
  }

  return result;
}

}  // namespace zxdb
