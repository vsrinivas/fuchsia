// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <iomanip>
#include <sstream>

#include "src/developer/debug/zxdb/client/remote_api.h"
#include "src/developer/debug/zxdb/client/session.h"
#include "src/developer/debug/zxdb/client/system.h"
#include "src/developer/debug/zxdb/common/err.h"
#include "src/developer/debug/zxdb/console/command.h"
#include "src/developer/debug/zxdb/console/console.h"
#include "src/developer/debug/zxdb/console/output_buffer.h"
#include "src/developer/debug/zxdb/console/verbs.h"
#include "src/lib/fxl/strings/string_printf.h"

namespace zxdb {

namespace {

// List Processes --------------------------------------------------------------

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
    out.Append(err);
  else
    OutputProcessTreeRecord(reply.root, 0, &out);
  Console::get()->Output(out);
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

// System Info -----------------------------------------------------------------

const char kSysInfoShortHelp[] =
    "sys-info: Get general information about the target system.";

const char kSysInfoHelp[] =
    R"(sys-info

Get general information about the target system.
This includes aspects such as build version, number of CPUs, memory, etc.)";

void OnSysInfo(const Err& err, debug_ipc::SysInfoReply sys_info) {
  if (err.has_error()) {
    Console::get()->Output(err);
    return;
  }

  OutputBuffer out;
  out.Append(fxl::StringPrintf("Version: %s\n", sys_info.version.c_str()));
  out.Append(fxl::StringPrintf("Num CPUs: %u\n", sys_info.num_cpus));

  // We don't have total ram for minidumps. We can assume a 0 value is always
  // invalid and just not print it.
  out.Append("Memory (MiB): ");
  if (sys_info.memory_mb) {
    out.Append(fxl::StringPrintf("%u\n", sys_info.memory_mb));
  } else {
    out.Append(Syntax::kComment, "<Unknown>\n");
  }

  out.Append(
      fxl::StringPrintf("HW Breakpoints: %u\n", sys_info.hw_breakpoint_count));
  out.Append(
      fxl::StringPrintf("HW Watchpoints: %u\n", sys_info.hw_watchpoint_count));

  Console::get()->Output(std::move(out));
}

Err DoSysInfo(ConsoleContext* context, const Command& cmd) {
  debug_ipc::SysInfoRequest request;
  context->session()->remote_api()->SysInfo(request, &OnSysInfo);
  return Err();
}

}  // namespace

void AppendSystemVerbs(std::map<Verb, VerbRecord>* verbs) {
  (*verbs)[Verb::kListProcesses] =
      VerbRecord(&DoListProcesses, {"ps"}, kListProcessesShortHelp,
                 kListProcessesHelp, CommandGroup::kGeneral);
  (*verbs)[Verb::kSysInfo] =
      VerbRecord(&DoSysInfo, {"sys-info"}, kSysInfoShortHelp, kSysInfoHelp,
                 CommandGroup::kGeneral);
}

}  // namespace zxdb
