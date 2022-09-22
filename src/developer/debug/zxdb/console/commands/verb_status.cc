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
#include "src/developer/debug/zxdb/console/format_filter.h"
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

void RunVerbStatus(const Command& cmd, fxl::RefPtr<CommandContext> cmd_context) {
  ConsoleContext* console_context = cmd_context->GetConsoleContext();

  OutputBuffer out;
  out.Append(GetConnectionStatus(console_context->session()));
  out.Append("\n");

  if (!console_context->session()->IsConnected())
    return cmd_context->Output(out);

  out.Append(GetFilterStatus(console_context));
  out.Append("\n");
  out.Append(GetProcessStatus(console_context));
  out.Append("\n");

  // Attempt to get the agent's state.
  console_context->session()->remote_api()->Status(
      {},
      [out = std::move(out), cmd_context](const Err& err, debug_ipc::StatusReply reply) mutable {
        if (!cmd_context->GetConsoleContext())
          return;  // Console gone, nothing to do.
        if (err.has_error())
          return cmd_context->ReportError(err);

        out.Append(GetLimboStatus(reply.limbo));
        cmd_context->Output(out);
      });
}

OutputBuffer FormatProcessRecords(std::vector<debug_ipc::ProcessRecord> records, int indent) {
  // Sort by name.
  std::sort(records.begin(), records.end(),
            [](const debug_ipc::ProcessRecord& lhs, const debug_ipc::ProcessRecord& rhs) {
              return lhs.process_name < rhs.process_name;
            });

  std::vector<std::vector<std::string>> rows;

  for (const debug_ipc::ProcessRecord& record : records) {
    auto& row = rows.emplace_back();
    row.reserve(4);

    row.push_back(std::to_string(record.process_koid));
    row.push_back(record.process_name);
    if (record.component) {
      row.push_back(record.component->url.substr(record.component->url.find_last_of('/') + 1));
    }
  }

  OutputBuffer out;
  FormatTable({ColSpec(Align::kRight, 0, "Koid", indent), ColSpec(Align::kLeft, 0, "Name"),
               ColSpec(Align::kLeft, 0, "Component")},
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
     (e.g. "ffx debug connect"). See your environment's documentation.

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

OutputBuffer GetFilterStatus(ConsoleContext* context) {
  OutputBuffer result;
  result.Append(Syntax::kHeading, "Filters\n");
  result.Append("  Newly launched processes matching a filter will be automatically attached.\n");

  if (context->session()->system().GetFilters().empty()) {
    result.Append("\n  There are no filters. Use \"attach <process-name>\" to create one.\n");
  } else {
    result.Append(FormatFilterList(context, 2));
  }

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
    result.Append(fxl::StringPrintf("  %zu process(es) waiting on exception. ", limbo.size()));
    result.Append(
        "Run \"attach <KOID>\" to load one into\n"
        "  zxdb or \"detach <KOID>\" to terminate them. See \"help jitd\" for more\n"
        "  information on Just-In-Time Debugging.\n");

    result.Append(FormatProcessRecords(limbo, 4));
  }

  return result;
}

}  // namespace zxdb
