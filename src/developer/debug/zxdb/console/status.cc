// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/console/status.h"

#include "src/developer/debug/zxdb/client/job_context.h"
#include "src/developer/debug/zxdb/client/session.h"
#include "src/developer/debug/zxdb/client/system.h"
#include "src/developer/debug/zxdb/client/target.h"
#include "src/developer/debug/zxdb/console/console_context.h"
#include "src/developer/debug/zxdb/console/format_job.h"
#include "src/developer/debug/zxdb/console/format_target.h"
#include "src/lib/fxl/strings/string_printf.h"

namespace zxdb {

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

}  // namespace zxdb
