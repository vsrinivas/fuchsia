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

void OnSysInfo(const Err& err, debug_ipc::SysInfoReply sys_info) {
  if (err.has_error()) {
    Console::get()->Output(err);
    return;
  }

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

  Console::get()->Output(std::move(out));
}

Err RunVerbSysInfo(ConsoleContext* context, const Command& cmd) {
  debug_ipc::SysInfoRequest request;
  context->session()->remote_api()->SysInfo(request, &OnSysInfo);
  return Err();
}

}  // namespace

VerbRecord GetSysInfoVerbRecord() {
  return VerbRecord(&RunVerbSysInfo, {"sys-info"}, kSysInfoShortHelp, kSysInfoHelp,
                    CommandGroup::kGeneral);
}

}  // namespace zxdb
