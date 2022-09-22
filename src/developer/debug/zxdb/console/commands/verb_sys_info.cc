// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/console/commands/verb_sys_info.h"

#include "src/developer/debug/zxdb/client/remote_api.h"
#include "src/developer/debug/zxdb/client/session.h"
#include "src/developer/debug/zxdb/console/command.h"
#include "src/developer/debug/zxdb/console/console.h"
#include "src/developer/debug/zxdb/console/output_buffer.h"
#include "src/developer/debug/zxdb/console/verbs.h"
#include "src/lib/fxl/strings/string_printf.h"

namespace zxdb {

namespace {

const char kSysInfoShortHelp[] = "sys-info: Get general information about the target system.";
const char kSysInfoHelp[] =
    R"(sys-info

  Get general information about the target system. This includes aspects such as
  build version, number of CPUs, memory, etc.)";

void OnSysInfo(const Err& err, debug_ipc::SysInfoReply sys_info,
               fxl::RefPtr<CommandContext> cmd_context) {
  if (err.has_error())
    return cmd_context->ReportError(err);

  OutputBuffer out;
  out.Append(fxl::StringPrintf("Version: %s\n", sys_info.version.c_str()));
  out.Append(fxl::StringPrintf("Num CPUs: %u\n", sys_info.num_cpus));

  // We don't have total ram for minidumps. We can assume a 0 value is always invalid and just not
  // print it.
  out.Append("Memory (MiB): ");
  if (sys_info.memory_mb) {
    out.Append(fxl::StringPrintf("%u\n", sys_info.memory_mb));
  } else {
    out.Append(Syntax::kComment, "<Unknown>\n");
  }

  out.Append(fxl::StringPrintf("HW Breakpoints: %u\n", sys_info.hw_breakpoint_count));
  out.Append(fxl::StringPrintf("HW Watchpoints: %u\n", sys_info.hw_watchpoint_count));

  cmd_context->Output(out);
}

void RunVerbSysInfo(const Command& cmd, fxl::RefPtr<CommandContext> cmd_context) {
  debug_ipc::SysInfoRequest request;
  cmd_context->GetConsoleContext()->session()->remote_api()->SysInfo(
      request, [cmd_context](const Err& err, debug_ipc::SysInfoReply sys_info) {
        OnSysInfo(err, std::move(sys_info), cmd_context);
      });
}

}  // namespace

VerbRecord GetSysInfoVerbRecord() {
  return VerbRecord(&RunVerbSysInfo, {"sys-info"}, kSysInfoShortHelp, kSysInfoHelp,
                    CommandGroup::kGeneral);
}

}  // namespace zxdb
